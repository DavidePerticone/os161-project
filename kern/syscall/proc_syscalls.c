/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <pt.h>
#include <current.h>
#include <swapfile.h>
#include <opt-waitpid.h>

#define PRINT_TABLES 0

/*
 * simple proc management system calls
 */
void sys__exit(int status)
{

    pid_t pid = curproc->p_pid;


#if OPT_WAITPID
  struct proc *p = curproc;
  p->p_status = status & 0xff; /* just lower 8 bits returned */
  proc_remthread(curthread);
  lock_acquire(p->p_wlock);
  cv_signal(p->p_cv, p->p_wlock);
  lock_release(p->p_wlock);

#else
  /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();
  as_destroy(as);
#endif

  /* thread exits. proc data structure will be lost */
  free_ipt_process(pid);
  /* free swap_table entries when process exits */
  free_swap_table(pid);
#if PRINT_TABLES
  print_ipt();
  print_swap();
#endif

  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void)status; // TODO: status handling
}

int sys_waitpid(pid_t pid, userptr_t statusp, int options)
{
#if OPT_WAITPID
  struct proc *p = proc_search_pid(pid);
  int s;
  (void)options; /* not handled */
  if (p == NULL)
    return -1;
  s = proc_wait(p);
  if (statusp != NULL)
    *(int *)statusp = s;
  return pid;
#else
  (void)options; /* not handled */
  (void)pid;
  (void)statusp;
  return -1;
#endif
}

pid_t sys_getpid(void)
{
#if OPT_WAITPID
  KASSERT(curproc != NULL);
  return curproc->p_pid;
#else
  return -1;
#endif
}
