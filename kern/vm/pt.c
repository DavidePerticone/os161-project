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


/* inverted page table */
static struct ipt_entry *ipt;
static int nRamFrames;

int create_ipt(void){

    int i;
    nRamFrames = ((int)ram_getsize())/PAGE_SIZE; 
    KASSERT(nRamFrames != 0);
    ipt=kmalloc(sizeof(struct ipt_entry)*nRamFrames);

    if(ipt == NULL){
        return -1;
    }

    for(i=0; i<nRamFrames; i++){
        ipt[i].pid=-1;
    }

    return 0;

}

/* Given a pid and vaddr, get the physical frame, if in memory */
/* TODO: Speed up linear search */

paddr_t ipt_lookup(pid_t pid, vaddr_t vaddr) {

    KASSERT(pid >= 0);
    KASSERT(vaddr != 0);

    int i;

    for(i=0; i<nRamFrames; i++){
        if(ipt[i].pid == pid){
            if(ipt[i].vaddr == vaddr){
                /* if frame is in memory return its physical address */
                return (paddr_t)i*PAGE_SIZE;
            }
        }
    }

    /* return 0 in case the frame is not in memory */
    return 0;

}

int ipt_add(pid_t pid, paddr_t paddr, vaddr_t vaddr){

    int frame_index;

    KASSERT(pid >= 0);
    KASSERT(paddr != 0);
    KASSERT(vaddr != 0);
    
    frame_index = paddr/PAGE_SIZE;
    KASSERT(frame_index <= nRamFrames);

    ipt[frame_index].pid=pid;
    ipt[frame_index].vaddr=vaddr;

return 0;

}
