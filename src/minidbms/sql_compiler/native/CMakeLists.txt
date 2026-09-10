cmake_minimum_required(VERSION 3.16)
project(zby_minisql_cpp VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(minisql STATIC
  src/common.cc src/lexer.cc src/ll1.cc src/semantic.cc src/planner.cc
  src/expression_eval.cc src/optimizer.cc src/compiler.cc src/format.cc)
target_include_directories(minisql PUBLIC include)
if(MSVC)
  target_compile_options(minisql PRIVATE /W4 /utf-8)
else()
  target_compile_options(minisql PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(minisql_cli app/main.cc)
target_link_libraries(minisql_cli PRIVATE minisql)
add_executable(minisql_bridge app/bridge.cc)
target_link_libraries(minisql_bridge PRIVATE minisql)
if(WIN32)
  target_link_libraries(minisql_cli PRIVATE shell32)
endif()
