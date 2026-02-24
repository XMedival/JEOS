#pragma once
#include "types.h"

#define BLK_MAX_DEVICES  8
#define BLK_NAME_LEN     16

struct blk_request {
    u64  lba;
    u32  count;
    void *buf;
    u8   write;
    volatile u8  done;
    volatile i32 status;
};

struct blk_device;

struct blk_ops {
    int (*submit)(struct blk_device *dev, struct blk_request *req);
};

struct blk_device {
    char name[BLK_NAME_LEN];
    u32  sector_size;
    struct blk_ops ops;
    void *priv;

    volatile struct blk_request *current_req;
};

struct blk_device *blk_register(const char *name, struct blk_ops ops,
                                 u32 sector_size, void *priv);
struct blk_device *blk_get(const char *name);

int blk_submit_sync(struct blk_device *dev, u64 lba, u32 count,
                    void *buf, u8 write);

static inline int blk_read(struct blk_device *dev, u64 lba, u32 count,
                            void *buf) {
    return blk_submit_sync(dev, lba, count, buf, 0);
}

static inline int blk_write(struct blk_device *dev, u64 lba, u32 count,
                             const void *buf) {
    return blk_submit_sync(dev, lba, count, (void *)buf, 1);
}

// Called by driver when the current request completes (from ISR or polling).
void blk_complete(struct blk_device *dev, i32 status);
