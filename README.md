# boringrep
A GUI for Grep with multi-threaded file searching

boringrep is written in C++ and uses
- pcre2 for regular expressions, 
- raylib for the UI
- mio for memory-mapping files
- libfmt for string formatting

## Features
- File and text searching
- Shows context around a matching line

## Building
boringrep needs CMake and Conan to build.

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

Made for the [2022 Wheel Reinvention Jam](https://handmade.network/jam).
