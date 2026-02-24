#pragma once
#include "types.h"
#include "vfs.h"
#include "blk.h"

#define DEVFS_MAX_NODES  64

/* Register a character or block device file in devfs.
   mode must include a type bit (VFS_S_IFCHR, VFS_S_IFBLK, etc.).
   fops and priv are forwarded directly to any opened vfs_file. */
i32 devfs_register(const char *name, vfs_mode_t mode,
                   const struct vfs_file_ops *fops, void *priv);

/* Remove a previously registered device. */
i32 devfs_unregister(const char *name);

/* Wrap a blk_device as a byte-addressable character device in devfs. */
i32 devfs_register_blk(struct blk_device *dev);

/* Register /dev/fb and /dev/fbctl using current framebuffer state.
   Call after kconsole_init() and devfs_init(). */
void devfs_register_fb(void);

/* Register the devfs filesystem type with the VFS and add built-in devices
   (null, zero, cons). Call after vfs_init(). */
void devfs_init(void);
