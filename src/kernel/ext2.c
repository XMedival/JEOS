#include "ext2.h"
#include "blk.h"
#include "mem.h"
#include "string.h"
#include "print.h"

/* ---- per-filesystem private state ---- */

struct ext2_priv {
    struct blk_device *dev;
    u32 block_size;
    u32 blocks_per_group;
    u32 inodes_per_group;
    u32 inode_size;
    u32 num_groups;
    u32 first_inode;
    struct ext2_bgd *bgdt;
    u32 bgdt_pages;
};

/* ---- block I/O ---- */

static i32 ext2_read_block(struct ext2_priv *p, u32 blkno, void *buf)
{
    u32 spb = p->block_size / p->dev->sector_size;
    return blk_read(p->dev, (u64)blkno * spb, spb, buf);
}

/* ---- inode read ---- */

static i32 ext2_read_inode(struct ext2_priv *p, u32 ino, struct ext2_inode *dst)
{
    if (ino == 0) return -1;

    u32 grp        = (ino - 1) / p->inodes_per_group;
    u32 idx        = (ino - 1) % p->inodes_per_group;
    u32 itbl       = p->bgdt[grp].inode_table;
    u32 byte_off   = idx * p->inode_size;
    u32 blk_off    = byte_off / p->block_size;
    u32 off_in_blk = byte_off % p->block_size;

    u8 *buf = (u8 *)kalloc(1);
    if (!buf) return -1;

    i32 rc = ext2_read_block(p, itbl + blk_off, buf);
    if (rc == 0)
        memcpy(dst, buf + off_in_blk, sizeof(struct ext2_inode));

    kfree(buf, 1);
    return rc;
}

/* ---- block mapping ---- */

static u32 ext2_block_map(struct ext2_priv *p, const struct ext2_inode *ei, u32 lbn)
{
    const u32 ptrs = p->block_size / 4;

    if (lbn < 12) return ei->block[lbn];
    lbn -= 12;

    /* Singly indirect */
    if (lbn < ptrs) {
        if (!ei->block[12]) return 0;
        u32 *buf = (u32 *)kalloc(1);
        if (!buf) return 0;
        ext2_read_block(p, ei->block[12], buf);
        u32 blk = buf[lbn];
        kfree(buf, 1);
        return blk;
    }
    lbn -= ptrs;

    /* Doubly indirect */
    if (lbn < ptrs * ptrs) {
        if (!ei->block[13]) return 0;
        u32 *buf = (u32 *)kalloc(1);
        if (!buf) return 0;
        ext2_read_block(p, ei->block[13], buf);
        u32 l1 = buf[lbn / ptrs];
        kfree(buf, 1);
        if (!l1) return 0;
        buf = (u32 *)kalloc(1);
        if (!buf) return 0;
        ext2_read_block(p, l1, buf);
        u32 blk = buf[lbn % ptrs];
        kfree(buf, 1);
        return blk;
    }
    lbn -= ptrs * ptrs;

    /* Triply indirect */
    if (!ei->block[14]) return 0;
    u32 *buf = (u32 *)kalloc(1);
    if (!buf) return 0;
    ext2_read_block(p, ei->block[14], buf);
    u32 l1 = buf[lbn / (ptrs * ptrs)];
    kfree(buf, 1);
    if (!l1) return 0;
    buf = (u32 *)kalloc(1);
    if (!buf) return 0;
    ext2_read_block(p, l1, buf);
    u32 l2 = buf[(lbn / ptrs) % ptrs];
    kfree(buf, 1);
    if (!l2) return 0;
    buf = (u32 *)kalloc(1);
    if (!buf) return 0;
    ext2_read_block(p, l2, buf);
    u32 blk = buf[lbn % ptrs];
    kfree(buf, 1);
    return blk;
}

/* ---- VFS mode conversion ---- */

static vfs_mode_t ext2_vfs_mode(u16 m)
{
    vfs_mode_t type;
    switch (m & 0xF000) {
        case EXT2_S_IFREG:  type = VFS_S_IFREG;  break;
        case EXT2_S_IFDIR:  type = VFS_S_IFDIR;  break;
        case EXT2_S_IFLNK:  type = VFS_S_IFLNK;  break;
        case EXT2_S_IFCHR:  type = VFS_S_IFCHR;  break;
        case EXT2_S_IFBLK:  type = VFS_S_IFBLK;  break;
        case EXT2_S_IFIFO:  type = VFS_S_IFIFO;  break;
        case EXT2_S_IFSOCK: type = VFS_S_IFSOCK; break;
        default:            type = 0;             break;
    }
    return type | (m & 0x0FFF);
}

