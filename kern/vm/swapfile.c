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

#define MAX_SIZE 1024 * 1024 * 9
#define ENTRIES (MAX_SIZE / 4096)

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
    result = vfs_open((char *)"./swapfile", O_RDWR | O_CREAT | O_TRUNC, 777, &v);
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
            

            return 0;
        }
    }

    spinlock_release(&swap_lock);

    return 1;
}

static int
file_write_paddr(struct vnode *vn, vaddr_t buf_ptr, size_t size, off_t offset)
{
    struct iovec iov;
    struct uio u;
    int result, nwrite;

    iov.iov_ubase = (userptr_t)(buf_ptr);
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

int swap_out(vaddr_t page, int segment_victim)
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
            swap_table[i].page = page;
            spinlock_release(&swap_lock);

            result = file_write_paddr(v, page, PAGE_SIZE, i * PAGE_SIZE);
            if (result != PAGE_SIZE)
            {
                panic("Unable to swap page out");
            }

            KASSERT(result >= 0);
            
            return 0;
        }
    }
    spinlock_release(&swap_lock);

    panic("Out of swapspace\n");
}



/* TODO: free swap_table entries when process exits */