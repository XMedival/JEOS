#include "proc.h"
#include "elf.h"
#include "gdt.h"
#include "idt.h"
#include "mem.h"
#include "vfs.h"
#include "print.h"
#include "syscall.h"
#include "x86.h"

static struct spinlock proc_lock;
struct proc proc_table[MAX_PROCS];
static u32 next_pid = 1;

void proc_init(void)  { initlock(&proc_lock, "proc"); }
void acquire_proc_lock(void) { acquire(&proc_lock); }
void release_proc_lock(void) { release(&proc_lock); }
struct context **cpu_context_ptr(void) { return &mycpu()->scheduler_ctx; }

/* ---- fd helpers ---- */

i32 fd_alloc(struct proc *p, struct vfs_file *f)
{
    for (int i = 0; i < MAX_FDS; i++) {
        if (!p->files[i]) {
            p->files[i] = f;
            return i;
        }
    }
    return -1;
}

struct vfs_file *fd_get(struct proc *p, i32 fd)
{
    if (fd < 0 || fd >= MAX_FDS) return 0;
    return p->files[fd];
}

void proc_close_fds(struct proc *p)
{
    for (int i = 0; i < MAX_FDS; i++) {
        if (p->files[i]) {
            vfs_close(p->files[i]);
            p->files[i] = 0;
        }
    }
}

/* Open /dev/null and /dev/cons for fds 0/1/2.  Silently ignores failures
   (e.g. if devfs isn't mounted yet). */
static void proc_init_fds(struct proc *p)
{
    struct vfs_file *f = 0;
    if (vfs_open("/dev/null", VFS_O_RDONLY, 0, &f) == VFS_OK)
        p->files[0] = f;

    f = 0;
    if (vfs_open("/dev/cons", VFS_O_WRONLY, 0, &f) == VFS_OK) {
        p->files[1] = f;
        vfs_file_get(f);          /* share same file for stderr */
        p->files[2] = f;
    }
}

/* ---- low-level process allocator ---- */

static struct proc *proc_alloc(void)
{
    acquire(&proc_lock);
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            struct proc *p = &proc_table[i];
            p->pid   = next_pid++;
            p->state = PROC_EMBRYO;
            release(&proc_lock);
            p->kstack = kalloc(KSTACK_SIZE / PAGE_SIZE);
            if (!p->kstack) { p->state = PROC_UNUSED; return 0; }
            memset(p->kstack, 0, KSTACK_SIZE);
            return p;
        }
    }
    release(&proc_lock);
    return 0;
}

/* Called the first time a process is scheduled.
   Releases proc_lock held by the scheduler across swtch. */
void forkret(void)
{
    release(&proc_lock);
}

/* ---- ELF loading helper ---- */

/* Load all PT_LOAD segments from elf_buf into pml4.
   Sets *entry_out to the ELF entry point.  Returns 0 on success. */
