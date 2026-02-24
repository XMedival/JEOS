#pragma once

#include "types.h"

/* ----------------------------- error style ----------------------------- */
/* Return 0 on success, negative error codes on failure */
#ifndef VFS_OK
#define VFS_OK 0
#endif

/* ----------------------------- error codes ----------------------------- */
/* Return 0 on success, negative error codes on failure */
#define VFS_EINVAL   (-22)
#define VFS_ENOENT   (-2)
#define VFS_ENOTDIR  (-20)
#define VFS_EEXIST   (-17)
#define VFS_ENOSYS   (-38)
#define VFS_ENOMEM   (-12)
#define VFS_EBUSY    (-16)

/* ----------------------------- basic types ----------------------------- */

typedef u32 vfs_mode_t;   /* permission bits + file type */
typedef u32 vfs_uid_t;
typedef u32 vfs_gid_t;
typedef u64 vfs_ino_t;
typedef u64 vfs_off_t;

/* Common Unix-ish mode bits (keep/adjust as you like) */
#define VFS_S_IFMT   0170000
#define VFS_S_IFREG  0100000
#define VFS_S_IFDIR  0040000
#define VFS_S_IFLNK  0120000
#define VFS_S_IFCHR  0020000
#define VFS_S_IFBLK  0060000
#define VFS_S_IFIFO  0010000
#define VFS_S_IFSOCK 0140000

/* open flags (subset) */
#define VFS_O_RDONLY  0x0001
#define VFS_O_WRONLY  0x0002
#define VFS_O_RDWR    0x0003
#define VFS_O_CREAT   0x0100
#define VFS_O_TRUNC   0x0200
#define VFS_O_APPEND  0x0400
#define VFS_O_EXCL    0x0800

/* seek whence */
#define VFS_SEEK_SET  0
#define VFS_SEEK_CUR  1
#define VFS_SEEK_END  2

/* dentry lookup flags */
#define VFS_LOOKUP_FOLLOW   0x01  /* follow symlinks (if you implement them) */

/* mount flags */
#define VFS_MS_RDONLY       0x01

/* ----------------------------- forward decls ----------------------------- */

struct vfs_fs_type;
struct vfs_superblock;
struct vfs_mount;

struct vfs_inode;
struct vfs_dentry;
struct vfs_file;

/* ----------------------------- stat-ish ----------------------------- */

struct vfs_stat {
  vfs_ino_t   ino;
  vfs_mode_t  mode;
  u32         nlink;
  vfs_uid_t   uid;
  vfs_gid_t   gid;
  u64         size;
  u64         blocks;
  u64         atime; /* timestamps: pick your unit */
  u64         mtime;
  u64         ctime;
};

/* directory entry emitted by readdir */
struct vfs_dirent {
  vfs_ino_t ino;
  u16       reclen;
  u8        type;         /* DT_* style if you want */
  char      name[256];    /* keep it fixed for now */
};

/* ----------------------------- ops tables ----------------------------- */

struct vfs_inode_ops {
  /* directory ops */
  i32 (*lookup)(struct vfs_inode *dir, struct vfs_dentry *child); /* fill child->inode */
  i32 (*create)(struct vfs_inode *dir, struct vfs_dentry *child, vfs_mode_t mode);
  i32 (*mkdir)(struct vfs_inode *dir, struct vfs_dentry *child, vfs_mode_t mode);
  i32 (*unlink)(struct vfs_inode *dir, struct vfs_dentry *child);
  i32 (*rmdir)(struct vfs_inode *dir, struct vfs_dentry *child);
  i32 (*rename)(struct vfs_inode *old_dir, struct vfs_dentry *old_dent,
                struct vfs_inode *new_dir, struct vfs_dentry *new_dent);

  /* metadata */
  i32 (*getattr)(struct vfs_inode *inode, struct vfs_stat *st);
  i32 (*setattr)(struct vfs_inode *inode, const struct vfs_stat *st, u32 mask);

  /* symlink (optional) */
  i32 (*readlink)(struct vfs_inode *inode, char *buf, u64 bufsz);
};

struct vfs_file_ops {
  i32   (*open)(struct vfs_inode *inode, struct vfs_file *file);
  i32   (*close)(struct vfs_file *file);

  i64 (*read)(struct vfs_file *file, void *buf, u64 count, vfs_off_t *off);
  i64 (*write)(struct vfs_file *file, const void *buf, u64 count, vfs_off_t *off);
  vfs_off_t (*llseek)(struct vfs_file *file, vfs_off_t off, i32 whence);

