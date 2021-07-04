#ifndef _PT_H
#define _PT_H

#include <vm.h>

#define VALID_MASK 0x80000000
/* Vaddr space can have at most 4GB/PAGE_SIZE entries, 
   that is the number of frames in the system */
#define NUMBER_PAGES ((1024 * 1024)/PAGE_SIZE * 1024 * 4)


/*
 * The addrspace data structure contains the page table and the base addresses
 * of the segements. The number of pages of each segments are needed to check that
 * the addresses are in the correct range. 
 * Each entry of the page table contains the physical address corresponsing to the index 
 * and the MSB is used as valid/invalid bit (set with VALID_MASK)
 */
struct page_table
{
    unsigned int pt[NUMBER_PAGES];
};

void copy_pt(struct page_table *old_pt, struct page_table *new_pt);




#endif