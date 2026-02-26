#include "../include/syscall.h"
#include "../include/ulib.h"

static int strcmp_fixed(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

void _start() {
    char buf[1024];

    write(1, ">", 1);

    while (1) {
        int n = read(0, buf, sizeof(buf) - 1);
        if (n <= 0) break;                 // EOF albo błąd

        buf[n] = '\0';                     // teraz to C-string

        if (n > 0 && buf[n-1] == '\n')      // utnij newline
            buf[n-1] = '\0';

        write(1, buf, n);                  // echo (opcjonalnie zostaw)

        if (strcmp_fixed(buf, "ls") == 0) {
            const char *msg = "IT WORKED!!\n";
            write(1, msg, 12);
        }
    }
    _exit(1);
}
