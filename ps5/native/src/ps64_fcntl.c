/*
 * fcntl(O_NONBLOCK) doesn't take effect on sockets in a native PS5 app (in
 * the sandbox libc may also refuse it with EINVAL), so a socket stays
 * blocking and the first recv() with nothing to read never returns. Linked
 * with --wrap=fcntl, every fcntl on a socket also sets the console's own
 * non-blocking option SO_NBIO (0x1200), as the native app boilerplate does.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/socket.h>

#define CONSOLE_SO_NBIO 0x1200

int __real_fcntl(int fd, int cmd, ...);
int __wrap_fcntl(int fd, int cmd, ...);

static int is_socket(int fd)
{
    int type = 0;
    socklen_t len = sizeof(type);
    return getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0;
}

int __wrap_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    intptr_t arg = va_arg(ap, intptr_t);       /* every command PS64 and SDL use takes an int */
    va_end(ap);
    if (!is_socket(fd))
        return __real_fcntl(fd, cmd, arg);

    switch (cmd) {
    case F_SETFL: {
        int on = (arg & O_NONBLOCK) != 0;
        return setsockopt(fd, SOL_SOCKET, CONSOLE_SO_NBIO, &on, sizeof(on));
    }
    case F_GETFL: {
        int on = 0;
        socklen_t len = sizeof(on);
        if (getsockopt(fd, SOL_SOCKET, CONSOLE_SO_NBIO, &on, &len) < 0)
            return -1;
        return O_RDWR | (on ? O_NONBLOCK : 0);
    }
    case F_GETFD:
    case F_SETFD:
        return 0;                               /* an app never execs another program */
    }
    int rc = __real_fcntl(fd, cmd, arg);
    return rc;
}
