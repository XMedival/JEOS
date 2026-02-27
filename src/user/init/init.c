#include "../include/syscall.h"

static char *path = "/bin/jesh";

void _start() {
    write(1, "init.c\n", 7);
    while (1) {
        int pid = fork();
        if (pid == 0) {
            // in child
            const char *argv[] = { path, NULL };
            exec(path, argv);
            write(1, "OOPS\n", 5);
            _exit(2);
        } else if (pid > 0) {
            int status;
            wait(&status);
            write(1, "WAIT\n", 5);
        } else {
            write(1, "BIG OOPS\n", 9);
            _exit(1);
        }
    }
}
