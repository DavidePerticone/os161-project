#include <instrumentation.h>
#include <swapfile.h>
#include <kern/fcntl.h>
#include <syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/unistd.h>
#include <vfs.h>
#include <vm.h>
#include <kern/iovec.h>
#include <current.h>
#include <vnode.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>
#include <kern/errno.h>
#include <instrumentation.h>

/* TLB Faults: The number of TLB misses that have occurred (not including faults that cause
a program to crash). */
static long int tlb_misses;

/* TLB Faults with Free: The number of TLB misses for which there was free space in the
TLB to add the new TLB entry (i.e., no replacement is required).*/
static long int tlb_misses_free;

/*TLB Faults with Replace: The number of TLB misses for which there was no free space
for the new TLB entry, so replacement was required.*/
static long int tlb_misses_full;

/*TLB Invalidations: The number of times the TLB was invalidated (this counts the number
times the entire TLB is invalidated NOT the number of TLB entries invalidated)*/
static long int tlb_invalidations;

/* TLB Reloads: The number of TLB misses for pages that were already in memory.*/
static long int tlb_reloads;

/* Page Faults (Zeroed): The number of TLB misses that required a new page to be zero-
filled.*/

static long int new_pages_zeroed;

/* Page Faults (Disk): The number of TLB misses that required a page to be loaded from
disk. */

static long int faults_with_load;

/* Page Faults from ELF: The number of page faults that require getting a page from the ELF
file */

static long int faults_with_elf_load;

/* Page Faults from Swapfile: The number of page faults that require getting a page from the
swap file. */

static long int swap_out_pages;

/* Swapfile Writes: The number of page faults that require writing a page to the swap file. */

static long int swap_in_pages;

void init_instrumentation(void)
{

    tlb_misses = 0;
    tlb_misses_free = 0;
    tlb_misses_full = 0;
    tlb_invalidations = 0;
    tlb_reloads = 0;
    new_pages_zeroed = 0;
    faults_with_load = 0;
    faults_with_elf_load = 0;
    swap_out_pages = 0;
    swap_in_pages = 0;
}

void increase(long int indicator)
{

    switch (indicator)
    {
    case TLB_MISS:
        tlb_misses++;
        break;

    case TLB_MISS_FREE:
        tlb_misses_free++;
        break;

    case TLB_MISS_FULL:
        tlb_misses_full++;
        break;

    case TLB_INVALIDATION:
        tlb_invalidations++;
        break;

    case TLB_RELOAD:
        tlb_reloads++;
        break;

    case FAULT_WITH_LOAD:
        faults_with_load++;
        break;

    case FAULT_WITH_ELF_LOAD:
        faults_with_elf_load++;
        break;

    case SWAP_OUT_PAGE:
        swap_out_pages++;
        break;

    case SWAP_IN_PAGE:
        swap_in_pages++;
        break;

    case NEW_PAGE_ZEROED:
        new_pages_zeroed++;
        break;

    default:

        break;
    }

    return;
}

void print_statistics(void)
{
    int flag;

    kprintf("\n\nVirtual memory statistics:\n\n");
    kprintf("----------------------------------------\n");
    kprintf("TLB Faults: %ld                         \n", tlb_misses);
    kprintf("----------------------------------------\n");
    kprintf("TLB Faults with Free: %ld               \n", tlb_misses_free);
    kprintf("----------------------------------------\n");
    kprintf("|TLB Faults with Replace: %ld            \n", tlb_misses_full);
    kprintf("----------------------------------------\n");
    kprintf("|TLB Invalidations: %ld                  \n", tlb_invalidations);
    kprintf("----------------------------------------\n");
    kprintf("TLB Reloads: %ld                        \n", tlb_reloads);
    kprintf("----------------------------------------\n");
    kprintf("Page Faults (Zeroed): %ld               \n", new_pages_zeroed);
    kprintf("----------------------------------------\n");
    kprintf("Page Faults (Disk): %ld                 \n", faults_with_load);
    kprintf("----------------------------------------\n");
    kprintf("Page Faults from ELF: %ld               \n", faults_with_elf_load);
    kprintf("----------------------------------------\n");
    kprintf("Page Faults from Swapfile: %ld          \n", swap_in_pages);
    kprintf("----------------------------------------\n");
    kprintf("Swapfile Writes: %ld                    \n", swap_out_pages);
    kprintf("----------------------------------------\n\n");

    flag=1;

    if (tlb_misses_free + tlb_misses_full != tlb_misses)
    {
        kprintf("\nWarning: TLB Faults with Free + TLB Faults with Replace != TLB Faults\n");
        flag=0;
    }

    if (tlb_reloads + faults_with_load + new_pages_zeroed != tlb_misses)
    {
        kprintf("\nWarning: TLB Reloads + Page Faults (Disk) + Page Faults (Zeroed) != TLB Faults \n");
        flag=0;
    }

    if (faults_with_elf_load + swap_in_pages != faults_with_load)
    {
        kprintf("\nWarning: Page Faults from ELF %ld + Swapfile Writes %ld != Page Faults (Disk) %ld\n", faults_with_elf_load, swap_out_pages, faults_with_load);
        flag=0;
    }

    if(flag){
        kprintf("All sums are correct.\n\n");
    }
}