#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include<types.h>
struct swap_entry
{

    pid_t pid;
    vaddr_t page;
    off_t file_offset;
    struct swap_entry *next, *previous;

};

void init_swapfile(void);
int swap_in(vaddr_t page);
int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim);

void free_swap_table(pid_t pid);
void print_swap(void);


#endif

