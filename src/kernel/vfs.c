#include "vfs.h"
#include "mem.h"
#include "string.h"

/* ----------------------------- globals ----------------------------- */

static struct vfs_fs_type *g_fs_types = 0;   /* singly linked list */
static struct vfs_mount   *g_mounts   = 0;   /* singly linked list */

/* root of namespace */
static struct vfs_mount  *g_root_mnt  = 0;
static struct vfs_dentry *g_root_dent = 0;

/* ----------------------------- refcount helpers ----------------------------- */

void vfs_inode_get(struct vfs_inode *ino) { if (ino) ino->refcnt++; }

void vfs_inode_put(struct vfs_inode *ino)
{
  if (!ino) return;
  if (--ino->refcnt == 0) {
    /* VFS doesn't know how to free fs-private inodes safely here.
       Most kernels free inodes via inode cache + FS callbacks.
       For now: do nothing. FS can manage inode lifetime in priv. */
  }
}

void vfs_dentry_get(struct vfs_dentry *d) { if (d) d->refcnt++; }

void vfs_dentry_put(struct vfs_dentry *d)
{
  if (!d) return;
  if (--d->refcnt == 0) {
    /* same story as inode: if this is VFS-allocated temporary dentry, free it.
       We'll only free dentries that have parent==NULL (temporary). */
    if (d->parent == 0) {
      if (d->inode) vfs_inode_put(d->inode);
      kfree(d, 1);
    }
  }
}

void vfs_file_get(struct vfs_file *f) { if (f) f->refcnt++; }

void vfs_file_put(struct vfs_file *f)
{
  if (!f) return;
  if (--f->refcnt == 0) {
    if (f->inode) vfs_inode_put(f->inode);
    kfree(f, 1);
  }
}

/* ----------------------------- init/root ----------------------------- */

void vfs_init(void)
{
  g_fs_types = 0;
  g_mounts   = 0;
  g_root_mnt = 0;
  g_root_dent = 0;
}

i32 vfs_set_root(struct vfs_mount *mnt, struct vfs_dentry *root)
{
  if (!mnt || !root) return VFS_EINVAL;
  g_root_mnt = mnt;
  g_root_dent = root;
  return VFS_OK;
}

/* ----------------------------- fs registry ----------------------------- */

static struct vfs_fs_type *vfs_find_fs(const char *name)
{
  struct vfs_fs_type *it = g_fs_types;
  while (it) {
    if (it->name && name && (kstrcmp(it->name, name) == 0)) return it;
    it = it->next;
  }
  return 0;
}

i32 vfs_register_fs(struct vfs_fs_type *type)
{
  if (!type || !type->name || !type->mount) return VFS_EINVAL;
  if (vfs_find_fs(type->name)) return VFS_EEXIST;

  type->next = g_fs_types;
  g_fs_types = type;
  return VFS_OK;
}

i32 vfs_unregister_fs(struct vfs_fs_type *type)
{
  if (!type) return VFS_EINVAL;

  /* refuse if mounted */
  struct vfs_mount *m = g_mounts;
  while (m) {
    if (m->sb && m->sb->type == type) return VFS_EBUSY;
    m = m->next;
  }

  struct vfs_fs_type **pp = &g_fs_types;
  while (*pp) {
    if (*pp == type) {
      *pp = type->next;
      type->next = 0;
      return VFS_OK;
    }
    pp = &(*pp)->next;
  }
  return VFS_ENOENT;
}

/* ----------------------------- internal helpers ----------------------------- */

static u8 vfs_is_dir(const struct vfs_inode *ino)
{
  if (!ino) return 0;
  return (ino->mode & VFS_S_IFMT) == VFS_S_IFDIR;
}

static struct vfs_mount *vfs_find_mount_by_mountpoint(struct vfs_dentry *mp)
{
  struct vfs_mount *m = g_mounts;
  while (m) {
    if (m->mountpoint == mp) return m;
    m = m->next;
  }
  return 0;
}

