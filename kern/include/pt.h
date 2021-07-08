#ifndef _PT_H
#define _PT_H

#include <vm.h>


#define PID_MASK 0xFC000000
/* Vaddr space can have at most 4GB/PAGE_SIZE entries, 
   that is the number of frames in the system */


/*
 * The addrspace data structure contains the page table and the base addresses
 * of the segements. The number of pages of each segments are needed to check that
 * the addresses are in the correct range. 
 * Each entry of the page table contains the physical address corresponsing to the index 
 * and the MSB is used as valid/invalid bit (set with VALID_MASK)
 */

struct ipt_entry{
pid_t pid;
vaddr_t vaddr;
};

paddr_t get_victim(vaddr_t *vaddr);
int init_victim(void);
int create_ipt(void);

/* Given a pid and vaddr, get the physical frame, if in memory */
paddr_t ipt_lookup(pid_t pid, vaddr_t vaddr);
/* set an entry in the ipt */
int ipt_add(pid_t pid, paddr_t paddr, vaddr_t vaddr);


#endif