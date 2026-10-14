// Minimal stand-in for Arduino's String, just enough for initext.h on a host.
#pragma once
#include <cctype>
#include <string>
#include <strings.h>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  unsigned int length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  void trim() {
    size_t a = 0, b = s_.size();
    while (a < b && isspace((unsigned char)s_[a])) a++;
    while (b > a && isspace((unsigned char)s_[b - 1])) b--;
    s_ = s_.substr(a, b - a);
  }
  bool startsWith(const char* p) const { return s_.rfind(p, 0) == 0; }
  bool endsWith(const char* p) const {
    std::string q(p);
    return s_.size() >= q.size() && s_.compare(s_.size() - q.size(), q.size(), q) == 0;
  }
  String substring(unsigned a) const { return a >= s_.size() ? String() : String(s_.substr(a)); }
  String substring(unsigned a, unsigned b) const {
    if (b > s_.size()) b = s_.size();
    return a >= b ? String() : String(s_.substr(a, b - a));
  }
  int indexOf(char c, unsigned from = 0) const { auto p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char* t, unsigned from = 0) const { auto p = s_.find(t, from); return p == std::string::npos ? -1 : (int)p; }
  bool equalsIgnoreCase(const String& o) const { return strcasecmp(s_.c_str(), o.s_.c_str()) == 0; }
  void remove(unsigned i) { if (i < s_.size()) s_.erase(i); }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  const std::string& str() const { return s_; }

 private:
  std::string s_;
};
