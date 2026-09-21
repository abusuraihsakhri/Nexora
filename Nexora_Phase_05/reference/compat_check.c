#include <ai/tensor.h>
#include <ai/device.h>
#include <ai/work.h>
#include <nexora/abi.h>

_Static_assert(AI_MAX_DIMS == NEXORA_ABI_MAX_DIMS, "dimension limit changed");
_Static_assert(AI_DTYPE_F32 == NEXORA_DTYPE_F32, "F32 value mismatch");
_Static_assert(AI_DTYPE_F16 == NEXORA_DTYPE_F16, "F16 value mismatch");
_Static_assert(AI_DTYPE_BF16 == NEXORA_DTYPE_BF16, "BF16 value mismatch");
_Static_assert(AI_DTYPE_I8 == NEXORA_DTYPE_I8, "I8 value mismatch");
_Static_assert(AI_DTYPE_I32 == NEXORA_DTYPE_I32, "I32 value mismatch");
_Static_assert(AI_LOC_CPU_RAM == NEXORA_LOC_CPU_RAM, "CPU RAM location mismatch");
_Static_assert(AI_LOC_GPU_HBM == NEXORA_LOC_GPU_HBM, "GPU HBM location mismatch");
_Static_assert(AI_LOC_NPU_MEM == NEXORA_LOC_NPU_MEM, "NPU location mismatch");
_Static_assert(AI_LOC_NVME == NEXORA_LOC_NVME, "NVMe location mismatch");
_Static_assert(AI_LOC_REMOTE == NEXORA_LOC_REMOTE, "remote location mismatch");
_Static_assert(AI_TENSOR_PERSISTENT == NEXORA_TENSOR_PERSISTENT, "persistent flag mismatch");
_Static_assert(AI_TENSOR_EPHEMERAL == NEXORA_TENSOR_EPHEMERAL, "ephemeral flag mismatch");
_Static_assert(AI_TENSOR_READONLY == NEXORA_TENSOR_READONLY, "readonly flag mismatch");
_Static_assert(AI_TENSOR_PINNED == NEXORA_TENSOR_PINNED, "pinned flag mismatch");
_Static_assert(AI_DEVICE_CPU == NEXORA_DEVICE_CPU, "CPU device value mismatch");
_Static_assert(AI_DEVICE_GPU == NEXORA_DEVICE_GPU, "GPU device value mismatch");
_Static_assert(AI_DEVICE_NPU == NEXORA_DEVICE_NPU, "NPU device value mismatch");
_Static_assert(AI_DEVICE_NIC == NEXORA_DEVICE_NIC, "NIC device value mismatch");
_Static_assert(AI_OP_NOOP == NEXORA_OP_NOOP, "NOOP mismatch");
_Static_assert(AI_OP_MATMUL == NEXORA_OP_MATMUL, "MATMUL mismatch");
_Static_assert(AI_OP_ATTENTION == NEXORA_OP_ATTENTION, "ATTENTION mismatch");
_Static_assert(AI_OP_ACTIVATION == NEXORA_OP_ACTIVATION, "ACTIVATION mismatch");
_Static_assert(AI_OP_NORMALIZATION == NEXORA_OP_NORMALIZATION, "NORMALIZATION mismatch");
_Static_assert(AI_OP_EMBEDDING == NEXORA_OP_EMBEDDING, "EMBEDDING mismatch");
_Static_assert(AI_OP_TRANSFER == NEXORA_OP_TRANSFER, "TRANSFER mismatch");
_Static_assert(AI_OP_CUSTOM == NEXORA_OP_CUSTOM, "CUSTOM mismatch");
_Static_assert(AI_WORK_PENDING == NEXORA_WORK_PENDING, "PENDING mismatch");
_Static_assert(AI_WORK_READY == NEXORA_WORK_READY, "READY mismatch");
_Static_assert(AI_WORK_RUNNING == NEXORA_WORK_RUNNING, "RUNNING mismatch");
_Static_assert(AI_WORK_DONE == NEXORA_WORK_DONE, "DONE mismatch");
_Static_assert(AI_WORK_FAILED == NEXORA_WORK_FAILED, "FAILED mismatch");
_Static_assert(AI_MAX_INPUTS == NEXORA_ABI_MAX_IO_TENSORS, "input tensor limit mismatch");
/* Original starter had AI_MAX_OUTPUTS=4; Phase-5 ABI permits 8. This is intentionally not asserted. */
int main(void) { return 0; }
