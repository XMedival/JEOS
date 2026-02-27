#include "../include/syscall.h"
#include "../include/ulib.h"

#define LINE_MAX 128

static int read_line(int in_fd, int out_fd, char line[LINE_MAX]);

void _start() {
    int in = open("/dev/cons", O_RDONLY);

    for (;;) {
        write(1, "> ", 2);
        char line[LINE_MAX];
        int n = read_line(in, 1, line);

        // TODO: parse/exec line[0..n)
    }

    _exit(1);
}

static int read_line(int in_fd, int out_fd, char line[LINE_MAX]) {
    int len = 0;

    for (;;) {
        char c;
        int n = read(in_fd, &c, 1);
        if (n <= 0) continue;

        if (c == '\r') c = '\n';

        if (c == '\n') {
            write(out_fd, "\n", 1);
            line[len] = 0;
            return len;
        }

        if (c == 0x08 || c == 0x7f) { // BS / DEL
            if (len > 0) {
                len--;
                write(out_fd, "\b \b", 3);
            }
            continue;
        }

        if (len < LINE_MAX - 1) {
            line[len++] = c;
            write(out_fd, &c, 1); // echo typed char
        } else {
            // overflow policy: ignore / bell / auto-submit
            // write(out_fd, "\a", 1);
        }
    }
}
