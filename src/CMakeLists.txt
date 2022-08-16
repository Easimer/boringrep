add_executable(boringrep)

target_sources(boringrep
    PRIVATE
    entry.cpp
    ui.cpp
    ui.hpp
)

target_link_libraries(boringrep
  PRIVATE
    CONAN_PKG::pcre2
    CONAN_PKG::mio
    CONAN_PKG::fmt
    CONAN_PKG::raylib
)

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
