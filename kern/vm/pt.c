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

/* inverted page table */
static struct ipt_entry *ipt;
static int nRamFrames;
static int last_victim;
static int victim;
static struct spinlock ipt_lock = SPINLOCK_INITIALIZER;
static int ipt_active=0;

void print_ipt(void)
{
    int i;
    kprintf("Entry - PID - VADDR\n");
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);
    for (i = 0; i < nRamFrames; i++)
    {
        kprintf("%d -   %d   - %d\n", i, ipt[i].pid, ipt[i].vaddr / PAGE_SIZE);
    }
    spinlock_release(&ipt_lock);
}

int init_victim(void)
{
    spinlock_acquire(&ipt_lock);
        KASSERT(ipt_active);

    last_victim = -1;
    spinlock_release(&ipt_lock);

    return victim;
}

/* return the selected victim */
paddr_t get_victim(vaddr_t *vaddr, pid_t *pid)
{
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
        if (ipt[i].pid != -1 && i > last_victim)
        {
            /* update last victim: if last frame is selected, start again from the beginning */
            last_victim = i == nRamFrames - 1 ? 0 : i;
            *vaddr = ipt[i].vaddr;
            *pid = ipt[i].pid;
            /* return paddr of victmi */
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
    ipt = kmalloc(sizeof(struct ipt_entry) * nRamFrames);

    if (ipt == NULL)
    {
        return -1;
    }
    spinlock_acquire(&ipt_lock);
    for (i = 0; i < nRamFrames; i++)
    {
        ipt[i].pid = -1;
    }
    ipt_active=1;
    spinlock_release(&ipt_lock);

    return 0;
}

/* Given a pid and vaddr, get the physical frame, if in memory */
/* TODO: Speed up linear search */

paddr_t ipt_lookup(pid_t pid, vaddr_t vaddr)
{
    int i;

    KASSERT(pid >= 0);
    KASSERT(vaddr != 0);
    spinlock_acquire(&ipt_lock);
    KASSERT(ipt_active);

    for (i = 0; i < nRamFrames; i++)
    {
        if (ipt[i].pid == pid)
        {
            if (ipt[i].vaddr == vaddr)
            {
                /* if frame is in memory return its physical address */
                spinlock_release(&ipt_lock);
                return (paddr_t)i * PAGE_SIZE;
            }
        }
    }
    spinlock_release(&ipt_lock);

    /* return 0 in case the frame is not in memory */
    return 0;
}

/* 
 * Convinience function used to add an entry in the ipt.
 */

int ipt_add(pid_t pid, paddr_t paddr, vaddr_t vaddr)
{
    int frame_index;

    KASSERT(pid >= 0);
    KASSERT(paddr != 0);
    KASSERT(vaddr != 0);

    frame_index = paddr / PAGE_SIZE;
    KASSERT(frame_index < nRamFrames);
    spinlock_acquire(&ipt_lock);
    if(ipt_active){
    KASSERT(ipt_active);

    ipt[frame_index].pid = pid;
    ipt[frame_index].vaddr = vaddr;
    }
    spinlock_release(&ipt_lock);

    return 0;
}

int ipt_kadd(pid_t pid, paddr_t paddr, vaddr_t vaddr)
{
    int frame_index;
    KASSERT(pid == -1);
    
    frame_index = paddr / PAGE_SIZE;
    KASSERT(frame_index < nRamFrames);
    spinlock_acquire(&ipt_lock);
    if(ipt_active){
    KASSERT(ipt_active);

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
        }
    }
    spinlock_release(&ipt_lock);
}