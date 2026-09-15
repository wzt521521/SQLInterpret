cmake_minimum_required(VERSION 3.16)
project(minidbms VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/minidbms/sql_compiler/native ${CMAKE_BINARY_DIR}/sql_compiler)

add_library(wzt_storage
    cpp/src/storage/page.cpp
    cpp/src/storage/errors.cpp
    cpp/src/storage/file_manager.cpp
    cpp/src/storage/buffer.cpp
    cpp/src/storage/storage_manager.cpp
)
target_include_directories(wzt_storage PUBLIC cpp/include)

add_library(minidb_core
    cpp/src/engine/catalog_manager.cpp
    cpp/src/engine/record.cpp
    cpp/src/engine/storage_engine.cpp
    cpp/src/engine/executor.cpp
    cpp/src/engine/database.cpp
)
target_include_directories(minidb_core PUBLIC cpp/include)
target_link_libraries(minidb_core PUBLIC minisql wzt_storage)

add_executable(minidb_cli cpp/src/cli/main.cpp)
target_link_libraries(minidb_cli PRIVATE minidb_core)

add_executable(minidb_core_bridge cpp/src/bridge/main.cpp)
target_link_libraries(minidb_core_bridge PRIVATE minidb_core)

if(MSVC)
    foreach(target wzt_storage minidb_core minidb_cli minidb_core_bridge)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(target wzt_storage minidb_core minidb_cli minidb_core_bridge)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

enable_testing()
add_executable(test_storage_cpp cpp/tests/test_storage.cpp)
target_link_libraries(test_storage_cpp PRIVATE wzt_storage)
add_test(NAME storage_cpp_tests COMMAND test_storage_cpp)

add_executable(test_engine_cpp cpp/tests/test_engine.cpp)
target_link_libraries(test_engine_cpp PRIVATE minidb_core)
add_test(NAME engine_cpp_tests COMMAND test_engine_cpp)
