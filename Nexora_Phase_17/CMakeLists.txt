cmake_minimum_required(VERSION 3.16)
project(nexora_phase17 C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(nexora_phase17 STATIC
    src/phase17/health.c
    src/phase17/trace.c
    src/phase17/watchdog.c
    src/phase17/build_info.c)
target_include_directories(nexora_phase17 PUBLIC include)
target_compile_options(nexora_phase17 PRIVATE -Wall -Wextra -Wpedantic)

add_executable(phase17_tests tests/test_phase17.c)
target_link_libraries(phase17_tests PRIVATE nexora_phase17)
enable_testing()
add_test(NAME phase17_tests COMMAND phase17_tests)
