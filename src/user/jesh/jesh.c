#include "../include/syscall.h"
#include "../include/ulib.h"

static char buf[1024];

void _start() {

    write(1, ">", 1);

    int fd = open("/dev/cons", O_RDONLY);

    while (1) {
        int n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            write(1, buf, n);
        }
    }
    _exit(1);
}
