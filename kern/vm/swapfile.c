#include <swapfile.h>
#include <kern/fcntl.h>
#include <syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/unistd.h>
#include <vfs.h>
#include <vm.h>
#include <kern/iovec.h>
#include <current.h>
#include <vnode.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>
#include <kern/errno.h>
#include <instrumentation.h>
#include <opt-list.h>
#include <addrspace.h>
#include <coremap.h>

#define DUMPOUT 0
#define DUMPIN 0

#if OPT_LIST

int swap_fd;

static struct swap_entry *free_list_head;
static struct swap_entry *free_list_tail;

static struct swap_entry *swap_list;
static struct vnode *v = NULL;
static struct spinlock swap_lock = SPINLOCK_INITIALIZER;

static void add_free_entry(struct swap_entry *fentry)
{
    fentry->next = free_list_head->next;
    free_list_head->next = fentry;
}

static struct swap_entry *search_swap_list(pid_t pid, vaddr_t vaddr)
{
    struct swap_entry *tmp;

    tmp = swap_list->next;

    for (int i = 0; i < ENTRIES; i++)
    {
        if (tmp == free_list_tail)
        {
            return NULL;
        }

        if (tmp->pid == pid && tmp->page == vaddr)
        {
            return tmp;
        }

        tmp = tmp->next;
    }

    panic("Should not get here while searching in swap list\n");
}

static struct swap_entry *search_swap_list_pid(pid_t pid, struct swap_entry **tmp)
{
    spinlock_acquire(&swap_lock);
    
    if (*tmp == NULL)
    {
        *tmp = swap_list->next;
    }

    for (int i = 0; i < ENTRIES; i++)
    {
        if (*tmp == free_list_tail)
        {
            return NULL;
            spinlock_release(&swap_lock);

        }

        if ((*tmp)->pid == pid)
        {
            spinlock_release(&swap_lock);
            return *tmp;
        }

        *tmp = (*tmp)->next;
    }

    panic("Should not get here while searching in swap list\n");
}

static void add_swap_list(struct swap_entry *entry)
{

    entry->next = swap_list->next;
    entry->previous = swap_list;
    swap_list->next->previous = entry;
    swap_list->next = entry;
}

static void remove_swap_list(struct swap_entry *entry)
{

    struct swap_entry *prev, *next;

    KASSERT(entry != swap_list && entry != free_list_tail);

    prev = entry->previous;
    next = entry->next;
    prev->next = next;
    next->previous = prev;
}

/* end list functions for swap_list */

/* list functions for free entries */

static void entry_list_init(int maxN)
{

    int i;
    struct swap_entry *tmp;

    free_list_head = kmalloc(sizeof(struct swap_entry));
    free_list_tail = kmalloc(sizeof(struct swap_entry));
    free_list_head->next = free_list_tail;

    swap_list = kmalloc(sizeof(struct swap_entry));
    swap_list->next = free_list_tail;
    swap_list->previous = NULL;

    for (i = 0; i < maxN; i++)
    {
        tmp = kmalloc(sizeof(struct swap_entry));
        tmp->pid = -1;
        tmp->file_offset = i * PAGE_SIZE;
        add_free_entry(tmp);
    }
}

static struct swap_entry *get_free_entry(void)
{
    struct swap_entry *tmp;

    tmp = free_list_head->next;
    if (tmp == free_list_tail)
    {
        panic("no free entry in swaptable");
    }
    free_list_head->next = tmp->next;

    return tmp;
}

/* end list functions for free entries*/

/* 
 * Initialize swapfile. If the file does not exists, it is created.
 * If the file exists, it is truncated at length 0 (content cleared).
 */
void init_swapfile(void)
{
    int result;
    result = vfs_open((char *)"./SWAPFILE", O_RDWR | O_CREAT | O_TRUNC, 777, &v);
    KASSERT(result != -1);
    entry_list_init(ENTRIES);
}

static int
file_read_paddr(struct vnode *vn, paddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nread;

    iov.iov_ubase = (userptr_t)(PADDR_TO_KVADDR(buf_ptr));
    iov.iov_len = size;

    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = size; // amount to read from the file
    u.uio_offset = offset;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = NULL;

    result = VOP_READ(vn, &u);
    if (result)
    {
        return result;
    }

    if (u.uio_resid != 0)
    {
        kprintf("SWAPPING IN: short read on page - problems reading?\n");
        return EFAULT;
    }

    nread = size - u.uio_resid;
    return (nread);
}

