#include "item.h"
#include "st.h"
#include <types.h>
#include <lib.h>

struct item {
  Key key;
  int index;
};

typedef struct STnode *link;
link NEW(Item item, link next);
int hashU(Key v, int M);
Item searchST(link t, Key k, link z);
void visitR(link h, link z);
link deleteR(link x, Key k);
static link link_list;
static int free_link;
static int n_entries;

struct STnode
{
    struct item item;
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
   
    int j=0;
    while(link_list[free_link].item.index != -1){
        free_link++;
        j++;
        if(free_link >= n_entries){
            free_link=0;
        }
        if(j >= n_entries){
            panic("No free entry in hash table\n");
        }

    }
    link x = &link_list[free_link];
    KASSERT(x != NULL);
    
    x->item.index = item->index;
    x->item.key.kaddr = item->key.kaddr;
    x->item.key.kpid = item->key.kpid;
    x->next = next;
    return x;
}

ST STinit(int maxN)
{
    int i;
    ST st = kmalloc(sizeof *st);
    KASSERT(st != NULL);
    link_list=kmalloc(sizeof(struct STnode)*maxN);
    item_init();
    for(i=0; i<maxN; i++){
        link_list[i].item.index=-1;
    }
    free_link=0;
    n_entries=maxN;
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
    st->heads[i] = NEW(item, st->heads[i]);

    return;
}

Item searchST(link t, Key k, link z)
{
    int comparison;

    if (t == z)
        return ITEMsetnull();

     Key KEY = KEYget(&(t->item));

    comparison=(KEYcompare(KEY, k));

    if ( comparison == 0)
        return &t->item;

    return (searchST(t->next, k, z));
}

int STsearch(ST st, pid_t pid, vaddr_t addr)
{
    Key k;
    k.kaddr = addr;
    k.kpid = pid;
    int index;
    index = hashU(k, st->M);
    Item res = searchST(st->heads[index], k, st->z);
    return res != NULL ? res->index : -1;
}

link deleteR(link x, Key k)
{
    if (x == NULL)
        return NULL;

    if ((KEYcompare(KEYget(&(x->item)), k)) == 0)
    {
        link t = x->next;
        x->item.index=-1;
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
        kprintf("st->heads[%d]: %d", i, st->heads[i]->item.key.kaddr);
        visitR(st->heads[i], st->z);
        kprintf("\n");
    }

    return;
}

