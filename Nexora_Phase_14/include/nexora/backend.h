#ifndef NEXORA_BACKEND_H
#define NEXORA_BACKEND_H

#include <stdint.h>
#include <nexora/abi.h>

struct nexora_process;

struct nexora_backend_ops {
    nexora_status_t (*retain)(uint8_t handle_type, void *object);
    nexora_status_t (*tensor_create)(struct nexora_process *process,
                                     const struct nexora_tensor_desc *desc,
                                     void **object_out,
                                     uint64_t *rights_out);
    nexora_status_t (*tensor_map)(struct nexora_process *process,
                                  void *tensor_object,
                                  const struct nexora_tensor_map *request,
                                  uintptr_t *user_address_out);
    nexora_status_t (*tensor_release)(struct nexora_process *process,
                                      void *tensor_object);
    nexora_status_t (*work_submit)(struct nexora_process *process,
                                   const struct nexora_work_desc *desc,
                                   void *const *input_objects,
                                   void *const *output_objects,
                                   void **work_object_out);
    nexora_status_t (*work_wait)(struct nexora_process *process,
                                 void *work_object,
                                 uint64_t timeout_ns,
                                 struct nexora_work_result *result_out);
    nexora_status_t (*work_release)(struct nexora_process *process,
                                    void *work_object);
    nexora_status_t (*device_query)(struct nexora_process *process,
                                    uint32_t ordinal,
                                    struct nexora_device_info *info_out);
};

void nexora_backend_install(const struct nexora_backend_ops *ops);
const struct nexora_backend_ops *nexora_backend_get(void);

#endif
