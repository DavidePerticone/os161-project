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

/* copy a page table into another one */
void copy_pt(struct page_table *old_pt, struct page_table *new_pt){

    int i;

    KASSERT(old_pt != NULL);
    KASSERT(new_pt != NULL);

    for (i = 0; i < NUMBER_PAGES; i++)
    {
        /* copy page table */
        new_pt->pt[i] = old_pt->pt[i];
    }


}
