#include "syscall.h"
#include "gdt.h"
#include "kconsole.h"
#include "mem.h"
#include "panic.h"
#include "pipe.h"
#include "print.h"
#include "proc.h"
#include "spinlock.h"
#include "vfs.h"
#include "x86.h"

extern void syscall_entry(void);

void init_syscall(void) {
    u64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    u64 star = ((u64)0x33 << 48) | ((u64)KERNEL_CS << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (u64)syscall_entry);
    wrmsr(MSR_FMASK, 0x200);
}

/* Validate a user-space pointer: must be below the upper canonical boundary. */
#define USER_PTR_MAX 0x800000000000UL
static int valid_user_ptr(const void *p) { return (u64)p < USER_PTR_MAX; }

/* ---- syscall implementations ---- */

static void sys_exit(i32 status) {
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    if (!p) return;
    acquire_proc_lock();

    proc_close_fds(p);
    printf("proc: %d, code: %d\r\n", p->pid, status);

    p->exit_code = status;
    p->state = PROC_ZOMBIE;

    c->proc = 0;
    swtch(&p->context, c->scheduler_ctx);

    panic("SYS_EXIT: RETURNED");
}

static i64 sys_open(const char *path, u64 flags) {
    struct proc *p = current_proc;
    if (!p || !valid_user_ptr(path)) return -1;

    /* Map simple open flags: 0=rdonly, 1=wronly, 2=rdwr */
    u32 vflags = VFS_O_RDONLY;
    if ((flags & 3) == 1) vflags = VFS_O_WRONLY;
    else if ((flags & 3) == 2) vflags = VFS_O_RDWR;

    struct vfs_file *f = 0;
    if (vfs_open(path, vflags, 0, &f) != VFS_OK) return -1;

    i32 fd = fd_alloc(p, f);
    if (fd < 0) { vfs_close(f); return -1; }
    return fd;
}

static i64 sys_close(u64 fd) {
    struct proc *p = current_proc;
    if (!p) return -1;
    struct vfs_file *f = fd_get(p, (i32)fd);
    if (!f) return -1;
    p->files[fd] = 0;
    vfs_close(f);
    return 0;
}

static i64 sys_read(u64 fd, void *buf, u64 len) {
    struct proc *p = current_proc;
    if (!p || !valid_user_ptr(buf)) return -1;
    struct vfs_file *f = fd_get(p, (i32)fd);
    if (!f) return -1;
    return vfs_read(f, buf, (u32)len);
}

static i64 sys_write(u64 fd, const void *buf, u64 len) {
    if (!valid_user_ptr(buf)) return -1;

    struct proc *p = current_proc;
    /* Fast path: if fd 1/2 and no vfs file yet, use direct putc */
    struct vfs_file *f = p ? fd_get(p, (i32)fd) : 0;
    if (!f && (fd == 1 || fd == 2)) {
        for (u64 i = 0; i < len; i++) putc(((const char *)buf)[i]);
        return (i64)len;
    }
    if (!f) return -1;
    return vfs_write(f, buf, (u32)len);
}

static i64 sys_seek(u64 fd, i64 off, u64 whence) {
    struct proc *p = current_proc;
    if (!p) return -1;
    struct vfs_file *f = fd_get(p, (i32)fd);
    if (!f) return -1;
    return vfs_seek(f, off, (int)whence);
}

static i64 sys_fstat(u64 fd, struct vfs_stat *st) {
    struct proc *p = current_proc;
    if (!p || !valid_user_ptr(st)) return -1;
    struct vfs_file *f = fd_get(p, (i32)fd);
    if (!f) return -1;
    if (vfs_fstat(f, st) != VFS_OK) return -1;
    return 0;
}

static i64 sys_stat(const char *path, struct vfs_stat *st) {
    if (!valid_user_ptr(path) || !valid_user_ptr(st)) return -1;
    if (vfs_stat(path, st) != VFS_OK) return -1;
    return 0;
}

static i64 sys_getpid(void) {
    if (!current_proc) return -1;
    return (i64)current_proc->pid;
}

static i64 sys_wait(i32 *status_out) {
    struct proc *parent = current_proc;
    if (!parent) return -1;

    for (;;) {
        /* Scan for any zombie child */
        acquire_proc_lock();
        extern struct proc proc_table[];
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *c = &proc_table[i];
            if (c->state != PROC_ZOMBIE || c->ppid != parent->pid) continue;
            i32 pid = (i32)c->pid;
            if (status_out && valid_user_ptr(status_out))
                *status_out = c->exit_code;
            /* Reap: free address space and kstack */
            free_user_pml4(c->pml4);
            kfree(c->pml4, 1);
            kfree(c->kstack, KSTACK_SIZE / PAGE_SIZE);
            c->pml4   = 0;
            c->kstack = 0;
            c->state  = PROC_UNUSED;
            release_proc_lock();
            return pid;
        }
        release_proc_lock();
        /* No zombie child yet — yield and retry */
        yield();
    }
}