/* Find a mount by inode identity (ino number + superblock).
   Used by vfs_lookup_child because child dentries are temporaries —
   comparing pointers would never match the original mountpoint dentry. */
static struct vfs_mount *vfs_find_mount_by_inode(struct vfs_inode *ino)
{
  if (!ino) return 0;
  struct vfs_mount *m = g_mounts;
  while (m) {
    if (m->mountpoint && m->mountpoint->inode &&
        m->mountpoint->inode->ino == ino->ino &&
        m->mountpoint->inode->sb  == ino->sb)
      return m;
    m = m->next;
  }
  return 0;
}

/* Temporary dentry allocator used for lookup and leaf ops.
   parent==0 marks it as "temp" so vfs_dentry_put will kfree it. */
static struct vfs_dentry *vfs_tmp_dentry(const char *name, u64 len, struct vfs_dentry *parent)
{
  if (len >= sizeof(((struct vfs_dentry*)0)->name)) return 0;

  struct vfs_dentry *d = (struct vfs_dentry*)kalloc(1);
  if (!d) return 0;

  memset(d, 0, sizeof(*d));
  d->refcnt = 1;
  d->parent = parent;
  d->name_len = (u16)len;
  if (len) memcpy(d->name, name, len);
  d->name[len] = 0;
  d->inode = 0;
  d->is_mountpoint = 0;
  d->priv = 0;
  return d;
}

/* Parse next path component.
   - *p points into string; will be advanced past component and separators.
   - returns component start + length, or len=0 if end. */
static void vfs_next_component(const char **p, const char **out_s, u64 *out_len)
{
  const char *s = *p;

  while (*s == '/') s++;          /* skip slashes */
  if (*s == 0) {                  /* end */
    *out_s = s;
    *out_len = 0;
    *p = s;
    return;
  }

  const char *start = s;
  while (*s && *s != '/') s++;

  *out_s = start;
  *out_len = (u64)(s - start);

  while (*s == '/') s++;
  *p = s;
}

static u8 vfs_comp_eq(const char *s, u64 len, const char *lit)
{
  u64 n = kstrlen(lit);
  if (n != len) return 0;
  for (u64 i = 0; i < len; i++) {
    if (s[i] != lit[i]) return 0;
  }
  return 1;
}

/* Lookup a single name under dir dentry in current mount.
   Returns 0 and updates (mnt,dent) to the child.
   Uses FS lookup op; no caching. */
static i32 vfs_lookup_child(struct vfs_mount **mnt, struct vfs_dentry **dir_dent,
                            const char *name, u64 len, u32 flags,
                            struct vfs_dentry **out_child)
{
  (void)flags;

  struct vfs_inode *dir_ino = (*dir_dent)->inode;
  if (!dir_ino || !vfs_is_dir(dir_ino)) return VFS_ENOTDIR;
  if (!dir_ino->iops || !dir_ino->iops->lookup) return VFS_ENOSYS;

  struct vfs_dentry *child = vfs_tmp_dentry(name, len, *dir_dent);
  if (!child) return VFS_ENOMEM;

  i32 rc = dir_ino->iops->lookup(dir_ino, child);
  if (rc < 0) { vfs_dentry_put(child); return rc; }
  if (!child->inode) { vfs_dentry_put(child); return VFS_ENOENT; }

  /* if this inode is a mountpoint in any namespace, jump into mounted root */
  if (child->inode) {
    struct vfs_mount *sub = vfs_find_mount_by_inode(child->inode);
    if (sub && sub->root) {
      *mnt = sub;
      *dir_dent = sub->root;
      *out_child = sub->root;
      vfs_dentry_put(child);
      return VFS_OK;
    }
  }

  *out_child = child;
  return VFS_OK;
}

/* Resolve path to parent directory + leaf name.
   Useful for create/mkdir/unlink.
   leaf ptr points into original path (not copied). */
