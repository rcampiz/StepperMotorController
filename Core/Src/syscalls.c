/**
 * @file syscalls.c
 * @brief Minimal system call stubs for newlib
 *
 * These are minimal implementations of system calls required by newlib.
 * They provide just enough functionality for basic embedded operation.
 */

#include <sys/stat.h>
#include <errno.h>

#undef errno
extern int errno;

// Increase heap pointer
extern char _end; // Defined by the linker
extern char _estack; // Defined by the linker
static char *heap_end = 0;

void *_sbrk(int incr) {
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &_end;
    }

    prev_heap_end = heap_end;

    // Check if we have enough space
    if (heap_end + incr > &_estack) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_end += incr;
    return (void *)prev_heap_end;
}

int _close(int file) {
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file) {
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len) {
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _write(int file, char *ptr, int len) {
    (void)file;
    (void)ptr;
    (void)len;
    return len;
}

void _exit(int status) {
    (void)status;
    while (1) {
        // Infinite loop
    }
}

int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void) {
    return 1;
}