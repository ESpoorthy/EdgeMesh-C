cmake_minimum_required(VERSION 3.20)
project(edgemesh-c C)

# -----------------------------------------------------------------------
# Language standard
# -----------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# -----------------------------------------------------------------------
# Compiler flags (applied to all targets)
# -----------------------------------------------------------------------
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -D_POSIX_C_SOURCE=200809L
)

# -----------------------------------------------------------------------
# Include path (all headers live under include/)
# -----------------------------------------------------------------------
include_directories(include)

# -----------------------------------------------------------------------
# OBJECT libraries — one per source module
# -----------------------------------------------------------------------

add_library(common_obj OBJECT
    src/common/log.c
    src/common/io_utils.c
    src/common/time_utils.c
)

add_library(http_obj OBJECT
    src/http/parser.c
    src/http/response.c
)

add_library(backend_obj OBJECT
    src/backend/pool.c
    src/backend/selector.c
)

add_library(health_obj OBJECT
    src/health/checker.c
)

add_library(metrics_obj OBJECT
    src/metrics/metrics.c
)

add_library(admin_api_obj OBJECT
    src/admin_api/handler.c
    src/admin_api/json_builder.c
)

add_library(proxy_obj OBJECT
    src/proxy/proxy.c
)

# -----------------------------------------------------------------------
# Main edgemesh executable
# -----------------------------------------------------------------------
add_executable(edgemesh
    src/main.c
    $<TARGET_OBJECTS:common_obj>
    $<TARGET_OBJECTS:http_obj>
    $<TARGET_OBJECTS:backend_obj>
    $<TARGET_OBJECTS:health_obj>
    $<TARGET_OBJECTS:metrics_obj>
    $<TARGET_OBJECTS:admin_api_obj>
    $<TARGET_OBJECTS:proxy_obj>
)
target_link_libraries(edgemesh PRIVATE pthread m)

# -----------------------------------------------------------------------
# demo_backend — standalone demo server
# -----------------------------------------------------------------------
add_executable(demo_backend
    src/demo_backend/demo_backend.c
)
target_link_libraries(demo_backend PRIVATE pthread)

# -----------------------------------------------------------------------
# benchmark_client
# -----------------------------------------------------------------------
add_executable(benchmark_client
    benchmark/benchmark.c
)
target_link_libraries(benchmark_client PRIVATE pthread m)

# -----------------------------------------------------------------------
# CTest test executables
# -----------------------------------------------------------------------
enable_testing()

# test_http_parser
add_executable(test_http_parser
    tests/test_http_parser.c
    $<TARGET_OBJECTS:http_obj>
    $<TARGET_OBJECTS:common_obj>
)
target_link_libraries(test_http_parser PRIVATE pthread m)
add_test(NAME test_http_parser COMMAND test_http_parser)

# test_selector
add_executable(test_selector
    tests/test_selector.c
    $<TARGET_OBJECTS:backend_obj>
    $<TARGET_OBJECTS:common_obj>
)
target_link_libraries(test_selector PRIVATE pthread m)
add_test(NAME test_selector COMMAND test_selector)

# test_health
add_executable(test_health
    tests/test_health.c
    $<TARGET_OBJECTS:backend_obj>
    $<TARGET_OBJECTS:common_obj>
)
target_link_libraries(test_health PRIVATE pthread m)
add_test(NAME test_health COMMAND test_health)

# test_metrics
add_executable(test_metrics
    tests/test_metrics.c
    $<TARGET_OBJECTS:metrics_obj>
    $<TARGET_OBJECTS:common_obj>
)
target_link_libraries(test_metrics PRIVATE pthread m)
add_test(NAME test_metrics COMMAND test_metrics)

# test_admin (stub — full tests in FEAT-002)
add_executable(test_admin
    tests/test_admin.c
    $<TARGET_OBJECTS:admin_api_obj>
    $<TARGET_OBJECTS:backend_obj>
    $<TARGET_OBJECTS:metrics_obj>
    $<TARGET_OBJECTS:common_obj>
)
target_link_libraries(test_admin PRIVATE pthread m)
add_test(NAME test_admin COMMAND test_admin)
