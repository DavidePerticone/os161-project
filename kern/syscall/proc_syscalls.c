/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
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
#include "opt-fork.h"
#include <mips/trapframe.h>

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


#if OPT_FORK
static void
call_enter_forked_process(void *tfv, unsigned long dummy) {
  struct trapframe *tf = (struct trapframe *)tfv;
  (void)dummy;
  enter_forked_process(tf); 
 
  panic("enter_forked_process returned (should not happen)\n");
}

int sys_fork(struct trapframe *ctf, pid_t *retval) {
  struct trapframe *tf_child;
  struct proc *newp;
  int result;

  KASSERT(curproc != NULL);

  newp = proc_create_runprogram(curproc->p_name);
  if (newp == NULL) {
    return ENOMEM;
  }

  newp->p_eh=curproc->p_eh;

  /* done here as we need to duplicate the address space 
     of thbe current process */
  as_copy(curproc->p_addrspace, &(newp->p_addrspace), curproc->p_pid);
  if(newp->p_addrspace == NULL){
    proc_destroy(newp); 
    return ENOMEM; 
  }

  /* we need a copy of the parent's trapframe */
  tf_child = kmalloc(sizeof(struct trapframe));
  if(tf_child == NULL){
    proc_destroy(newp);
    return ENOMEM; 
  }
  memcpy(tf_child, ctf, sizeof(struct trapframe));

  /* TO BE DONE: linking parent/child, so that child terminated 
     on parent exit */

  result = thread_fork(
		 curthread->t_name, newp,
		 call_enter_forked_process, 
		 (void *)tf_child, (unsigned long)0/*unused*/);

  if (result){
    proc_destroy(newp);
    kfree(tf_child);
    return ENOMEM;
  }

  *retval = newp->p_pid;

  return 0;
}
#endif