static i32 elf_load_segments(u64 *pml4, const u8 *elf_buf, u64 elf_size, u64 *entry_out)
{
    (void)elf_size;
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_buf;
    if (ehdr->e_magic != ELF_MAGIC || ehdr->e_class != ELFCLASS64 ||
        ehdr->e_machine != EM_X86_64)
        return -1;

    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_buf + ehdr->e_phoff);
    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        u64 va_start = phdr[i].p_vaddr & ~(PAGE_SIZE - 1);
        u64 va_end   = (phdr[i].p_vaddr + phdr[i].p_memsz + PAGE_SIZE - 1)
                       & ~(PAGE_SIZE - 1);
        u64 flags = PTE_USER | ((phdr[i].p_flags & PF_W) ? PTE_WRITE : 0);

        for (u64 va = va_start; va < va_end; va += PAGE_SIZE) {
            void *page = kalloc(1);
            if (!page) return -1;
            memset(page, 0, PAGE_SIZE);

            u64 seg_start    = phdr[i].p_vaddr;
            u64 seg_file_end = seg_start + phdr[i].p_filesz;
            if (va + PAGE_SIZE > seg_start && va < seg_file_end) {
                u64 copy_start = (va > seg_start) ? va : seg_start;
                u64 copy_end   = (va + PAGE_SIZE < seg_file_end)
                                 ? va + PAGE_SIZE : seg_file_end;
                u64 src_off = phdr[i].p_offset + (copy_start - seg_start);
                u64 dst_off = copy_start - va;
                memcpy((u8 *)page + dst_off, elf_buf + src_off,
                       copy_end - copy_start);
            }
            map_page_pml4(pml4, va, VIRT_TO_PHYS((u64)page), flags);
        }
    }

    /* Map user stack - map pages from BASE up to TOP (inclusive) */
    for (u64 va = USER_STACK_BASE; va < USER_STACK_TOP + PAGE_SIZE; va += PAGE_SIZE) {
        void *stack = kalloc(1);
        if (!stack) return -1;
        memset(stack, 0, PAGE_SIZE);
        map_page_pml4(pml4, va, VIRT_TO_PHYS((u64)stack),
                      PTE_USER | PTE_WRITE);
    }

    *entry_out = ehdr->e_entry;
    klog("EXEC", "e_entry=%x", ehdr->e_entry);
    return 0;
}

/* Push argc/argv onto the user stack per the System V AMD64 ABI.
   Stack layout at entry (addresses grow downward):
     [strings ...] [null] [envp ptrs...] 0 [argv ptrs...] 0 [argc]
   We support only argv (no envp).  Returns the new user RSP. */
static u64 setup_user_stack(u64 *pml4, const char *const *argv)
{
    /* Count args and total string bytes */
    int argc = 0;
    u64 str_bytes = 0;
    if (argv) {
        while (argv[argc]) {
            u64 l = 0; while (argv[argc][l]) l++; l++;  /* include NUL */
            str_bytes += l;
            argc++;
        }
    }

    /* We write into the mapped stack page at USER_STACK_BASE.
       Access it via the kernel's HHDM mapping of the physical page. */
    u64 phys = 0;
    {
        u64 va = USER_STACK_BASE;
        pte_t *p4 = pml4;
        if (!(p4[((va)>>39)&0x1FF] & PTE_PRESENT)) return USER_STACK_TOP;
        pte_t *p3 = (pte_t *)PHYS_TO_VIRT(p4[((va)>>39)&0x1FF] & PAGE_FRAME_MASK);
        if (!(p3[((va)>>30)&0x1FF] & PTE_PRESENT)) return USER_STACK_TOP;
        pte_t *p2 = (pte_t *)PHYS_TO_VIRT(p3[((va)>>30)&0x1FF] & PAGE_FRAME_MASK);
        if (!(p2[((va)>>21)&0x1FF] & PTE_PRESENT)) return USER_STACK_TOP;
        pte_t *p1 = (pte_t *)PHYS_TO_VIRT(p2[((va)>>21)&0x1FF] & PAGE_FRAME_MASK);
        if (!(p1[((va)>>12)&0x1FF] & PTE_PRESENT)) return USER_STACK_TOP;
        phys = p1[((va)>>12)&0x1FF] & PAGE_FRAME_MASK;
    }
    u8 *kpage = (u8 *)PHYS_TO_VIRT(phys);  /* kernel VA of stack page */

    /* Write strings at top of page, then pointers below */
    u8 *str_ptr = kpage + PAGE_SIZE;
    u64 *argv_uvas = (u64 *)kalloc(1);  /* temp buffer for user VAs */
    if (!argv_uvas) return USER_STACK_TOP;

    for (int i = argc - 1; i >= 0; i--) {
        u64 l = 0; while (argv[i][l]) l++; l++;
        str_ptr -= l;
        memcpy(str_ptr, argv[i], l);
        /* user VA = USER_STACK_BASE + offset within page */
        argv_uvas[i] = USER_STACK_BASE + (u64)(str_ptr - kpage);
    }

    /* Align RSP down to 16 bytes before pushing pointer table */
    u64 rsp = USER_STACK_BASE + (u64)(str_ptr - kpage);
    rsp &= ~(u64)15;

    /* Write (from high to low): 0 (end of envp), 0 (end of argv), argv ptrs, argc */
    /* We push via the kpage mapping */
    u64 *sp = (u64 *)(kpage + (rsp - USER_STACK_BASE));

    /* null envp terminator */
    sp--; *sp = 0;
    /* null argv terminator */
    sp--; *sp = 0;
    /* argv pointers (reverse order so [0] is at lowest address) */
    for (int i = argc - 1; i >= 0; i--) {
        sp--; *sp = argv_uvas[i];
    }
    /* argc */
    sp--; *sp = (u64)argc;

    kfree(argv_uvas, 1);

    return USER_STACK_BASE + (u64)((u8 *)sp - kpage);
}

