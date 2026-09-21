#include <kernel/dma.h>
#include <kernel/memory.h>
#include <arch/x86_64/io.h>

bool dma_alloc(dma_buffer *buffer, usize size, usize alignment) {
    if (!buffer || size == 0) {
        return false;
    }

    if (alignment < 64) {
        alignment = 64;
    }

    void *ptr = kalloc(size, alignment);
    buffer->virt = ptr;
    buffer->phys = (u64)(usize)ptr;
    buffer->size = size;
    buffer->alignment = alignment;
    dma_zero(buffer);
    return true;
}

void dma_zero(dma_buffer *buffer) {
    if (!buffer || !buffer->virt) {
        return;
    }
    volatile u8 *p = (volatile u8 *)buffer->virt;
    for (usize i = 0; i < buffer->size; ++i) {
        p[i] = 0;
    }
    io_fence();
}

void dma_sync_for_device(const dma_buffer *buffer) {
    (void)buffer;
    io_fence();
}

void dma_sync_for_cpu(const dma_buffer *buffer) {
    (void)buffer;
    io_fence();
}