int swap_in(vaddr_t page, paddr_t paddr)
{

    int result;
    pid_t pid;
    pid = curproc->p_pid;
    struct swap_entry *entry;
    off_t offset;

    /* page must be in swap file */
    spinlock_acquire(&swap_lock);

    entry = search_swap_list(pid, page);

    if (entry != NULL)
    {
        offset = entry->file_offset;
        remove_swap_list(entry);
        spinlock_release(&swap_lock);
        result = file_read_paddr(v, paddr, PAGE_SIZE, offset);
        KASSERT(result == PAGE_SIZE);

        spinlock_acquire(&swap_lock);
        add_free_entry(entry);
        spinlock_release(&swap_lock);

        increase(SWAP_IN_PAGE);
        increase(FAULT_WITH_LOAD);
        return 0;
    }

    spinlock_release(&swap_lock);

    return 1;
}
/*
 *  This function actually swaps out the page. In principle, there is a problem.
 *  If a process tries to swap out a page that does not own, the conversion from
 *  vaddr of that page (the victim) to paddr, is done using the address space 
 *  of the current process (the one doing the swap). This means that the translation
 *  will be incorrect, and the process will swap out a frame that is not the one 
 *  corresponding to the victmi.
 *  In order to solve the problem, we perform a write as if we are writing from kernel
 *  swap. Basically, we receive the paddr of the page to swap out, we add KSEG0, and we
 *  perform the write with UIO_SYSSPACE. In this way, the translation is done as if it was
 *  a kernel address, allowing us to perform a write using almost directly a physical address.
 */

static int
file_write_paddr(struct vnode *vn, paddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nwrite;

    iov.iov_ubase = (userptr_t)(PADDR_TO_KVADDR(buf_ptr));
    iov.iov_len = size;

    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = size; // amount to read from the file
    u.uio_offset = offset;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_WRITE;
    u.uio_space = NULL;

    result = VOP_WRITE(vn, &u);
    if (result)
    {
        return result;
    }

    nwrite = size - u.uio_resid;
    return (nwrite);
}

/*
 * swap_out receives both paddr and vaddr. 
 * paddr is used to actually perform the swap-out (i.e., we give it to the inner function).
 * vaddr is used to save the entry in the swap table. A future look-up in the swap table,
 * will get the vaddr of the frame we are looking for, so we must save the vaddr when performing
 * swap out.
 */

int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim, pid_t pid_victim)
{

    int result;
    struct swap_entry *entry;

    /*if the page to swap out is in the segment, do not swap out */
    if (segment_victim == 1)
    {
        return 0;
    }
    KASSERT(curproc->p_pid == pid_victim);

    spinlock_acquire(&swap_lock);

    entry = get_free_entry();
    entry->page = vaddr;
    entry->pid = pid_victim;

    spinlock_release(&swap_lock);
    result = file_write_paddr(v, paddr, PAGE_SIZE, entry->file_offset);
    if (result != PAGE_SIZE)
    {
        panic("Unable to swap page out");
    }
    spinlock_acquire(&swap_lock);
    add_swap_list(entry);
    spinlock_release(&swap_lock);
    KASSERT(result >= 0);
    increase(SWAP_OUT_PAGE);
    return 0;
}

void free_swap_table(pid_t pid)
{

    struct swap_entry *tmp = swap_list->next, *next;

    spinlock_acquire(&swap_lock);

    KASSERT(pid >= 0);

    for (int i = 0; i < ENTRIES; i++)
    {
        if (tmp == free_list_tail)
        {
            spinlock_release(&swap_lock);
            return;
        }

        next = tmp->next;

        if (tmp->pid == pid)
        {
            remove_swap_list(tmp);
            add_free_entry(tmp);
        }

        tmp = next;
    }

    panic("Should not get here while freeing swap list\n");

    //  free_recursive(pid, swap_list);
}

static void print_recursive(struct swap_entry *next)
{

    if (next == free_list_tail)
    {
        return;
    }

    kprintf("%llu -   %d   - %d\n", next->file_offset / PAGE_SIZE, next->pid, next->page / PAGE_SIZE);

    print_recursive(next->next);
}

