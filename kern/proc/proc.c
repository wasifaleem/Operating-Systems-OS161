/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <synch.h>


struct proc_meta {
    pid_t parent_pid;
    volatile bool exited;
    volatile int exit_code;
    struct lock* lk;
    struct cv* cv;
	struct proc* proc;
};

static struct proc_meta* proc_meta_create(void);
static void proc_meta_destroy(struct proc_meta*);

static struct proc_meta* process_metadata[PID_MAX];

static int assign_pid(struct proc* proc);
static int reclaim_pid(pid_t* pid);

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* file table */
	proc->p_fdtable = fdtable_create();
	if (proc->p_fdtable == NULL) {
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	/* file table */
	fdtable_destroy(proc->p_fdtable);

//	reclaim_pid(&proc->pid);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by fork.
 *
 * It will have parent's address space
 */
struct proc *proc_create_fork(int* errcode) {
    KASSERT(curproc != NULL);

	struct proc *newproc;
    int result;

	newproc = proc_create(curproc->p_name);
	if (newproc == NULL) {
        *errcode = ENOMEM;
        return NULL;
	}
	newproc->parent_pid = curproc->pid;
    if ((result = assign_pid(newproc))) {
        proc_destroy(newproc);
        *errcode = result;
        return NULL;
    }
    fdtable_copy(curproc->p_fdtable, newproc->p_fdtable);

	if ((result = as_copy(curproc->p_addrspace, &newproc->p_addrspace))) {
		fdtable_destroy(newproc->p_fdtable);
		proc_destroy(newproc);
        *errcode = result;
        return NULL;
	}
    /*
     * Lock the current process to copy its current directory.
     * (We don't need to lock the new process, though, as we have
     * the only reference to it.)
     */
//    spinlock_acquire(&curproc->p_lock);
//    if (curproc->p_cwd != NULL) {
//        VOP_INCREF(curproc->p_cwd);
//        newproc->p_cwd = curproc->p_cwd;
//    }
//    spinlock_release(&curproc->p_lock);

    return newproc;
}


/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	if (assign_pid(newproc)) {
		proc_destroy(newproc);
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

void exit_pid(pid_t *pid, int exitcode) {
	kprintf("S exit_pid pid:%d\n", *pid);
	KASSERT(process_metadata[*pid] != NULL);
	KASSERT(process_metadata[*pid] != 0);

	lock_acquire(process_metadata[*pid]->lk);
	KASSERT(process_metadata[*pid]->exited== false);

	for (int i = 0; i < PID_MAX; ++i) {
        if (process_metadata[i] != NULL
                && process_metadata[i]->parent_pid == *pid) {
			process_metadata[i]->parent_pid = -1;
//			if (process_metadata[i]->exited) {
//				reclaim_pid(&i);
//			}
		}
    }
	KASSERT(process_metadata[*pid] != NULL);
	process_metadata[*pid]->exited = true;
    process_metadata[*pid]->exit_code = exitcode;
//	proc_destroy(process_metadata[*pid]->proc);
	cv_broadcast(process_metadata[*pid]->cv, process_metadata[*pid]->lk);
    lock_release(process_metadata[*pid]->lk);
	kprintf("exit_pid pid:%d parent:%d\n", *pid, (process_metadata[*pid]->parent_pid));
	thread_exit(); // does not return
}

int wait_pid(pid_t *pid, int* exitcode) {
	KASSERT(process_metadata[*pid] != NULL);
	KASSERT(process_metadata[*pid] != 0);

	struct proc_meta *pmeta = process_metadata[*pid];
	if (pmeta == NULL) {
		return ESRCH;
	}
	if (pmeta->parent_pid != curproc->pid) {
		return ECHILD;
	}
	lock_acquire(pmeta->lk);
	while (pmeta->exited == false) {
		cv_wait(pmeta->cv, pmeta->lk);
	}
	KASSERT(pmeta->exited== true);
	*exitcode = pmeta->exit_code;
	lock_release(pmeta->lk);
	kprintf("wait_pid pid:%d parent:%d\n", *pid, (process_metadata[*pid]->parent_pid));

	reclaim_pid(pid);
	return 0;
}

static int assign_pid(struct proc* proc) {
	for (int i = PID_MIN+1; i <= PID_MAX; ++i) {
		if (process_metadata[i] == NULL) {
			proc->pid = i;
            struct proc_meta *pmeta = proc_meta_create();
            if (pmeta != NULL) {
                pmeta->proc = proc;
                process_metadata[i] = pmeta;
				kprintf("assigned pid:%d from parent:%d\n", proc->pid, proc->parent_pid);
				return 0;
            } else {
                return ENOMEM;
            }
		}
	}
	return ENPROC;
}

static int reclaim_pid(pid_t *pid) {
	struct proc_meta* m = process_metadata[*pid];
	if (m != NULL) {
		kprintf("reclaim pid:%d to proc:%s parent:%d\n", m->proc->pid, m->proc->p_name, m->proc->parent_pid);
		proc_meta_destroy(m);
		process_metadata[*pid] = NULL;
		return 0;
	}
	return ESRCH;
}

struct proc_meta *proc_meta_create(void) {
    struct proc_meta* procm = kmalloc(sizeof(struct proc_meta));
    if (procm == NULL) {
        return NULL;
    }
    procm->lk = lock_create("proc_meta_lock");
    if (procm->lk == NULL) {
        kfree(procm);
        return NULL;
    }
    procm->cv = cv_create("proc_meta_cv");
    if (procm->cv == NULL) {
        kfree(procm->lk);
        kfree(procm);
    }
    procm->exited = false;
	procm->parent_pid = curproc->pid;

    return procm;
}

void proc_meta_destroy(struct proc_meta *procm) {
    kfree(procm->cv);
    kfree(procm->lk);
    kfree(procm);
}
