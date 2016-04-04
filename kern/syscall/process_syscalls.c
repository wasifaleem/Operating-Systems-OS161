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


int sys_getpid(pid_t *retval)
{
	*retval = curproc->pid;
	return 0;
}

void sys__exit(int code)
{
	exit_pid(curproc->pid, code);
}


int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval)
{
	*retval = -1;
	int result, exitcode = -1;

	if (pid < PID_MIN) {
		return ESRCH;
	}

	if (options != 0
		&& curproc->pid != pid) {
		return EINVAL;
	}

	bool status_requested = status != NULL;


	if ((result = wait_pid(pid, &exitcode))) {
		return result;
	}

	if (status_requested && (result = copyout(&exitcode, status, sizeof(int)))) {
		return result;
	}

	*retval = pid;
	return 0;
}

static void child_entry(void *data1, unsigned long data2)
{
	(void) data2;
	struct trapframe ctf = *(struct trapframe *) data1;
	kfree(data1);
	enter_forked_process(&ctf);
}

int sys_fork(struct trapframe *tf, pid_t *retval)
{
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

int sys_execv(userptr_t program, userptr_t args, int *retval)
{
	*retval = -1;
	int result;
	char *pad_null = NULL;
	char *path = kmalloc(PATH_MAX);
	size_t actual;

	if ((result = copyinstr(program, path, PATH_MAX, &actual)) != 0) {
		return result;
	}

	if (actual < 2) {
		return EINVAL;
	}

	size_t kbuff_len = 0;
	unsigned int nargs = 1;
	char *kbuff = kmalloc(ARG_MAX);
	if (kbuff == NULL) {
		kfree(kbuff);
		return EFAULT;
	}

	char *arg;
	for (; ; nargs++, kbuff_len += actual, args += sizeof(char *)) {
		if ((result = copyin(args, &arg, sizeof(char *)))) {
			kfree(kbuff);
			return result;
		}

		if (arg == NULL) {
			break;
		}

		if ((result = copyinstr((const_userptr_t) arg, kbuff + kbuff_len, ARG_MAX, &actual))) {
			kfree(kbuff);
			return result;
		}
	}
	actual = 0;

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
	struct addrspace *old = proc_setas(as);
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

	stackptr = stackptr - kbuff_len;

	vaddr_t string_start, args_start;
	string_start = stackptr - (stackptr & (sizeof(void *) - 1));
	args_start = string_start - nargs * sizeof(vaddr_t);

	for (size_t i = 0, j = 0; i < kbuff_len; i = i + actual, j++) {
		vaddr_t current_str = string_start + i;
		vaddr_t current_argv = args_start + (j * sizeof(vaddr_t));
		if ((result = copyout(&current_str, (userptr_t) current_argv, sizeof(char *)))) {
			kfree(kbuff);
			return result;
		}

		if ((result = copyoutstr(kbuff + i, (userptr_t) current_str, ARG_MAX, &actual))) {
			kfree(kbuff);
			return result;
		}

		if (j == nargs) {
			if ((result = copyout(&pad_null, (userptr_t) current_argv, sizeof(char *)))) {
				kfree(kbuff);
				return result;
			}
		}
	}

	kfree(path);
	kfree(kbuff);
	if (old) {
		as_destroy(old);
	}

	*retval = 0;
	enter_new_process(nargs - 1, (userptr_t) args_start, NULL, args_start, entrypoint);


	return 0;
}