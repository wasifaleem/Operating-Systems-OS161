#include <limits.h>
#include <types.h>
#include <copyinout.h>
#include <fcntl.h>
#include <syscall.h>

int sys_open(userptr_t filename, int flags, int *retval) {
    char path[PATH_MAX];
    int err;
    size_t actual;

    if ((err = copyinstr(filename, path, PATH_MAX, &actual)) != 0) {
        return err;
    }

    if (flags && O_ACCMODE) {

    }
}