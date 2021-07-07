#include <swapfile.h>
#include <kern/fcntl.h>
#include <syscall.h>
#include <types.h>
#include <lib.h>
#include <kern/unistd.h>
#include <vfs.h>

//static int fd;
static struct vnode *v = NULL;

void init_swapfile(void)
{
    int result;

    if (v == NULL)
    {
        kprintf("2\n");
        result = vfs_open((char *)"./swapfile", O_RDWR | O_CREAT | O_TRUNC, 777, &v);
        kprintf("3\n");
        KASSERT(result != -1);
    }
}
