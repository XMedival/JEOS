#include "string.h"

u64 kstrlen(const char *s)
{
  if (!s) return 0;
  u64 n = 0;
  while (s[n]) n++;
  return n;
}

i32 kstrcmp(const char *a, const char *b)
{
  if (a == b) return 0;
  if (!a) return -1;
  if (!b) return  1;

  while (*a && (*a == *b)) { a++; b++; }
  return (i32)((u8)*a) - (i32)((u8)*b);
}

/* Compare a path component (pointer+len) with a literal */
u8 kstreq_nlit(const char *s, u64 len, const char *lit)
{
  u64 i = 0;
  for (; i < len && lit[i]; i++) {
    if (s[i] != lit[i]) return 0;
  }
  return (i == len && lit[i] == 0);
}
