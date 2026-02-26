/* Minimal userspace runtime library */
#include <stddef.h>

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

int strcmp(const char *s1, const char *s2) {
    int i = 0, flag = 0;
    while (flag == 0) {
        if (s1[i] > s2[i]) {
            flag = 1;
        } else if (s1[i] < s2[i]) {
            flag = -1;
        } else 
            i++;
    }
    return flag;
}
