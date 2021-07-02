#ifndef _VM_TLB_H

#define _VM_TLB_H

int vm_fault(int, vaddr_t);
int tlb_get_rr_victim(void);
void as_activate(void);



#endif
