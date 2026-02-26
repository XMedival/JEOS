#include "ps2.h"
#include "print.h"
#include "vfs.h"
#include "x86.h"
#include "devfs.h"
#include "ring.h"
#include "string.h"
#include "serial.h"

static void ps2_wait_write(void) {
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL);
}

static void ps2_wait_read(void) {
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL));
}

#define KBD_BUF_LEN 1024
static char kbd_ring_storage[KBD_BUF_LEN];
ring_t kbd_ring_buf;

#define MOUSE_BUF_LEN 256
static char mouse_ring_storage[MOUSE_BUF_LEN];
ring_t mouse_ring_buf;

static i32 mouse_x = 0;
static i32 mouse_y = 0;
static u8 mouse_buttons = 0;
static u8 mouse_enabled = 1;

static u8 kbd_modifiers = 0;
#define KBD_MOD_SHIFT  0x01
#define KBD_MOD_CTRL   0x04
#define KBD_MOD_ALT    0x08
#define KBD_MOD_CAPS   0x40

static const u8 modifier_scancodes[] = {
    [0x2A] = KBD_MOD_SHIFT,  // Left shift
    [0x36] = KBD_MOD_SHIFT,  // Right shift  
    [0x1D] = KBD_MOD_CTRL,   // Left ctrl
    [0x9D] = KBD_MOD_CTRL,   // Right ctrl
    [0x38] = KBD_MOD_ALT,    // Right alt (AltGr)
    [0xB8] = KBD_MOD_ALT,    // Right alt release
    [0x3A] = KBD_MOD_CAPS,   // Caps lock
};

static const u32 scancode_to_rune[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
    [0x0E] = 0x08,   // Backspace
    [0x0F] = '\t',   // Tab
    [0x1C] = '\n',   // Enter
};

static const u32 scancode_to_rune_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
    [0x28] = '"', [0x29] = '~',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
};

