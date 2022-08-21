#include "win32.hpp"

#if _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

std::vector<std::string> W32_GetLogicalDriveStrings() {
  std::vector<std::string> ret;
  char buffer[1024];
  auto sizChars = GetLogicalDriveStringsA(1024, buffer);

  size_t offStart = 0;
  for (size_t offCur = 0; offCur < sizChars; offCur++) {
    if (buffer[offCur] == 0) {
      ret.push_back(std::string(buffer + offStart, offCur - offStart));
      offStart = offCur + 1;
    }
  }

  return ret;
}
#else
std::vector<std::string> W32_GetLogicalDriveStrings() {
  return {};
}
#endif