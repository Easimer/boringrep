#pragma once

#ifdef BORINGREP_TRACY_ENABLE
#undef TRACY_ENABLE
#define TRACY_ENABLE BORINGREP_TRACY_ENABLE
#endif
#include <Tracy.hpp>
