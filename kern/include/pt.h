#ifndef _PT_H
#define _PT_H

#include <vm.h>

#define VALID_MASK 0x80000000
/* Vaddr space can have at most 4GB/PAGE_SIZE entries, 
   that is the number of frames in the system */
#define NUMBER_PAGES (1024 * 1024 * 1024 * 4) / PAGE_SIZE
#define DUMBVM_STACKPAGES 18

/*
 * The addrspace data structure contains the page table and the base addresses
 * of the segements. The number of pages of each segments are needed to check that
 * the addresses are in the correct range. 
 * Each entry of the page table contains the physical address corresponsing to the index 
 * and the MSB is used as valid/invalid bit (set with VALID_MASK)
 */
struct addrspace
{
    unsigned int page_table[NUMBER_PAGES];
    vaddr_t as_vbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    size_t as_npages2;
}

struct addrspace *
as_create(void)
{
    int i;
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL)
    {
        return NULL;
    }

    for (i = 0; i < NUMBER_PAGES; i++)
    {
        /* set all pages as invalid -- pure demand loading */
        as->page_table[i] &= !VALID_MASK;
    }

    return as;
}

/*unsigned int page_table[NUMBER_PAGES];
    vaddr_t as_vbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    size_t as_npages2;*/

int as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    newas = as_create();
    if (newas == NULL)
    {
        return ENOMEM;
    }

    KASSERT(old != NULL);
    KASSERT(old->as_vbase1 != NULL); 
    KASSERT(old->as_npages1 > 0); 
    KASSERT(old->as_vbase2 != NULL); 
    KASSERT(old->as_npages2 > 0); 

    newas->as_vbase1 = old->as_vbase1;
    newas->as_npages1 = old->as_npages1;
    newas->as_vbase2 = old->as_vbase2;
    newas->as_npages2 = old->as_npages2;

     for (i = 0; i < NUMBER_PAGES; i++)
    {
        /* copy page table */
        newas->page_table[i] = old->page_table[i];
    }


    *ret = newas;
    return 0;
}

 void
 as_destroy(struct addrspace *as)
 {
     KASSERT(as != NULL);
     KASSERT(as->page_table != NULL);
     kfree(as);
 }

#endif