  /* directory iteration */
  i32 (*readdir)(struct vfs_file *file, struct vfs_dirent *out);
};

struct vfs_super_ops {
  i32 (*sync)(struct vfs_superblock *sb);
  i32 (*statfs)(struct vfs_superblock *sb, void *out /* your fsinfo struct */);
};

/* ----------------------------- core objects ----------------------------- */

struct vfs_inode {
  vfs_ino_t ino;
  vfs_mode_t mode;
  u32 refcnt;

  struct vfs_superblock *sb;

  const struct vfs_inode_ops *iops;
  const struct vfs_file_ops  *fops;

  void *priv; /* fs-specific inode data */
};

struct vfs_dentry {
  u32 refcnt;

  char name[256];
  u16 name_len;

  struct vfs_inode  *inode;   /* may be NULL for negative dentry */
  struct vfs_dentry *parent;

  /* mountpoint support */
  u8 is_mountpoint;

  void *priv; /* fs-specific dentry data (optional) */
};

struct vfs_superblock {
  u32 flags;
  u32 refcnt;

  const struct vfs_super_ops *sops;

  struct vfs_fs_type *type;
  struct vfs_dentry  *root;     /* root dentry of this fs instance */

  void *priv; /* fs-specific superblock data */
};

struct vfs_mount {
  u32 flags;
  u32 refcnt;

  struct vfs_superblock *sb;

  /* where it is mounted */
  struct vfs_dentry *mountpoint;

  /* root inside this mount */
  struct vfs_dentry *root;

  /* optional: link mounts in a list */
  struct vfs_mount *next;
};

struct vfs_file {
  u32 refcnt;

  u32 flags;        /* VFS_O_* */
  vfs_off_t pos;

  struct vfs_inode *inode;
  const struct vfs_file_ops *fops;

  void *priv;       /* fs-specific per-open state */
};

/* ----------------------------- fs type registration ----------------------------- */

struct vfs_fs_type {
  const char *name;

  /* Create/initialize superblock + root based on device/opts */
  i32 (*mount)(struct vfs_superblock *sb, void *device, const char *opts);
  void (*unmount)(struct vfs_superblock *sb);

  struct vfs_fs_type *next;
};

/* Register/unregister filesystem implementations */
i32 vfs_register_fs(struct vfs_fs_type *type);
i32 vfs_unregister_fs(struct vfs_fs_type *type);

/* ----------------------------- mount API ----------------------------- */

/* Mount fs 'type_name' backed by 'device' at 'target_path' */
i32 vfs_mount(const char *type_name, void *device, const char *target_path,
              u32 mount_flags, const char *opts);

i32 vfs_umount(const char *target_path);

/* ----------------------------- path resolution ----------------------------- */

struct vfs_path {
  struct vfs_mount  *mnt;
  struct vfs_dentry *dentry;
};

/* Resolve a path to a (mount,dentry). flags: VFS_LOOKUP_* */
i32 vfs_lookup(const char *path, u32 flags, struct vfs_path *out);

/* ----------------------------- fd-style API ----------------------------- */
/* You can implement these as syscall backend helpers. */

i32   vfs_open(const char *path, u32 flags, vfs_mode_t mode, struct vfs_file **out);
i32   vfs_close(struct vfs_file *f);

i64 vfs_read(struct vfs_file *f, void *buf, u64 count);
i64 vfs_write(struct vfs_file *f, const void *buf, u64 count);
vfs_off_t vfs_seek(struct vfs_file *f, vfs_off_t off, i32 whence);

i32   vfs_stat(const char *path, struct vfs_stat *st);
i32   vfs_fstat(struct vfs_file *f, struct vfs_stat *st);

i32   vfs_mkdir(const char *path, vfs_mode_t mode);
i32   vfs_unlink(const char *path);

/* ----------------------------- refcount helpers ----------------------------- */

void vfs_inode_get(struct vfs_inode *ino);
void vfs_inode_put(struct vfs_inode *ino);

void vfs_dentry_get(struct vfs_dentry *d);
void vfs_dentry_put(struct vfs_dentry *d);

void vfs_file_get(struct vfs_file *f);
void vfs_file_put(struct vfs_file *f);

/* ----------------------------- init ----------------------------- */

/* Call during boot. Creates root mount, registers built-in fs, etc. */
void vfs_init(void);

/* Set the initial root mount/dentry (e.g., after mounting your rootfs) */
i32 vfs_set_root(struct vfs_mount *mnt, struct vfs_dentry *root);
