cmake_minimum_required(VERSION 3.21)
project(boringrep)

set(CMAKE_CXX_STANDARD 17)

include(./cmake/conan.cmake)

conan_cmake_run(
  CONANFILE ${CMAKE_SOURCE_DIR}/conanfile.txt
  BUILD missing
  BASIC_SETUP CMAKE_TARGETS
)

add_subdirectory(src)