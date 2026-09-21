#ifndef NEXORA_USER_LIB_H
#define NEXORA_USER_LIB_H

#include <nexora/abi.h>

nexora_status_t nexora_abi_query(struct nexora_abi_info *info);
nexora_status_t ai_tensor_create(const struct nexora_tensor_desc *desc, nexora_handle_t *handle);
nexora_status_t ai_tensor_map(nexora_handle_t handle, const struct nexora_tensor_map *request, uintptr_t *address);
nexora_status_t ai_tensor_release(nexora_handle_t handle);
nexora_status_t ai_work_submit(const struct nexora_work_desc *desc, nexora_handle_t *handle);
nexora_status_t ai_work_wait(nexora_handle_t handle, uint64_t timeout_ns, struct nexora_work_result *result);
nexora_status_t ai_cap_delegate(struct nexora_cap_delegate *request);
nexora_status_t ai_device_query(uint32_t ordinal, struct nexora_device_info *info);

#endif
