#include "../include/syscall.h"

static int old_x = -1;
static int old_y = -1;
static int initialized = 0;

static void draw_cursor(int fb, int x, int y, struct fb_info *info, int clear) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)info->width) x = info->width - 1;
    if (y >= (int)info->height) y = info->height - 1;
    
    int size = 32;
    for (int dy = -size; dy < size; dy++) {
        for (int dx = -size; dx < size; dx++) {
            if (dx*dx + dy*dy < size*size) {
                unsigned long off = (y + dy) * info->pitch + (x + dx) * 4;
                if (off + 4 <= info->size && off >= 0) {
                    seek(fb, off, 0);
                    unsigned char pixel[4] = {0, 0, clear ? 0 : 255, 255};
                    write(fb, pixel, 4);
                }
            }
        }
    }
}

static int parse_int(const char *s, int *out) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    *out = neg ? -val : val;
    return val;
}

static int read_mouse_pos(int mousectl, int *x, int *y, int *buttons) {
    char buf[64];
    int n = read(mousectl, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
    
    // const char dbg[] = "mouse: ";
    // write(1, dbg, sizeof(dbg) - 1);
    // write(1, buf, n);
    // write(1, "\n", 1);
    
    if (buf[0] == '-' || (buf[0] >= '0' && buf[0] <= '9')) {
        const char *p = buf;
        parse_int(p, x);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        parse_int(p, y);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        parse_int(p, buttons);
        return 1;
    }
    return 0;
}

void _start(void) {
    struct fb_info fb;
    if (fbinfo(&fb) < 0) {
        for (;;) { int s; wait(&s); }
    }

    int fbfd = open("/dev/fb", O_RDWR);
    if (fbfd < 0) {
        for (;;) { int s; wait(&s); }
    }

    int mouse = open("/dev/mouse", O_RDONLY);
    int mousectl = open("/dev/mousectl", O_RDWR);
    
    if (mouse < 0 || mousectl < 0) {
        for (;;) { int s; wait(&s); }
    }

    // Draw test circle in center
    int cx = fb.width / 2;
    int cy = fb.height / 2;
    int r = 50;
    for (int dy = -r; dy < r; dy++) {
        for (int dx = -r; dx < r; dx++) {
            if (dx*dx + dy*dy < r*r) {
                unsigned long off = (cy + dy) * fb.pitch + (cx + dx) * 4;
                if (off + 4 <= fb.size) {
                    seek(fbfd, off, 0);
                    unsigned char pixel[4] = {0, 255, 0, 255}; // Green
                    write(fbfd, pixel, 4);
                }
            }
        }
    }

    char buf[256];
    for (;;) {
        int n = read(mouse, buf, sizeof(buf));
        if (n > 0) {
            int x, y, buttons;
            read_mouse_pos(mousectl, &x, &y, &buttons);
            
            // const char pos[] = "pos update\n";
            // write(1, pos, sizeof(pos) - 1);
            
            if (initialized) {
                draw_cursor(fbfd, old_x, old_y, &fb, 1);
            }
            draw_cursor(fbfd, x, y, &fb, 0);
            old_x = x;
            old_y = y;
            initialized = 1;
        }
    }
}
