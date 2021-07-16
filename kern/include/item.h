/*
 * Modified version of the hash table library given at the laboratory number 11
 * of the Algorithm and Programming course, held by Stefano Quer at PoliTo. 
 * The library has been modified extensively modified to be suitable for the needs 
 * of this project. The original version can be found at the following link:
 * http://fmgroup.polito.it/quer/teaching/apaEn/laib/testi/lab11-HTLibrary/
 */

#ifndef _ITEM_H_
#define _ITEM_H_

#define MAXST 10

#include <types.h>

typedef struct item* Item;

typedef struct key_s{
    pid_t kpid;
    vaddr_t kaddr;
} Key;

Item ITEMscan (pid_t pid, vaddr_t addr, int index);
int ITEMcheckvoid(Item data);
Item ITEMsetvoid(void);
int KEYcompare(Key k1, Key k2);
Key KEYget(Item data);
Item ITEMsetnull(void);
void item_init(void);
#endif