static i32 vfs_lookup_parent(const char *path, u32 flags,
                             struct vfs_mount **out_mnt,
                             struct vfs_dentry **out_parent,
                             const char **out_leaf, u64 *out_leaf_len)
{
  if (!path || !out_mnt || !out_parent || !out_leaf || !out_leaf_len) return VFS_EINVAL;
  if (!g_root_mnt || !g_root_dent) return VFS_EINVAL;

  struct vfs_mount  *mnt = g_root_mnt;
  struct vfs_dentry *cur = g_root_dent;

  /* follow any filesystem overlaid on top of the root (e.g. ext2 over initfs) */
  if (cur->is_mountpoint) {
    struct vfs_mount *overlay = vfs_find_mount_by_mountpoint(cur);
    if (overlay) { mnt = overlay; cur = overlay->root; }
  }

  const char *p = path;
  if (*p == '/') {
    while (*p == '/') p++;
  }

  const char *comp = 0;
  u64 comp_len = 0;

  const char *last = 0;
  u64 last_len = 0;

  while (1) {
    vfs_next_component(&p, &comp, &comp_len);
    if (comp_len == 0) break; /* end */

    last = comp;
    last_len = comp_len;

    /* peek if there's more after this */
    const char *peek = p;
    const char *ncomp; u64 nlen;
    vfs_next_component(&peek, &ncomp, &nlen);

    if (nlen == 0) {
      /* last component -> stop at parent */
      *out_mnt = mnt;
      *out_parent = cur;
      *out_leaf = last;
      *out_leaf_len = last_len;
      return VFS_OK;
    }

    if (vfs_comp_eq(comp, comp_len, ".")) {
      continue;
    } else if (vfs_comp_eq(comp, comp_len, "..")) {
      /* cross mount boundary if needed */
      if (cur == mnt->root && mnt->mountpoint) {
        cur = mnt->mountpoint;
        /* parent mount is the one containing mountpoint; find it */
        /* naive: search mounts for one whose root can reach mountpoint; we don’t track parent.
           For now, just stay in current root namespace: best effort. */
      } else if (cur->parent) {
        cur = cur->parent;
      }
      continue;
    }

    struct vfs_dentry *child = 0;
    i32 rc = vfs_lookup_child(&mnt, &cur, comp, comp_len, flags, &child);
    if (rc < 0) return rc;

    /* if child returned is temp (normal), it becomes new cur; keep it until function exits.
       but we don't have caching; so we'd leak temps if we don't free. For parent walk we can
       treat cur as temp and free previous when advancing. */
    if (cur != g_root_dent && cur != mnt->root) {
      /* cur may be a temp dentry; free it */
      vfs_dentry_put(cur);
    }
    cur = child;
  }

  /* path has no leaf (e.g. "/" or "") -> parent is current, leaf empty */
  *out_mnt = mnt;
  *out_parent = cur;
  *out_leaf = "";
  *out_leaf_len = 0;
  return VFS_OK;
}

/* ----------------------------- lookup API ----------------------------- */

i32 vfs_lookup(const char *path, u32 flags, struct vfs_path *out)
{
  if (!path || !out) return VFS_EINVAL;
  if (!g_root_mnt || !g_root_dent) return VFS_EINVAL;

  struct vfs_mount  *mnt = g_root_mnt;
  struct vfs_dentry *cur = g_root_dent;

  /* follow any filesystem overlaid on top of the root */
  if (cur->is_mountpoint) {
    struct vfs_mount *overlay = vfs_find_mount_by_mountpoint(cur);
    if (overlay) { mnt = overlay; cur = overlay->root; }
  }

  const char *p = path;
  if (*p == '/') {
    while (*p == '/') p++;
  }

  const char *comp;
  u64 comp_len;

  while (1) {
    vfs_next_component(&p, &comp, &comp_len);
    if (comp_len == 0) break;

    if (vfs_comp_eq(comp, comp_len, ".")) {
      continue;
    }
    if (vfs_comp_eq(comp, comp_len, "..")) {
      if (cur == mnt->root && mnt->mountpoint) {
        cur = mnt->mountpoint;
      } else if (cur->parent) {
        cur = cur->parent;
      }
      continue;
    }

    struct vfs_dentry *child = 0;
    i32 rc = vfs_lookup_child(&mnt, &cur, comp, comp_len, flags, &child);
    if (rc < 0) return rc;

    /* free previous temp if applicable */
    if (cur != g_root_dent && cur != mnt->root) {
      vfs_dentry_put(cur);
    }
    cur = child;
  }

  out->mnt = mnt;
  out->dentry = cur;
  return VFS_OK;
}

