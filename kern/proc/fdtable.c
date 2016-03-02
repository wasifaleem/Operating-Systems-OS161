#include <fdtable.h>
#include <lib.h>
#include <synch.h>
#include <vnode.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/fcntl.h>
#include <vfs.h>

struct fdtable *fdtable_create(void) {
    struct fdtable *fdtable;

    fdtable = kmalloc(sizeof(*fdtable));
    if (fdtable == NULL) {
        return NULL;
    }

    for (int i = 0; i < OPEN_MAX; ++i) {
        fdtable->fdt_descs[i] = NULL;
    }

    return fdtable;
}

void fdtable_destroy(struct fdtable *fdtable) {
    KASSERT(fdtable != NULL);

    kfree(fdtable);
}

void fdtable_copy(struct fdtable *from, struct fdtable *to) {
    KASSERT(from != NULL);
    KASSERT(to != NULL);

    for (int i = 0; i < OPEN_MAX; ++i) {
        if (from->fdt_descs[i] != NULL) {
            to->fdt_descs[i] = from->fdt_descs[i];

            lock_acquire(to->fdt_descs[i]->fd_lock);
            ++(to->fdt_descs[i]->fd_ref_count);
            lock_release(to->fdt_descs[i]->fd_lock);
        }
    }
}

struct fdesc *fdesc_create(const char *path, int flags) {
    struct fdesc *fdesc;

    fdesc = kmalloc(sizeof(*fdesc));
    if (fdesc == NULL) {
        return NULL;
    }
    fdesc->fd_path = kstrdup(path);
    if (fdesc->fd_path == NULL) {
        kfree(fdesc);
        return NULL;
    }

    fdesc->fd_flags = flags;
    fdesc->fd_offset = 0;
    fdesc->fd_ref_count = 1;

    fdesc->fd_vnode = NULL;
    fdesc->fd_vnode = kmalloc(sizeof(struct vnode *));
    if (fdesc->fd_vnode == NULL) {
        kfree(fdesc->fd_path);
        kfree(fdesc);
        return NULL;
    }

    fdesc->fd_lock = lock_create(path);
    if (fdesc->fd_lock == NULL) {
        kfree(fdesc->fd_vnode);
        kfree(fdesc->fd_path);
        kfree(fdesc);
        return NULL;
    }

    return fdesc;
}

void fdesc_destroy(struct fdesc *fdesc) {
    KASSERT(fdesc != NULL);
    KASSERT(fdesc->fd_ref_count == 0);

    lock_destroy(fdesc->fd_lock);
    kfree(fdesc->fd_vnode);
    kfree(fdesc->fd_path);
    kfree(fdesc);
}


int find_available_fdesc(void) {
    for (int i = 3; i < OPEN_MAX; ++i) {
        if ((curproc->p_fdtable)->fdt_descs[i] == NULL) {
            return i;
        }
    }
    return -1;
}

int validate_fdesc(int fd) {
    if (fd < 0 || fd >= OPEN_MAX || curproc->p_fdtable->fdt_descs[fd] == NULL) {
        return EBADF;
    }
    return 0;
}

void init_console_fdescs(void) {
    kprintf("Creating console from proc:thread %s:%s...\n", curproc->p_name, curthread->t_name);

    int result;
    char *stdin = kstrdup("con:");
    char *stdout = kstrdup("con:");
    char *stderr = kstrdup("con:");

    struct fdesc* stds[3];

    stds[STDIN_FILENO] = fdesc_create(stdin, O_RDONLY);
    result = vfs_open(stdin, O_RDONLY, 0, &stds[STDIN_FILENO]->fd_vnode);
    KASSERT(result == 0);

    stds[STDOUT_FILENO] = fdesc_create(stdout, O_WRONLY);
    result = vfs_open(stdout, O_WRONLY, 0, &stds[STDOUT_FILENO]->fd_vnode);
    KASSERT(result == 0);

    stds[STDERR_FILENO] = fdesc_create(stderr, O_WRONLY);
    result = vfs_open(stderr, O_WRONLY, 0, &stds[STDERR_FILENO]->fd_vnode);
    KASSERT(result == 0);

    curproc->p_fdtable->fdt_descs[STDIN_FILENO] = stds[STDIN_FILENO];
    curproc->p_fdtable->fdt_descs[STDOUT_FILENO] = stds[STDOUT_FILENO];
    curproc->p_fdtable->fdt_descs[STDERR_FILENO] = stds[STDERR_FILENO];
}
