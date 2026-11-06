// UTF-8 decoding shared by the engine (Controller, Composer, CharModel), so
// malformed bytes read the same everywhere.
#pragma once

#include <string>

namespace ProjectKey {

// Decodes one UTF-8 sequence at `i`, advancing it; malformed bytes decode as U+FFFD.
inline char32_t NextCodePoint(const std::string& s, size_t& i) {
  unsigned char c = static_cast<unsigned char>(s[i++]);
  int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xe ? 2 : (c >> 3) == 0x1e ? 3 : -1;
  if (extra < 0) return 0xfffd;
  char32_t cp = extra == 0 ? c : c & (0x3f >> extra);
  for (int k = 0; k < extra; k++) {
    if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) return 0xfffd;
    cp = cp << 6 | (static_cast<unsigned char>(s[i++]) & 0x3f);
  }
  return cp;
}

}  // namespace ProjectKey
