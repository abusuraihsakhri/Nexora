cmake_minimum_required(VERSION 3.16)
project(nexora_phase10 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(nexora_phase10 STATIC
    kernel/phase10/health.c
    kernel/phase10/checkpoint.c
    kernel/phase10/audit.c
    kernel/phase10/recovery.c
    kernel/phase10/watchdog.c
    kernel/phase10/distributed_recovery.c
)
target_include_directories(nexora_phase10 PUBLIC include)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nexora_phase10 PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(test_phase10 tests/test_phase10.c)
target_link_libraries(test_phase10 PRIVATE nexora_phase10)

add_executable(test_phase10_adversarial tests/test_phase10_adversarial.c)
target_link_libraries(test_phase10_adversarial PRIVATE nexora_phase10)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_phase10 PRIVATE -Wall -Wextra -Werror -pedantic)
    target_compile_options(test_phase10_adversarial PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

enable_testing()
add_test(NAME phase10 COMMAND test_phase10)
add_test(NAME phase10_adversarial COMMAND test_phase10_adversarial)
