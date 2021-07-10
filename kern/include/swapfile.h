#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include<types.h>
struct swap_entry
{

    pid_t pid;
    vaddr_t page;

};

void init_swapfile(void);
int swap_in(vaddr_t page);
int swap_out(vaddr_t page, int segment_victim);
void free_swap_table(pid_t pid);
void print_swap(void);


#endif