static const struct vfs_inode_ops ext2_inode_ops;
static const struct vfs_file_ops  ext2_file_ops;

static struct vfs_inode *ext2_make_vfs_inode(struct vfs_superblock *sb,
                                              u32 ino_num,
                                              const struct ext2_inode *ei)
{
    struct ext2_inode *copy = (struct ext2_inode *)kalloc(1);
    if (!copy) return 0;
    memcpy(copy, ei, sizeof(*ei));

    struct vfs_inode *vino = (struct vfs_inode *)kalloc(1);
    if (!vino) { kfree(copy, 1); return 0; }
    memset(vino, 0, sizeof(*vino));

    vino->ino    = ino_num;
    vino->mode   = ext2_vfs_mode(ei->mode);
    vino->refcnt = 1;
    vino->sb     = sb;
    vino->iops   = &ext2_inode_ops;
    vino->fops   = &ext2_file_ops;
    vino->priv   = copy;
    return vino;
}

/* ---- inode ops ---- */

static i32 ext2_getattr(struct vfs_inode *vino, struct vfs_stat *st)
{
    struct ext2_inode *ei = (struct ext2_inode *)vino->priv;
    st->ino    = vino->ino;
    st->mode   = vino->mode;
    st->nlink  = ei->links_count;
    st->uid    = ei->uid;
    st->gid    = ei->gid;
    st->size   = (u64)ei->size_low | ((u64)ei->size_high << 32);
    st->blocks = ei->blocks_count;
    st->atime  = ei->atime;
    st->mtime  = ei->mtime;
    st->ctime  = ei->ctime;
    return VFS_OK;
}

static i32 ext2_lookup(struct vfs_inode *dir, struct vfs_dentry *child)
{
    struct ext2_inode *dei  = (struct ext2_inode *)dir->priv;
    struct ext2_priv  *priv = (struct ext2_priv *)dir->sb->priv;
    u32 dir_size = dei->size_low;

    u8 *buf = (u8 *)kalloc(1);
    if (!buf) return VFS_ENOMEM;

    i32 result  = VFS_ENOENT;
    u32 cur_blk = ~0u;
    u32 offset  = 0;

    while (offset < dir_size) {
        u32 lbn     = offset / priv->block_size;
        u32 off_blk = offset % priv->block_size;

        if (lbn != cur_blk) {
            u32 phys = ext2_block_map(priv, dei, lbn);
            if (!phys) break;
            if (ext2_read_block(priv, phys, buf) != 0) break;
            cur_blk = lbn;
        }

        struct ext2_dirent *de = (struct ext2_dirent *)(buf + off_blk);
        if (de->rec_len < 8) break;

        if (de->inode != 0 && de->name_len == child->name_len) {
            u8 match = 1;
            for (u8 i = 0; i < de->name_len; i++) {
                if (de->name[i] != child->name[i]) { match = 0; break; }
            }
            if (match) {
                struct ext2_inode ei;
                if (ext2_read_inode(priv, de->inode, &ei) != 0) {
                    result = -1; break;
                }
                struct vfs_inode *vino = ext2_make_vfs_inode(dir->sb, de->inode, &ei);
                if (!vino) { result = VFS_ENOMEM; break; }
                child->inode = vino;
                result = VFS_OK;
                break;
            }
        }

        offset += de->rec_len;
    }

    kfree(buf, 1);
    return result;
}

/* ---- file ops ---- */

static i32 ext2_open(struct vfs_inode *inode, struct vfs_file *file)
{
    (void)inode; (void)file;
    return VFS_OK;
}

static i32 ext2_close(struct vfs_file *file)
{
    (void)file;
    return VFS_OK;
}

