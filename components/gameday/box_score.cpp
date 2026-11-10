// Box score reads for player props (props.h).
#include "espn_parse.h"
#include "props.h"

#include <cstring>

namespace espn {
namespace detail {

namespace {

// One box score column a prop can use.
struct BoxCol {
  Sport sport;
  const char *cat;  // nullptr: any category
  const char *key;  // ESPN's stat key
  uint8_t atom;
};

constexpr uint8_t kOuts = 0x80;  // innings pitched, read as outs

constexpr BoxCol kBoxCols[] = {
    {Sport::FOOTBALL, "passing", "completions/passingAttempts", atom::CMP},
    {Sport::FOOTBALL, "passing", "passingYards", atom::PASS_YDS},
    {Sport::FOOTBALL, "passing", "passingTouchdowns", atom::PASS_TD},
    {Sport::FOOTBALL, "passing", "interceptions", atom::INT},
    {Sport::FOOTBALL, "rushing", "rushingAttempts", atom::RUSH_ATT},
    {Sport::FOOTBALL, "rushing", "rushingYards", atom::RUSH_YDS},
    {Sport::FOOTBALL, "rushing", "rushingTouchdowns", atom::RUSH_TD},
    {Sport::FOOTBALL, "receiving", "receptions", atom::REC},
    {Sport::FOOTBALL, "receiving", "receivingYards", atom::REC_YDS},
    {Sport::FOOTBALL, "receiving", "receivingTouchdowns", atom::REC_TD},
    {Sport::FOOTBALL, "defensive", "totalTackles", atom::TACKLES},
    {Sport::FOOTBALL, "defensive", "sacks", atom::SACKS},
    {Sport::FOOTBALL, "defensive", "defensiveTouchdowns", atom::OTHER_TD},
    {Sport::FOOTBALL, "interceptions", "interceptionTouchdowns", atom::OTHER_TD},
    {Sport::FOOTBALL, "kickReturns", "kickReturnTouchdowns", atom::OTHER_TD},
    {Sport::FOOTBALL, "puntReturns", "puntReturnTouchdowns", atom::OTHER_TD},
    {Sport::FOOTBALL, "kicking", "totalKickingPoints", atom::KICK_PTS},
    {Sport::BASKETBALL, nullptr, "points", atom::PTS},
    {Sport::BASKETBALL, nullptr, "rebounds", atom::REB},
    {Sport::BASKETBALL, nullptr, "assists", atom::AST},
    {Sport::BASKETBALL, nullptr, "threePointFieldGoalsMade-threePointFieldGoalsAttempted", atom::THREES},
    {Sport::BASKETBALL, nullptr, "steals", atom::STL},
    {Sport::BASKETBALL, nullptr, "blocks", atom::BLK},
    {Sport::BASEBALL, "batting", "hits", atom::H},
    {Sport::BASEBALL, "batting", "runs", atom::R},
    {Sport::BASEBALL, "batting", "RBIs", atom::RBI},
    {Sport::BASEBALL, "batting", "homeRuns", atom::HR},
    {Sport::BASEBALL, "pitching", "strikeouts", atom::K},
    {Sport::BASEBALL, "pitching", "hits", atom::HA},
    {Sport::BASEBALL, "pitching", "earnedRuns", atom::ER},
    {Sport::BASEBALL, "pitching", "fullInnings.partInnings", atom::OUTS | kOuts},
    {Sport::HOCKEY, nullptr, "goals", atom::G},
    {Sport::HOCKEY, nullptr, "assists", atom::A},
    {Sport::HOCKEY, nullptr, "shotsTotal", atom::SOG},
    {Sport::HOCKEY, nullptr, "blockedShots", atom::BS},
    {Sport::HOCKEY, nullptr, "saves", atom::SV},
};

uint8_t col_atom(Sport sport, const char *cat, const char *key) {
  if (key == nullptr)
    return kNoAtom;
  for (const BoxCol &c : kBoxCols)
    if (c.sport == sport && (c.cat == nullptr || strcmp(c.cat, cat) == 0) && strcmp(c.key, key) == 0)
      return c.atom;
  return kNoAtom;
}

// "21/34" is 21, "1.5" 1.5, "-3" -3: in half units.
int box_half(const char *s) {
  const bool neg = *s == '-';
  if (neg)
    s++;
  long whole = 0;
  while (*s >= '0' && *s <= '9' && whole < 10000)
    whole = whole * 10 + (*s++ - '0');
  int h = (int) whole * 2;
  if (*s == '.' && s[1] >= '5' && s[1] <= '9')
    h++;
  return neg ? -h : h;
}

// Innings pitched "5.2" is 17 outs.
int box_outs(const char *s) {
  long whole = 0;
  while (*s >= '0' && *s <= '9' && whole < 1000)
    whole = whole * 10 + (*s++ - '0');
  const int part = *s == '.' && s[1] >= '0' && s[1] <= '2' ? s[1] - '0' : 0;
  return ((int) whole * 3 + part) * 2;
}

}  // namespace

void fill_box_filter(JsonDocument &f) {
  JsonObject s = f["boxscore"]["players"].add<JsonObject>()["statistics"].add<JsonObject>();
  s["name"] = true;
  s["type"] = true;
  s["keys"] = true;
  JsonObject a = s["athletes"].add<JsonObject>();
  a["athlete"]["id"] = true;
  a["stats"] = true;
  a["didNotPlay"] = true;
}

bool box_from_doc(JsonObjectConst root, Sport sport, const PlayerWatch &w, std::vector<BoxLine> &out) {
  JsonObjectConst box = root["boxscore"];
  if (box.isNull())
    return false;
  out.clear();
  for (JsonObjectConst team : box["players"].as<JsonArrayConst>())
    for (JsonObjectConst s : team["statistics"].as<JsonArrayConst>()) {
      const char *cat = s["name"].as<const char *>();
      if (cat == nullptr)
        cat = s["type"].as<const char *>();
      if (cat == nullptr)
        cat = "";
      uint8_t cols[32];
      size_t n = 0;
      for (JsonVariantConst k : s["keys"].as<JsonArrayConst>())
        if (n < sizeof(cols))
          cols[n++] = col_atom(sport, cat, k.as<const char *>());
      for (JsonObjectConst a : s["athletes"].as<JsonArrayConst>()) {
        const uint32_t id = id_of(a["athlete"]["id"]);
        if (!w.has(id))
          continue;
        BoxLine *line = nullptr;
        for (BoxLine &l : out)
          if (l.athlete == id)
            line = &l;
        if (line == nullptr) {
          if (out.size() >= (size_t) PlayerWatch::kMax)
            continue;
          out.push_back(BoxLine{});
          line = &out.back();
          line->athlete = id;
        }
        JsonArrayConst stats = a["stats"];
        if ((a["didNotPlay"] | false) || stats.size() == 0)
          continue;
        line->played = true;
        size_t i = 0;
        for (JsonVariantConst v : stats) {
          if (i >= n)
            break;
          const uint8_t c = cols[i++];
          const char *t = v.as<const char *>();
          if (c == kNoAtom || t == nullptr)
            continue;
          const uint8_t at = c & ~kOuts;
          const int sum = line->v[at] + ((c & kOuts) != 0 ? box_outs(t) : box_half(t));
          line->v[at] = (int16_t) (sum < INT16_MIN ? INT16_MIN : sum > INT16_MAX ? INT16_MAX : sum);
        }
      }
    }
  return true;
}

}  // namespace detail

void BoxCutter::feed(char c) {
  this->used_++;
  if (this->str_) {
    if (this->esc_)
      this->esc_ = false;
    else if (c == '\\')
      this->esc_ = true;
    else if (c == '"')
      this->str_ = false;
    if (this->key_) {
      if (!this->str_) {
        this->key_ = false;
        this->box_ = this->len_ == 8 && memcmp(this->name_, "boxscore", 8) == 0;
      } else if (this->len_ < sizeof(this->name_)) {
        this->name_[this->len_++] = c;
      }
    }
    return;
  }
  switch (c) {
    case '"':
      this->str_ = true;
      if (this->depth_ == 1 && this->want_key_) {
        this->key_ = true;
        this->want_key_ = false;
        this->len_ = 0;
      }
      break;
    case '{':
    case '[':
      if (++this->depth_ == 1)
        this->want_key_ = c == '{';
      break;
    case '}':
    case ']':
      if (this->depth_ > 0 && --this->depth_ == 1 && this->box_)
        this->state_ = CLOSE;
      break;
    case ',':
      if (this->depth_ == 1)
        this->want_key_ = true;
      break;
    default:
      break;
  }
}

}  // namespace espn
