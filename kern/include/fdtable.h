#ifndef _FD_TABLE_H_
#define _FD_TABLE_H_

#include <limits.h>
#include <types.h>

struct fdesc {
    char * fd_path;
    int fd_flags;
    off_t fd_offset;
    unsigned int fd_ref_count;
    struct vnode *fd_vnode;
    struct lock *fd_lock;
};

struct fdtable {
    struct fdesc *fdt_descs[OPEN_MAX];
};

struct fdtable* fdtable_create(void);
void fdtable_destroy(struct fdtable *);
/* copies curproc filetable to */
void fdtable_copy(struct fdtable * from, struct fdtable * to);


struct fdesc *fdesc_create(struct vnode* vn, const char * path, int flags);
int find_available_fdesc(void);
int validate_fdesc(int fd);
void init_console_fdescs(void);

void fdesc_destroy(struct fdesc *);
void release_fdesc(struct fdesc *);


#endif /* _FD_TABLE_H_ */
