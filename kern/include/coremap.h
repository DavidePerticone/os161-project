#ifndef _COREMAP_H
#define _COREMAP_H

#include <types.h>

int init_freeRamFrames(int nRamFrames);
int init_allocSize(int nRamFrames);
void destroy_freeRamFrames(void);
void print_freeRamFrames(void);
int freeppages(paddr_t addr, long npages);
paddr_t getppages(unsigned long npages, int kmem);

#endif