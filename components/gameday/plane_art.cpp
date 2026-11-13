// Plane pictures (plane_art.h): drawn for this project.
#include "plane_art.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace flights {

namespace {

template<int W, int H> struct Art {
  uint8_t bits[(W * H + 3) / 4]{};
};

// . clear, # body, a tail, o detail.
template<int W, int H> constexpr Art<W, H> pack(const char (&s)[W * H + 1]) {
  Art<W, H> a{};
  for (int i = 0; i < W * H; i++) {
    const char c = s[i];
    const uint8_t v = c == '#' ? kArtBody : c == 'a' ? kArtTail : c == 'o' ? kArtDetail : kArtClear;
    a.bits[i / 4] = (uint8_t) (a.bits[i / 4] | (v << (2 * (i % 4))));
  }
  return a;
}

constexpr auto kGeneric = pack<32, 10>(
    "aa.............................."
    "aaa............................."
    ".aaa............................"
    ".aaaa..........................."
    "..aaaa######################...."
    "aaa#############################"
    "...#############################"
    ".....#######################...."
    "................oooo............"
    "...............ooooo............");

constexpr auto kLight = pack<15, 7>(
    "a....########.."
    "aa....#....#..."
    "aa....#o#o##..o"
    ".############oo"
    "...#########..o"
    "......o....o..."
    "......o....o...");

constexpr auto kTwinProp = pack<21, 8>(
    "aaaa................."
    ".aa.................."
    ".aa.......#####......"
    "..##################."
    "..##o#o#o#o#o######o#"
    "....######oooooo##o.."
    "..............o...o.."
    "..................o..");

constexpr auto kTurboprop = pack<30, 8>(
    "aaaaa........................."
    ".aa.....................o....."
    ".aa.......############..o....."
    "..aa..###################o##.."
    "..aaa#####################o###"
    "...###o#o#o#o#o#o#o#o####o####"
    "......################..o#...."
    "........................o.....");

constexpr auto kBizjet = pack<23, 8>(
    "aaaaa.................."
    "..aa..................."
    "..aa..................."
    "...aooo................"
    "...ooooo############..."
    "..######o#o#o#o#o######"
    ".....#############o##.."
    "..........ooooo........");

constexpr auto kRegional = pack<32, 10>(
    "aaaa............................"
    ".aa............................."
    ".aaa............................"
    "..aa..ooo......................."
    "..aaaoooo################......."
    "aa#######################o#####."
    "...###o#o#o#o#o#o#o#o#o#o#o#####"
    ".....#########################.."
    "..............ooo..............."
    ".............oooo...............");

constexpr auto kNarrow = pack<41, 11>(
    "aa......................................."
    "aaa......................................"
    ".aaa....................................."
    ".aaaa...................................."
    "..aaaa##########################........."
    "aaa######################################"
    "...####o#o#o#o#o#o#o#o#o#o#o#o#o#o######."
    ".....################################...."
    "...................ooooo................."
    "..................oooooo................."
    "...................oooo..................");

constexpr auto kWide = pack<49, 13>(
    "aa..............................................."
    "aaa.............................................."
    ".aaa............................................."
    ".aaaa............................................"
    "..aaaa..........................................."
    "..aaaaa.........................................."
    "..aaaaa######################################...."
    "aaaa############################################."
    "...######o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#####"
    ".....###########################################."
    ".........####################################...."
    ".......................oooooo...................."
    "......................ooooooo....................");

constexpr auto kJumbo = pack<56, 15>(
    "aa......................................................"
    "aaa....................................................."
    ".aaa...................................................."
    ".aaaa..................................................."
    "..aaaa.................................................."
    "..aaaaa.................................########........"
    "..aaaaaa...........................##############......."
    "...aaaaa############################################...."
    "aaaa#################################################..."
    "...#####o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#####"
    ".....##################################################."
    ".........###########################################...."
    "...................ooooo......ooooo....................."
    "..................oooooo.....oooooo....................."
    "...................oooo.......oooo......................");

constexpr auto kHeli = pack<19, 9>(
    "ooooooooooooooooooo"
    "..........o........"
    "........######....."
    "o......##o#o###...."
    "aaaaaa#########o..."
    "a.......########..."
    ".........######...."
    ".........o....o...."
    ".......oooooooooo..");

constexpr auto kFighter = pack<24, 6>(
    ".a......................"
    ".aa...............o....."
    ".aaa.............ooo...."
    ".aaaa##################."
    "oaa#####################"
    "...aaa######............");

constexpr auto kMilCargo = pack<46, 12>(
    "aaaaaaaa......................................"
    "...aaa........................................"
    "...aaa........................................"
    "..aaa........................................."
    "..aaa.......#####################............."
    ".aaa......ooooo.........ooooo................."
    ".aaa##########################################"
    ".#############################################"
    "######o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o#o######."
    "..##########################################.."
    ".....##################################......."
    "..........######################..............");

const PlaneSprite kSprites[] = {
    {32, 10, kGeneric.bits},
    {15, 7, kLight.bits},
    {21, 8, kTwinProp.bits},
    {30, 8, kTurboprop.bits},
    {23, 8, kBizjet.bits},
    {32, 10, kRegional.bits},
    {41, 11, kNarrow.bits},
    {49, 13, kWide.bits},
    {56, 15, kJumbo.bits},
    {19, 9, kHeli.bits},
    {24, 6, kFighter.bits},
    {46, 12, kMilCargo.bits},
};
static_assert(sizeof(kSprites) / sizeof(kSprites[0]) == kPlaneKinds - 1, "a picture per kind");

// Exact codes win; else the longest * prefix.
struct KindCodes {
  PlaneKind kind;
  const char *codes;
};
const KindCodes kKindCodes[] = {
    {PlaneKind::NONE, "BALL SHIP UAV PARA"},
    {PlaneKind::LIGHT,
     "PA32 PA46 P46T PA18 PA22 PA24 PA28 PA38 BE33 BE35 BE36 B36T BE23 BE19 PC12 PC6 PC21 T6 TEX2 KODI "
     "TBM7 TBM8 TBM9 DA20 DA40 DV20 GA8 J3 AA5 AA1 TB20 TB21 S22T COL3 COL4 P210 T206 T210 DHC2 "
     "DHC3 GLID ULAC C1* C2* P28* SR2* M20* RV*"},
    {PlaneKind::TWIN_PROP,
     "BE20 BE30 B350 BE9L BE9T BE10 BE55 BE58 BE60 BE76 BE95 PA23 PA27 PA30 PA31 PA34 PA44 P68 "
     "DA42 DA62 MU2 AC90 AC95 SW3 BN2P GA7 C31* C32* C33* C34* C40* C41* C42* C44* PAY*"},
    {PlaneKind::TURBOPROP,
     "SF34 SB20 E120 B190 BE99 D328 JS31 JS32 JS41 F50 DHC6 DHC7 L410 SW4 AN26 Y12 CL2T CL2P AT4* AT7* "
     "DH8*"},
    {PlaneKind::BIZJET,
     "E35L E50P E55P E545 E550 H25A H25B H25C HA4T BE40 PRM1 F2TH F900 HDJT PC24 SF50 EA50 ASTR "
     "GALX WW24 G150 G200 G280 C25* C50* C51* C52* C55* C56* C65* C68* C70* C75* CL* GL* GA* FA* "
     "LJ*"},
    {PlaneKind::REGIONAL,
     "F70 F100 B461 B462 B463 RJ70 RJ85 RJ1H SU95 AJ27 E135 E145 E45X CRJ* E17* E19* E29* E75*"},
    {PlaneKind::NARROW,
     "A318 A319 A19N A20N A21N BCS1 BCS3 C919 P8 E737 C40 K35R K35E R135 B703 E3TF E3CF E6 A31* "
     "B37M B38M B39M B3XM A32* B73* B75* B71* B72* MD8* MD9* DC9*"},
    {PlaneKind::WIDE,
     "E767 A306 A30B A310 A3ST A337 MD11 DC10 L101 KC10 IL96 IL86 A33* A34* A35* B76* B77* B78*"},
    {PlaneKind::JUMBO, "BLCF VC25 B74* A38*"},
    {PlaneKind::HELI,
     "R22 R44 R66 B06 B06T B204 B205 B212 B222 B230 B407 B412 B427 B429 B430 B505 B47G A109 A119 "
     "A139 A169 A189 H60 H64 H47 H53 H53S H160 H269 H500 UH1 UH1Y S61 S64 S70 S76 S92 MD52 MD60 "
     "EXPL NH90 EH10 BK17 V22 CH47 AS32 AS3B AS50 AS55 AS65 EN28 EN48 G2CA MI8 KA32 UHEL GYRO EC*"},
    {PlaneKind::FIGHTER,
     "F14 F15 F16 F18H F18S F22 F35 F4 F5 F117 T38 T45 A10 EUFI RFAL TOR HAWK L39 AV8B M346 B1"},
    {PlaneKind::MIL_CARGO,
     "A124 A225 C130 C30J C17 C5 C5M A400 C160 C27J CN35 C295 AN12 IL76 B52 E390"},
};

// Matched length; a prefix needs more after it.
size_t match(const char *s, size_t n, const char *type, bool &prefix) {
  prefix = n > 0 && s[n - 1] == '*';
  const size_t len = prefix ? n - 1 : n;
  if (strncmp(s, type, len) != 0)
    return 0;
  return (type[len] != '\0') == prefix ? len : 0;
}

}  // namespace

