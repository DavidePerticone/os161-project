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
static int last_victim;
static int victim;
static struct spinlock ipt_lock = SPINLOCK_INITIALIZER;
static int ipt_active = 0;
static int first_paddr;

static ST ipt_hash = NULL;

void setLoading(int set, int entry)
{

    KASSERT(set == 0 || set == 1);
    KASSERT(entry > 0 && entry < nRamFrames);
    spinlock_acquire(&ipt_lock);
    if (set)
    {
        ipt[entry].vaddr |= LOAD_MASK;
    }
    else
    {

        ipt[entry].vaddr &= ~LOAD_MASK;
    }
    spinlock_release(&ipt_lock);
}

int isLoading(int entry)
{
    KASSERT(entry > 0 && entry < nRamFrames);
    /*Protect the read of an entry, avoid other threads changing the entry while the read in on*/
    spinlock_acquire(&ipt_lock);
    int val = ipt[entry].vaddr;
    spinlock_release(&ipt_lock);
    /*Get the MSB of the entry, if set return 1 else 0. This tells if the entry is still loading or not*/
    return (val & LOAD_MASK) == LOAD_MASK;
}

/*DEBUG function used to check the behavior of the page table*/
void print_ipt(void)
{
    int i;
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    kprintf("<< IPT >>\n");

    for (i = 0; i < nRamFrames; i++)
    {
        kprintf("%d -   %d   - %d\n", i, ipt[i].pid, ipt[i].vaddr / PAGE_SIZE);
    }
    spinlock_release(&ipt_lock);
}

int init_victim(int first_avail_paddress)
{
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);
    last_victim = -1;
    first_paddr = first_avail_paddress;
    spinlock_release(&ipt_lock);

    return victim;
}

/* return the selected victim */
paddr_t get_victim(vaddr_t *vaddr, pid_t *pid)
{
    int tlb_entry;

    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    /* for each ram frames */
    for (int i = 0; i < nRamFrames; i++)
    {
        /* 
         *Till a ram frame is free (pid == -1) and its position is greater 
         *than the previous victim .
         *This guarantees that pages allocated to kernel are not touched and implements a 
         *round robin policy.
         */
        if (ipt[i].pid != -2 && i > last_victim && first_paddr <= i * PAGE_SIZE)
        {
            /* update last victim: if last frame is selected, start again from the beginning */
            last_victim = i == nRamFrames - 1 ? 0 : i;
            *vaddr = ipt[i].vaddr;
            *pid = ipt[i].pid;
            /* free ipt entry */
            ipt[i].pid = -1;
            /* free tlb entry */
            tlb_entry = tlb_probe(ipt[i].vaddr, 0);
            /* if victim page is in the tlb, invalidate the entry */
            if (tlb_entry >= 0)
            {
                tlb_write(TLBHI_INVALID(tlb_entry), TLBLO_INVALID(), tlb_entry);
            }
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
    ipt_hash = STinit(577);

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
/* TODO: Speed up linear search */

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
        item = ITEMscan(pid, (vaddr & ~LOAD_MASK), frame_index);
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
    spinlock_acquire(&ipt_lock);
    KASSERT(pid > 0);
    KASSERT(vaddr < 0x80000000);
    STdelete(ipt_hash, pid, vaddr);
    spinlock_release(&ipt_lock);

    return 0;
}