void print_swap(void)
{

    spinlock_acquire(&swap_lock);

    kprintf("<< SWAP TABLE >>\n");

    print_recursive(swap_list->next);

    spinlock_release(&swap_lock);
}

void duplicate_swap_pages(pid_t old_pid, pid_t new_pid)
{

    int result;
    paddr_t paddr;
    struct swap_entry *tmp;
    struct swap_entry *entry;
    paddr = as_prepare_load(1);

    /* page must be in swap file */

    while (search_swap_list_pid(old_pid, &tmp) != NULL)
    {
        spinlock_acquire(&swap_lock);

        entry = get_free_entry();

        spinlock_release(&swap_lock);

        result = file_read_paddr(v, paddr, PAGE_SIZE, tmp->file_offset);
        if (result != PAGE_SIZE)
        {
            panic("Unable to read page from swap file");
        }
        result = file_write_paddr(v, paddr, PAGE_SIZE,  tmp->file_offset);
        if (result != PAGE_SIZE)
        {
            panic("Unable to swap page out for fork");
        }
        spinlock_acquire(&swap_lock);
        entry->page = tmp->page;
        entry->pid = new_pid;

        spinlock_release(&swap_lock);

    }
    spinlock_release(&swap_lock);


}

#else

int swap_fd;

static struct swap_entry swap_table[ENTRIES];
static struct vnode *v = NULL;
static struct spinlock swap_lock = SPINLOCK_INITIALIZER;

static void print_swap_internal(void)
{

    kprintf("<< SWAP TABLE >>\n");

    for (int i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid != -1)
        {
            kprintf("%d -   %d   - %d\n", i, swap_table[i].pid, swap_table[i].page / PAGE_SIZE);
        }
    }
}

/* 
 * Initialize swapfile. If the file does not exists, it is created.
 * If the file exists, it is truncated at length 0 (content cleared).
 */
void init_swapfile(void)
{
    int i, result;
    result = vfs_open((char *)"./SWAPFILE", O_RDWR | O_CREAT | O_TRUNC, 777, &v);
    KASSERT(result != -1);
    spinlock_acquire(&swap_lock);
    for (i = 0; i < ENTRIES; i++)
    {
        swap_table[i].pid = -1;
    }
    spinlock_release(&swap_lock);
}

static int
file_read_paddr(struct vnode *vn, paddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nread;

    iov.iov_ubase = (userptr_t)(PADDR_TO_KVADDR(buf_ptr));
    iov.iov_len = size;

    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = size; // amount to read from the file
    u.uio_offset = offset;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = NULL;

    result = VOP_READ(vn, &u);
    if (result)
    {
        return result;
    }

    if (u.uio_resid != 0)
    {
        kprintf("SWAPPING IN: short read on page - problems reading?\n");
        return EFAULT;
    }

    nread = size - u.uio_resid;
    return (nread);
}

int swap_in(vaddr_t page, paddr_t paddr)
{

    int result, i;
    pid_t pid;
    pid = curproc->p_pid;

    spinlock_acquire(&swap_lock);
    /* page must be in swap file */
    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == pid && swap_table[i].page == page)
        {
            spinlock_release(&swap_lock);
            result = file_read_paddr(v, paddr, PAGE_SIZE, i * PAGE_SIZE);

            spinlock_acquire(&swap_lock);
            swap_table[i].pid = -1;
#if DUMPIN
            kprintf("Swapping in PID %d PAGE %d\n", curproc->p_pid, page / PAGE_SIZE);
#endif
            spinlock_release(&swap_lock);

            KASSERT(result == PAGE_SIZE);
            increase(SWAP_IN_PAGE);
            increase(FAULT_WITH_LOAD);
            return 0;
        }
    }
    spinlock_release(&swap_lock);
    return 1;
}
/*
 *  This function actually swaps out the page. In principle, there is a problem.
 *  If a process tries to swap out a page that does not own, the conversion from
 *  vaddr of that page (the victim) to paddr, is done using the address space 
 *  of the current process (the one doing the swap). This means that the translation
 *  will be incorrect, and the process will swap out a frame that is not the one 
 *  corresponding to the victmi.
 *  In order to solve the problem, we perform a write as if we are writing from kernel
 *  swap. Basically, we receive the paddr of the page to swap out, we add KSEG0, and we
 *  perform the write with UIO_SYSSPACE. In this way, the translation is done as if it was
 *  a kernel address, allowing us to perform a write using almost directly a physical address.
 */

