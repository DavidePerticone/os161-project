#include "item.h"
#include "st.h"
#include <types.h>
#include <lib.h>

typedef struct STnode *link;
link NEW(Item item, link next);
int hashU(Key v, int M);
Item searchST(link t, Key k, link z);
void visitR(link h, link z);
link deleteR(link x, Key k);

struct STnode
{
    Item item;
    link next;
};

struct symboltable
{
    link *heads;
    int M;
    link z;
};

link NEW(Item item, link next)
{
    link x = kmalloc(sizeof *x);
    KASSERT(x != NULL);
    x->item = item;
    x->next = next;
    return x;
}

ST STinit(int maxN)
{
    int i;
    ST st = kmalloc(sizeof *st);
    KASSERT(st != NULL);

    st->M = maxN;
    st->heads = kmalloc(st->M * sizeof(link));
    KASSERT(st->heads != NULL);
    st->z = NEW(ITEMsetvoid(), NULL);

    for (i = 0; i < st->M; i++)
        st->heads[i] = st->z;

    return st;
}

int hashU(Key v, int M)
{
    int sum = v.kaddr + v.kpid;
    int h = sum % M;
    return h;
}

void STinsert(ST st, Item item)
{
    int i;

    i = hashU(KEYget(item), st->M);

    kprintf(" hash index = %d\n", i);

    st->heads[i] = NEW(item, st->heads[i]);

    return;
}

Item searchST(link t, Key k, link z)
{
    if (t == z)
        return ITEMsetvoid();

    if ((KEYcompare(KEYget(t->item), k)) == 0)
        return t->item;

    return (searchST(t->next, k, z));
}

Item STsearch(ST st, pid_t pid, vaddr_t addr)
{
    Key k;
    k.kaddr = addr;
    k.kpid = pid;
    return searchST(st->heads[hashU(k, st->M)], k, st->z);
}

link deleteR(link x, Key k)
{
    if (x == NULL)
        return NULL;

    if ((KEYcompare(KEYget(x->item), k)) == 0)
    {
        link t = x->next;
        kfree(x);
        return t;
    }

    x->next = deleteR(x->next, k);

    return x;
}

void STdelete(ST st, pid_t pid, vaddr_t addr)
{
    Key k;
    k.kpid = pid;
    k.kaddr = addr;
    int i = hashU(k, st->M);
    st->heads[i] = deleteR(st->heads[i], k);

    return;
}

void visitR(link h, link z)
{
    if (h == z)
        return;

    visitR(h->next, z);

    return;
}

void STdisplay(ST st)
{
    int i;

    for (i = 0; i < st->M; i++)
    {
        kprintf("st->heads[%d]: ", i);
        visitR(st->heads[i], st->z);
        kprintf("\n");
    }

    return;
}