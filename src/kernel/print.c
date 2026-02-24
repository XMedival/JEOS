#include "print.h"
#include "kconsole.h"
#include "serial.h"
#include <stdarg.h>

void putc(char c) {
    serial_putc(c);
    kconsole_putc(c);
}

void puts(const char *s) {
    while (*s) {
        putc(*s++);
    }
}

void putdec(u64 n) {
    if (n == 0) {
        putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (--i >= 0) {
        putc(buf[i]);
    }
}

static void puthex_n(u64 n, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        u8 nibble = (n >> (i * 4)) & 0xF;
        putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
}

void puthex8(u8 n) {
    puthex_n(n, 2);
}

void puthex16(u16 n) {
    puthex_n(n, 4);
}

void puthex32(u32 n) {
    puthex_n(n, 8);
}

void puthex64(u64 n) {
    puthex_n(n, 16);
}

void puthex(u64 n) {
    if (n == 0) {
        putc('0');
        return;
    }
    // Find highest non-zero nibble
    int digits = 16;
    while (digits > 1 && ((n >> ((digits - 1) * 4)) & 0xF) == 0) {
        digits--;
    }
    puthex_n(n, digits);
}

// Helper: get string length
static int strlen_(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

// Helper: print padding
static void pad(int count, char c) {
    while (count-- > 0) putc(c);
}

// Helper: format number to buffer, return length
static int fmt_dec(char *buf, u64 n) {
    if (n == 0) { buf[0] = '0'; return 1; }
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    // reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    return i;
}

static int fmt_hex(char *buf, u64 n) {
    if (n == 0) { buf[0] = '0'; return 1; }
    int i = 0;
    while (n > 0) {
        u8 nib = n & 0xF;
        buf[i++] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        n >>= 4;
    }
    // reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    return i;
}

static void do_vprintf(const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            continue;
        }
        fmt++;  // skip '%'

        // Parse flags
        int left_align = 0, zero_pad = 0, center = 0;
        while (*fmt == '-' || *fmt == '0' || *fmt == '^') {
            if (*fmt == '-') left_align = 1;
            if (*fmt == '0') zero_pad = 1;
            if (*fmt == '^') center = 1;
            fmt++;
        }
        if (left_align || center) zero_pad = 0;

        // Parse width
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        char buf[24];
        int len = 0;
        char padchar = zero_pad ? '0' : ' ';

        switch (*fmt) {
        case 'd':
        case 'i': {
            i64 n = va_arg(args, i64);
            int neg = n < 0;
            if (neg) n = -n;
            len = fmt_dec(buf, (u64)n);
            int total = len + neg;
            if (!left_align && !zero_pad) pad(width - total, ' ');
            if (neg) putc('-');
            if (!left_align && zero_pad) pad(width - total, '0');
            for (int i = 0; i < len; i++) putc(buf[i]);
            if (left_align) pad(width - total, ' ');
            break;
        }
        case 'u': {
            u64 n = va_arg(args, u64);
            len = fmt_dec(buf, n);
            if (!left_align) pad(width - len, padchar);
            for (int i = 0; i < len; i++) putc(buf[i]);
            if (left_align) pad(width - len, ' ');
            break;
        }
        case 'x': {
            u64 n = va_arg(args, u64);
            len = fmt_hex(buf, n);
            if (!left_align) pad(width - len, padchar);
            for (int i = 0; i < len; i++) putc(buf[i]);
            if (left_align) pad(width - len, ' ');
            break;
        }
        case 'X': {
            u64 n = va_arg(args, u64);
            len = fmt_hex(buf, n);
            int total = len + 2;
            if (!left_align) pad(width - total, padchar);
            puts("0x");
            for (int i = 0; i < len; i++) putc(buf[i]);
            if (left_align) pad(width - total, ' ');
            break;
        }
        case 'p': {
            u64 n = va_arg(args, u64);
            puts("0x");
            puthex64(n);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            len = strlen_(s);
            int padding = width - len;
            if (padding < 0) padding = 0;
            if (center) {
                int left_pad = padding / 2;
                int right_pad = padding - left_pad;
                pad(left_pad, ' ');
                puts(s);
                pad(right_pad, ' ');
            } else {
                if (!left_align) pad(padding, ' ');
                puts(s);
                if (left_align) pad(padding, ' ');
            }
            break;
        }
        case 'c':
            if (!left_align) pad(width - 1, ' ');
            putc((char)va_arg(args, int));
            if (left_align) pad(width - 1, ' ');
            break;
        case '%':
            putc('%');
            break;
        default:
            putc('%');
            putc(*fmt);
            break;
        }
        fmt++;
    }
}

void printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    do_vprintf(fmt, args);
    va_end(args);
}

/* ---- Structured kernel log ---- */

void klog(const char *tag, const char *fmt, ...) {
    printf("\r\n  [ %s ] ", tag);
    va_list args; va_start(args, fmt);
    do_vprintf(fmt, args);
    va_end(args);
}

void klog_ok(const char *tag, const char *fmt, ...) {
    printf("\r\n  [ \033[32m%s\033[0m ] ", tag);
    va_list args; va_start(args, fmt);
    do_vprintf(fmt, args);
    va_end(args);
}

void klog_fail(const char *tag, const char *fmt, ...) {
    printf("\r\n  [ \033[31m%s\033[0m ] ", tag);
    va_list args; va_start(args, fmt);
    do_vprintf(fmt, args);
    va_end(args);
}
