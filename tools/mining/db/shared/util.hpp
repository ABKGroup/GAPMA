#pragma once

#include "types.hpp"

#include <string>
#include <vector>

std::string trim(const std::string& s);
std::string upper(std::string s);
std::string read_text(const fs::path& p);
#ifdef LOGIC_MINOR_USE_YOSYS
std::string yosys_quote(const std::string& s);
bool flatten_with_yosys_api(const fs::path& in_netlist, const fs::path& out_netlist, const std::string& top_name);
#endif
bool is_ident_start(char c);
bool is_ident_char(char c);
std::string strip_comments(const std::string& s);
fs::path resolve(const fs::path& base, const std::string& p);
std::vector<fs::path> glob_files(const fs::path& root, const std::string& pat);
