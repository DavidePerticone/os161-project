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

#define MAX_SIZE 1024 * 1024 * 9
#define ENTRIES (MAX_SIZE / 4096)

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
    kprintf("TAIL: %p\n", free_list_tail);
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
file_read_paddr(struct vnode *vn, vaddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nread;

    iov.iov_ubase = (userptr_t)(buf_ptr);
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

int swap_in(vaddr_t page)
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
        result = file_read_paddr(v, page, PAGE_SIZE, offset);
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

int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim)
{

    /*if the page to swap out is in the segment, do not swap out */
    if (segment_victim == 1)
    {
        return 0;
    }
    int result;
    pid_t pid;
    struct swap_entry *entry;

    pid = curproc->p_pid;

    spinlock_acquire(&swap_lock);

    entry = get_free_entry();
    entry->page = vaddr;
    entry->pid = pid;

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

    struct swap_entry *tmp=swap_list->next, *next;

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

        tmp=next;
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


#else


int swap_fd;

static struct swap_entry swap_table[ENTRIES];
static struct vnode *v = NULL;
static struct spinlock swap_lock = SPINLOCK_INITIALIZER;


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
file_read_paddr(struct vnode *vn, vaddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nread;

    iov.iov_ubase = (userptr_t)(buf_ptr);
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

int swap_in(vaddr_t page)
{

    int result, i;
    pid_t pid;
    pid = curproc->p_pid;

    /* page must be in swap file */
    spinlock_acquire(&swap_lock);
    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == pid && swap_table[i].page == page)
        {
            //kprintf("Swapping in\n");
            swap_table[i].pid = -1;
            spinlock_release(&swap_lock);
            result = file_read_paddr(v, page, PAGE_SIZE, i * PAGE_SIZE);
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


int swap_out(paddr_t paddr, vaddr_t vaddr, int segment_victim)
{

    /*if the page to swap out is in the segment, do not swap out */
    if (segment_victim == 1)
    {
        return 0;
    }
    int result, i;
    pid_t pid;

    pid = curproc->p_pid;

    spinlock_acquire(&swap_lock);
    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == -1)
        {
            swap_table[i].pid = pid;
            swap_table[i].page = vaddr;
            spinlock_release(&swap_lock);

            result = file_write_paddr(v, paddr, PAGE_SIZE, i * PAGE_SIZE);
            if (result != PAGE_SIZE)
            {
                panic("Unable to swap page out");
            }

            KASSERT(result >= 0);
            increase(SWAP_OUT_PAGE);
            return 0;
        }
    }
    spinlock_release(&swap_lock);

    print_swap();

    panic("Out of swapspace\n");
}



void free_swap_table(pid_t pid)
{
    spinlock_acquire(&swap_lock);

    KASSERT(pid >= 0);

    for(int i = 0; i < ENTRIES; i++) {
        if(swap_table[i].pid == pid) {
            swap_table[i].pid = -1;
        }
    }

    spinlock_release(&swap_lock);
}

void print_swap(void)
{

    spinlock_acquire(&swap_lock);

    kprintf("<< SWAP TABLE >>\n");

    for(int i = 0; i < ENTRIES; i++) {
        kprintf("%d -   %d   - %d\n", i, swap_table[i].pid, swap_table[i].page / PAGE_SIZE);
    }

    spinlock_release(&swap_lock);

}



#endif