static void kbd_put_rune(u32 rune) {
    char buf[4];
    int n = 0;
    if (rune < 0x80) {
        buf[n++] = (char)rune;
    } else if (rune < 0x800) {
        buf[n++] = (char)(0xC0 | (rune >> 6));
        buf[n++] = (char)(0x80 | (rune & 0x3F));
    } else if (rune < 0x10000) {
        buf[n++] = (char)(0xE0 | (rune >> 12));
        buf[n++] = (char)(0x80 | ((rune >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (rune & 0x3F));
    } else {
        buf[n++] = (char)(0xF0 | (rune >> 18));
        buf[n++] = (char)(0x80 | ((rune >> 12) & 0x3F));
        buf[n++] = (char)(0x80 | ((rune >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (rune & 0x3F));
    }
    ring_write(&kbd_ring_buf, buf, n);
}

static void kbd_handle_scancode(u8 scancode) {
    u8 key = scancode & 0x7F;
    int release = scancode & 0x80;

    // Handle modifiers
    if (modifier_scancodes[key]) {
        if (release) {
            kbd_modifiers &= ~modifier_scancodes[key];
        } else {
            kbd_modifiers |= modifier_scancodes[key];
        }
        return;
    }

    if (release) return;

    // Get rune
    u32 rune = 0;
    int use_shift = (kbd_modifiers & (KBD_MOD_SHIFT | KBD_MOD_CAPS));
    
    if (key < 128) {
        if (use_shift && scancode_to_rune_shift[key]) {
            rune = scancode_to_rune_shift[key];
        } else {
            rune = scancode_to_rune[key];
        }
    }

    if (rune) {
        kbd_put_rune(rune);
    }
}

void kbd_interrupt(void) {
    u8 scancode = inb(PS2_DATA_PORT);
    kbd_handle_scancode(scancode);
}

static void puti(char *buf, int val) {
    int i = 0;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { buf[i++] = '0'; }
    else {
        char tmp[12];
        int n = 0;
        while (val > 0) {
            tmp[n++] = '0' + (val % 10);
            val /= 10;
        }
        if (neg) buf[i++] = '-';
        while (n > 0) buf[i++] = tmp[--n];
    }
    buf[i] = '\0';
}

void mouse_interrupt(void) {
    static u8 mouse_buf[3];
    static u8 mouse_idx = 0;
    
    u8 data = inb(PS2_DATA_PORT);
    
    if (mouse_idx == 0 && !(data & (1 << 3))) {
        return;
    }
    
    mouse_buf[mouse_idx++] = data;
        if (mouse_idx == 3) {
            mouse_idx = 0;
            if (!(mouse_buf[0] & (1 << 3))) return;
            
            i32 dx = (i8)mouse_buf[1];
            if (mouse_buf[0] & MOUSE_X_OVERFLOW) {
                dx = 0;
            } else if (mouse_buf[0] & MOUSE_X_SIGN) {
                dx |= 0xFFFFFF00;
            }

            i32 dy = (i8)mouse_buf[2];
            if (mouse_buf[0] & MOUSE_Y_OVERFLOW) {
                dy = 0;
            } else if (mouse_buf[0] & MOUSE_Y_SIGN) {
                dy |= 0xFFFFFF00;
            }

        dy = -dy;

        u8 buttons = mouse_buf[0] & 0x07;
        
        if (mouse_enabled) {
            mouse_x += dx;
            mouse_y += dy;
            
            mouse_buttons = buttons;
            
            char buf[48];
            char *p = buf;
            *p++ = 'm';
            *p++ = ' ';
            puti(p, dx); p += kstrlen(p);
            *p++ = ' ';
            puti(p, dy); p += kstrlen(p);
            *p++ = ' ';
            puti(p, buttons); p += kstrlen(p);
            *p++ = '\n';
            
            ring_write(&mouse_ring_buf, buf, p - buf);
        }
    }
}

void mouse_get_pos(i32 *x, i32 *y) {
    *x = mouse_x;
    *y = mouse_y;
}

static i64 kbd_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off) {
    (void)f;
    (void)off;
    u32 n = count;
    if (n > KBD_BUF_LEN) n = KBD_BUF_LEN;
    return ring_read(&kbd_ring_buf, buf, n);
}

struct vfs_file_ops kbd_ops = {
    .read = kbd_read,
};

static i64 mouse_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off) {
    (void)f; (void)off;
    u32 n = count < MOUSE_BUF_LEN ? (u32)count : MOUSE_BUF_LEN;
    return ring_read(&mouse_ring_buf, buf, n);
}

struct vfs_file_ops mouse_ops = {
    .read = mouse_read,
};

static i64 mousectl_read(struct vfs_file *f, void *buf, u64 count, vfs_off_t *off) {
    (void)f; (void)off;
    char tmp[48];
    char *p = tmp;
    puti(p, mouse_x); p += kstrlen(p);
    *p++ = ' ';
    puti(p, mouse_y); p += kstrlen(p);
    *p++ = ' ';
    puti(p, mouse_buttons); p += kstrlen(p);
    *p++ = '\n';
    
    u32 len = (u32)(p - tmp);
    if (len > count) len = count;
    for (u32 i = 0; i < len; i++) ((char*)buf)[i] = tmp[i];
    return len;
}

static i64 mousectl_write(struct vfs_file *f, const void *buf, u64 count, vfs_off_t *off) {
    (void)f; (void)off;
    if (count >= 3) {
        const char *cmd = (const char *)buf;
        if (cmd[0] == 'e' && cmd[1] == 'n' && cmd[2] == 'a') mouse_enabled = 1;
        else if (cmd[0] == 'd' && cmd[1] == 'i' && cmd[2] == 's') mouse_enabled = 0;
    }
    return count;
}

struct vfs_file_ops mousectl_ops = {
    .read = mousectl_read,
    .write = mousectl_write,
};

void ps2_init(void) {
    ring_init(&kbd_ring_buf, kbd_ring_storage, KBD_BUF_LEN);
    ring_init(&mouse_ring_buf, mouse_ring_storage, MOUSE_BUF_LEN);

    // Disable both ports first
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_DISABLE_PORT1);
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_DISABLE_PORT2);

    // Flush output buffer
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA_PORT);
    }

    // Enable keyboard interrupt (bit 0) in controller config
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    ps2_wait_read();
    u8 config = inb(PS2_DATA_PORT);
    config |= 0x01;  // keyboard interrupt
    config |= 0x02;  // mouse interrupt
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    // Enable keyboard port
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_PORT1);
    klog_ok("KBD", "enabled");

    // Enable mouse port
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_PORT2);

    devfs_register("kbd", VFS_S_IFCHR | 0444, &kbd_ops, 0);
    devfs_register("mouse", VFS_S_IFCHR | 0444, &mouse_ops, 0);
    devfs_register("mousectl", VFS_S_IFCHR | 0666, &mousectl_ops, 0);

    // Send mouse reset command
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA_PORT, PS2_MOUSE_RESET);
    ps2_wait_read();
    (void)inb(PS2_DATA_PORT);  // Should be 0xAA (self-test pass)
    ps2_wait_read();
    (void)inb(PS2_DATA_PORT);  // Should be 0x00 (mouse ID)

    // Send "enable data reporting" to the mouse
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA_PORT, PS2_MOUSE_ENABLE);
    ps2_wait_read();
    inb(PS2_DATA_PORT);  // discard ACK (0xFA)
    klog_ok("MOUSE", "enabled");
}
