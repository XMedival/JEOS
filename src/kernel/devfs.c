#include "devfs.h"
#include "kconsole.h"
#include "mem.h"
#include "print.h"
#include "ps2.h"
#include "ring.h"
#include "string.h"

/* ---- device node registry (global, shared across all devfs mounts) ---- */

struct devfs_node {
    char     name[64];
    u16      name_len;
    vfs_ino_t ino;
    vfs_mode_t mode;
    const struct vfs_file_ops *fops;
    void    *priv;
    u8       in_use;
};

static struct devfs_node g_nodes[DEVFS_MAX_NODES];
static u32 g_next_ino = 2; /* ino 1 = root dir */

/* ---- forward declarations ---- */

static const struct vfs_inode_ops devfs_dir_iops;
static const struct vfs_inode_ops devfs_node_iops;
static const struct vfs_file_ops  devfs_dir_fops;

/* ---- node registry ---- */

i32 devfs_register(const char *name, vfs_mode_t mode,
                   const struct vfs_file_ops *fops, void *priv)
{
    if (!name || !fops) return VFS_EINVAL;
    u64 nlen = kstrlen(name);
    if (nlen == 0 || nlen >= 64) return VFS_EINVAL;

    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (g_nodes[i].in_use && kstrcmp(g_nodes[i].name, name) == 0)
            return VFS_EEXIST;
    }

    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (!g_nodes[i].in_use) {
            struct devfs_node *n = &g_nodes[i];
            memcpy(n->name, name, nlen + 1);
            n->name_len = (u16)nlen;
            n->ino      = g_next_ino++;
            n->mode     = mode;
            n->fops     = fops;
            n->priv     = priv;
            n->in_use   = 1;
            return VFS_OK;
        }
    }
    return VFS_ENOMEM;
}

i32 devfs_unregister(const char *name)
{
    if (!name) return VFS_EINVAL;
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (g_nodes[i].in_use && kstrcmp(g_nodes[i].name, name) == 0) {
            g_nodes[i].in_use = 0;
            return VFS_OK;
        }
    }
    return VFS_ENOENT;
}

static struct devfs_node *devfs_find(const char *name, u16 len)
{
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (g_nodes[i].in_use && g_nodes[i].name_len == len) {
            u8 match = 1;
            for (u16 j = 0; j < len; j++) {
                if (g_nodes[i].name[j] != name[j]) { match = 0; break; }
            }
            if (match) return &g_nodes[i];
        }
    }
    return 0;
}

/* ---- inode construction ---- */

static struct vfs_inode *devfs_make_root(struct vfs_superblock *sb)
{
    struct vfs_inode *vino = (struct vfs_inode *)kalloc(1);
    if (!vino) return 0;
    memset(vino, 0, sizeof(*vino));
    vino->ino    = 1;
    vino->mode   = VFS_S_IFDIR | 0755;
    vino->refcnt = 1;
    vino->sb     = sb;
    vino->iops   = &devfs_dir_iops;
    vino->fops   = &devfs_dir_fops;
    vino->priv   = 0;
    return vino;
}

static struct vfs_inode *devfs_make_node_inode(struct vfs_superblock *sb,
                                                struct devfs_node *node)
{
    struct vfs_inode *vino = (struct vfs_inode *)kalloc(1);
    if (!vino) return 0;
    memset(vino, 0, sizeof(*vino));
    vino->ino    = node->ino;
    vino->mode   = node->mode;
    vino->refcnt = 1;
    vino->sb     = sb;
    vino->iops   = &devfs_node_iops;
    vino->fops   = node->fops;
    vino->priv   = node->priv;
    return vino;
}

/* ---- directory inode ops ---- */

static i32 devfs_dir_lookup(struct vfs_inode *dir, struct vfs_dentry *child)
{
    (void)dir;
    struct devfs_node *node = devfs_find(child->name, child->name_len);
    if (!node) return VFS_ENOENT;

    struct vfs_inode *vino = devfs_make_node_inode(dir->sb, node);
    if (!vino) return VFS_ENOMEM;

    child->inode = vino;
    return VFS_OK;
}

