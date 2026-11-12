// Animated GIF writer for the review images
#pragma once
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

struct GifWriter {
  FILE *f{nullptr};
  int w{0}, h{0}, scale{1};
  std::vector<uint16_t> pal;  // RGB565 entries, global table
  std::vector<std::vector<uint8_t>> frames;
  std::vector<int> delays_cs;

  int index_of(uint16_t c) {
    for (size_t i = 0; i < pal.size(); i++)
      if (pal[i] == c) return (int) i;
    if (pal.size() >= 256) return 0;
    pal.push_back(c);
    return (int) pal.size() - 1;
  }
  void add(const uint16_t *px, int stride_px, int delay_cs) {
    std::vector<uint8_t> idx((size_t) w * scale * h * scale);
    for (int y = 0; y < h * scale; y++)
      for (int x = 0; x < w * scale; x++) {
        // LED look: a dark gap between dots
        const bool gap = scale >= 3 && (x % scale == scale - 1 || y % scale == scale - 1);
        idx[(size_t) y * w * scale + x] = (uint8_t) index_of(gap ? 0 : px[(y / scale) * stride_px + x / scale]);
      }
    frames.push_back(std::move(idx));
    delays_cs.push_back(delay_cs);
  }
  static void put16(std::vector<uint8_t> &o, int v) { o.push_back(v & 255); o.push_back(v >> 8); }
  // Plain LZW, codes up to 12 bits
  static void lzw(std::vector<uint8_t> &o, const std::vector<uint8_t> &in, int min_bits) {
    const int clear = 1 << min_bits, eoi = clear + 1;
    std::map<std::pair<int, int>, int> dict;
    int next = eoi + 1, bits = min_bits + 1;
    uint32_t acc = 0; int nacc = 0;
    std::vector<uint8_t> bytes;
    auto emit = [&](int code) {
      acc |= (uint32_t) code << nacc; nacc += bits;
      while (nacc >= 8) { bytes.push_back(acc & 255); acc >>= 8; nacc -= 8; }
    };
    emit(clear);
    int cur = in.empty() ? -1 : in[0];
    for (size_t i = 1; i < in.size(); i++) {
      auto key = std::make_pair(cur, (int) in[i]);
      auto it = dict.find(key);
      if (it != dict.end()) { cur = it->second; continue; }
      emit(cur);
      if (next < 4096) { dict[key] = next++; if (next > (1 << bits) && bits < 12) bits++; }
      else { emit(clear); dict.clear(); next = eoi + 1; bits = min_bits + 1; }
      cur = in[i];
    }
    if (cur >= 0) emit(cur);
    emit(eoi);
    if (nacc) bytes.push_back(acc & 255);
    o.push_back((uint8_t) min_bits);
    for (size_t i = 0; i < bytes.size(); i += 255) {
      size_t n = std::min<size_t>(255, bytes.size() - i);
      o.push_back((uint8_t) n);
      o.insert(o.end(), bytes.begin() + i, bytes.begin() + i + n);
    }
    o.push_back(0);
  }
  bool save(const std::string &path) {
    std::vector<uint8_t> o = {'G', 'I', 'F', '8', '9', 'a'};
    put16(o, w * scale); put16(o, h * scale);
    int bits = 1; while ((1 << bits) < (int) pal.size()) bits++;
    o.push_back(0xF0 | (bits - 1)); o.push_back(0); o.push_back(0);
    for (int i = 0; i < (1 << bits); i++) {
      uint16_t c = i < (int) pal.size() ? pal[i] : 0;
      o.push_back((uint8_t) (((c >> 11) & 31) * 255 / 31));
      o.push_back((uint8_t) (((c >> 5) & 63) * 255 / 63));
      o.push_back((uint8_t) ((c & 31) * 255 / 31));
    }
    const uint8_t loop[] = {0x21, 0xFF, 11, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 3, 1, 0, 0, 0};
    o.insert(o.end(), loop, loop + sizeof(loop));
    for (size_t k = 0; k < frames.size(); k++) {
      const uint8_t gce[] = {0x21, 0xF9, 4, 0, (uint8_t) (delays_cs[k] & 255), (uint8_t) (delays_cs[k] >> 8), 0, 0};
      o.insert(o.end(), gce, gce + sizeof(gce));
      o.push_back(0x2C); put16(o, 0); put16(o, 0); put16(o, w * scale); put16(o, h * scale); o.push_back(0);
      lzw(o, frames[k], std::max(2, bits));
    }
    o.push_back(0x3B);
    FILE *fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    fwrite(o.data(), 1, o.size(), fp);
    fclose(fp);
    return true;
  }
};
