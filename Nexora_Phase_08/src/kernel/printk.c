#include <kernel/printk.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define COM1       0x3F8

static volatile u16 *const vga = (volatile u16 *)0xB8000;
static usize row = 0;
static usize col = 0;
static u8 color = 0x0F;

static inline void outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {
    }
    outb(COM1, (u8)c);
}

static void vga_clear(void) {
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) {
            vga[y * VGA_WIDTH + x] = ((u16)color << 8) | ' ';
        }
    }
    row = 0;
    col = 0;
}

void console_init(void) {
    serial_init();
    vga_clear();
}

static void newline(void) {
    col = 0;
    if (++row >= VGA_HEIGHT) {
        row = 0;
        vga_clear();
    }
}

void kputc(char c) {
    if (c == '\n') {
        serial_putc('\r');
        serial_putc('\n');
        newline();
        return;
    }

    serial_putc(c);
    vga[row * VGA_WIDTH + col] = ((u16)color << 8) | (u8)c;

    if (++col >= VGA_WIDTH) {
        newline();
    }
}

void kputs(const char *s) {
    if (!s) return;
    while (*s) {
        kputc(*s++);
    }
}

void kprint_u64(u64 value) {
    char buf[32];
    usize i = 0;

    if (value == 0) {
        kputc('0');
        return;
    }

    while (value && i < sizeof(buf)) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i) {
        kputc(buf[--i]);
    }
}

void kprint_hex(u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    kputs("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        kputc(digits[(value >> shift) & 0xF]);
    }
}