static i32 devfs_dir_getattr(struct vfs_inode *vino, struct vfs_stat *st)
{
    st->ino   = vino->ino;
    st->mode  = vino->mode;
    st->nlink = 2;
    st->size  = 0;
    return VFS_OK;
}

static const struct vfs_inode_ops devfs_dir_iops = {
    .lookup  = devfs_dir_lookup,
    .getattr = devfs_dir_getattr,
};

/* ---- device node inode ops ---- */

static i32 devfs_node_getattr(struct vfs_inode *vino, struct vfs_stat *st)
{
    st->ino   = vino->ino;
    st->mode  = vino->mode;
    st->nlink = 1;
    st->size  = 0;
    return VFS_OK;
}

static const struct vfs_inode_ops devfs_node_iops = {
    .getattr = devfs_node_getattr,
};

/* ---- directory file ops (readdir) ---- */

static i32 devfs_dir_open(struct vfs_inode *inode, struct vfs_file *file)
{
    (void)inode; (void)file;
    return VFS_OK;
}

static i32 devfs_dir_close(struct vfs_file *file)
{
    (void)file;
    return VFS_OK;
}

static i32 devfs_readdir(struct vfs_file *file, struct vfs_dirent *out)
{
    u32 idx = (u32)file->pos;

    while (idx < DEVFS_MAX_NODES) {
        struct devfs_node *n = &g_nodes[idx];
        idx++;
        if (!n->in_use) continue;

        out->ino    = n->ino;
        out->reclen = sizeof(*out);
        /* derive DT type from mode */
        vfs_mode_t type = n->mode & VFS_S_IFMT;
        if      (type == VFS_S_IFCHR)  out->type = 3;
        else if (type == VFS_S_IFBLK)  out->type = 4;
        else if (type == VFS_S_IFREG)  out->type = 1;
        else                           out->type = 0;

        memcpy(out->name, n->name, n->name_len + 1);
        file->pos = idx;
        return VFS_OK;
    }

    return VFS_ENOENT; /* end of directory */
}

static const struct vfs_file_ops devfs_dir_fops = {
    .open    = devfs_dir_open,
    .close   = devfs_dir_close,
    .readdir = devfs_readdir,
};

/* ---- built-in: null ---- */

static i64 null_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off)
{
    (void)f; (void)buf; (void)count; (void)off;
    return 0;
}

static i64 null_write(struct vfs_file *f, const void *buf, u64 count, vfs_off_t *off)
{
    (void)f; (void)buf;
    *off += (vfs_off_t)count;
    return (i64)count;
}

static const struct vfs_file_ops null_fops = {
    .read  = null_read,
    .write = null_write,
};

/* ---- built-in: zero ---- */

static i64 zero_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off)
{
    (void)f;
    memset(buf, 0, count);
    *off += (vfs_off_t)count;
    return (i64)count;
}

static const struct vfs_file_ops zero_fops = {
    .read  = zero_read,
    .write = null_write,
};

/* ---- block device wrapper ---- */

static i64 blkdev_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off)
{
    struct blk_device *dev = (struct blk_device *)f->inode->priv;
    u32 ss = dev->sector_size;

    /* align down to sector boundary */
    u64 start_sec = (u64)*off / ss;
    u64 end_sec   = ((u64)*off + count + ss - 1) / ss;
    u64 nsecs     = end_sec - start_sec;

    u64 buf_pages = (nsecs * ss + PAGE_SIZE - 1) / PAGE_SIZE;
    if (buf_pages == 0) buf_pages = 1;

    u8 *tmp = (u8 *)kalloc(buf_pages);
    if (!tmp) return VFS_ENOMEM;

    i32 rc = blk_read(dev, start_sec, (u32)nsecs, tmp);
    if (rc != 0) { kfree(tmp, buf_pages); return -1; }

    u64 delta = (u64)*off - start_sec * ss;
    u64 avail = nsecs * ss - delta;
    if (avail > count) avail = count;
    memcpy(buf, tmp + delta, avail);
    kfree(tmp, buf_pages);

    *off += (vfs_off_t)avail;
    return (i64)avail;
}

