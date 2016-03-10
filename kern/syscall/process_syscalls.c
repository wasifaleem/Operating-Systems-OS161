#include <limits.h>
#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <current.h>
#include <proc.h>
#include <synch.h>
#include <uio.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <kern/wait.h>


int sys_getpid(pid_t *retval) {
    *retval = curproc->pid;
    return 0;
}

void sys__exit(int code) {
    exit_pid(&curproc->pid, _MKWAIT_EXIT(code));
}


int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval) {
    *retval = -1;
    int result, exitcode;
    if (options != 0
        && pid > 1
        && curproc->pid != pid) {
        return EINVAL;
    }
    if ((result = wait_pid(&pid, &exitcode))) {
        return result;
    }
    exitcode = _MKWAIT_EXIT(exitcode);
    if((result = copyout(&exitcode, status, sizeof(int)))) {
        return result;
    }
    *retval = pid;
    return 0;
}

static void child_entry(void *data1, unsigned long data2) {
    (void) data2;
    struct trapframe *ktf = data1;
    struct trapframe ctf = *ktf;
    kfree(ktf);
    kprintf("child_entry pid:%d\n", curproc->pid);
    enter_forked_process(&ctf);
}

int sys_fork(struct trapframe *tf, pid_t *retval) {
    *retval = -1;
    int result;
    struct proc *newproc;

    struct trapframe *ktf = kmalloc(sizeof(struct trapframe));
    if (ktf == NULL) {
        return ENOMEM;
    }
    *ktf = *tf; // no need for memcpy

    newproc = proc_create_fork(&result);
    if (newproc == NULL) {
        kfree(ktf);
        return result;
    }
    if ((result = thread_fork(curproc->p_name, newproc, child_entry, ktf, 0))) {
        kfree(ktf);
        proc_destroy(newproc);
        return result;
    }

    *retval = newproc->pid;
    return 0;
}