static i64 sys_dup(u64 fd) {
    struct proc *p = current_proc;
    if (!p) return -1;
    struct vfs_file *f = fd_get(p, (i32)fd);
    if (!f) return -1;
    vfs_file_get(f);
    i32 new_fd = fd_alloc(p, f);
    if (new_fd < 0) { vfs_close(f); return -1; }
    return new_fd;
}

static i64 sys_dup2(u64 old_fd, u64 new_fd) {
    struct proc *p = current_proc;
    if (!p || new_fd >= MAX_FDS) return -1;
    struct vfs_file *f = fd_get(p, (i32)old_fd);
    if (!f) return -1;
    if (old_fd == new_fd) return (i64)new_fd;
    /* Close existing file at new_fd */
    if (p->files[new_fd]) {
        vfs_close(p->files[new_fd]);
        p->files[new_fd] = 0;
    }
    vfs_file_get(f);
    p->files[new_fd] = f;
    return (i64)new_fd;
}

static i64 sys_brk(u64 new_brk) {
    struct proc *p = current_proc;
    if (!p) return -1;
    if (new_brk == 0) return (i64)p->brk;  /* query current brk */

    /* Clamp to reasonable range */
    if (new_brk < USER_HEAP_BASE || new_brk > 0x400000000UL) return (i64)p->brk;

    u64 old_brk  = p->brk;
    u64 old_page = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u64 new_page = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (new_brk > old_brk) {
        /* Map new pages */
        for (u64 va = old_page; va < new_page; va += PAGE_SIZE) {
            void *pg = kalloc(1);
            if (!pg) return (i64)p->brk;  /* out of memory */
            memset(pg, 0, PAGE_SIZE);
            map_page_pml4(p->pml4, va, VIRT_TO_PHYS((u64)pg), PTE_USER | PTE_WRITE);
        }
    }
    /* (shrinking: leave pages mapped — simple approach) */
    p->brk = new_brk;
    return (i64)new_brk;
}

static i64 sys_pipe(i32 *fds) {
    if (!valid_user_ptr(fds)) return -1;
    struct proc *p = current_proc;
    if (!p) return -1;
    struct vfs_file *r = 0, *w = 0;
    if (pipe_create(&r, &w) != 0) return -1;
    i32 rfd = fd_alloc(p, r);
    i32 wfd = fd_alloc(p, w);
    if (rfd < 0 || wfd < 0) {
        if (rfd >= 0) { p->files[rfd] = 0; vfs_close(r); }
        else vfs_close(r);
        vfs_close(w);
        return -1;
    }
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

struct fb_info {
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u64 addr;
    u64 size;
};

static i64 sys_fbinfo(struct fb_info *info) {
    if (!valid_user_ptr(info)) return -1;
    u32 w, h, p, b;
    kconsole_get_info(&w, &h, &p, &b);
    info->width = w;
    info->height = h;
    info->pitch = p;
    info->bpp = b;
    info->addr = (u64)kconsole_get_addr();
    info->size = kconsole_get_size();
    return 0;
}

/* Called from syscall_entry.S
   Argument order: rdi=num, rsi=a1, rdx=a2, r10=a3, r8=a4, r9=a5 */
i64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    if (num == SYS_EXIT) {
        puts("EXIT ");
        sys_exit((i32)a1); 
        puts("RET ");
        return 0;
    }
    (void)a4; (void)a5;
    switch (num) {
    case SYS_WRITE:  return sys_write(a1, (const void *)a2, a3);
    case SYS_GETPID: return sys_getpid();
    case SYS_EXEC:   return proc_exec((const char *)a1, (const char *const *)a2);
    case SYS_FORK:   return proc_fork();
    case SYS_OPEN:   return sys_open((const char *)a1, a2);
    case SYS_CLOSE:  return sys_close(a1);
    case SYS_READ:   return sys_read(a1, (void *)a2, a3);
    case SYS_SEEK:   return sys_seek(a1, (i64)a2, a3);
    case SYS_FSTAT:  return sys_fstat(a1, (struct vfs_stat *)a2);
    case SYS_STAT:   return sys_stat((const char *)a1, (struct vfs_stat *)a2);
    case SYS_WAIT:   return sys_wait((i32 *)a1);
    case SYS_DUP:    return sys_dup(a1);
    case SYS_DUP2:   return sys_dup2(a1, a2);
    case SYS_BRK:    return sys_brk(a1);
    case SYS_PIPE:   return sys_pipe((i32 *)a1);
    case SYS_FBINFO: return sys_fbinfo((struct fb_info *)a1);
    default:         return -1;
    }
}
