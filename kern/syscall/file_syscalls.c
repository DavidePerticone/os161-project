/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys_read and sys_write.
 * just works (partially) on stdin/stdout
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <clock.h>
#include <syscall.h>
#include <current.h>
#include <lib.h>
#include "opt-paging.h"

#if OPT_PAGING

#include <copyinout.h>
#include <vnode.h>
#include <vfs.h>
#include <limits.h>
#include <uio.h>
#include <proc.h>
#include <kern/seek.h>

/* max num of system wide open files */
#define SYSTEM_OPEN_MAX (10 * OPEN_MAX)

#define USE_KERNEL_BUFFER 0

/* system open file table */
struct openfile
{
  struct vnode *vn;
  off_t offset;
  unsigned int countRef;
};

struct openfile systemFileTable[SYSTEM_OPEN_MAX];

void openfileIncrRefCount(struct openfile *of)
{
  if (of != NULL)
    of->countRef++;
}

#if USE_KERNEL_BUFFER

static int
file_read(int fd, userptr_t buf_ptr, size_t size)
{
  struct iovec iov;
  struct uio ku;
  int result, nread;
  struct vnode *vn;
  struct openfile *of;
  void *kbuf;

  if (fd < 0 || fd > OPEN_MAX)
    return -1;
  of = curproc->fileTable[fd];
  if (of == NULL)
    return -1;
  vn = of->vn;
  if (vn == NULL)
    return -1;

  kbuf = kmalloc(size);
  uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_READ);
  result = VOP_READ(vn, &ku);
  if (result)
  {
    return result;
  }
  of->offset = ku.uio_offset;
  nread = size - ku.uio_resid;
  copyout(kbuf, buf_ptr, nread);
  kfree(kbuf);
  return (nread);
}

static int
file_write(int fd, userptr_t buf_ptr, size_t size)
{
  struct iovec iov;
  struct uio ku;
  int result, nwrite;
  struct vnode *vn;
  struct openfile *of;
  void *kbuf;

  if (fd < 0 || fd > OPEN_MAX)
    return -1;
  of = curproc->fileTable[fd];
  if (of == NULL)
    return -1;
  vn = of->vn;
  if (vn == NULL)
    return -1;

  kbuf = kmalloc(size);
  copyin(buf_ptr, kbuf, size);
  uio_kinit(&iov, &ku, kbuf, size, of->offset, UIO_WRITE);
  result = VOP_WRITE(vn, &ku);
  if (result)
  {
    return result;
  }
  kfree(kbuf);
  of->offset = ku.uio_offset;
  nwrite = size - ku.uio_resid;
  return (nwrite);
}

#else

static int
file_read(int fd, userptr_t buf_ptr, size_t size)
{
  struct iovec iov;
  struct uio u;
  int result;
  struct vnode *vn;
  struct openfile *of;

  if (fd < 0 || fd > OPEN_MAX)
    return -1;
  of = curproc->fileTable[fd];
  if (of == NULL)
    return -1;
  vn = of->vn;
  if (vn == NULL)
    return -1;

  iov.iov_ubase = buf_ptr;
  iov.iov_len = size;

  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = size; // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg = UIO_USERISPACE;
  u.uio_rw = UIO_READ;
  u.uio_space = curproc->p_addrspace;

  result = VOP_READ(vn, &u);
  if (result)
  {
    return result;
  }

  of->offset = u.uio_offset;
  return (size - u.uio_resid);
}

static int
file_write(int fd, userptr_t buf_ptr, size_t size)
{
  struct iovec iov;
  struct uio u;
  int result, nwrite;
  struct vnode *vn;
  struct openfile *of;

  if (fd < 0 || fd > OPEN_MAX)
    return -1;
  of = curproc->fileTable[fd];
  if (of == NULL)
    return -1;
  vn = of->vn;
  if (vn == NULL)
    return -1;

  iov.iov_ubase = buf_ptr;
  iov.iov_len = size;

  u.uio_iov = &iov;
  u.uio_iovcnt = 1;
  u.uio_resid = size; // amount to read from the file
  u.uio_offset = of->offset;
  u.uio_segflg = UIO_USERISPACE;
  u.uio_rw = UIO_WRITE;
  u.uio_space = curproc->p_addrspace;

  result = VOP_WRITE(vn, &u);
  if (result)
  {
    return result;
  }
  of->offset = u.uio_offset;
  nwrite = size - u.uio_resid;
  return (nwrite);
}

