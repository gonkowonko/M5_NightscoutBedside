// Host test for include/initext.h (the INI editor behind the settings page).
//
//   clang++ -std=c++17 -I test/host -I include test/host/test_initext.cpp -o /tmp/t && /tmp/t SD/M5NS.INI
//
// Applies the same edits as saveConfig() to the given INI file and checks that
// every line it doesn't manage survives, line endings are kept, Wi-Fi sections
// are replaced, and saving twice changes nothing. Prints the result with
// tokens and passwords masked.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "WString.h"
#include "initext.h"

static const std::set<std::string> kManaged = {
    "nightscout", "token", "tz", "show_mgdl", "time_format", "date_format", "yellow_low",
    "yellow_high", "red_low", "red_high", "brightness1", "brightness2", "brightness3",
    "stale_minutes", "temp_offset", "night_mode", "night_start", "night_end", "night_brightness"};

static std::string keyOf(const String& line) {
  String t = IniText::trimmed(line);
  int eq = t.indexOf('=');
  if (eq <= 0 || t.startsWith(";") || t.startsWith("#")) return "";
  std::string k = IniText::trimmed(t.substring(0, eq)).str();
  for (auto& c : k) c = tolower(c);
  return k;
}

static void applySave(IniText& ini) {
  ini.set("config", "nightscout", "https://cgm.example.com");
  ini.set("config", "token", "secret-token");
  ini.set("config", "tz", "GMT0BST,M3.5.0/1,M10.5.0");
  ini.set("config", "show_mgdl", "0");
  ini.set("config", "time_format", "0");
  ini.set("config", "date_format", "0");
  ini.set("config", "yellow_low", "4");
  ini.set("config", "yellow_high", "9.1");
  ini.set("config", "red_low", "3.9");
  ini.set("config", "red_high", "12.8");
  ini.set("config", "brightness1", "40");
  ini.set("config", "brightness2", "100");
  ini.set("config", "brightness3", "1");
  ini.set("config", "stale_minutes", "10");
  ini.set("config", "temp_offset", "-2.5");
  ini.set("config", "night_mode", "0");
  ini.set("config", "night_start", "22:00");
  ini.set("config", "night_end", "07:00");
  ini.set("config", "night_brightness", "1");
  ini.removeSectionsStartingWith("wlan");
  ini.set("wlan1", "ssid", "Home");
  ini.set("wlan1", "pass", "pw1");
  ini.set("wlan2", "ssid", "Phone");
  ini.set("wlan2", "pass", "");
}

int main(int argc, char** argv) {
  if (argc < 2) return fprintf(stderr, "usage: %s M5NS.INI\n", argv[0]), 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  String original(ss.str());
  bool crlf = original.indexOf("\r\n") >= 0;

  IniText ini;
  ini.parse(original);
  std::vector<String> before = ini.lines;
  applySave(ini);
  String once = ini.render();

  // Every unmanaged line outside [wlanN] sections survives, in order.
  std::vector<std::string> kept, after;
  bool inWlan = false;
  for (auto& l : before) {
    String name;
    if (IniText::isSection(l, &name)) inWlan = name.substring(0, 4).equalsIgnoreCase("wlan");
    if (!inWlan && !kManaged.count(keyOf(l)) && IniText::trimmed(l).length()) kept.push_back(l.str());
  }
  inWlan = false;
  for (auto& l : ini.lines) {
    String name;
    if (IniText::isSection(l, &name)) inWlan = name.substring(0, 4).equalsIgnoreCase("wlan");
    if (!inWlan && !kManaged.count(keyOf(l)) && IniText::trimmed(l).length()) after.push_back(l.str());
  }
  assert(kept == after && "unmanaged lines changed");

  // Each managed key appears exactly once in [config].
  for (auto& k : kManaged) {
    int n = 0;
    for (auto& l : ini.lines) n += keyOf(l) == k;
    assert(n == 1 && "managed key missing or duplicated");
  }
  // Line endings kept; idempotent.
  assert(crlf == (once.indexOf("\r\n") >= 0));
  IniText again;
  again.parse(once);
  applySave(again);
  assert(again.render() == once && "second save changed the file");
  assert(ini.findSection("wlan3") < 0 && ini.findSection("wlan2") >= 0);

  for (auto& l : ini.lines) {
    std::string k = keyOf(l);
    std::cout << ((k == "token" || k == "pass") ? IniText::trimmed(l).substring(0, l.indexOf('=') + 1).str() + " ****" : l.str())
              << "\n";
  }
  std::cout << "\nOK: " << before.size() << " lines in, " << ini.lines.size() << " out, "
            << (crlf ? "CRLF" : "LF") << " kept, idempotent\n";
}
