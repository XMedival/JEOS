#include "pipe.h"
#include "mem.h"
#include "spinlock.h"
#include "proc.h"

#define PIPE_BUF 4096

/* Pipe shared state — one instance per pipe() call. */
struct pipe {
    struct spinlock lock;
    u8   buf[PIPE_BUF];
    u32  read_pos;
    u32  write_pos;
    u32  count;
    int  read_open;
    int  write_open;
};

/* Tag stored in inode->ino to tell which end a file represents */
#define PIPE_READ_END  1
#define PIPE_WRITE_END 2

static i64 pipe_read(struct vfs_file *f, void *buf, u64 len, vfs_off_t *off)
{
    (void)off;
    struct pipe *p = (struct pipe *)f->inode->priv;
    u8 *dst = (u8 *)buf;
    u64 n = 0;
    while (n < len) {
        acquire(&p->lock);
        while (p->count == 0) {
            if (!p->write_open) {    /* writer closed: EOF */
                release(&p->lock);
                return (i64)n;
            }
            release(&p->lock);
            yield();
            acquire(&p->lock);
        }
        dst[n++] = p->buf[p->read_pos % PIPE_BUF];
        p->read_pos++;
        p->count--;
        release(&p->lock);
    }
    return (i64)n;
}

static i64 pipe_write(struct vfs_file *f, const void *buf, u64 len, vfs_off_t *off)
{
    (void)off;
    struct pipe *p = (struct pipe *)f->inode->priv;
    const u8 *src = (const u8 *)buf;
    for (u64 i = 0; i < len; i++) {
        acquire(&p->lock);
        while (p->count == PIPE_BUF) {
            if (!p->read_open) {     /* broken pipe */
                release(&p->lock);
                return -1;
            }
            release(&p->lock);
            yield();
            acquire(&p->lock);
        }
        p->buf[p->write_pos % PIPE_BUF] = src[i];
        p->write_pos++;
        p->count++;
        release(&p->lock);
    }
    return (i64)len;
}

static i32 pipe_close(struct vfs_file *f)
{
    struct pipe *p = (struct pipe *)f->inode->priv;
    acquire(&p->lock);
    if (f->inode->ino == PIPE_WRITE_END)
        p->write_open--;
    else
        p->read_open--;
    int dead = (!p->read_open && !p->write_open);
    release(&p->lock);
    if (dead)
        kfree(p, 1);
    return 0;
}

static struct vfs_file_ops pipe_read_ops  = { .read  = pipe_read,
                                              .close = pipe_close };
static struct vfs_file_ops pipe_write_ops = { .write = pipe_write,
                                              .close = pipe_close };

static struct vfs_file *make_pipe_end(struct pipe *p,
                                      struct vfs_file_ops *ops,
                                      vfs_ino_t end_tag)
{
    struct vfs_inode *ino = kalloc(1);
    if (!ino) return 0;
    memset(ino, 0, sizeof(*ino));
    ino->fops   = ops;
    ino->priv   = p;
    ino->ino    = end_tag;
    ino->refcnt = 1;

    struct vfs_file *f = kalloc(1);
    if (!f) { kfree(ino, 1); return 0; }
    memset(f, 0, sizeof(*f));
    f->inode  = ino;
    f->refcnt = 1;
    return f;
}

i32 pipe_create(struct vfs_file **r_out, struct vfs_file **w_out)
{
    struct pipe *p = kalloc(1);
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    initlock(&p->lock, "pipe");
    p->read_open  = 1;
    p->write_open = 1;

    struct vfs_file *r = make_pipe_end(p, &pipe_read_ops,  PIPE_READ_END);
    struct vfs_file *w = make_pipe_end(p, &pipe_write_ops, PIPE_WRITE_END);
    if (!r || !w) {
        if (r) { kfree(r->inode, 1); kfree(r, 1); }
        if (w) { kfree(w->inode, 1); kfree(w, 1); }
        kfree(p, 1);
        return -1;
    }
    *r_out = r;
    *w_out = w;
    return 0;
}
