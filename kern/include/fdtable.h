#ifndef _FD_TABLE_H_
#define _FD_TABLE_H_

#include <limits.h>
#include <types.h>

struct fdesc {
    const char * fd_path;
    unsigned int fd_flags;
    off_t fd_offset;
//    mode_t fd_mode;
    unsigned int fd_ref_count;
    struct vnode *fd_vnode;
    struct lock *fd_lock;
};

struct fdesc *fdesc_create(const char * path, unsigned int flags);

void fdesc_destroy(struct fdesc *);

struct fdtable {
    struct fdesc *fdt_descs[OPEN_MAX];
};

struct fdtable *fdtable_create();

/* copies curproc filetable to */
void fdtable_copy(struct fdtable * from, struct fdtable * to);

void fdtable_destroy(struct fdtable *);

#endif /* _FD_TABLE_H_ */