const PlaneSprite *plane_sprite(PlaneKind k) {
  const int i = (int) k;
  return i >= 1 && i < kPlaneKinds ? &kSprites[i - 1] : nullptr;
}

PlaneKind plane_kind(const char *type, const char *category) {
  if (type != nullptr && type[0] != '\0') {
    PlaneKind best = PlaneKind::NONE;
    size_t best_len = 0;
    for (const KindCodes &k : kKindCodes)
      for (const char *s = k.codes; *s != '\0';) {
        size_t n = 0;
        while (s[n] != '\0' && s[n] != ' ')
          n++;
        bool prefix = false;
        const size_t len = match(s, n, type, prefix);
        if (len > 0 && !prefix)
          return k.kind;
        if (len > best_len) {
          best = k.kind;
          best_len = len;
        }
        s += n;
        while (*s == ' ')
          s++;
      }
    if (best != PlaneKind::NONE)
      return best;
  }
  if (category != nullptr && strlen(category) == 2 && category[1] >= '1' && category[1] <= '7') {
    static const PlaneKind a[] = {PlaneKind::LIGHT,  PlaneKind::BIZJET,  PlaneKind::NARROW, PlaneKind::NARROW,
                                  PlaneKind::WIDE,   PlaneKind::FIGHTER, PlaneKind::HELI};
    static const PlaneKind b[] = {PlaneKind::LIGHT, PlaneKind::NONE,    PlaneKind::NONE, PlaneKind::LIGHT,
                                  PlaneKind::GENERIC, PlaneKind::NONE, PlaneKind::NONE};
    const int i = category[1] - '1';
    if (category[0] == 'A')
      return a[i];
    if (category[0] == 'B')
      return b[i];
  }
  return PlaneKind::GENERIC;
}

}  // namespace flights