static int
file_write_paddr(struct vnode *vn, paddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nwrite;

    iov.iov_ubase = (userptr_t)(PADDR_TO_KVADDR(buf_ptr));
    iov.iov_len = size;

    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = size; // amount to read from the file
    u.uio_offset = offset;
    u.uio_segflg = UIO_SYSSPACE;
    u.uio_rw = UIO_WRITE;
    u.uio_space = NULL;

    result = VOP_WRITE(vn, &u);
    if (result)
    {
        return result;
    }

    nwrite = size - u.uio_resid;
    return (nwrite);
}

/*
 * swap_out receives both paddr and vaddr. 
 * paddr is used to actually perform the swap-out (i.e., we give it to the inner function).
 * vaddr is used to save the entry in the swap table. A future look-up in the swap table,
 * will get the vaddr of the frame we are looking for, so we must save the vaddr when performing
 * swap out.
 */

int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim, pid_t pid_victim)
{

    int result, i;

    KASSERT(pid_victim != -1);

    /*if the page to swap out is in the segment, do not swap out */
    if (segment_victim == 1)
    {
        return 0;
    }

    spinlock_acquire(&swap_lock);
    /* iterate though the swap_table to find a free entry */
    for (i = 0; i < ENTRIES; i++)
    {
        /* if pid of entry is -1, it is free */
        if (swap_table[i].pid == -1)
        {
            /* set pid of entry to -2, so that no one else can select the entry as free */
            swap_table[i].pid = -2;

#if DUMPOUT
            kprintf("Start swapping out PID %d PAGE %d\n", pid_victim, vaddr / PAGE_SIZE);
#endif
            spinlock_release(&swap_lock);
            /* actually write on file */
            result = file_write_paddr(v, paddr, PAGE_SIZE, i * PAGE_SIZE);
            spinlock_acquire(&swap_lock);
            if (result != PAGE_SIZE)
            {
                panic("Unable to swap page out");
            }

            KASSERT(result >= 0);

            swap_table[i].pid = pid_victim;
            swap_table[i].page = vaddr;

            spinlock_release(&swap_lock);
            increase(SWAP_OUT_PAGE);
            return 0;
        }
    }
    spinlock_release(&swap_lock);

    panic("Out of swapspace\n");
}

void free_swap_table(pid_t pid)
{
    spinlock_acquire(&swap_lock);

    KASSERT(pid >= 0);

    for (int i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == pid)
        {
            swap_table[i].pid = -1;
        }
    }

    spinlock_release(&swap_lock);
}

void print_swap(void)
{

    spinlock_acquire(&swap_lock);

    kprintf("<< SWAP TABLE >>\n");

    for (int i = 0; i < ENTRIES; i++)
    {
        kprintf("%d -   %d   - %d\n", i, swap_table[i].pid, swap_table[i].page / PAGE_SIZE);
    }

    spinlock_release(&swap_lock);
}

void duplicate_swap_pages(pid_t old_pid, pid_t new_pid)
{

    int result, i, j;
    paddr_t paddr;

    paddr = as_prepare_load(1);

    spinlock_acquire(&swap_lock);
    /* page must be in swap file */
    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == old_pid)
        {
            for (j = 0; j < ENTRIES; j++)
            {
                if (swap_table[j].pid == -1)
                {
                    swap_table[j].pid = new_pid;
                    swap_table[j].page = swap_table[i].page;
                    print_swap_internal();
                    spinlock_release(&swap_lock);

                    result = file_read_paddr(v, paddr, PAGE_SIZE, i * PAGE_SIZE);
                    if (result != PAGE_SIZE)
                    {
                        panic("Unable to read page from swap file");
                    }
                    result = file_write_paddr(v, paddr, PAGE_SIZE, j * PAGE_SIZE);
                    if (result != PAGE_SIZE)
                    {
                        panic("Unable to swap page out for fork");
                    }
                    spinlock_acquire(&swap_lock);

                    KASSERT(result >= 0);
                    break;
                }
            }
            if (j == ENTRIES)
            {
                panic("Swap file is full");
            }
        }
    }
    spinlock_release(&swap_lock);
    freeppages(paddr, paddr / PAGE_SIZE);
}

#endif

