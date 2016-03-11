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
#include <kern/fcntl.h>
#include <vfs.h>


#define NARG_MAX 1024

int sys_getpid(pid_t *retval) {
    *retval = curproc->pid;
    return 0;
}

void sys__exit(int code) {
    exit_pid(curproc->pid, code);
}


int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval) {
    *retval = -1;
    int result, exitcode;

    if (pid < PID_MIN) {
        return ESRCH;
    }

    if (options != 0
        && curproc->pid != pid) {
        return EINVAL;
    }
    if ((result = wait_pid(pid, &exitcode))) {
        return result;
    }

    if(status != NULL && (result = copyout(&exitcode, status, sizeof(int)))) {
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

struct karg_buff {
    char* buff;
    size_t len;
    int nargs;
};

int sys_execv(userptr_t program, userptr_t args, int *retval) {

    return 0;
}