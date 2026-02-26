#include "../include/syscall.h"

void _start() {
    write(1, "TEST", 4);
    _exit(42);
    for (;;);
}
