// Still PNGs for review images, any number of colors
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

inline uint32_t png_crc(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c;
}

// Scaled RGB565, with LED gaps from scale 3
inline bool png_save(const std::string &path, const uint16_t *px, int w, int h, int scale) {
  const int W = w * scale, H = h * scale;
  std::vector<uint8_t> raw;
  raw.reserve((size_t) H * (W * 3 + 1));
  for (int y = 0; y < H; y++) {
    raw.push_back(0);
    for (int x = 0; x < W; x++) {
      const bool gap = scale >= 3 && (x % scale == scale - 1 || y % scale == scale - 1);
      const uint16_t c = gap ? 0 : px[(y / scale) * w + x / scale];
      const uint8_t r = (uint8_t) ((c >> 11) << 3), g = (uint8_t) (((c >> 5) & 63) << 2), b = (uint8_t) ((c & 31) << 3);
      raw.push_back((uint8_t) (r | r >> 5));
      raw.push_back((uint8_t) (g | g >> 6));
      raw.push_back((uint8_t) (b | b >> 5));
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t at = 0; at < raw.size() || at == 0;) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + n >= raw.size() ? 1 : 0);
    z.push_back((uint8_t) (n & 255));
    z.push_back((uint8_t) (n >> 8));
    z.push_back((uint8_t) (~n & 255));
    z.push_back((uint8_t) ((~n >> 8) & 255));
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    at += n;
    if (n == 0)
      break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  const uint32_t adler = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8)
    z.push_back((uint8_t) (adler >> s));
  FILE *f = fopen(path.c_str(), "wb");
  if (f == nullptr)
    return false;
  auto be32 = [](std::vector<uint8_t> &o, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
      o.push_back((uint8_t) (v >> s));
  };
  auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> o;
    be32(o, (uint32_t) data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    o.insert(o.end(), body.begin(), body.end());
    be32(o, png_crc(body.data(), body.size()) ^ 0xFFFFFFFFu);
    fwrite(o.data(), 1, o.size(), f);
  };
  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(kSig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t) W);
  be32(ihdr, (uint32_t) H);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return fclose(f) == 0;
}