static i64 blkdev_write(struct vfs_file *f, const void *buf, u64 count, vfs_off_t *off)
{
    struct blk_device *dev = (struct blk_device *)f->inode->priv;
    u32 ss = dev->sector_size;

    u64 start_sec = (u64)*off / ss;
    u64 end_sec   = ((u64)*off + count + ss - 1) / ss;
    u64 nsecs     = end_sec - start_sec;
    u64 buf_pages = (nsecs * ss + PAGE_SIZE - 1) / PAGE_SIZE;
    if (buf_pages == 0) buf_pages = 1;

    u8 *tmp = (u8 *)kalloc(buf_pages);
    if (!tmp) return VFS_ENOMEM;

    /* read-modify-write for partial sectors */
    blk_read(dev, start_sec, (u32)nsecs, tmp);

    u64 delta = (u64)*off - start_sec * ss;
    memcpy(tmp + delta, buf, count);

    i32 rc = blk_write(dev, start_sec, (u32)nsecs, tmp);
    kfree(tmp, buf_pages);
    if (rc != 0) return -1;

    *off += (vfs_off_t)count;
    return (i64)count;
}

static vfs_off_t blkdev_llseek(struct vfs_file *f, vfs_off_t off, i32 whence)
{
    (void)f;
    if (whence == VFS_SEEK_SET) { f->pos = off; return off; }
    if (whence == VFS_SEEK_CUR) { f->pos += off; return f->pos; }
    return VFS_EINVAL;
}

static const struct vfs_file_ops blkdev_fops = {
    .read    = blkdev_read,
    .write   = blkdev_write,
    .llseek  = blkdev_llseek,
};

i32 devfs_register_blk(struct blk_device *dev)
{
    if (!dev) return VFS_EINVAL;
    return devfs_register(dev->name, VFS_S_IFBLK | 0600, &blkdev_fops, dev);
}

/* ---- built-in: cons (read/write - Plan 9 style) ---- */

static i64 cons_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off) {
    (void)f;
    (void)off;
    u32 n = count;
    if (n > KBD_BUF_LEN) n = KBD_BUF_LEN;
    return ring_read(&kbd_ring_buf, buf, n);
}

static i64 cons_write(struct vfs_file *f, const void *buf, u64 count, vfs_off_t *off)
{
    (void)f;
    const u8 *p = (const u8 *)buf;
    for (u64 i = 0; i < count; i++) putc((char)p[i]);
    *off += (vfs_off_t)count;
    return (i64)count;
}

static const struct vfs_file_ops cons_fops = {
    .read  = cons_read,
    .write = cons_write,
};

/* ---- built-in: fb (raw framebuffer read/write by byte offset) ---- */

static i64 fb_dev_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off)
{
    (void)f;
    u8 *base = (u8 *)kconsole_get_addr();
    u64 size  = kconsole_get_size();
    if (!base || (u64)*off >= size) return 0;
    if ((u64)*off + count > size) count = size - (u64)*off;
    memcpy(buf, base + *off, count);
    *off += (vfs_off_t)count;
    return (i64)count;
}

static i64 fb_dev_write(struct vfs_file *f, const void *buf, u64 count, vfs_off_t *off)
{
    (void)f;
    u8 *base = (u8 *)kconsole_get_addr();
    u64 size  = kconsole_get_size();
    if (!base || (u64)*off >= size) return 0;
    if ((u64)*off + count > size) count = size - (u64)*off;
    memcpy(base + *off, buf, count);
    *off += (vfs_off_t)count;
    return (i64)count;
}

static vfs_off_t fb_dev_llseek(struct vfs_file *f, vfs_off_t off, i32 whence)
{
    u64 size = kconsole_get_size();
    vfs_off_t newpos;
    if      (whence == VFS_SEEK_SET) newpos = off;
    else if (whence == VFS_SEEK_CUR) newpos = f->pos + off;
    else if (whence == VFS_SEEK_END) newpos = (vfs_off_t)size + off;
    else return (vfs_off_t)VFS_EINVAL;
    if (newpos > size) newpos = size;
    f->pos = newpos;
    return newpos;
}

