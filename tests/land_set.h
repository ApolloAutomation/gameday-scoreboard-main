// A made-up landmark set, encoded as the device page does.
#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

struct LandPlaceIn {
  int dlat, dlon, cls;
  const char *name;
};
struct LandLineIn {
  int kind;
  std::vector<std::pair<int, int>> pts;
};

inline std::vector<uint8_t> land_encode(int lat_e2, int lon_e2, int reach, const std::vector<LandPlaceIn> &places,
                                        const std::vector<LandLineIn> &lines) {
  std::vector<uint8_t> b;
  auto u16 = [&b](int v) {
    b.push_back((uint8_t) (v & 255));
    b.push_back((uint8_t) ((v >> 8) & 255));
  };
  size_t npt = 0;
  for (const LandLineIn &l : lines)
    npt += l.pts.size();
  b.push_back('G');
  b.push_back('L');
  b.push_back(2);
  b.push_back(0);
  u16(lat_e2);
  u16(lon_e2);
  u16((int) places.size());
  u16((int) lines.size());
  u16((int) npt);
  u16(reach);
  for (const LandPlaceIn &p : places) {
    u16(p.dlat);
    u16(p.dlon);
    b.push_back((uint8_t) p.cls);
    const size_t n = strlen(p.name);
    b.push_back((uint8_t) n);
    for (size_t i = 0; i < 14; i++)
      b.push_back(i < n ? (uint8_t) p.name[i] : 0);
  }
  for (const LandLineIn &l : lines) {
    b.push_back((uint8_t) l.kind);
    b.push_back((uint8_t) l.pts.size());
  }
  for (const LandLineIn &l : lines)
    for (const auto &pt : l.pts) {
      u16(pt.first);
      u16(pt.second);
    }
  return b;
}

// Invented towns around 40.00, -90.00; offsets in 1/4000 degree.
inline std::vector<LandPlaceIn> land_sample_places() {
  return {{600, 2800, 4, "BRIGHTON"},   {-1200, -3600, 3, "CEDARVALE"}, {1800, -1800, 3, "DUNMORE"},
          {-400, 1200, 2, "ELMWOOD"},   {1000, -600, 2, "FAIRHAVEN"},   {-1400, 3800, 2, "GLENBROOK"},
          {300, -1000, 1, "HOLLIS"},    {-700, -1600, 1, "IVYDALE"},    {-200, 4800, 1, "JUNIPER"},
          {120, 400, 1, "KESTREL"},     {-300, -300, 1, "LANGLEY"},     {3000, 800, 2, "MAPLETON"},
          {-3200, 1800, 3, "NORWOOD"},  {1400, 2200, 1, "OAKRIDGE"},    {-2400, -4400, 2, "PINECREST"},
          {3600, -5200, 4, "ST CLAIR"}};
}

inline std::vector<LandLineIn> land_sample_lines() {
  return {
      {0, {{-9000, -4600}, {-3000, -4300}, {-1000, -4700}, {1000, -4200}, {3000, -4500}, {9000, -4400}}},
      {0, {{-2600, -4600}, {-2500, 0}, {-2700, 5000}, {-2600, 12000}}},
      {1, {{9000, 1200}, {4800, 1800}, {3000, 3000}, {2500, 5200}, {3000, 6800}, {2600, 12000}}},
      {2, {{-240, -12000}, {-120, -4000}, {-40, 0}, {160, 2600}, {200, 12000}}},
      {2, {{-9000, 700}, {-2000, 640}, {0, 520}, {2400, 600}, {9000, 400}}},
      {2, {{160, 2600}, {400, 2500}, {840, 2800}, {760, 3200}, {400, 3300}, {160, 2600}}},
      {2, {{-4000, -12000}, {-1200, -3600}, {1800, -1800}, {6000, -1000}}},
  };
}

inline std::vector<uint8_t> land_sample() { return land_encode(4000, -9000, 250, land_sample_places(), land_sample_lines()); }
