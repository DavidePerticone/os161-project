#ifndef _INSTRUMENTATION_H
#define _INSTRUMENTATION_H


#define TLB_MISS 0 //OK
#define TLB_MISS_FREE 1 //OK
#define TLB_MISS_FULL 2 //OK
#define TLB_INVALIDATION 3 //OK
#define TLB_RELOAD 4 //OK
#define FAULT_WITH_LOAD 5 //OK
#define FAULT_WITH_ELF_LOAD 6
#define SWAP_OUT_PAGE 7
#define SWAP_IN_PAGE 8
#define NEW_PAGE_ZEROED 9 //OK



void init_instrumentation(void);

void increase(long int indicator);

void print_statistics(void);



#endif //_INSTRUMENTATION_H