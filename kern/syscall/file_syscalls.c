#include <limits.h>
#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <kern/errno.h>
#include <fdtable.h>
#include <current.h>
#include <proc.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/stat.h>
#include <synch.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <kern/seek.h>

int sys_open(userptr_t filename, int flags, int *retval) {
    *retval = -1;
    char path[PATH_MAX];
    int result;
    size_t actual;

    if ((result = copyinstr(filename, path, PATH_MAX, &actual)) != 0) {
        return result;
    }

    if (actual == 0) {
        return EFAULT;
    }

    int fd = find_available_fdesc();
    if (fd < 0) {
        return EMFILE;
    }

    struct fdesc *pfd = fdesc_create(path, flags);
    if (pfd == NULL) {
        return ENOMEM;
    }

    if ((result = vfs_open(path, flags, 0, &pfd->fd_vnode))) {
        release_fdesc(pfd);
        return result;
    }

    if (flags && O_APPEND) {
        struct stat s;
        if ((result = VOP_STAT(pfd->fd_vnode, &s))) {
            release_fdesc(pfd);
            return result;
        } else {
            lock_acquire(pfd->fd_lock);
            pfd->fd_offset = s.st_size;
            lock_release(pfd->fd_lock);
        }
    }
    curproc->p_fdtable->fdt_descs[fd] = pfd;
    *retval = fd;

    return 0;
}

int sys_close(int fd, int *retval) {
    *retval = -1;
    int result;
    if ((result = validate_fdesc(fd))) {
        return result;
    }
    struct fdesc *fdsc = curproc->p_fdtable->fdt_descs[fd];
    release_fdesc(fdsc);
    curproc->p_fdtable->fdt_descs[fd] = NULL;
    *retval = 0;
    return 0;
}


int sys_read(int fd, const_userptr_t buff, size_t buflen, int *retval) {
    *retval = -1;
    int result;
    if ((result = validate_fdesc(fd))) {
        return result;
    }

    void *kbuf = kmalloc(sizeof(char*));
    if ((result = copyout(kbuf, (userptr_t) buff, sizeof(char*))) != 0) {
        kfree(kbuf);
        return result;
    }
    kfree(kbuf);

    struct fdesc *fdsc = curproc->p_fdtable->fdt_descs[fd];
    if (fdsc->fd_flags & O_WRONLY) {
        return EBADF;
    }

    struct iovec iov;
    struct uio u;

    lock_acquire(fdsc->fd_lock);
    uio_uinit(&iov, &u, (userptr_t) buff, buflen, fdsc->fd_offset, UIO_READ);

    result = VOP_READ(fdsc->fd_vnode, &u);
    if (result) {
        lock_release(fdsc->fd_lock);
        return result;
    }

    fdsc->fd_offset = u.uio_offset;
    lock_release(fdsc->fd_lock);

    *retval = buflen - u.uio_resid;
    return 0;
}


int sys_write(int fd, const_userptr_t buff, size_t nbytes, int *retval) {
    *retval = -1;
    int result;
    if ((result = validate_fdesc(fd))) {
        return result;
    }

    void *kbuf = kmalloc(sizeof(char*));
    if ((result = copyin((const_userptr_t) buff, kbuf, sizeof(char*))) != 0) {
        kfree(kbuf);
        return result;
    }
    kfree(kbuf);

    struct fdesc *fdsc = curproc->p_fdtable->fdt_descs[fd];

    if (fdsc->fd_flags == O_RDONLY || fdsc->fd_flags == O_CREAT) {
        return EBADF;
    }

    struct iovec iov;
    struct uio u;

    lock_acquire(fdsc->fd_lock);
    uio_uinit(&iov, &u, (userptr_t) buff, nbytes, fdsc->fd_offset, UIO_WRITE);

    result = VOP_WRITE(fdsc->fd_vnode, &u);
    if (result) {
        lock_release(fdsc->fd_lock);
        return result;
    }

    fdsc->fd_offset = u.uio_offset;
    lock_release(fdsc->fd_lock);

    *retval = nbytes - u.uio_resid;

    return 0;
}


int sys_dup2(int oldfd, int newfd, int *retval) {
    *retval = -1;
    int result;
    if ((result = validate_fdesc(oldfd))) {
        return result;
    }
    if (newfd < 0 || newfd >= OPEN_MAX) {
        return EBADF;
    }
    if (oldfd == newfd) {
        *retval = oldfd;
        return 0;
    }
    struct fdesc *old_fdsc = curproc->p_fdtable->fdt_descs[oldfd];
    struct fdesc *new_fdsc = curproc->p_fdtable->fdt_descs[newfd];
    if (new_fdsc != NULL) {
        sys_close(newfd, &result);
    }
    new_fdsc = old_fdsc;

    lock_acquire(new_fdsc->fd_lock);
    new_fdsc->fd_ref_count++;
    curproc->p_fdtable->fdt_descs[newfd] = new_fdsc;
    lock_release(new_fdsc->fd_lock);

    *retval = newfd;
    return 0;
}

int sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
    *retval = -1;
    int result;
    if ((result = validate_fdesc(fd))) {
        return result;
    }

    struct fdesc *fdsc = curproc->p_fdtable->fdt_descs[fd];
    if (!VOP_ISSEEKABLE(fdsc->fd_vnode)) {
        return ESPIPE;
    }
    lock_acquire(fdsc->fd_lock);
    switch (whence) {
        case SEEK_SET: {
            *retval = pos;
            break;
        }
        case SEEK_CUR: {
            *retval = fdsc->fd_offset + pos;
            break;
        }
        case SEEK_END: {
            struct stat s;
            if ((result = VOP_STAT(fdsc->fd_vnode, &s))) {
                return result;
            } else {
                *retval = pos + s.st_size;
            }
            break;
        }
        default: {
            lock_release(fdsc->fd_lock);
            return EINVAL;
        }
    }

    if (*retval < 0) {
//        *retval= -1;
        lock_release(fdsc->fd_lock);
        return EINVAL;
    }

    fdsc->fd_offset = *retval;
    lock_release(fdsc->fd_lock);

    return 0;
}


int sys_chdir(const_userptr_t pathname, int *retval) {
    *retval = -1;
    char path[PATH_MAX];
    int result;
    size_t actual;

    if ((result = copyinstr(pathname, path, PATH_MAX, &actual)) != 0) {
        return result;
    }
    if (actual == 0) {
        return EFAULT;
    }

    if ((result = vfs_chdir(path))) {
        return result;
    }
    *retval = 0;
    return 0;
}

int sys___getcwd(const_userptr_t buff, size_t buflen, int *retval) {
    *retval = -1;
    int result;

    void *kbuf = kmalloc(buflen);
    if ((result = copyout(kbuf, (userptr_t) buff, buflen)) != 0) {
        kfree(kbuf);
        return result;
    }
    kfree(kbuf);

    struct iovec iov;
    struct uio u;

    uio_uinit(&iov, &u, (userptr_t) buff, buflen, 0, UIO_READ);
    if ((result = vfs_getcwd(&u))) {
        return result;
    }
    *retval = buflen - u.uio_resid;

    return 0;
}