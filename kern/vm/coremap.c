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

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;
static int nRamFrames = 0;
static unsigned char *freeRamFrames = NULL;
static unsigned long *allocSize = NULL;

int init_freeRamFrames(int ramFrames)
{
    int i;
    nRamFrames = ramFrames;
    freeRamFrames = kmalloc(sizeof(unsigned char) * nRamFrames);
    if (freeRamFrames == NULL)
        return 1;

    for (i = 0; i < nRamFrames; i++)
    {
        freeRamFrames[i] = (unsigned char)0;
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

    if (!isTableActive())
        return 0;
    spinlock_acquire(&freemem_lock);
    for (i = 0, first = found = -1; i < nRamFrames; i++)
    {
        if (freeRamFrames[i])
        {
            if (i == 0 || !freeRamFrames[i - 1])
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
            freeRamFrames[i] = (unsigned char)0;
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

paddr_t
getppages(unsigned long npages)
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
    }else{
        
    }
    if (paddr != 0 && isTableActive())
    {
        spinlock_acquire(&freemem_lock);
        allocSize[paddr / PAGE_SIZE] = npages;
        spinlock_release(&freemem_lock);
    }

    if (paddr == 0  && isTableActive())
    {
        /* get physical and virtual address of victmim */
        paddr = get_victim(&vaddr, &pid_victim);
        /* get address space of the process whose page is the victim */
        as_victim = pid_getas(pid_victim);
        /* get in which segment the page is */
        victim_segment = address_segment(vaddr, as_victim);
        /* swap page out */
        result = swap_out(vaddr, victim_segment);
        if (result)
        {
            return 0;
        }
    }

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
        freeRamFrames[i] = (unsigned char)1;
    }

    spinlock_release(&freemem_lock);

    return 1;
}