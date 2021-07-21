#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <types.h>
#include <opt-list.h>
#include <vm.h>

#define MAX_SIZE 1024 * 1024 * 9
#define ENTRIES (MAX_SIZE / PAGE_SIZE)

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
int swap_in(vaddr_t page, paddr_t paddr);
int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim, pid_t pid_victim);

void free_swap_table(pid_t pid);
void print_swap(void);
void duplicate_swap_pages(pid_t old_pid, pid_t new_pid);

#endif