static i64 ext2_read(struct vfs_file *file, void *buf, u64 count, vfs_off_t *off)
{
    struct ext2_inode *ei   = (struct ext2_inode *)file->inode->priv;
    struct ext2_priv  *priv = (struct ext2_priv *)file->inode->sb->priv;
    u64 size = (u64)ei->size_low | ((u64)ei->size_high << 32);

    if ((u64)*off >= size) return 0;
    if ((u64)*off + count > size) count = size - (u64)*off;

    u8 *blk_buf = (u8 *)kalloc(1);
    if (!blk_buf) return VFS_ENOMEM;

    u64 done    = 0;
    u32 cur_blk = ~0u;
    u8 *out     = (u8 *)buf;

    while (done < count) {
        u32 lbn     = (u32)(((u64)*off + done) / priv->block_size);
        u32 blk_off = (u32)(((u64)*off + done) % priv->block_size);
        u64 chunk   = priv->block_size - blk_off;
        if (chunk > count - done) chunk = count - done;

        if (lbn != cur_blk) {
            u32 phys = ext2_block_map(priv, ei, lbn);
            if (!phys) break;
            if (ext2_read_block(priv, phys, blk_buf) != 0) break;
            cur_blk = lbn;
        }

        memcpy(out + done, blk_buf + blk_off, chunk);
        done += chunk;
    }

    kfree(blk_buf, 1);
    *off += (vfs_off_t)done;
    return (i64)done;
}

static i32 ext2_readdir(struct vfs_file *file, struct vfs_dirent *out)
{
    struct ext2_inode *ei   = (struct ext2_inode *)file->inode->priv;
    struct ext2_priv  *priv = (struct ext2_priv *)file->inode->sb->priv;
    u32 dir_size = ei->size_low;

    if ((u32)file->pos >= dir_size) return VFS_ENOENT;

    u8 *buf = (u8 *)kalloc(1);
    if (!buf) return VFS_ENOMEM;

    i32 rc      = VFS_ENOENT;
    u32 cur_blk = ~0u;
    u32 offset  = (u32)file->pos;

    while (offset < dir_size) {
        u32 lbn     = offset / priv->block_size;
        u32 blk_off = offset % priv->block_size;

        if (lbn != cur_blk) {
            u32 phys = ext2_block_map(priv, ei, lbn);
            if (!phys) break;
            if (ext2_read_block(priv, phys, buf) != 0) break;
            cur_blk = lbn;
        }

        struct ext2_dirent *de = (struct ext2_dirent *)(buf + blk_off);
        if (de->rec_len < 8) break;

        offset += de->rec_len;

        if (de->inode == 0) continue; /* deleted entry */

        out->ino    = de->inode;
        out->reclen = sizeof(*out);
        out->type   = de->file_type;
        u8 nlen = de->name_len < 255 ? de->name_len : 255;
        memcpy(out->name, de->name, nlen);
        out->name[nlen] = 0;

        file->pos = offset;
        rc = VFS_OK;
        break;
    }

    kfree(buf, 1);
    return rc;
}

/* ---- ops tables ---- */

static const struct vfs_inode_ops ext2_inode_ops = {
    .lookup  = ext2_lookup,
    .getattr = ext2_getattr,
};

static const struct vfs_file_ops ext2_file_ops = {
    .open    = ext2_open,
    .close   = ext2_close,
    .read    = ext2_read,
    .readdir = ext2_readdir,
};

/* ---- mount / unmount ---- */