int sys_lseek(int fd, off_t offset, int whence, int *retval) {
  struct openfile *of=NULL;; 	

  of = curproc->fileTable[fd];

  if (of == NULL) {
    kprintf("Error in sys_lseek: fd %d for process %d, file not open\n",
      fd, curproc->p_pid);
    *retval = -1;
    return EBADF;
  }
  int errcode = 0;
  int res = file_seek(of, offset, whence, &errcode);
  if (res != 0 || errcode != 0) {
    DEBUG(DB_SYSCALL, "Error in sys_lseek: fd %d for process %d, code=%d, err=%s\n",
      fd, curproc->p_pid, errcode, strerror(errcode));
    *retval = -1;
    return errcode;
  }

  KASSERT(of->offset == offset);

  *retval = of->offset;
  return 0;
}

int file_seek(struct openfile *of, off_t offset, int whence, int *errcode) {
	if (!of || !filedes_is_seekable(of)) {
		*errcode = EBADF;
		return -1;
	}
	off_t new_offset = offset;

	switch(whence) {
  case SEEK_SET:
		new_offset = offset;
		break;
	default:
		*errcode = EINVAL;
    kprintf("Error in file_seek: different seek start position not implemented\n");
		return -1;
	}

	if (new_offset < 0) {
		*errcode = EINVAL;
		return -1;
	}
	DEBUGASSERT(of->offset >= 0);
	of->offset = new_offset;
	return 0;
}

/* check if the file is seekable */
bool filedes_is_seekable(struct openfile *of) {
	KASSERT(of);
	// if (file_des->ftype != FILEDES_TYPE_REG) return false;
	if (!of->vn) return false;
	return VOP_ISSEEKABLE(of->vn);
}

#endif

/*
 * file system calls for open/close
 */
int sys_open(userptr_t path, int openflags, mode_t mode, int *errp)
{
  int fd, i;
  struct vnode *v;
  struct openfile *of = NULL;
  ;
  int result;

  result = vfs_open((char *)path, openflags, mode, &v);
  if (result)
  {
    *errp = ENOENT;
    return -1;
  }
  /* search system open file table */
  for (i = 0; i < SYSTEM_OPEN_MAX; i++)
  {
    if (systemFileTable[i].vn == NULL)
    {
      of = &systemFileTable[i];
      of->vn = v;
      of->offset = 0; // TODO: handle offset with append
      of->countRef = 1;
      break;
    }
  }
  if (of == NULL)
  {
    // no free slot in system open file table
    *errp = ENFILE;
  }
  else
  {
    for (fd = STDERR_FILENO + 1; fd < OPEN_MAX; fd++)
    {
      if (curproc->fileTable[fd] == NULL)
      {
        curproc->fileTable[fd] = of;
        return fd;
      }
    }
    // no free slot in process open file table
    *errp = EMFILE;
  }

  vfs_close(v);
  return -1;
}

/*
 * file system calls for open/close
 */
int sys_close(int fd)
{
  struct openfile *of = NULL;
  struct vnode *vn;

  if (fd < 0 || fd > OPEN_MAX)
    return -1;
  of = curproc->fileTable[fd];
  if (of == NULL)
    return -1;
  curproc->fileTable[fd] = NULL;

  if (--of->countRef > 0)
    return 0; // just decrement ref cnt
  vn = of->vn;
  of->vn = NULL;
  if (vn == NULL)
    return -1;

  vfs_close(vn);
  return 0;
}

#endif

/*
 * simple file system calls for write/read
 */

static struct spinlock sp = SPINLOCK_INITIALIZER;
int sys_write(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;
  

  if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
  {
#if OPT_PAGING
    return file_write(fd, buf_ptr, size);
#else
    kprintf("sys_write supported only to stdout\n");
    return -1;
#endif
  }
  spinlock_acquire(&sp);
  for (i = 0; i < (int)size; i++)
  {
    putch(p[i]);
  }
  spinlock_release(&sp);
  return (int)size;
}

int sys_read(int fd, userptr_t buf_ptr, size_t size)
{
  int i;
  char *p = (char *)buf_ptr;

  if (fd != STDIN_FILENO)
  {
#if OPT_PAGING
    return file_read(fd, buf_ptr, size);
#else
    kprintf("sys_read supported only to stdin\n");
    return -1;
#endif
  }

  for (i = 0; i < (int)size; i++)
  {
    p[i] = getch();
    if (p[i] < 0)
      return i;
  }

  return (int)size;
}