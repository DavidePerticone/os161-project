#include <coremap.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <segments.h>
#include <thread.h>
#include <addrspace.h>
#include <swapfile.h>
#include <syscall.h>
#include <vm_tlb.h>
#include <st.h>
#include <item.h>
#include "opt-paging.h"


static void SetBit(int *A, int k)
{

    A[k / 32] |= 1 << (k % 32); // Set the bit at the k-th position in A[i]
}

static void ClearBit(int *A, int k)
{

    A[k / 32] &= ~(1 << (k % 32));
}

static int TestBit(int *A, int k)
{
    if ((A[k / 32] & (1 << (k % 32))) != 0)
        return 1;
    else
        return 0;
}

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;
static int nRamFrames = 0;
static int *freeRamFrames = NULL;
static unsigned long *allocSize = NULL;

void print_freeRamFrames(void)
{
    int i;
    kprintf("Frame Status\n");

    for (i = 0; i < nRamFrames; i++)
    {
        kprintf("  %d    %d\n", i, TestBit(freeRamFrames, i));
    }
}

int init_freeRamFrames(int ramFrames)
{
    int i;
    nRamFrames = ramFrames;
    freeRamFrames = kmalloc(sizeof(int) * nRamFrames / 32 == 0 ? nRamFrames / 32 : nRamFrames / 32 + 1);
    if (freeRamFrames == NULL)
        return 1;

    for (i = 0; i < nRamFrames; i++)
    {
        ClearBit(freeRamFrames, i);
    }

    return 0;
}

void destroy_freeRamFrames(void)
{
    freeRamFrames = NULL;
}

int init_allocSize(int ramFrames)
{
    /* dimension of freeRamFrames and allocSize must be equal */

    KASSERT(ramFrames == nRamFrames);
    int i;
    allocSize = kmalloc(sizeof(unsigned long) * nRamFrames);
    if (allocSize == NULL)
        return 1;

    for (i = 0; i < nRamFrames; i++)
    {
        allocSize[i] = 0;
    }

    return 0;
}

static paddr_t
getfreeppages(unsigned long npages)
{
    paddr_t addr;
    long i, first, found, np = (long)npages;
    int result;

    if (!isTableActive())
        return 0;
    spinlock_acquire(&freemem_lock);

    for (i = 0, first = found = -1; i < nRamFrames; i++)
    {
        result = TestBit(freeRamFrames, i);
        if (result)
        {
            result = TestBit(freeRamFrames, i - 1);
            if (i == 0 || !result)
                first = i; /* set first free in an interval */
            if (i - first + 1 >= np)
            {
                found = first;
                break;
            }
        }
    }

    if (found >= 0)
    {
        for (i = found; i < found + np; i++)
        {
            ClearBit(freeRamFrames, i);
        }
        allocSize[found] = np;
        addr = (paddr_t)found * PAGE_SIZE;
    }
    else
    {
        addr = 0;
    }

    spinlock_release(&freemem_lock);

    return addr;
}

static void
as_zero_region(paddr_t paddr, unsigned npages)
{
    bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

paddr_t
getppages(unsigned long npages, int kmem)
{
    paddr_t paddr;
    vaddr_t vaddr;
    pid_t pid_victim;
    struct addrspace *as_victim;
    int victim_segment, result;

    /* try freed pages first */
    paddr = getfreeppages(npages);
    if (paddr == 0)
    {
        /* call stealmem */
        spinlock_acquire(&stealmem_lock);
        paddr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
    }
    else
    {
    }
    if (paddr != 0 && isTableActive())
    {
        spinlock_acquire(&freemem_lock);
        allocSize[paddr / PAGE_SIZE] = npages;
        spinlock_release(&freemem_lock);
    }

    if (paddr == 0 && isTableActive())
    {
        spinlock_acquire(&freemem_lock);
        /* we can only get one page at a time, otherwise in case of swap problems occur */
        if (!kmem)
        {
            KASSERT(npages == 1);
        }
        if (paddr == 0 && kmem && npages!=1)
        {
            print_freeRamFrames();
            panic("No contiguous %ld free ram frames for kernel allocation", npages);
        }
        /* get physical and virtual address of victmim */
        paddr = get_victim(&vaddr, &pid_victim);
        /* no victim found */
        if (paddr == 0)
        {
            panic("It was not possible to find a page victim.\nAre you allocating more kernel memory than the available ram size?");
        }
        /* get address space of the process whose page is the victim */
        as_victim = pid_getas(pid_victim);
        /* get in which segment the page is */
        victim_segment = address_segment(vaddr, as_victim);
        /* swap page out */
        spinlock_release(&freemem_lock);
        result = swap_out(paddr, vaddr, victim_segment);
        if (result)
        {
            return 0;
        }
    }

    KASSERT(paddr != 0);
    as_zero_region(paddr, npages);
    return paddr;
}

int freeppages(paddr_t addr, long first_page)
{
    long i, first, np = (long)allocSize[first_page];

    if (!isTableActive())
        return 0;
    first = addr / PAGE_SIZE;
    KASSERT(allocSize != NULL);
    KASSERT(nRamFrames > first);

    spinlock_acquire(&freemem_lock);
    for (i = first; i < first + np; i++)
    {
        SetBit(freeRamFrames, i);
    }
    // print_freeRamFrames();

    spinlock_release(&freemem_lock);

    return (int)np;
}
