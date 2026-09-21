#include "mock_backend.h"
#include <nexora/backend.h>
#include <nexora/handle.h>
#include <nexora/process.h>
#include <string.h>

#define MOCK_TENSORS 32
#define MOCK_WORKS 32

struct mock_tensor {
    int used;
    unsigned refs;
    uint64_t id;
    struct nexora_tensor_desc desc;
};

struct mock_work {
    int used;
    unsigned refs;
    uint64_t id;
    struct nexora_work_desc desc;
};

static struct mock_tensor tensors[MOCK_TENSORS];
static struct mock_work works[MOCK_WORKS];
static uint64_t next_tensor_id;
static uint64_t next_work_id;
static unsigned tensor_map_calls;

static nexora_status_t mock_retain(uint8_t type, void *object) {
    if (!object) return NEXORA_ERR(NEXORA_EINVAL);
    if (type == NEXORA_HANDLE_TENSOR) {
        ((struct mock_tensor *)object)->refs++;
        return NEXORA_OK;
    }
    if (type == NEXORA_HANDLE_WORK) {
        ((struct mock_work *)object)->refs++;
        return NEXORA_OK;
    }
    return NEXORA_ERR(NEXORA_EINVAL);
}

static nexora_status_t mock_tensor_create(struct nexora_process *process,
                                          const struct nexora_tensor_desc *desc,
                                          void **object_out,
                                          uint64_t *rights_out) {
    (void)process;
    for (unsigned i = 0; i < MOCK_TENSORS; ++i) {
        if (!tensors[i].used) {
            tensors[i].used = 1;
            tensors[i].refs = 1;
            tensors[i].id = next_tensor_id++;
            tensors[i].desc = *desc;
            *object_out = &tensors[i];
            *rights_out = NEXORA_RIGHT_READ | NEXORA_RIGHT_MAP | NEXORA_RIGHT_DELEGATE;
            if ((desc->flags & NEXORA_TENSOR_READONLY) == 0) *rights_out |= NEXORA_RIGHT_WRITE;
            return NEXORA_OK;
        }
    }
    return NEXORA_ERR(NEXORA_ENOSPC);
}

static nexora_status_t mock_tensor_map(struct nexora_process *process,
                                       void *tensor_object,
                                       const struct nexora_tensor_map *request,
                                       uintptr_t *user_address_out) {
    (void)process;
    struct mock_tensor *tensor = tensor_object;
    if (!tensor || !tensor->used) return NEXORA_ERR(NEXORA_ESTALE);
    tensor_map_calls++;
    if (request->offset != 0) return NEXORA_ERR(NEXORA_EINVAL);
    *user_address_out = (uintptr_t)(0x40000000ull + tensor->id * 0x200000ull);
    return NEXORA_OK;
}

static nexora_status_t mock_tensor_release(struct nexora_process *process, void *tensor_object) {
    (void)process;
    struct mock_tensor *tensor = tensor_object;
    if (!tensor || !tensor->used || tensor->refs == 0) return NEXORA_ERR(NEXORA_ESTALE);
    tensor->refs--;
    if (tensor->refs == 0) tensor->used = 0;
    return NEXORA_OK;
}

static nexora_status_t mock_work_submit(struct nexora_process *process,
                                        const struct nexora_work_desc *desc,
                                        void *const *input_objects,
                                        void *const *output_objects,
                                        void **work_object_out) {
    (void)process; (void)input_objects; (void)output_objects;
    for (unsigned i = 0; i < MOCK_WORKS; ++i) {
        if (!works[i].used) {
            works[i].used = 1;
            works[i].refs = 1;
            works[i].id = next_work_id++;
            works[i].desc = *desc;
            *work_object_out = &works[i];
            return NEXORA_OK;
        }
    }
    return NEXORA_ERR(NEXORA_ENOSPC);
}

static nexora_status_t mock_work_wait(struct nexora_process *process,
                                      void *work_object,
                                      uint64_t timeout_ns,
                                      struct nexora_work_result *result_out) {
    (void)process;
    struct mock_work *work = work_object;
    if (!work || !work->used) return NEXORA_ERR(NEXORA_ESTALE);
    if (timeout_ns == 0) return NEXORA_ERR(NEXORA_ETIMEDOUT);
    result_out->state = NEXORA_WORK_DONE;
    result_out->device_id = 0;
    result_out->completion_code = 0;
    result_out->started_ns = 100;
    result_out->completed_ns = 200;
    return NEXORA_OK;
}

static nexora_status_t mock_work_release(struct nexora_process *process, void *work_object) {
    (void)process;
    struct mock_work *work = work_object;
    if (!work || !work->used || work->refs == 0) return NEXORA_ERR(NEXORA_ESTALE);
    work->refs--;
    if (work->refs == 0) work->used = 0;
    return NEXORA_OK;
}

static nexora_status_t mock_device_query(struct nexora_process *process,
                                         uint32_t ordinal,
                                         struct nexora_device_info *info_out) {
    (void)process;
    static const struct {
        uint32_t kind;
        const char *name;
        uint64_t memory;
    } devices[] = {
        { NEXORA_DEVICE_CPU, "Nexora CPU domain", 8ull << 30 },
        { NEXORA_DEVICE_GPU, "Nexora simulated GPU", 16ull << 30 },
        { NEXORA_DEVICE_NPU, "Nexora simulated NPU", 4ull << 30 },
    };
    if (ordinal >= sizeof(devices) / sizeof(devices[0])) return NEXORA_ERR(NEXORA_ENOENT);
    info_out->id = ordinal;
    info_out->kind = devices[ordinal].kind;
    info_out->numa_node = 0;
    info_out->memory_bytes = devices[ordinal].memory;
    info_out->feature_bits = 0;
    strncpy(info_out->name, devices[ordinal].name, sizeof(info_out->name) - 1);
    info_out->name[sizeof(info_out->name) - 1] = '\0';
    return NEXORA_OK;
}

static const struct nexora_backend_ops ops = {
    .retain = mock_retain,
    .tensor_create = mock_tensor_create,
    .tensor_map = mock_tensor_map,
    .tensor_release = mock_tensor_release,
    .work_submit = mock_work_submit,
    .work_wait = mock_work_wait,
    .work_release = mock_work_release,
    .device_query = mock_device_query,
};

void mock_backend_reset(void) {
    memset(tensors, 0, sizeof(tensors));
    memset(works, 0, sizeof(works));
    next_tensor_id = 1;
    next_work_id = 1;
    tensor_map_calls = 0;
}

void mock_backend_install(void) {
    nexora_backend_install(&ops);
}

unsigned mock_live_tensors(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < MOCK_TENSORS; ++i) n += tensors[i].used ? 1u : 0u;
    return n;
}

unsigned mock_live_works(void) {
    unsigned n = 0;
    for (unsigned i = 0; i < MOCK_WORKS; ++i) n += works[i].used ? 1u : 0u;
    return n;
}

unsigned mock_tensor_map_calls(void) {
    return tensor_map_calls;
}
