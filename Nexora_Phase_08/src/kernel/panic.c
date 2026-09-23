#include <kernel/panic.h>
#include <kernel/printk.h>

__attribute__((noreturn))
void panic(const char *message) {
    kputs("\n*** AIKernel PANIC ***\n");
    kputs(message ? message : "(no message)");
    kputs("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
