#ifndef NEXORA_KERNEL_DMA_H
#define NEXORA_KERNEL_DMA_H

#include <kernel/types.h>

typedef struct {
    void *virt;
    u64 phys;
    usize size;
    usize alignment;
} dma_buffer;

/*
 * Phase-7 experimental DMA allocator.
 *
 * The current starter kernel identity-maps low physical memory. kalloc()
 * therefore yields a usable physical address only while that invariant
 * remains true. Later PMM/IOMMU work must replace the translation policy.
 */
bool dma_alloc(dma_buffer *buffer, usize size, usize alignment);
void dma_zero(dma_buffer *buffer);
void dma_sync_for_device(const dma_buffer *buffer);
void dma_sync_for_cpu(const dma_buffer *buffer);

#endif