static const struct vfs_file_ops fb_fops = {
    .read    = fb_dev_read,
    .write   = fb_dev_write,
    .llseek  = fb_dev_llseek,
};

/* ---- built-in: fbctl (read-only info text) ---- */

/* Max text length: "width=9999 height=9999 pitch=99999 bpp=32\n" < 64 bytes */
#define FBCTL_BUF_LEN  64

static i64 fbctl_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off)
{
    (void)f;
    u32 w = 0, h = 0, p = 0, bpp = 0;
    kconsole_get_info(&w, &h, &p, &bpp);

    /* Build the info string */
    char tmp[FBCTL_BUF_LEN];
    /* Simple integer-to-decimal formatting (no snprintf available) */
    u64 pos = 0;
#define APPEND_STR(s) do { \
    const char *_s = (s); \
    while (*_s && pos < FBCTL_BUF_LEN - 1) tmp[pos++] = *_s++; \
} while (0)
#define APPEND_U32(v) do { \
    u32 _v = (v); char _d[12]; int _n = 0; \
    if (_v == 0) { _d[_n++] = '0'; } \
    else { while (_v) { _d[_n++] = '0' + (_v % 10); _v /= 10; } } \
    for (int _i = _n - 1; _i >= 0 && pos < FBCTL_BUF_LEN - 1; _i--) \
        tmp[pos++] = _d[_i]; \
} while (0)
    APPEND_STR("width=");  APPEND_U32(w);
    APPEND_STR(" height="); APPEND_U32(h);
    APPEND_STR(" pitch=");  APPEND_U32(p);
    APPEND_STR(" bpp=");    APPEND_U32(bpp);
    APPEND_STR("\n");
    tmp[pos] = 0;
#undef APPEND_STR
#undef APPEND_U32

    u64 len = pos;
    if ((u64)*off >= len) return 0;
    u64 avail = len - (u64)*off;
    if (avail > count) avail = count;
    memcpy(buf, tmp + *off, avail);
    *off += (vfs_off_t)avail;
    return (i64)avail;
}

static const struct vfs_file_ops fbctl_fops = {
    .read = fbctl_read,
};

void devfs_register_fb(void)
{
    devfs_register("fb",    VFS_S_IFCHR | 0660, &fb_fops,    0);
    devfs_register("fbctl", VFS_S_IFCHR | 0444, &fbctl_fops, 0);
}

/* ---- mount / unmount ---- */

static i32 devfs_mount_fs(struct vfs_superblock *sb, void *device, const char *opts)
{
    (void)device; (void)opts;

    struct vfs_inode *root_vino = devfs_make_root(sb);
    if (!root_vino) return VFS_ENOMEM;

    struct vfs_dentry *root_dent = (struct vfs_dentry *)kalloc(1);
    if (!root_dent) { kfree(root_vino, 1); return VFS_ENOMEM; }
    memset(root_dent, 0, sizeof(*root_dent));
    root_dent->refcnt   = 1;
    root_dent->name[0]  = '/';
    root_dent->name[1]  = 0;
    root_dent->name_len = 1;
    root_dent->inode    = root_vino;

    sb->priv = 0;
    sb->root = root_dent;
    return VFS_OK;
}

static void devfs_unmount(struct vfs_superblock *sb)
{
    (void)sb;
}

/* ---- registration ---- */

static struct vfs_fs_type devfs_fs_type = {
    .name    = "devfs",
    .mount   = devfs_mount_fs,
    .unmount = devfs_unmount,
    .next    = 0,
};

void devfs_init(void)
{
    vfs_register_fs(&devfs_fs_type);

    /* built-in devices */
    devfs_register("null", VFS_S_IFCHR | 0666, &null_fops, 0);
    devfs_register("zero", VFS_S_IFCHR | 0666, &zero_fops, 0);
    devfs_register("cons", VFS_S_IFCHR | 0666, &cons_fops, 0);
}
