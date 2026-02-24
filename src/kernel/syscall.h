#pragma once
#include "types.h"

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_EXEC   3
#define SYS_FORK   4
#define SYS_OPEN   5
#define SYS_CLOSE  6
#define SYS_READ   7
#define SYS_SEEK   8
#define SYS_FSTAT  9
#define SYS_STAT   10
#define SYS_WAIT   11
#define SYS_DUP    12
#define SYS_DUP2   13
#define SYS_BRK    14
#define SYS_PIPE   15
#define SYS_FBINFO 16

// MSR addresses
#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

#define EFER_SCE  (1UL << 0)  // syscall enable

void init_syscall(void);
