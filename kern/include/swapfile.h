#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <types.h>
#include <opt-list.h>
struct swap_entry
{

    pid_t pid;
    vaddr_t page;
    #if OPT_LIST
    off_t file_offset;
    struct swap_entry *next, *previous;
    #endif

};

void init_swapfile(void);
int swap_in(vaddr_t page);
int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim);

void free_swap_table(pid_t pid);
void print_swap(void);


#endif

