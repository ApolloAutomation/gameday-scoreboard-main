// How the panel lays a splash out, for the tests that check a splash's words
// fit it (test_alerts.cpp, test_players.cpp).
#pragma once

#include <string>
#include <vector>

namespace splash_test {

// How LVGL lays a splash out: a line break at "\n", else at a space or after
// a hyphen once the next word won't fit. The lines, or none when a word is
// longer than a line (it would be cut in the middle).
inline std::vector<std::string> layout(const std::string &text, size_t width) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    std::string seg = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    start = nl == std::string::npos ? text.size() + 1 : nl + 1;
    std::vector<std::string> words;  // each with the hyphen it ends with
    std::string w;
    for (char c : seg) {
      if (c == ' ') {
        if (!w.empty())
          words.push_back(w);
        w.clear();
      } else {
        w += c;
        if (c == '-') {
          words.push_back(w);
          w.clear();
        }
      }
    }
    if (!w.empty())
      words.push_back(w);
    std::string line;
    for (size_t i = 0; i < words.size(); i++) {
      if (words[i].size() > width)
        return {};
      const bool joined = i > 0 && words[i - 1].back() == '-';
      const std::string sep = line.empty() || joined ? "" : " ";
      if (line.size() + sep.size() + words[i].size() > width) {
        out.push_back(line);
        line = words[i];
      } else {
        line += sep + words[i];
      }
    }
    out.push_back(line);
  }
  return out;
}

inline bool fits(const std::string &text, size_t width, size_t max_lines) {
  const std::vector<std::string> lines = layout(text, width);
  return !lines.empty() && lines.size() <= max_lines;
}

}  // namespace splash_test
