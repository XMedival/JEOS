#include "vfs.h"
#include "mem.h"
#include "string.h"

/* Minimal in-memory root filesystem.
   Supports mkdir so mount-points like /dev and /mnt can be created
   without any backing storage. */

#define INITFS_MAX_DIRS  8

struct initfs_dir {
    char  name[64];
    u16   name_len;
    u8    in_use;
    u32   ino;
};

static struct initfs_dir g_dirs[INITFS_MAX_DIRS];
static u32 g_next_ino = 2;

static const struct vfs_inode_ops initfs_dir_iops; /* forward */

static struct vfs_inode *initfs_make_dir(struct vfs_superblock *sb, u32 ino)
{
    struct vfs_inode *v = (struct vfs_inode *)kalloc(1);
    if (!v) return 0;
    memset(v, 0, sizeof(*v));
    v->ino    = ino;
    v->mode   = VFS_S_IFDIR | 0755;
    v->refcnt = 1;
    v->sb     = sb;
    v->iops   = &initfs_dir_iops;
    return v;
}

static i32 initfs_lookup(struct vfs_inode *dir, struct vfs_dentry *child)
{
    for (int i = 0; i < INITFS_MAX_DIRS; i++) {
        struct initfs_dir *d = &g_dirs[i];
        if (!d->in_use) continue;
        if (d->name_len == child->name_len &&
            kstrcmp(d->name, child->name) == 0) {
            child->inode = initfs_make_dir(dir->sb, d->ino);
            return child->inode ? VFS_OK : VFS_ENOMEM;
        }
    }
    return VFS_ENOENT;
}

static i32 initfs_mkdir(struct vfs_inode *dir, struct vfs_dentry *child, vfs_mode_t mode)
{
    (void)mode;
    /* reject if name already exists */
    for (int i = 0; i < INITFS_MAX_DIRS; i++) {
        struct initfs_dir *d = &g_dirs[i];
        if (d->in_use && d->name_len == child->name_len &&
            kstrcmp(d->name, child->name) == 0)
            return VFS_EEXIST;
    }
    for (int i = 0; i < INITFS_MAX_DIRS; i++) {
        struct initfs_dir *d = &g_dirs[i];
        if (!d->in_use) {
            memcpy(d->name, child->name, child->name_len);
            d->name[child->name_len] = 0;
            d->name_len = child->name_len;
            d->ino      = g_next_ino++;
            d->in_use   = 1;
            child->inode = initfs_make_dir(dir->sb, d->ino);
            return child->inode ? VFS_OK : VFS_ENOMEM;
        }
    }
    return VFS_ENOMEM;
}

static i32 initfs_getattr(struct vfs_inode *v, struct vfs_stat *st)
{
    st->ino   = v->ino;
    st->mode  = v->mode;
    st->nlink = 2;
    st->size  = 0;
    return VFS_OK;
}

static const struct vfs_inode_ops initfs_dir_iops = {
    .lookup  = initfs_lookup,
    .mkdir   = initfs_mkdir,
    .getattr = initfs_getattr,
};

static i32 initfs_mount_fs(struct vfs_superblock *sb, void *dev, const char *opts)
{
    (void)dev; (void)opts;
    struct vfs_inode *root_ino = initfs_make_dir(sb, 1);
    if (!root_ino) return VFS_ENOMEM;

    struct vfs_dentry *root_dent = (struct vfs_dentry *)kalloc(1);
    if (!root_dent) { kfree(root_ino, 1); return VFS_ENOMEM; }
    memset(root_dent, 0, sizeof(*root_dent));
    root_dent->refcnt   = 1;
    root_dent->name[0]  = '/';
    root_dent->name[1]  = 0;
    root_dent->name_len = 1;
    root_dent->inode    = root_ino;
    sb->root = root_dent;
    return VFS_OK;
}

static struct vfs_fs_type initfs_fs_type = {
    .name    = "initfs",
    .mount   = initfs_mount_fs,
    .unmount = 0,
    .next    = 0,
};

void initfs_init(void)
{
    vfs_register_fs(&initfs_fs_type);
}
