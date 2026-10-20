cmake_minimum_required(VERSION 3.16)
project(EdgeMeshC VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(EDGEMESH_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

find_package(Threads REQUIRED)

function(edgemesh_apply_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(EDGEMESH_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

add_library(edgemesh_http src/http.c)
target_include_directories(edgemesh_http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
edgemesh_apply_warnings(edgemesh_http)

add_library(edgemesh_backend_pool src/backend_pool.c)
target_include_directories(edgemesh_backend_pool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(edgemesh_backend_pool PUBLIC Threads::Threads)
edgemesh_apply_warnings(edgemesh_backend_pool)

add_executable(edgemesh-proxy src/proxy_main.c src/proxy.c)
target_link_libraries(edgemesh-proxy PRIVATE edgemesh_http edgemesh_backend_pool Threads::Threads)
edgemesh_apply_warnings(edgemesh-proxy)

add_executable(edgemesh-backend src/demo_backend.c)
edgemesh_apply_warnings(edgemesh-backend)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_http tests/test_http.c)
  target_link_libraries(test_http PRIVATE edgemesh_http)
  edgemesh_apply_warnings(test_http)
  add_test(NAME http_parser COMMAND test_http)

  add_executable(test_backend_pool tests/test_backend_pool.c)
  target_link_libraries(test_backend_pool PRIVATE edgemesh_backend_pool)
  edgemesh_apply_warnings(test_backend_pool)
  add_test(NAME backend_pool_unit COMMAND test_backend_pool)

  add_executable(test_proxy tests/test_proxy.c)
  target_include_directories(test_proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(test_proxy PRIVATE Threads::Threads)
  edgemesh_apply_warnings(test_proxy)
  add_test(NAME proxy_integration_round_robin COMMAND test_proxy $<TARGET_FILE:edgemesh-backend> $<TARGET_FILE:edgemesh-proxy> round_robin)
  add_test(NAME proxy_integration_least_connections COMMAND test_proxy $<TARGET_FILE:edgemesh-backend> $<TARGET_FILE:edgemesh-proxy> least_connections)
  set_tests_properties(proxy_integration_round_robin proxy_integration_least_connections PROPERTIES TIMEOUT 35)
endif()
