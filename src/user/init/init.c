#include "../include/syscall.h"

static char *path = "/bin/jesh";

void _start() {
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
        } else {
            write(1, "BIG OOPS\n", 9);
            _exit(1);
        }
    }
}