/* ----------------------------- mount API ----------------------------- */

i32 vfs_mount(const char *type_name, void *device, const char *target_path,
              u32 mount_flags, const char *opts)
{
  if (!type_name || !target_path) return VFS_EINVAL;

  struct vfs_fs_type *type = vfs_find_fs(type_name);
  if (!type) return VFS_ENOENT;

  /* resolve mountpoint; special-case the initial root mount */
  u8 is_root_mount = (target_path[0] == '/' && target_path[1] == '\0' && !g_root_mnt);
  struct vfs_dentry *mp_dent = 0;

  if (!is_root_mount) {
    struct vfs_path mp;
    i32 rc = vfs_lookup(target_path, VFS_LOOKUP_FOLLOW, &mp);
    if (rc < 0) return rc;
    if (!mp.dentry || !mp.dentry->inode) return VFS_EINVAL;
    if (!vfs_is_dir(mp.dentry->inode)) return VFS_ENOTDIR;
    mp_dent = mp.dentry;
  }

  /* create superblock */
  struct vfs_superblock *sb = (struct vfs_superblock*)kalloc(1);
  if (!sb) return VFS_ENOMEM;
  memset(sb, 0, sizeof(*sb));
  sb->refcnt = 1;
  sb->flags = mount_flags;
  sb->type = type;

  i32 rc = type->mount(sb, device, opts);
  if (rc < 0) { kfree(sb, 1); return rc; }
  if (!sb->root || !sb->root->inode) {
    if (type->unmount) type->unmount(sb);
    kfree(sb, 1);
    return VFS_EINVAL;
  }

  /* create mount */
  struct vfs_mount *mnt = (struct vfs_mount*)kalloc(1);
  if (!mnt) {
    if (type->unmount) type->unmount(sb);
    kfree(sb, 1);
    return VFS_ENOMEM;
  }
  memset(mnt, 0, sizeof(*mnt));
  mnt->refcnt = 1;
  mnt->flags = mount_flags;
  mnt->sb = sb;
  mnt->root = sb->root;

  if (is_root_mount) {
    g_root_mnt  = mnt;
    g_root_dent = mnt->root;
  } else {
    mnt->mountpoint = mp_dent;
    mp_dent->is_mountpoint = 1;
  }

  /* add to mount list */
  mnt->next = g_mounts;
  g_mounts = mnt;

  return VFS_OK;
}

i32 vfs_umount(const char *target_path)
{
  if (!target_path) return VFS_EINVAL;

  struct vfs_path mp;
  i32 rc = vfs_lookup(target_path, VFS_LOOKUP_FOLLOW, &mp);
  if (rc < 0) return rc;

  struct vfs_mount **pp = &g_mounts;
  while (*pp) {
    if ((*pp)->mountpoint == mp.dentry) {
      struct vfs_mount *mnt = *pp;

      /* naive busy check: only refcnt==1 */
      if (mnt->refcnt != 1) return VFS_EBUSY;

      mp.dentry->is_mountpoint = 0;

      *pp = mnt->next;

      if (mnt->sb) {
        if (mnt->sb->type && mnt->sb->type->unmount)
          mnt->sb->type->unmount(mnt->sb);
        kfree(mnt->sb, 1);
      }
      kfree(mnt, 1);
      return VFS_OK;
    }
    pp = &(*pp)->next;
  }
  return VFS_ENOENT;
}

