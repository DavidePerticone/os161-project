#ifndef _VM_TLB_H
#define _VM_TLB_H

#include <types.h>
#include <addrspace.h>
int vm_fault(int, vaddr_t);
int tlb_get_rr_victim(void);
void as_activate(void);
/* give fault address get the segment in which it is */
int address_segment(vaddr_t faultaddress, struct addrspace *as);



#endif
