#pragma once
#include "types.h"

u64 kstrlen(const char *s);
i32 kstrcmp(const char *a, const char *b);
u8 kstreq_nlit(const char *s, u64 len, const char *lit);
