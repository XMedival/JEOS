#pragma once
#include "types.h"
#include "x86.h"

#define MAX_CPUS 16

struct proc;
struct context;

struct cpu {
  // Accessed from syscall_entry.S via %gs:offset — DO NOT reorder
  u64 kernel_rsp;               // offset 0
  u64 scratch_rsp;              // offset 8
  struct proc *proc;            // offset 16: current running process
  struct context *scheduler_ctx;// offset 24: scheduler's saved context
  u8 apic_id;
  u8 ncli;           // depth of pushcli nesting
  u8 intena;         // were interrupts enabled before pushcli?
  u8 cpu_id;         // index into cpus[]
};

struct spinlock {
  u8 locked;
  char *name;
  struct cpu *cpu;
};

extern struct cpu cpus[MAX_CPUS];
extern u32 ncpu;

static inline struct cpu* mycpu(void) {
  return (struct cpu *)(u64)rdmsr(MSR_GS_BASE);
}

static inline void pushcli(void) {
  u64 rflags = read_rflags();
  cli();
  struct cpu *c = mycpu();
  if (c->ncli == 0)
    c->intena = (rflags & 0x200) != 0;
  c->ncli++;
}

static inline void popcli(void) {
  if (read_rflags() & 0x200)
    hlt();  // panic: popcli with interrupts enabled
  struct cpu *c = mycpu();
  if (c->ncli == 0)
    hlt();  // panic: popcli underflow
  c->ncli--;
  if (c->ncli == 0 && c->intena)
    sti();
}

static inline u8 holding(struct spinlock *lk) {
  u8 r;
  pushcli();
  r = lk->locked && lk->cpu == mycpu();
  popcli();
  return r;
}

static inline void acquire(struct spinlock *lk) {
  pushcli();
  if (holding(lk))
    hlt();  // panic: acquire held lock

  while (xchg(&lk->locked, 1) != 0)
    ;

  __sync_synchronize();
  lk->cpu = mycpu();
}

static inline void release(struct spinlock *lk) {
  if (!holding(lk))
    hlt();  // panic: release unheld lock

  lk->cpu = 0;
  __sync_synchronize();

  asm volatile("movb $0, %0" : "+m"(lk->locked) :);

  popcli();
}

static inline void initlock(struct spinlock *lk, char *name) {
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}
