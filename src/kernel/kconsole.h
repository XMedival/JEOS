#pragma once
#include "types.h"
#include "limine.h"

void kconsole_init(struct limine_framebuffer *fb);
void kconsole_putc(char c);
void kconsole_clear(void);

/* Return framebuffer geometry. pitch_bytes is bytes per row. bpp is always 32. */
void kconsole_get_info(u32 *width, u32 *height, u32 *pitch_bytes, u32 *bpp);

/* Direct access to the linear framebuffer (ARGB, row-major).
   Returns the virtual address of pixel (0,0) and total size in bytes. */
void *kconsole_get_addr(void);
u64   kconsole_get_size(void);
