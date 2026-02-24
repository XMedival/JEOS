#pragma once

#include "types.h"

typedef struct {
    char   *buf;   // backing storage
    u32 cap;  // capacity (e.g. 1024)
    u32 head; // next write index
    u32 len;  // number of valid bytes (<= cap)
} ring_t;

static inline void ring_init(ring_t *r, char *storage, u32 capacity) {
    r->buf  = storage;
    r->cap  = capacity;
    r->head = 0;
    r->len  = 0;
}

static inline void ring_putc(ring_t *r, char c) {
    r->buf[r->head] = c;
    r->head++;
    if (r->head == r->cap) r->head = 0;
    if (r->len < r->cap) r->len++;
}

// Write n bytes into ring (overwrites oldest on overflow)
static inline void ring_write(ring_t *r, const char *data, u32 n) {
    // If n >= cap, only last cap bytes matter; write those.
    if (n >= r->cap) {
        data += (n - r->cap);
        n = r->cap;
        // Reset to a simple state and fill sequentially.
        r->head = 0;
        r->len  = r->cap;
        for (u32 i = 0; i < r->cap; i++) r->buf[i] = data[i];
        return;
    }

    for (u32 i = 0; i < n; i++) ring_putc(r, data[i]);
}

// Oldest element index (logical start)
static inline u32 ring_start(const ring_t *r) {
    // (head - len) mod cap, avoiding negatives
    u32 start = r->head;
    if (start >= r->len) start -= r->len;
    else start = r->cap - (r->len - start);
    return start;
}

// Read out up to out_cap bytes in oldest->newest order.
// Returns number of bytes written to out. Consumes the data.
static inline u32 ring_read(ring_t *r, char *out, u32 out_cap)
{
    u32 n = r->len;
    if (n > out_cap) n = out_cap;
    if (n == 0) return 0;

    u32 i = 0;
    u32 idx = ring_start(r);      // index of oldest

    while (i < n) {
        out[i++] = r->buf[idx++];
        if (idx == r->cap) idx = 0;
    }
    r->len -= n;
    return n;
}