/* ----------------------------- file API ----------------------------- */

i32 vfs_open(const char *path, u32 flags, vfs_mode_t mode, struct vfs_file **out)
{
  if (!path || !out) return VFS_EINVAL;
  *out = 0;

  struct vfs_inode *ino = 0;

  if (flags & VFS_O_CREAT) {
    struct vfs_mount *pmnt = 0;
    struct vfs_dentry *pdir = 0;
    const char *leaf = 0; u64 leaf_len = 0;

    i32 rc = vfs_lookup_parent(path, VFS_LOOKUP_FOLLOW, &pmnt, &pdir, &leaf, &leaf_len);
    if (rc < 0) return rc;
    if (leaf_len == 0) return VFS_EINVAL;

    if (!pdir->inode || !vfs_is_dir(pdir->inode)) return VFS_ENOTDIR;
    if (!pdir->inode->iops || !pdir->inode->iops->create) return VFS_ENOSYS;

    struct vfs_dentry *child = vfs_tmp_dentry(leaf, leaf_len, pdir);
    if (!child) return VFS_ENOMEM;

    rc = pdir->inode->iops->create(pdir->inode, child, mode);
    if (rc < 0) { vfs_dentry_put(child); return rc; }

    ino = child->inode;
    if (!ino) { vfs_dentry_put(child); return VFS_EINVAL; }

    /* child is temp; keep inode, drop dentry */
    vfs_inode_get(ino);
    vfs_dentry_put(child);
  } else {
    struct vfs_path p;
    i32 rc = vfs_lookup(path, VFS_LOOKUP_FOLLOW, &p);
    if (rc < 0) return rc;

    if (!p.dentry || !p.dentry->inode) return VFS_ENOENT;
    ino = p.dentry->inode;
    vfs_inode_get(ino);

    /* if lookup returned temp, release it */
    if (p.dentry != g_root_dent && p.mnt && p.dentry != p.mnt->root)
      vfs_dentry_put(p.dentry);
  }

  struct vfs_file *f = (struct vfs_file*)kalloc(1);
  if (!f) { vfs_inode_put(ino); return VFS_ENOMEM; }
  memset(f, 0, sizeof(*f));
  f->refcnt = 1;
  f->flags = flags;
  f->inode = ino;
  f->fops = ino->fops;
  f->pos = 0;

  if (f->fops && f->fops->open) {
    i32 rc = f->fops->open(ino, f);
    if (rc < 0) { vfs_file_put(f); return rc; }
  }

  *out = f;
  return VFS_OK;
}

i32 vfs_close(struct vfs_file *f)
{
  if (!f) return VFS_EINVAL;

  if (f->fops && f->fops->close) {
    i32 rc = f->fops->close(f);
    if (rc < 0) return rc;
  }

  vfs_file_put(f);
  return VFS_OK;
}

i64 vfs_read(struct vfs_file *f, void *buf, u64 count)
{
  if (!f || !buf) return VFS_EINVAL;
  if (!f->fops || !f->fops->read) return VFS_ENOSYS;

  return f->fops->read(f, buf, count, &f->pos);
}

i64 vfs_write(struct vfs_file *f, const void *buf, u64 count)
{
  if (!f || !buf) return VFS_EINVAL;
  if (!f->fops || !f->fops->write) return VFS_ENOSYS;

  return f->fops->write(f, buf, count, &f->pos);
}

vfs_off_t vfs_seek(struct vfs_file *f, vfs_off_t off, i32 whence)
{
  if (!f) return (vfs_off_t)VFS_EINVAL;
  if (f->fops && f->fops->llseek) return f->fops->llseek(f, off, whence);

  /* generic seek only if we can stat size */
  struct vfs_stat st;
  if (!f->inode || !f->inode->iops || !f->inode->iops->getattr) return (vfs_off_t)VFS_ENOSYS;
  if (f->inode->iops->getattr(f->inode, &st) < 0) return (vfs_off_t)VFS_EINVAL;

  vfs_off_t newpos = f->pos;
  if (whence == VFS_SEEK_SET) newpos = off;
  else if (whence == VFS_SEEK_CUR) newpos = f->pos + off;
  else if (whence == VFS_SEEK_END) newpos = st.size + off;
  else return (vfs_off_t)VFS_EINVAL;

  f->pos = newpos;
  return newpos;
}

