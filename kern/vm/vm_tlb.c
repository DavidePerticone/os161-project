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
#include <addrspace.h>
#include <swapfile.h>
#include <syscall.h>


/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define DUMBVM_STACKPAGES 18

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	int victim;
	int segment;
	int tlb_entry;
	int result;
	static int count_tlb_miss = 0;
	static int count_tlb_miss_free = 0;
	static int count_tlb_miss_replace = 0;

	init_swapfile();

	/*every time we are in this function, means that a tlb miss occurs*/
	count_tlb_miss++;
	DEBUG(DB_VM, "TLB faults -> %d\n", count_tlb_miss);

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype)
	{
	case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		/* TODO: terminate the process instead of panicking */
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

	/* Assert that the address space has been set up properly. */
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
		/* data segmente, RW segment */
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

	/* check if page is in memory */
	paddr = ipt_lookup(curproc->p_pid, faultaddress);
	/* if it is in memory, paddr will be different than 0 */

	if (paddr == 0)
	/* page not in ipt */
	{
		/* are we in code or data segment? then, we should load the needed page */
		if (segment == 1 || segment == 2)
		{
			/* page_offset_from_segbase will store the offset of the desired page from the offset of the segment */
			vaddr_t page_offset_from_segbase;
			/* faultaddress is at page multiple, if we subtract the segment address we find the offset from the segment base */
			page_offset_from_segbase = faultaddress - (segment == 1 ? as->as_vbase1 : as->as_vbase2);

			/* as_prepare_load is a wrapper for getppages() -> will allocate a page and return the offset */
			paddr = as_prepare_load(1);
			/* if all pages are occupied, use victim */
			if (paddr == 0)
			{
				paddr = get_victim();
			}

			KASSERT(paddr != 0);
			/* 
			 * first set ipt and TLB, otherwise cannot do translation while loading page
			 * in particular, within emu_doread, when callind memcpy, the translation is
			 * done when assigning from kernel buffer to user buffer (by the MMU that consults the TLB).
			 * Therefore, and entry is needed in the TLB even though the page is not yet loaded.
			 * Just not to forget, add the entry in the IPT
			 */
			result = ipt_add(curproc->p_pid, paddr, faultaddress);
			if (result)
			{
				return -1;
			}

			/* make sure it's page-aligned */
			KASSERT((paddr & PAGE_FRAME) == paddr);

			/* Disable interrupts on this CPU while frobbing the TLB. */
			spl = splhigh();

			/* add entry in the TLB */
			for (i = 0; i < NUM_TLB; i++)
			{
				tlb_read(&ehi, &elo, i);
				if (elo & TLBLO_VALID)
				{
					continue;
				}
				count_tlb_miss_free++;
				DEBUG(DB_VM, "TLB faults with Free -> %d\n", count_tlb_miss_free);
				ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
				tlb_write(ehi, elo, i);
				splx(spl);
				break;
			}
			/* if all entry are occupied, find a victim and replace it */
			if (i == NUM_TLB)
			{
				/*select a victim to be replaced*/
				victim = tlb_get_rr_victim();

				/*write on the victim entry the new value*/
				count_tlb_miss_replace++;
				DEBUG(DB_VM, "TLB faults with Replace -> %d\n", count_tlb_miss_replace);

				ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
				tlb_write(ehi, elo, victim);
				splx(spl);
			}

			/* load page at vaddr = faultaddress */
			result = load_page(page_offset_from_segbase, faultaddress, segment);
			if (result)
			{
				return -1;
			}
			/* after loading the page, set the entry to READ_ONLY in case of code page */
			if (segment == 1)
			{
				/* Disable interrupts on this CPU while frobbing the TLB. */
				spl = splhigh();
				tlb_entry = tlb_probe(ehi, 0);
				KASSERT(tlb_entry >= 0);
				/* use !TLBLO_DIRTY to set the dirty bit to 0 and leave ther rest untouched */
				tlb_write(ehi, elo & !TLBLO_DIRTY, tlb_entry);
				splx(spl);
			}
			return 0;
		}
		else
		{
			/* as_prepare_load is a wrapper for getppages() -> will allocate a page and return the offset */
			paddr = as_prepare_load(1);

			if (paddr == 0)
			{
				paddr = get_victim();
			}

			KASSERT(paddr != 0);
			/* 
			 * 
			 */
			result = ipt_add(curproc->p_pid, paddr, faultaddress);
			if (result)
			{
				return -1;
			}

			/* make sure it's page-aligned */
			KASSERT((paddr & PAGE_FRAME) == paddr);

			/* Disable interrupts on this CPU while frobbing the TLB. */
			spl = splhigh();

			/* add entry in the TLB */
			for (i = 0; i < NUM_TLB; i++)
			{
				tlb_read(&ehi, &elo, i);
				if (elo & TLBLO_VALID)
				{
					continue;
				}
				count_tlb_miss_free++;
				DEBUG(DB_VM, "TLB faults with Free -> %d\n", count_tlb_miss_free);

				ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
				tlb_write(ehi, elo, i);
				splx(spl);
				break;
			}
			/* if all entry are occupied, find a victim and replace it */
			if (i == NUM_TLB)
			{
				/*select a victim to be replaced*/
				victim = tlb_get_rr_victim();

				/*write on the victim entry the new value*/
				count_tlb_miss_replace++;
				DEBUG(DB_VM, "TLB faults with Replace -> %d\n", count_tlb_miss_replace);

				ehi = faultaddress;
				elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
				DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
				tlb_write(ehi, elo, victim);
				splx(spl);
			}
		}
		return 0;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i = 0; i < NUM_TLB; i++)
	{
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID)
		{
			continue;
		}
		count_tlb_miss_free++;
		DEBUG(DB_VM, "TLB faults with Free -> %d\n", count_tlb_miss_free);

		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	/*select a victim to be replaced*/
	victim = tlb_get_rr_victim();

	/*write on the victim entry the new value*/
	count_tlb_miss_replace++;
	DEBUG(DB_VM, "TLB faults with Replace -> %d\n", count_tlb_miss_replace);

	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
	tlb_write(ehi, elo, victim);
	splx(spl);
	return 0;
}

/*Select a victim on the TLB using a Round Robin algorithm*/
int tlb_get_rr_victim(void)
{
	int victim;
	static unsigned int next_victim = 0;
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	return victim;
}

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}