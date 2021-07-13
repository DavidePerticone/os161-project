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
