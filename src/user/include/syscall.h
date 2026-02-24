#pragma once
#include <stddef.h>
#include <stdint.h>

typedef long          ssize_t;
typedef unsigned long size_t;
typedef long          off_t;

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_GETPID  2
#define SYS_EXEC    3
#define SYS_FORK    4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_READ    7
#define SYS_SEEK    8
#define SYS_FSTAT   9
#define SYS_STAT    10
#define SYS_WAIT    11
#define SYS_DUP     12
#define SYS_DUP2    13
#define SYS_BRK     14
#define SYS_PIPE    15
#define SYS_FBINFO  16

/* ── open flags ──────────────────────────────────────────
   Low 2 bits select access mode, rest are modifiers.     */
#define O_RDONLY  0   /* read only                        */
#define O_WRONLY  1   /* write only                       */
#define O_RDWR    2   /* read + write                     */
#define O_CREAT   0x100
#define O_TRUNC   0x200
#define O_APPEND  0x400

/* ── seek whence values ──────────────────────────────── */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── raw syscall wrappers ─────────────────────────────── */
static inline long syscall0(long n) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n) : "rcx","r11","memory");
    return r;
}
static inline long syscall1(long n, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n),"D"(a1) : "rcx","r11","memory");
    return r;
}
static inline long syscall2(long n, long a1, long a2) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n),"D"(a1),"S"(a2) : "rcx","r11","memory");
    return r;
}
static inline long syscall3(long n, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n),"D"(a1),"S"(a2),"d"(a3) : "rcx","r11","memory");
    return r;
}
static inline long syscall4(long n, long a1, long a2, long a3, long a4) {
    long r;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n),"D"(a1),"S"(a2),"d"(a3),"r"(r10) : "rcx","r11","memory");
    return r;
}

/* ── libc-like helpers ───────────────────────────────── */
static inline void _exit(int code) {
    syscall1(SYS_EXIT, code);
    __builtin_unreachable();
}
static inline ssize_t write(int fd, const void *buf, size_t n) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)n);
}
static inline ssize_t read(int fd, void *buf, size_t n) {
    return syscall3(SYS_READ, fd, (long)buf, (long)n);
}
static inline int open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (long)path, (long)flags);
}
static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (long)fd);
}
static inline off_t seek(int fd, off_t off, int whence) {
    return (off_t)syscall3(SYS_SEEK, (long)fd, (long)off, (long)whence);
}
static inline int fork(void) {
    return (int)syscall0(SYS_FORK);
}
static inline int exec(const char *path, const char **argv) {
    return (int)syscall2(SYS_EXEC, (long)path, (long)argv);
}
static inline int wait(int *status) {
    return (int)syscall1(SYS_WAIT, (long)status);
}
static inline int dup2(int old, int newfd) {
    return (int)syscall2(SYS_DUP2, (long)old, (long)newfd);
}
static inline int pipe(int fds[2]) {
    return (int)syscall1(SYS_PIPE, (long)fds);
}
static inline void *brk(void *addr) {
    return (void *)syscall1(SYS_BRK, (long)addr);
}
static inline int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

struct fb_info {
    unsigned long width;
    unsigned long height;
    unsigned long pitch;
    unsigned long bpp;
    unsigned long addr;
    unsigned long size;
};

static inline int fbinfo(struct fb_info *info) {
    return (int)syscall1(SYS_FBINFO, (long)info);
}
