#include <vm_tlb.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <types.h>
#include <segments.h>
#include <thread.h>
#include <swapfile.h>
#include <syscall.h>
#include <instrumentation.h>

/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES 18

static struct spinlock tlb_fault_lock = SPINLOCK_INITIALIZER;


static void update_tlb(vaddr_t faultaddress, paddr_t paddr)
{

	int i;
	uint32_t ehi, elo;
	uint32_t ehi1, elo1;
	int victim;
	int spl;
	/* Disable interrupts on this CPU while frobbing the TLB. */

	ehi = faultaddress;

	if (address_segment(faultaddress, curproc->p_addrspace) == 1)
	{
		elo = (paddr & ~TLBLO_DIRTY) | TLBLO_VALID;
	}
	else
	{
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	}

	spl = splhigh();

	/* add entry in the TLB */
	for (i = 0; i < NUM_TLB; i++)
	{
		tlb_read(&ehi1, &elo1, i);
		if (elo & TLBLO_VALID)
		{
			continue;
		}

		increase(TLB_MISS_FREE);
		tlb_write(ehi, elo, i);

		break;
	}
	/* if all entry are occupied, find a victim and replace it */
	if (i == NUM_TLB)
	{
		/*select a victim to be replaced*/
		victim = tlb_get_rr_victim();
		tlb_write(ehi, elo, victim);
	}
	splx(spl);
}

int address_segment(vaddr_t faultaddress, struct addrspace *as)
{

	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	int segment;

	KASSERT(as == curproc->p_addrspace);

	/* Assert that the address space has been set up properly. */
	KASSERT(as != NULL);
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	/* understand in which segment we are, so as to behave accordingly */
	if (faultaddress >= vbase1 && faultaddress < vtop1)
	{
		/* we are in segment one (due to ELF file segments division)*/
		segment = 1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2)
	{
		/* data segment, RW segment */
		segment = 2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop)
	{
		/* stack segment, RW segment */
		segment = 3;
	}
	else
	{
		return EFAULT;
	}

	return segment;
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr;
	struct addrspace *as;
	int segment;
	int result;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype)
	{
	case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		/* terminate the process instead of panicking */
		kprintf("VM_FAULT_READONLY: process exited\n");
		sys__exit(-1);

		/* should not get here */
		panic("VM: got VM_FAULT_READONLY, should not get here\n");
	case VM_FAULT_READ:
	case VM_FAULT_WRITE:
		break;
	default:
		return EINVAL;
	}

	if (curproc == NULL)
	{
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL)
	{
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* get in which segment the faulting address is */
	segment = address_segment(faultaddress, as);
	if (segment == EFAULT)
	{
		kprintf("PID: %d\n", curproc->p_pid);
		kprintf("SEGMENTATION FAULT: process exited\n");
		sys__exit(-1);

		/* should not get here */
		panic("VM: got SEGMENTATION FAULT, should not get here\n");
		return EFAULT;
	}

	increase(TLB_MISS);

	spinlock_acquire(&tlb_fault_lock);
	/* check if page is in memory */
	paddr = ipt_lookup(curproc->p_pid, faultaddress);

	/* if it is in memory, paddr will be different than 0 */

	if (paddr == 0)
	/* page not in ipt */
	{
		/* are we in code or data segment? then, we should load the needed page */
		if (segment == 1 || segment == 2)
		{
			/* 
			 * page_offset_from_segbase will store the offset of the desired page 
			 * from the offset of the segment 
			 */
			vaddr_t page_offset_from_segbase;
			/* 
			 * faultaddress is at page multiple, if we subtract the segment address 
			 * we find the offset from the segment base 
			 */
			page_offset_from_segbase = faultaddress - (segment == 1 ? as->as_vbase1 : as->as_vbase2);
			spinlock_release(&tlb_fault_lock);

			/* as_prepare_load is a wrapper for getppages() -> will allocate a page and return the offset */

			paddr = as_prepare_load(1);
			KASSERT(paddr != 0);

			/* make sure it's page-aligned */
			KASSERT((paddr & PAGE_FRAME) == paddr);

			/* look in the swapfile (if the faulting address is not in code segment) */

			if (segment != 1)
			{
				result = swap_in(faultaddress, paddr);
			}
			else
			{
				result = 1;
			}

			as_complete_load(curproc->p_addrspace);
			if (result)
			{
				/* load page at vaddr = faultaddress if not in swapfile */

				result = load_page(page_offset_from_segbase, faultaddress, segment, paddr);
				if (result)
				{
					return -1;
				}
			}
			spinlock_acquire(&tlb_fault_lock);
			result = ipt_add(curproc->p_pid, paddr, faultaddress);

			if (result)
			{
				return -1;
			}

			update_tlb(faultaddress, paddr);

			spinlock_release(&tlb_fault_lock);

			return 0;
		}
		else
		{
			spinlock_release(&tlb_fault_lock);

			/* as_prepare_load is a wrapper for getppages() -> will allocate a page and return the offset */
			paddr = as_prepare_load(1);

			/* make sure it's page-aligned */
			KASSERT((paddr & PAGE_FRAME) == paddr);

			result = swap_in(faultaddress, paddr);
			if (result)
			{
				increase(NEW_PAGE_ZEROED);
			}
			spinlock_acquire(&tlb_fault_lock);

			KASSERT(paddr != 0);
		
			result = ipt_add(curproc->p_pid, paddr, faultaddress);

			if (result)
			{
				return -1;
			}

			update_tlb(faultaddress, paddr);

			spinlock_release(&tlb_fault_lock);
		}

		return 0;
	}
	else
	{

		increase(TLB_RELOAD);
		/* make sure it's page-aligned */
		KASSERT((paddr & PAGE_FRAME) == paddr);
		
		update_tlb(faultaddress, paddr);
		spinlock_release(&tlb_fault_lock);

		return 0;
	}
}

/*Select a victim on the TLB using a Round Robin algorithm*/
int tlb_get_rr_victim(void)
{
	int victim;
	static unsigned int next_victim = 0;
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	increase(TLB_MISS_FULL);
	return victim;
}
void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}