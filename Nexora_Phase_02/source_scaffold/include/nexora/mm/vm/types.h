#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

typedef enum { PAGE_SIZE_4K=12, PAGE_SIZE_2M=21, PAGE_SIZE_1G=30 } page_size_t;
typedef struct { phys_addr_t start; page_size_t size; } phys_frame_t;
typedef struct { virt_addr_t start; page_size_t size; } virt_page_t;

typedef uint32_t vm_permissions_t;
enum { VM_READ=1u<<0, VM_WRITE=1u<<1, VM_EXECUTE=1u<<2, VM_USER=1u<<3 };

typedef uint64_t vm_flags_t;
enum {
    VM_GLOBAL=1ull<<0, VM_WIRED=1ull<<1, VM_DEVICE=1ull<<2, VM_SHARED=1ull<<3,
    VM_COW=1ull<<4, VM_LAZY=1ull<<5, VM_GUARD=1ull<<6, VM_HUGE=1ull<<7
};

typedef enum {
    MEM_INTENT_GENERAL, MEM_INTENT_KERNEL, MEM_INTENT_USER, MEM_INTENT_CODE,
    MEM_INTENT_STACK, MEM_INTENT_HEAP, MEM_INTENT_TENSOR, MEM_INTENT_MODEL_WEIGHTS,
    MEM_INTENT_KV_CACHE, MEM_INTENT_DMA, MEM_INTENT_ACCEL_SHARED,
    MEM_INTENT_DEVICE, MEM_INTENT_TEMPORARY
} memory_intent_t;

typedef enum {
    VM_OK=0, VM_ERR_INVALID_ADDRESS, VM_ERR_NONCANONICAL, VM_ERR_MISALIGNED,
    VM_ERR_INVALID_ALIGNMENT, VM_ERR_ADDRESS_OVERFLOW, VM_ERR_INVALID_PERMISSIONS,
    VM_ERR_ALREADY_MAPPED, VM_ERR_NOT_MAPPED, VM_ERR_OUT_OF_MEMORY,
    VM_ERR_UNSUPPORTED_PAGE_SIZE, VM_ERR_RANGE_CONFLICT, VM_ERR_PAGE_SIZE_CONFLICT,
    VM_ERR_PERMISSION_UNSUPPORTED, VM_ERR_HUGE_REQUIRED
} vm_result_t;
