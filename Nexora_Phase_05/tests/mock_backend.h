#ifndef NEXORA_TEST_MOCK_BACKEND_H
#define NEXORA_TEST_MOCK_BACKEND_H

void mock_backend_reset(void);
void mock_backend_install(void);
unsigned mock_live_tensors(void);
unsigned mock_live_works(void);
unsigned mock_tensor_map_calls(void);

#endif
