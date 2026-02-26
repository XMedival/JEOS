#include "../include/syscall.h"
#include "../include/ulib.h"

void _start() {
    char buf[1024];
    write(1, "TEST", 4);
    while (1) {
        int n = read(0, buf, sizeof(buf));
        write(1, buf, n);
        if (strcmp(buf, "ls") == 0) {
            char *msg = "IT WORKED!!";
            write(1, msg, 11);
        } 
    }
}
