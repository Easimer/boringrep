add_executable(boringrep)

target_sources(boringrep
    PRIVATE
    entry.cpp
)

target_link_libraries(boringrep
  PRIVATE
    CONAN_PKG::pcre2
    CONAN_PKG::mio
    CONAN_PKG::fmt
)
