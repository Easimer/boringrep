cmake_minimum_required(VERSION 3.21)
project(boringrep)

set(CMAKE_CXX_STANDARD 17)

include(./cmake/conan.cmake)
include(FetchContent)

conan_cmake_run(
  CONANFILE ${CMAKE_SOURCE_DIR}/conanfile.txt
  BUILD missing
  BASIC_SETUP CMAKE_TARGETS
)

FetchContent_Declare(
	raygui
	GIT_REPOSITORY https://github.com/raysan5/raygui.git
	GIT_TAG 3.1
)

FetchContent_Declare(
	tracy	
    GIT_REPOSITORY https://github.com/wolfpld/tracy
	GIT_TAG v0.8.2
)

FetchContent_MakeAvailable(raygui tracy)

add_subdirectory(src)