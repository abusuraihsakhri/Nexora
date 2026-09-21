#include <kernel/panic.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((noreturn))
void panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message ? message : "(no message)");
    abort();
}