static i32 ext2_mount_fs(struct vfs_superblock *sb, void *device, const char *opts)
{
    (void)opts;
    struct blk_device *dev = (struct blk_device *)device;
    if (!dev) return -1;

    /* Superblock is always at byte offset 1024.
       Read 1024 bytes; for 512-byte sectors that is LBA 2, count 2. */
    u32 sb_lba   = 1024 / dev->sector_size;
    u32 sb_sects = 1024 / dev->sector_size;
    if (sb_sects == 0) sb_sects = 1;

    u8 *raw = (u8 *)kalloc(1);
    if (!raw) return VFS_ENOMEM;

    if (blk_read(dev, sb_lba, sb_sects, raw) != 0) {
        kfree(raw, 1);
        return -1;
    }

    struct ext2_superblock *esb = (struct ext2_superblock *)raw;
    if (esb->signature != EXT2_SIGNATURE) {
        klog_fail("EXT2", "bad signature 0x%x", esb->signature);
        kfree(raw, 1);
        return -1;
    }

    struct ext2_priv *priv = (struct ext2_priv *)kalloc(1);
    if (!priv) { kfree(raw, 1); return VFS_ENOMEM; }
    memset(priv, 0, sizeof(*priv));

    priv->dev              = dev;
    priv->block_size       = 1024u << esb->block_size;
    priv->blocks_per_group = esb->block_per_group;
    priv->inodes_per_group = esb->inodes_per_group;
    priv->inode_size       = 128;
    priv->first_inode      = 11;

    if (esb->version_high >= 1) {
        struct ext2_superblock_ext *ext =
            (struct ext2_superblock_ext *)(raw + sizeof(struct ext2_superblock));
        priv->inode_size  = ext->inode_size;
        priv->first_inode = ext->first_inode;
    }

    priv->num_groups =
        (esb->total_blocs + esb->block_per_group - 1) / esb->block_per_group;

    /* BGD table: block 2 for 1024-byte blocks, block 1 for larger blocks */
    u32 bgdt_block = (priv->block_size == 1024) ? 2 : 1;
    u32 bgdt_bytes = priv->num_groups * (u32)sizeof(struct ext2_bgd);
    u32 bgdt_pages = (bgdt_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (bgdt_pages == 0) bgdt_pages = 1;

    priv->bgdt = (struct ext2_bgd *)kalloc(bgdt_pages);
    if (!priv->bgdt) { kfree(raw, 1); kfree(priv, 1); return VFS_ENOMEM; }
    priv->bgdt_pages = bgdt_pages;
    memset(priv->bgdt, 0, (u64)bgdt_pages * PAGE_SIZE);

    u32 bgdt_blks = (bgdt_bytes + priv->block_size - 1) / priv->block_size;
    u8 *bgdt_buf  = (u8 *)priv->bgdt;
    for (u32 i = 0; i < bgdt_blks; i++) {
        u8 *tmp = (u8 *)kalloc(1);
        if (!tmp) {
            kfree(priv->bgdt, bgdt_pages);
            kfree(raw, 1);
            kfree(priv, 1);
            return VFS_ENOMEM;
        }
        ext2_read_block(priv, bgdt_block + i, tmp);
        u64 written = (u64)i * priv->block_size;
        u64 copy    = priv->block_size;
        if (written + copy > bgdt_bytes) copy = bgdt_bytes - written;
        memcpy(bgdt_buf + written, tmp, copy);
        kfree(tmp, 1);
    }

    kfree(raw, 1);
    sb->priv = priv;

    struct ext2_inode root_ei;
    if (ext2_read_inode(priv, EXT2_ROOT_INO, &root_ei) != 0) {
        kfree(priv->bgdt, bgdt_pages);
        kfree(priv, 1);
        return -1;
    }

    struct vfs_inode *root_vino = ext2_make_vfs_inode(sb, EXT2_ROOT_INO, &root_ei);
    if (!root_vino) {
        kfree(priv->bgdt, bgdt_pages);
        kfree(priv, 1);
        return VFS_ENOMEM;
    }

    struct vfs_dentry *root_dent = (struct vfs_dentry *)kalloc(1);
    if (!root_dent) {
        kfree(root_vino->priv, 1);
        kfree(root_vino, 1);
        kfree(priv->bgdt, bgdt_pages);
        kfree(priv, 1);
        return VFS_ENOMEM;
    }
    memset(root_dent, 0, sizeof(*root_dent));
    root_dent->refcnt   = 1;
    root_dent->name[0]  = '/';
    root_dent->name[1]  = 0;
    root_dent->name_len = 1;
    root_dent->inode    = root_vino;
    sb->root = root_dent;

    klog_ok("EXT2", "mounted  block_size=%u  groups=%u",
           priv->block_size, priv->num_groups);
    return VFS_OK;
}

static void ext2_unmount(struct vfs_superblock *sb)
{
    if (!sb || !sb->priv) return;
    struct ext2_priv *priv = (struct ext2_priv *)sb->priv;
    kfree(priv->bgdt, priv->bgdt_pages);
    kfree(priv, 1);
    sb->priv = 0;
}

static struct vfs_fs_type ext2_fs_type = {
    .name    = "ext2",
    .mount   = ext2_mount_fs,
    .unmount = ext2_unmount,
    .next    = 0,
};

void ext2_init(void)
{
    vfs_register_fs(&ext2_fs_type);
}
