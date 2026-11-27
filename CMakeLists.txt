cmake_minimum_required(VERSION 3.16)

project(lock_free LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use a 64-bit C++ compiler.")
endif()

find_package(Threads REQUIRED)

add_library(lock_free INTERFACE)
target_compile_features(lock_free INTERFACE cxx_std_20)
target_include_directories(lock_free INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(lock_free_demo main.cpp)
target_link_libraries(lock_free_demo PRIVATE lock_free Threads::Threads)