/* ----------------------------- stat API ----------------------------- */

i32 vfs_fstat(struct vfs_file *f, struct vfs_stat *st)
{
  if (!f || !st) return VFS_EINVAL;
  if (!f->inode || !f->inode->iops || !f->inode->iops->getattr) return VFS_ENOSYS;
  return f->inode->iops->getattr(f->inode, st);
}

i32 vfs_stat(const char *path, struct vfs_stat *st)
{
  if (!path || !st) return VFS_EINVAL;

  struct vfs_path p;
  i32 rc = vfs_lookup(path, VFS_LOOKUP_FOLLOW, &p);
  if (rc < 0) return rc;

  if (!p.dentry || !p.dentry->inode) return VFS_ENOENT;
  if (!p.dentry->inode->iops || !p.dentry->inode->iops->getattr) return VFS_ENOSYS;

  rc = p.dentry->inode->iops->getattr(p.dentry->inode, st);

  /* drop temp dentry if needed */
  if (p.dentry != g_root_dent && p.mnt && p.dentry != p.mnt->root)
    vfs_dentry_put(p.dentry);

  return rc;
}

/* ----------------------------- mkdir/unlink API ----------------------------- */

i32 vfs_mkdir(const char *path, vfs_mode_t mode)
{
  if (!path) return VFS_EINVAL;

  struct vfs_mount *pmnt = 0;
  struct vfs_dentry *pdir = 0;
  const char *leaf = 0; u64 leaf_len = 0;

  i32 rc = vfs_lookup_parent(path, VFS_LOOKUP_FOLLOW, &pmnt, &pdir, &leaf, &leaf_len);
  if (rc < 0) return rc;
  if (leaf_len == 0) return VFS_EINVAL;

  if (!pdir->inode || !vfs_is_dir(pdir->inode)) return VFS_ENOTDIR;
  if (!pdir->inode->iops || !pdir->inode->iops->mkdir) return VFS_ENOSYS;

  struct vfs_dentry *child = vfs_tmp_dentry(leaf, leaf_len, pdir);
  if (!child) return VFS_ENOMEM;

  rc = pdir->inode->iops->mkdir(pdir->inode, child, mode);
  vfs_dentry_put(child);
  return rc;
}

i32 vfs_unlink(const char *path)
{
  if (!path) return VFS_EINVAL;

  struct vfs_mount *pmnt = 0;
  struct vfs_dentry *pdir = 0;
  const char *leaf = 0; u64 leaf_len = 0;

  i32 rc = vfs_lookup_parent(path, VFS_LOOKUP_FOLLOW, &pmnt, &pdir, &leaf, &leaf_len);
  if (rc < 0) return rc;
  if (leaf_len == 0) return VFS_EINVAL;

  if (!pdir->inode || !vfs_is_dir(pdir->inode)) return VFS_ENOTDIR;
  if (!pdir->inode->iops || !pdir->inode->iops->unlink) return VFS_ENOSYS;

  struct vfs_dentry *child = vfs_tmp_dentry(leaf, leaf_len, pdir);
  if (!child) return VFS_ENOMEM;

  /* to unlink, FS usually needs target inode; do lookup first */
  if (pdir->inode->iops->lookup) {
    i32 lrc = pdir->inode->iops->lookup(pdir->inode, child);
    if (lrc < 0) { vfs_dentry_put(child); return lrc; }
  }

  rc = pdir->inode->iops->unlink(pdir->inode, child);
  vfs_dentry_put(child);
  return rc;
}
