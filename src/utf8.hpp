#pragma once

#include <vector>
#include <string>

inline constexpr const char SEPARATORS[] = "!@#$%^&*()_+=-[]{}'\"\\|,.<>?/`~";

struct EditableUtf8String {
  std::vector<uint8_t> buf = {0};
  // Points at the null-terminator
  size_t offEnd = 0;

  EditableUtf8String() {}
  EditableUtf8String(const std::string &s) {
    buf.resize(s.size() + 1);
    offEnd = s.size();
    memcpy(buf.data(), s.data(), s.size());
    assert(buf[offEnd] == 0);
  }

  void TryGrow(uint32_t n = 1) {
    if (buf.size() == offEnd + 1) {
      buf.resize(buf.size() + n);
      assert(buf[offEnd] == 0);
    }
  }

  void Append(uint32_t codepoint) {
    if (0 <= codepoint && codepoint < 0x00'0080) {
      TryGrow(1);
      buf[offEnd++] = (uint8_t(codepoint & 0x7F));
    } else if (0x00'0080 <= codepoint && codepoint < 0x00'0800) {
      TryGrow(2);
      auto byte0 = ((codepoint >> 6) & 0x1F) | 0xC0;
      auto byte1 = ((codepoint >> 0) & 0x3F) | 0x80;
      buf[offEnd++] = (uint8_t(byte0));
      buf[offEnd++] = (uint8_t(byte1));
    } else if (0x00'0800 <= codepoint && codepoint < 0x01'0000) {
      TryGrow(3);
      auto byte0 = ((codepoint >> 12) & 0x0F) | 0xE0;
      auto byte1 = ((codepoint >> 6) & 0x3F) | 0x80;
      auto byte2 = ((codepoint >> 0) & 0x3F) | 0x80;
      buf[offEnd++] = (uint8_t(byte0));
      buf[offEnd++] = (uint8_t(byte1));
      buf[offEnd++] = (uint8_t(byte2));
    } else if (0x01'0000 <= codepoint && codepoint < 0x11'0000) {
      TryGrow(4);
      auto byte0 = ((codepoint >> 18) & 0x0F) | 0xF0;
      auto byte1 = ((codepoint >> 12) & 0x3F) | 0x80;
      auto byte2 = ((codepoint >> 6) & 0x3F) | 0x80;
      auto byte3 = ((codepoint >> 0) & 0x3F) | 0x80;
      buf[offEnd++] = (uint8_t(byte0));
      buf[offEnd++] = (uint8_t(byte1));
      buf[offEnd++] = (uint8_t(byte2));
      buf[offEnd++] = (uint8_t(byte3));
    } else {
      assert(!"invalid codepoint");
    }

    buf[offEnd] = 0;
    assert(buf[offEnd] == 0);
  }

  void Clear() {
    buf[0] = 0;
    offEnd = 0;
    assert(buf[offEnd] == 0);
  }

  void DeleteChar() {
    if (offEnd >= 4) {
      if ((buf[offEnd - 4] & 0xF0) == 0xF0) {
        offEnd -= 4;
        goto end;
      }
    }

    if (offEnd >= 3) {
      if ((buf[offEnd - 3] & 0xF0) == 0xE0) {
        offEnd -= 3;
        goto end;
      }
    }

    if (offEnd >= 2) {
      if ((buf[offEnd - 2] & 0xE0) == 0xC0) {
        offEnd -= 2;
        goto end;
      }
    }

    if (offEnd >= 1) {
      offEnd -= 1;
    }

  end:
    buf[offEnd] = 0;
  }

  const char *c_str() const { return (const char *)buf.data(); }
  bool IsEmpty() const { return offEnd == 0; }
  size_t ByteLength() const { return offEnd; }

  bool OffsetOfPreviousCharacter(size_t &out, size_t offStart) const {
    if (offStart == 0) {
      return false;
    }

    if (offStart > offEnd) {
      offStart = offEnd;
    }

    out = offStart;
    out--;
    if ((buf[out] & 0x80) == 0) {
      return true;
    }

    if (out == 0)
      return false;

    out--;
    if ((buf[out] & 0xE0) == 0xC0) {
      return true;
    }

    if (out == 0)
      return false;

    out--;
    if ((buf[out] & 0xF0) == 0xE0) {
      return true;
    }

    if (out == 0)
      return false;

    out--;
    if ((buf[out] & 0xF0) == 0xF0) {
      return true;
    }

    if (out == 0)
      return false;

    assert(0);
    std::abort();
  }

  size_t ByteLengthOfCharAt(size_t off) {
    assert(off < offEnd);
    if ((buf[off] & 0x80) == 0) {
      return 1;
    } else if ((buf[off] & 0xE0) == 0xC0) {
      return 2;
    } else if ((buf[off] & 0xF0) == 0xE0) {
      return 3;
    } else if ((buf[off] & 0xF0) == 0xF0) {
      return 4;
    } else {
      assert(!"invalid utf-8 string");
      std::abort();
    }
  }
};
