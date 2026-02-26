#include "../include/syscall.h"
#include "../include/ulib.h"

static char buf;

void _start() {

    write(1, ">", 1);

    int fd = open("/dev/cons", O_RDONLY);

    for (;;) {
        int n = read(fd, &buf, 1);
        if (n > 0) {
            write(1, &buf, 1);
        }
    }
    _exit(1);
}
