cmake_minimum_required(VERSION 3.7.2 FATAL_ERROR)

project(asio_connection_tools LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# FindXXX.cmake scripts for external packages
list(APPEND CMAKE_MODULE_PATH
        ${CMAKE_CURRENT_LIST_DIR}/cmake)

find_package(ASIO REQUIRED)

add_library(connection_tools INTERFACE)
target_include_directories(connection_tools INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        )
target_link_libraries(connection_tools INTERFACE ASIO::ASIO)

add_library(eld::connection_tools ALIAS connection_tools)

# tests
find_package(GTest CONFIG REQUIRED)
enable_testing()
add_subdirectory(test)

# sandbox
add_executable(sandbox sandbox/sandbox.cpp)
target_link_libraries(sandbox PRIVATE eld::connection_tools)