/* Set up the kernel stack for first scheduling via forkret → trapret.
   Copies from p->tf if already set (for fork), otherwise initializes fresh. */
static void kstack_setup(struct proc *p, u64 entry, u64 user_rsp)
{
    extern void trapret(void);

    u8 *sp = p->kstack + KSTACK_SIZE;

    sp -= sizeof(struct trap_frame);
    struct trap_frame *tf_on_stack = (struct trap_frame *)sp;

    if (p->state == PROC_RUNNABLE && p->tf.rip != 0) {
        memcpy(tf_on_stack, &p->tf, sizeof(struct trap_frame));
    } else {
        memset(tf_on_stack, 0, sizeof(struct trap_frame));
        tf_on_stack->cs     = USER_CS;
        tf_on_stack->ss     = USER_DS;
        tf_on_stack->rip    = entry;
        tf_on_stack->rsp    = user_rsp;
        tf_on_stack->rflags = 0x202;  /* IF=1 */
    }

    sp -= sizeof(u64);
    *(u64 *)sp = (u64)trapret;

    sp -= sizeof(struct context);
    p->context = (struct context *)sp;
    memset(p->context, 0, sizeof(struct context));
    p->context->rip = (u64)forkret;
}

/* ---- Read an ELF from the VFS ---- */

static u8 *read_elf(const char *path, u32 *pages_out)
{
    struct vfs_file *f = 0;
    if (vfs_open(path, VFS_O_RDONLY, 0, &f) != VFS_OK) return 0;

    struct vfs_stat st;
    if (vfs_fstat(f, &st) != VFS_OK) { vfs_close(f); return 0; }

    u32 npages = (u32)((st.size + PAGE_SIZE - 1) / PAGE_SIZE);
    u8 *buf = kalloc(npages);
    if (!buf) { vfs_close(f); return 0; }

    if (vfs_read(f, buf, st.size) < 0) {
        kfree(buf, npages); vfs_close(f); return 0;
    }
    vfs_close(f);
    *pages_out = npages;
    return buf;
}

/* ---- proc_create ---- */

struct proc *proc_create(const char *path)
{
    u32 elf_pages = 0;
    u8 *elf_buf = read_elf(path, &elf_pages);
    if (!elf_buf) {
        klog_fail("PROC", "cannot read %s", path);
        return 0;
    }

    struct proc *p = proc_alloc();
    if (!p) { kfree(elf_buf, elf_pages); return 0; }

    p->pml4 = create_user_pml4();
    if (!p->pml4) { kfree(elf_buf, elf_pages); p->state = PROC_UNUSED; return 0; }

    u64 entry = 0;
    if (elf_load_segments(p->pml4, elf_buf, (u64)elf_pages * PAGE_SIZE, &entry) != 0) {
        klog_fail("PROC", "ELF load failed");
        free_user_pml4(p->pml4);
        kfree(p->pml4, 1);
        kfree(elf_buf, elf_pages);
        p->state = PROC_UNUSED;
        return 0;
    }
    kfree(elf_buf, elf_pages);

