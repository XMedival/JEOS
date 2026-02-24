#include "spinlock.h"

struct cpu cpus[MAX_CPUS];
u32 ncpu;
