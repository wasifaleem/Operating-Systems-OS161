#include <fdtable.h>
#include <lib.h>
#include <synch.h>
#include <vnode.h>

struct fdesc *fdesc_create(const char *path, unsigned int flags) {
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
    fdesc->fd_offset= 0;
    fdesc->fd_ref_count = 1;

//  TODO: allocate vnode when needed?
    fdesc->fd_vnode = NULL;
//    fdesc->fd_vnode = kmalloc(sizeof(struct vnode *));
//    if (fdesc->fd_vnode == NULL) {
//        kfree(fdesc->fd_path);
//        kfree(fdesc);
//        return NULL;
//    }

    fdesc->fd_lock = lock_create(path);
    if (fdesc->fd_lock == NULL) {
//        kfree(fdesc->fd_vnode);
        kfree(fdesc->fd_path);
        kfree(fdesc);
        return NULL;
    }

    return fdesc;
}


void fdesc_destroy(struct fdesc *fdesc) {
    KASSERT(fdesc != NULL);
    KASSERT(fdesc->fd_ref_count == 0);

    // TODO: clean up vnode?

    lock_destroy(fdesc->fd_lock);
    kfree(fdesc->fd_path);
    kfree(fdesc);
}

struct fdtable *fdtable_create() {
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
