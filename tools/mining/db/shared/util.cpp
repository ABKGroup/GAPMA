#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifdef LOGIC_MINOR_USE_YOSYS
#include "kernel/yosys.h"
#endif

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string upper(std::string s) {
  for (auto& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

std::string read_text(const fs::path& p) {
  std::ifstream ifs(p);
  if (!ifs) throw std::runtime_error("cannot open: " + p.string());
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

#ifdef LOGIC_MINOR_USE_YOSYS
std::string yosys_quote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

bool flatten_with_yosys_api(const fs::path& in_netlist,
                            const fs::path& out_netlist,
                            const std::string& top_name) {
  try {
    Yosys::yosys_setup();
    Yosys::run_pass("read_verilog " + yosys_quote(in_netlist.string()));
    if (!top_name.empty()) Yosys::run_pass("hierarchy -top " + top_name);
    else Yosys::run_pass("hierarchy -auto-top");
    Yosys::run_pass("flatten");
    Yosys::run_pass("write_verilog " + yosys_quote(out_netlist.string()));
    Yosys::yosys_shutdown();
    return fs::exists(out_netlist);
  } catch (const std::exception& e) {
    std::cerr << "[error] flatten_with_yosys_api: " << e.what() << "\n";
    try { Yosys::yosys_shutdown(); } catch (const std::exception& e2) {
      std::cerr << "[error] yosys_shutdown during cleanup: " << e2.what() << "\n";
    } catch (...) {
      std::cerr << "[error] yosys_shutdown during cleanup: unknown exception\n";
    }
    return false;
  } catch (...) {
    std::cerr << "[error] flatten_with_yosys_api: unknown exception\n";
    try { Yosys::yosys_shutdown(); } catch (...) {}
    return false;
  }
}
#endif

bool is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string strip_comments(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
      if (i + 1 < s.size()) i += 2;
      continue;
    }
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
      i += 2;
      while (i < s.size() && s[i] != '\n') ++i;
      continue;
    }
    if (i + 1 < s.size() && s[i] == '(' && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == ')')) ++i;
      if (i + 1 < s.size()) i += 2;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  return out;
}

fs::path resolve(const fs::path& base, const std::string& p) {
  fs::path pp(p);
  if (pp.is_absolute()) return pp;
  return fs::absolute(base / pp);
}

std::vector<fs::path> glob_files(const fs::path& root, const std::string& pat) {
  std::vector<fs::path> v;
  if (!fs::exists(root)) return v;
  for (auto& e : fs::directory_iterator(root)) {
    if (!e.is_regular_file()) continue;
    if (fnmatch(pat.c_str(), e.path().filename().string().c_str(), 0) == 0) {
      v.push_back(fs::absolute(e.path()));
    }
  }
  std::sort(v.begin(), v.end());
  return v;
}
