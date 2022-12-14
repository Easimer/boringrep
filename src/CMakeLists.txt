if (WIN32)
add_executable(boringrep WIN32)
else()
add_executable(boringrep)
endif()

target_sources(boringrep
    PRIVATE
    entry.cpp
    ui.cpp
    ui.hpp
    utf8.hpp
    win32.cpp
    win32.hpp
    mmap.cpp
    mmap.hpp
)

target_link_libraries(boringrep
  PRIVATE
    CONAN_PKG::pcre2
    CONAN_PKG::mio
    CONAN_PKG::fmt
    CONAN_PKG::raylib

    Tracy::TracyClient
)

if(${BORINGREP_TRACY_ENABLE})
  target_compile_definitions(boringrep PRIVATE -DBORINGREP_TRACY_ENABLE=1)
else()
  target_compile_definitions(boringrep PRIVATE -DBORINGREP_TRACY_ENABLE=0)
endif()

target_precompile_headers(boringrep
  PRIVATE
    <string>
    <filesystem>
    <thread>
    <mutex>
    <queue>
    <functional>
    <optional>

    <fmt/core.h>
)

target_include_directories(boringrep
  PRIVATE
    ${raygui_SOURCE_DIR}/src/
)