    kstack_setup(p, entry, USER_STACK_TOP);
    p->brk  = USER_HEAP_BASE;
    p->ppid = 0;
    proc_init_fds(p);

    /* Copy name from basename of path */
    const char *name = path;
    for (const char *s = path; *s; s++) if (*s == '/') name = s + 1;
    int j = 0;
    while (name[j] && j < 15) { p->name[j] = name[j]; j++; }
    p->name[j] = 0;

    acquire(&proc_lock);
    p->state = PROC_RUNNABLE;
    release(&proc_lock);

    klog_ok("PROC", "pid %u  '%s'  entry=%p", p->pid, p->name, (void*)entry);
    return p;
}

/* syscall_entry save layout from top of kstack (kstop = kstack + KSTACK_SIZE):
     kstop-8:   user RSP
     kstop-16:  user RIP (rcx saved by syscall instruction)
     kstop-24:  user RFLAGS (r11 saved by syscall)
     kstop-32:  rbx    kstop-40:  rbp    kstop-48:  r12
     kstop-56:  r13    kstop-64:  r14    kstop-72:  r15
     kstop-80:  rdi    kstop-88:  rsi    kstop-96:  rdx
     kstop-104: r10    kstop-112: r8     kstop-120: r9           */
static void build_fork_tf(struct trap_frame *dst, const struct trap_frame *src)
{
    memset(dst, 0, sizeof(*dst));
    dst->rsp    = src->rsp;
    dst->rip    = src->rip;
    dst->rcx    = src->rip;
    dst->rflags = src->rflags;
    dst->r11    = src->rflags;
    dst->rbx    = src->rbx;
    dst->rbp    = src->rbp;
    dst->r12    = src->r12;
    dst->r13    = src->r13;
    dst->r14    = src->r14;
    dst->r15    = src->r15;
    dst->rdi    = src->rdi;
    dst->rsi    = src->rsi;
    dst->rdx    = src->rdx;
    dst->r10    = src->r10;
    dst->r8     = src->r8;
    dst->r9     = src->r9;
    dst->cs     = USER_CS;
    dst->ss     = USER_DS;
    dst->rax    = 0;   /* fork() returns 0 in child */
}

/* ---- proc_fork ---- */

i32 proc_fork(void)
{
    struct proc *parent = current_proc;
    if (!parent) return -1;

    struct proc *child = proc_alloc();
    if (!child) return -1;

    /* Copy address space */
    child->pml4 = create_user_pml4();
    if (!child->pml4) { child->state = PROC_UNUSED; return -1; }
    copy_user_pml4(child->pml4, parent->pml4);

    /* Build child's kernel stack for forkret → trapret → iretq path.
       Copy parent's user register state from parent->tf (embedded in proc). */
    kstack_setup(child, 0, 0);
    build_fork_tf(&child->tf, &parent->tf);

    /* Inherit fd table — share the same vfs_file objects (refcounted) */
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->files[i]) {
            vfs_file_get(parent->files[i]);
            child->files[i] = parent->files[i];
        }
    }

    /* Copy process name */
    for (int i = 0; i < 16; i++) child->name[i] = parent->name[i];
    child->ppid = parent->pid;
    child->brk  = parent->brk;

    acquire(&proc_lock);
    child->state = PROC_RUNNABLE;
    release(&proc_lock);

    return (i32)child->pid;
}

/* ---- proc_exec ---- */

