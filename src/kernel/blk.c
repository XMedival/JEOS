#include "blk.h"
#include "print.h"
#include "x86.h"
#include "vfs.h"

static struct blk_device blk_devices[BLK_MAX_DEVICES];
static u32 blk_device_count = 0;

struct blk_device *blk_register(const char *name, struct blk_ops ops,
                                  u32 sector_size, void *priv) {
    if (blk_device_count >= BLK_MAX_DEVICES) {
        klog_fail("BLK", "device table full");
        return 0;
    }

    struct blk_device *dev = &blk_devices[blk_device_count++];
    for (int i = 0; i < BLK_NAME_LEN - 1 && name[i]; i++)
        dev->name[i] = name[i];
    dev->ops         = ops;
    dev->sector_size = sector_size;
    dev->priv        = priv;
    dev->current_req = 0;
    return dev;
}

struct blk_device *blk_get(const char *name) {
    for (u32 i = 0; i < blk_device_count; i++) {
        const char *a = blk_devices[i].name;
        const char *b = name;
        int match = 1;
        while (*a || *b) {
            if (*a++ != *b++) { match = 0; break; }
        }
        if (match) return &blk_devices[i];
    }
    return 0;
}

int blk_submit_sync(struct blk_device *dev, u64 lba, u32 count,
                    void *buf, u8 write) {
    if (!dev) return -1;

    struct blk_request req = {
        .lba    = lba,
        .count  = count,
        .buf    = buf,
        .write  = write,
        .done   = 0,
        .status = 0,
    };

    dev->current_req = &req;

    if (dev->ops.submit(dev, &req) != 0) {
        dev->current_req = 0;
        return -1;
    }

    // Wait for completion (works for both interrupt-driven and polling drivers)
    while (!req.done)
        hlt();

    dev->current_req = 0;
    return req.status;
}

void blk_complete(struct blk_device *dev, i32 status) {
    volatile struct blk_request *req = dev->current_req;
    if (!req) return;  // spurious

    req->status = status;
    req->done   = 1;
}
