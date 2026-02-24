#pragma once
#include "types.h"

#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

static inline void outb(u16 port, u8 data) {
  asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline void outw(u16 port, u16 data) {
  asm volatile("outw %w0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline void outl(u16 port, u32 data) {
  asm volatile("outl %0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port) {
  u8 res;
  asm volatile("inb %w1, %b0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline u16 inw(u16 port) {
  u16 res;
  asm volatile("inw %w1, %w0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline u32 inl(u16 port) {
  u32 res;
  asm volatile("inl %w1, %0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline void cli(void) {
  asm volatile("cli");
}

static inline void sti(void) {
  asm volatile("sti");
}

static inline void hlt(void) {
  asm volatile("hlt");
}

static inline u64 read_rflags(void) {
  u64 rflags;
  asm volatile("pushfq; popq %0" : "=r"(rflags));
  return rflags;
}

static inline void lcr3(u64 val) {
  asm volatile("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline u64 rcr3(void) {
  u64 val;
  asm volatile("mov %%cr3, %0" : "=r"(val));
  return val;
}

static inline void wrmsr(u32 msr, u64 val) {
  u32 lo = (u32)val;
  u32 hi = (u32)(val >> 32);
  asm volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline u64 rdmsr(u32 msr) {
  u32 lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((u64)hi << 32) | lo;
}

static inline u8 xchg(volatile u8 *addr, u8 newval) {
  u8 result = newval;
  asm volatile("lock; xchgb %0, %1"
               : "+m"(*addr), "+r"(result)
               :
               : "cc");
  return result;
}