i32 proc_exec(const char *path, const char *const *argv)
{
    struct proc *p = current_proc;
    if (!p) return -1;

    u32 elf_pages = 0;
    u8 *elf_buf = read_elf(path, &elf_pages);
    if (!elf_buf) { klog("EXEC", "read_elf failed for %s", path); return -1; }
    klog("EXEC", "read_elf ok, %u pages", elf_pages);

    /* Build new address space before tearing down the old one */
    u64 *new_pml4 = create_user_pml4();
    if (!new_pml4) { kfree(elf_buf, elf_pages); klog("EXEC", "create_user_pml4 failed"); return -1; }
    klog("EXEC", "create_user_pml4 ok");

    u64 entry = 0;
    if (elf_load_segments(new_pml4, elf_buf, (u64)elf_pages * PAGE_SIZE, &entry) != 0) {
        free_user_pml4(new_pml4);
        kfree(new_pml4, 1);
        kfree(elf_buf, elf_pages);
        klog("EXEC", "elf_load_segments failed");
        return -1;
    }
    klog("EXEC", "elf_load_segments ok, entry=%x", entry);
    kfree(elf_buf, elf_pages);

    /* Set up argc/argv on the user stack */
    u64 user_rsp = setup_user_stack(new_pml4, argv);
    klog("EXEC", "setup_user_stack ok, rsp=%x", user_rsp);

    /* Reset heap break (can be done before or after lcr3) */
    p->brk = USER_HEAP_BASE;
    klog("EXEC", "brk reset");

    /* Redirect the pending sysret to the new entry point.
       syscall_entry saved user RIP at kstop-16 and user RSP at kstop-8.
       After proc_exec returns through syscall_handler → syscall_entry,
       those values are restored and sysret jumps to the new entry.

       IMPORTANT: This MUST be done BEFORE lcr3 switches to the new address space,
       because p->kstack is a kernel virtual address that may not be accessible
       in the new user page tables (which only have the kernel half mapped). */
    u64 kstop = (u64)p->kstack + KSTACK_SIZE;
    *(u64 *)(kstop -   8) = user_rsp;  /* new user RSP */
    *(u64 *)(kstop -  16) = entry;     /* new user RIP */
    *(u64 *)(kstop -  24) = 0x202;     /* new user RFLAGS: IF=1 */
    klog("EXEC", "kstack patched, entry=%x rsp=%x", entry, user_rsp);

    /* Update p->tf for fork()-after-exec() consistency */
    p->tf.rip = entry;
    p->tf.rsp = user_rsp;
    p->tf.rflags = 0x202;
    p->tf.cs = USER_CS;
    p->tf.ss = USER_DS;
    klog("EXEC", "p->tf updated");

    /* Tear down old address space and switch */
    u64 *old_pml4 = p->pml4;
    p->pml4 = new_pml4;
    free_user_pml4(old_pml4);
    kfree(old_pml4, 1);
    lcr3(VIRT_TO_PHYS((u64)new_pml4));
    klog("EXEC", "lcr3 done");

    /* Update name */
    const char *name = path;
    for (const char *s = path; *s; s++) if (*s == '/') name = s + 1;
    int j = 0;
    while (name[j] && j < 15) { p->name[j] = name[j]; j++; }
    p->name[j] = 0;

    /* Return 0 — sysret uses the patched saved values above */
    klog("EXEC", "returning 0");
    return 0;
}

/* ---- yield / scheduler ---- */

void yield(void)
{
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    if (!p) return;
    acquire(&proc_lock);
    p->state = PROC_RUNNABLE;
    swtch(&p->context, c->scheduler_ctx);
    release(&proc_lock);
}

void scheduler(void)
{
    struct cpu *c = mycpu();
    for (;;) {
        sti();
        acquire(&proc_lock);
        for (int i = 0; i < MAX_PROCS; i++) {
            struct proc *p = &proc_table[i];
            if (p->state != PROC_RUNNABLE) continue;
            p->state = PROC_RUNNING;
            c->proc  = p;

            lcr3(VIRT_TO_PHYS((u64)p->pml4));
            tss_set_rsp0((u64)p->kstack + KSTACK_SIZE);
            c->kernel_rsp = (u64)p->kstack + KSTACK_SIZE;

            swtch(&c->scheduler_ctx, p->context);

            c->proc = 0;
        }
        release(&proc_lock);
    }
}
