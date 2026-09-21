#include <kernel/frame.h>

#define BITMAP_WORDS (MAX_FRAMES / 64u)

static u64 g_frame_bitmap[BITMAP_WORDS];
static uintptr_t g_base_paddr = 0;
static usize g_total_frames = 0;
static usize g_free_frames = 0;

void frame_init(uintptr_t base_paddr, usize frame_count) {
    if (frame_count > MAX_FRAMES) {
        frame_count = MAX_FRAMES;
    }
    g_base_paddr = (base_paddr + PAGE_SIZE - 1u) & ~(uintptr_t)(PAGE_SIZE - 1u);
    g_total_frames = frame_count;
    g_free_frames = frame_count;

    for (usize i = 0; i < BITMAP_WORDS; ++i) {
        g_frame_bitmap[i] = 0;
    }
}

uintptr_t frame_alloc(void) {
    if (g_free_frames == 0) {
        return 0;
    }

    for (usize word_idx = 0; word_idx < (g_total_frames + 63u) / 64u; ++word_idx) {
        u64 word = g_frame_bitmap[word_idx];
        if (word != ~0ull) {
            /* Find first 0 bit */
            for (unsigned bit = 0; bit < 64u; ++bit) {
                usize frame_idx = word_idx * 64u + bit;
                if (frame_idx >= g_total_frames) {
                    return 0;
                }
                if ((word & (1ull << bit)) == 0) {
                    g_frame_bitmap[word_idx] |= (1ull << bit);
                    g_free_frames--;
                    return g_base_paddr + ((uintptr_t)frame_idx * PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

void frame_free(uintptr_t paddr) {
    if (paddr < g_base_paddr) return;
    usize offset = (usize)(paddr - g_base_paddr);
    if ((offset % PAGE_SIZE) != 0) return;

    usize frame_idx = offset / PAGE_SIZE;
    if (frame_idx >= g_total_frames) return;

    usize word_idx = frame_idx / 64u;
    unsigned bit = (unsigned)(frame_idx % 64u);

    if ((g_frame_bitmap[word_idx] & (1ull << bit)) != 0) {
        g_frame_bitmap[word_idx] &= ~(1ull << bit);
        g_free_frames++;
    }
}

usize frame_free_count(void) {
    return g_free_frames;
}

usize frame_total_count(void) {
    return g_total_frames;
}

bool frame_is_allocated(uintptr_t paddr) {
    if (paddr < g_base_paddr) return false;
    usize offset = (usize)(paddr - g_base_paddr);
    if ((offset % PAGE_SIZE) != 0) return false;

    usize frame_idx = offset / PAGE_SIZE;
    if (frame_idx >= g_total_frames) return false;

    usize word_idx = frame_idx / 64u;
    unsigned bit = (unsigned)(frame_idx % 64u);
    return (g_frame_bitmap[word_idx] & (1ull << bit)) != 0;
}
