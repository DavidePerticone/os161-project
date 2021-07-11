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

/* 
 * Initialize swapfile. If the file does not exists, it is created.
 * If the file exists, it is truncated at length 0 (content cleared).
 */
void init_swapfile(void)
{
    int i, result;
    result = vfs_open((char *)"./swapfile", O_RDWR | O_CREAT | O_TRUNC, 777, &v);
    KASSERT(result != -1);

    for (i = 0; i < ENTRIES; i++)
    {
        swap_table[i].pid = -1;
    }
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
    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == pid && swap_table[i].page == page)
        {
            result = file_read_paddr(v, page, PAGE_SIZE, i * PAGE_SIZE);
            KASSERT(result == PAGE_SIZE);
            swap_table[i].pid = -1;
            return 0;
        }
    }

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



static void print_swap(void)
{

    kprintf("<< SWAP TABLE >>\n");

    for(int i = 0; i < ENTRIES; i++) {
        kprintf("%d -   %d   - %d\n", i, swap_table[i].pid, swap_table[i].page / PAGE_SIZE);
    }


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


    for (i = 0; i < ENTRIES; i++)
    {
        if (swap_table[i].pid == -1)
        {

            result = file_write_paddr(v, paddr, PAGE_SIZE, i * PAGE_SIZE);
            if (result != PAGE_SIZE)
            {
                panic("Unable to swap page out");
            }

            KASSERT(result >= 0);
            swap_table[i].pid = pid;
            swap_table[i].page = vaddr;
            return 0;
        }
    }

    print_swap();

    panic("Out of swapspace\n");
}

