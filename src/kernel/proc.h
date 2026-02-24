#pragma once
#include "types.h"
#include "spinlock.h"
#include "vfs.h"

#define MAX_PROCS    64
#define KSTACK_SIZE  (4096 * 2)  // 8KB kernel stack
#define USER_STACK_TOP  0x7FFFFFF000UL
#define USER_STACK_BASE 0x7FFFFFE000UL
#define USER_HEAP_BASE  0x40000000UL    // brk starts here
#define MAX_FDS      32

// Process states
#define PROC_UNUSED   0
#define PROC_EMBRYO   1
#define PROC_RUNNABLE 2
#define PROC_RUNNING  3
#define PROC_ZOMBIE   4   // exited, waiting for parent to wait()

// Saved by swtch(), restored when switching to a process
struct context {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 rbx;
    u64 rbp;
    u64 rip;
};

struct trap_frame;

struct proc {
    u32 pid;
    u32 ppid;               // parent pid
    u32 state;
    i32 exit_code;          // set on exit, read by wait()
    u64 *pml4;              // page table (virtual address)
    u8  *kstack;            // kernel stack base (virtual)
    struct trap_frame *tf;  // trap frame on kernel stack
    struct context *context;
    u64 brk;                // current heap break (user VA)
    char name[16];
    struct vfs_file *files[MAX_FDS]; // open file descriptors
};

// Assembly context switch: saves old context, loads new
void swtch(struct context **old, struct context *new_ctx);

// Scheduler (called from kmain, never returns)
void scheduler(void);

// Yield current process (called from timer interrupt)
void yield(void);

// Create a process from an ELF file path
struct proc *proc_create(const char *path);

// Fork the current process; returns child pid in parent, 0 in child
i32 proc_fork(void);

// Replace current process address space with the ELF at path + argv
i32 proc_exec(const char *path, const char *const *argv);

// Initialize process subsystem (call before proc_create)
void proc_init(void);

// File descriptor helpers
i32 fd_alloc(struct proc *p, struct vfs_file *f);   // returns fd or -1
struct vfs_file *fd_get(struct proc *p, i32 fd);    // returns file or NULL

// Lock helpers for use by syscall.c
void acquire_proc_lock(void);
void release_proc_lock(void);

// The current running process (per-CPU)
#define current_proc (mycpu()->proc)

// Get scheduler context pointer (for syscall exit path)
struct context **cpu_context_ptr(void);
void proc_close_fds(struct proc *p);  // close all open file descriptors
