// Line-based INI editing used by saveConfig(). Header-only and dependent on
// nothing but Arduino's String, so it can be unit-tested on the host
// (see test/host/test_initext.cpp).
#pragma once

#include <vector>

// The INI file as a list of lines, edited in place.
struct IniText {
  std::vector<String> lines;
  const char* nl = "\r\n";  // keep the file's line endings (DOS on old cards)

  static String trimmed(const String& s) {
    String t = s;
    t.trim();
    return t;
  }
  static bool isSection(const String& line, String* name = nullptr) {
    String t = trimmed(line);
    if (!t.startsWith("[") || !t.endsWith("]")) return false;
    if (name) *name = trimmed(t.substring(1, t.length() - 1));
    return true;
  }
  int findSection(const char* section) const {
    String name;
    for (size_t i = 0; i < lines.size(); i++)
      if (isSection(lines[i], &name) && name.equalsIgnoreCase(section)) return i;
    return -1;
  }
  int sectionEnd(int header) const {
    size_t i = header + 1;
    while (i < lines.size() && !isSection(lines[i])) i++;
    return i;
  }

  void parse(const String& text) {
    lines.clear();
    if (text.indexOf("\r\n") < 0 && text.indexOf('\n') >= 0) nl = "\n";
    int start = 0;
    while (start <= (int)text.length()) {
      int end = text.indexOf('\n', start);
      if (end < 0) end = text.length();
      String line = text.substring(start, end);
      if (line.endsWith("\r")) line.remove(line.length() - 1);
      lines.push_back(line);
      start = end + 1;
    }
    trimTrailingBlankLines();
  }

  String render() const {
    String out;
    for (const String& line : lines) {
      out += line;
      out += nl;
    }
    return out;
  }

  void trimTrailingBlankLines() {
    while (!lines.empty() && trimmed(lines.back()).length() == 0) lines.pop_back();
  }

  void set(const char* section, const char* key, const String& value) {
    String entry = String(key) + " = " + value;
    int header = findSection(section);
    if (header < 0) {
      if (!lines.empty() && trimmed(lines.back()).length()) lines.push_back("");
      lines.push_back(String("[") + section + "]");
      lines.push_back(entry);
      return;
    }
    int end = sectionEnd(header);
    for (int i = header + 1; i < end; i++) {
      String t = trimmed(lines[i]);
      if (t.startsWith(";") || t.startsWith("#")) continue;
      int eq = t.indexOf('=');
      if (eq > 0 && trimmed(t.substring(0, eq)).equalsIgnoreCase(key)) {
        lines[i] = entry;
        return;
      }
    }
    // New key: after the section's last non-blank line.
    int at = end;
    while (at > header + 1 && trimmed(lines[at - 1]).length() == 0) at--;
    lines.insert(lines.begin() + at, entry);
  }

  void removeSectionsStartingWith(const char* prefix) {
    for (size_t i = 0; i < lines.size();) {
      String name;
      if (isSection(lines[i], &name) && name.substring(0, strlen(prefix)).equalsIgnoreCase(prefix)) {
        lines.erase(lines.begin() + i, lines.begin() + sectionEnd(i));
      } else {
        i++;
      }
    }
    trimTrailingBlankLines();
  }
};
