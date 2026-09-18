/* The handful of newlib hooks printf needs, written out rather than pulled in
 * from nosys.specs.
 *
 * Two reasons for doing it here.  The stubs from libnosys reference an `end`
 * symbol this linker script does not define, and more importantly _sbrk is
 * worth owning: the heap in the linker script is 1 KB on purpose, because
 * nothing in this firmware is supposed to allocate.  If _sbrk ever runs out,
 * that is information -- somebody has pulled in an allocator on a board whose
 * whole point is deterministic behaviour -- so it sets errno and fails
 * loudly instead of quietly handing back memory the stack was going to need.
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

extern char _sheap, _eheap;

void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr) {
    static char *brk;
    if (brk == 0) {
        brk = &_sheap;
    }
    char *const prev = brk;
    if (brk + incr > &_eheap) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

/* stdout is a UART, so it is a character device that is always ready and
 * never seekable.  Saying so keeps printf from asking further questions. */
int _isatty(int fd);
int _isatty(int fd) {
    (void)fd;
    return 1;
}

int _fstat(int fd, struct stat *st);
int _fstat(int fd, struct stat *st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

off_t _lseek(int fd, off_t off, int whence);
off_t _lseek(int fd, off_t off, int whence) {
    (void)fd;
    (void)off;
    (void)whence;
    return 0;
}

int _close(int fd);
int _close(int fd) {
    (void)fd;
    return -1;
}

int _read(int fd, char *buf, int len);
int _read(int fd, char *buf, int len) {
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}

int _getpid(void);
int _getpid(void) { return 1; }

int _kill(int pid, int sig);
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status);
void _exit(int status) {
    (void)status;
    for (;;) {
    }
}
