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

    if (status != NULL && (result = copyout(&exitcode, status, sizeof(int)))) {
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

int sys_execv(userptr_t program, userptr_t args, int *retval) {
    *retval = -1;
    int result;
    char path[PATH_MAX];
    size_t actual;

    if ((result = copyinstr(program, path, PATH_MAX, &actual)) != 0) {
        return result;
    }

    if (actual == 0) {
        return EFAULT;
    }

    size_t kbuff_len = 0;
    int nargs = 0;
    char *kbuff = kmalloc(ARG_MAX);
    if (kbuff == NULL) {
        kfree(kbuff);
        return ENOMEM;
    }

    char *arg;
    for (int i = 0; i < NARG_MAX; ++i) {
        if ((result = copyin(args, &arg, sizeof(char *)))) {
            kfree(kbuff);
            return result;
        }

        if (arg == NULL) {
            break;
        }

        if ((result = copyinstr((const_userptr_t) arg, kbuff + kbuff_len, PATH_MAX, &actual))) {
            kfree(kbuff);
            return result;
        }
        kbuff_len += actual;
        args += sizeof(char *);
        nargs++;
    }


    struct vnode *v;
    struct addrspace *as;
    vaddr_t entrypoint, stackptr;

    /* Open the file. */
    if ((result = vfs_open(path, O_RDONLY, 0, &v))) {
        kfree(kbuff);
        return result;
    }

    /* Create a new address space. */
    as = as_create();
    if (as == NULL) {
        kfree(kbuff);
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    proc_setas(as);
    as_activate();

    /* Load the executable. */
    if ((result = load_elf(v, &entrypoint))) {
        kfree(kbuff);
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);
    /* Define the user stack in the address space */
    if ((result = as_define_stack(as, &stackptr))) {
        kfree(kbuff);
        return result;
    }

    vaddr_t string_start, args_start, argv, current;
    size_t pos = 0;

    stackptr -= kbuff_len;
    stackptr -= (stackptr & (sizeof(void *) - 1));
    string_start = stackptr;

    stackptr -= (nargs + 1) * sizeof(vaddr_t);
    args_start = stackptr;

    argv = args_start;
    while (pos < kbuff_len) {
        current = string_start + pos;

        if ((result = copyout(&current, (userptr_t) argv, sizeof(char*)))) {
            kfree(kbuff);
            return result;
        }

        if ((result = copyoutstr(kbuff + pos, (userptr_t) current, PATH_MAX, &actual))) {
            kfree(kbuff);
            return result;
        }

        pos += actual;
        argv += sizeof(vaddr_t);

        if (pos == kbuff_len) {
            char* pad_null = NULL;
            result = copyout(&pad_null, (userptr_t) argv, sizeof(char*));
            if (result) {
                kfree(kbuff);
                return result;
            }
        }
    }

    *retval = 0;
    enter_new_process(nargs, (userptr_t) args_start, NULL, stackptr, entrypoint);


    return 0;
}