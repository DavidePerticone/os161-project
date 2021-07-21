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
#include <pt.h>
#include <coremap.h>
#include <st.h>
#include <item.h>
#include <opt-paging.h>

/* inverted page table */
static struct ipt_entry *ipt;
static int nRamFrames;
static struct spinlock ipt_lock = SPINLOCK_INITIALIZER;
static int ipt_active = 0;

static ST ipt_hash = NULL;

/*DEBUG function used to check the behavior of the page table*/
void print_ipt(void)
{
    int i;
    //  spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);
    kprintf("PID: %d\n", curproc->p_pid);
    kprintf("<< IPT >>\n");

    for (i = 0; i < nRamFrames; i++)
    {
        if (ipt[i].pid != -1)
        {
            kprintf("%d -   %d   - %d\n", i, ipt[i].pid, ipt[i].vaddr / PAGE_SIZE);
        }
    }
    //   spinlock_release(&ipt_lock);
}


// TO DO implement per process round robin, not global
/* return the selected victim */
paddr_t get_victim(vaddr_t *vaddr, pid_t *pid)
{
    int tlb_entry, spl;

    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    /* for each ram frames */
    for (int i = curproc->last_victim, j = 0; j < nRamFrames; j++, i++)
    {
        if (i == nRamFrames)
        {
            curproc->last_victim = -1;
            i = 0;
        }
        /* 
         *Till a ram frame is free (pid == -1) and its position is greater 
         *than the previous victim .
         *This guarantees that pages allocated to kernel are not touched and implements a 
         *round robin policy.
         */
        if ((ipt[i].pid == curproc->p_pid))
        {
            /* update last victim: if last frame is selected, start again from the beginning */
            curproc->last_victim = i + 1;
            *vaddr = ipt[i].vaddr;
            *pid = ipt[i].pid;
            /* delete entry from hash */
            hash_delete(*pid, *vaddr);

            /* free ipt entry: set it as kernel page so that no one can select it as a free while loading it*/
            ipt[i].pid = -2;
            /* free tlb entry */
            spl = splhigh();

            tlb_entry = tlb_probe(ipt[i].vaddr, 0);
            /* if victim page is in the tlb, invalidate the entry */
            if (tlb_entry >= 0)
            {
               tlb_write(TLBHI_INVALID(tlb_entry), TLBLO_INVALID(), tlb_entry);
            }
            splx(spl);

            /* return paddr of victim */
            spinlock_release(&ipt_lock);

            return i * PAGE_SIZE;
        }
    }
    /* error */
    spinlock_release(&ipt_lock);
    return 0;
}

/* 
 * Create an inverted page table IPT-
 * One entry for each frame in the ram.
 * Initialized pid of each entry to -1 to signal it is free
 */

int create_ipt(void)
{
    int i;
    nRamFrames = ((int)ram_getsize()) / PAGE_SIZE;
    KASSERT(nRamFrames != 0);
    ipt_hash = STinit(nRamFrames);

    ipt = kmalloc(sizeof(struct ipt_entry) * nRamFrames);

    if (ipt == NULL || ipt_hash == NULL)
    {
        return -1;
    }
    spinlock_acquire(&ipt_lock);
    for (i = 0; i < nRamFrames; i++)
    {
        ipt[i].pid = -1;
    }
    ipt_active = 1;
    spinlock_release(&ipt_lock);

    return 0;
}

/* Given a pid and vaddr, get the physical frame, if in memory */

paddr_t ipt_lookup(pid_t pid, vaddr_t vaddr)
{
    int index;
    int paddr;
    KASSERT(pid >= 0);
    KASSERT(vaddr != 0);
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    index = STsearch(ipt_hash, pid, vaddr);

    if (index == -1)
    {
        paddr = 0;
    }
    else
    {
        paddr = index * PAGE_SIZE;
    }

    spinlock_release(&ipt_lock);

    /* return 0 in case the frame is not in memory */
    return paddr;
}

/* 
 * Convinience function used to add an entry in the ipt.
 */

int ipt_add(pid_t pid, paddr_t paddr, vaddr_t vaddr)
{
    int frame_index;
    Item item;
    KASSERT(pid >= 0);
    KASSERT(paddr != 0);
    KASSERT(vaddr != 0);

    frame_index = paddr / PAGE_SIZE;
    KASSERT(frame_index < nRamFrames);

    spinlock_acquire(&ipt_lock);
    if (ipt_active)
    {

        item = ITEMscan(pid, vaddr, frame_index);
        KASSERT(ipt_active);
        ipt[frame_index].pid = pid;
        ipt[frame_index].vaddr = vaddr;

        /*Add entry to hash table*/
        STinsert(ipt_hash, item);
        // STdisplay(ipt_hash);
    }
    spinlock_release(&ipt_lock);

    return 0;
}

int ipt_kadd(pid_t pid, paddr_t paddr, vaddr_t vaddr)
{
    int frame_index;
    KASSERT(pid == -2 || pid == -1);
    frame_index = paddr / PAGE_SIZE;
    KASSERT(frame_index < nRamFrames);

    spinlock_acquire(&ipt_lock);
    if (ipt_active)
    {
        ipt[frame_index].pid = pid;
        ipt[frame_index].vaddr = vaddr;
    }
    spinlock_release(&ipt_lock);

    return 0;
}

/*
 * Convinience function to free all the enter of a process.
 * Normally during the __exit of a process to free IPT entries
 * and frame entries (in coremap).
 */

void free_ipt_process(pid_t pid)
{
    int i, result;
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    for (i = 0; i < nRamFrames; i++)
    {
        if (ipt[i].pid == pid)
        {
            ipt[i].pid = -1;
            result = freeppages(i * PAGE_SIZE, i);
            if (result == 0)
            {
                panic("Trying to free ipt entry while VM not active");
            }
            STdelete(ipt_hash, pid, ipt[i].vaddr);
        }
    }
    spinlock_release(&ipt_lock);
}

int hash_delete(pid_t pid, vaddr_t vaddr)
{
    KASSERT(pid > 0);
    KASSERT(vaddr < 0x80000000);
    STdelete(ipt_hash, pid, vaddr);

    return 0;
}

void hash_print(void)
{
    STdisplay(ipt_hash);
}