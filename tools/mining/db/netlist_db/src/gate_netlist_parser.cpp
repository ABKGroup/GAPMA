

#include "gate_netlist_parser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace gate_parser {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if (!ifs.is_open()) throw std::runtime_error("gate_parser: cannot open " + path);
  auto sz = ifs.tellg();
  std::string buf(static_cast<size_t>(sz), '\0');
  ifs.seekg(0);
  ifs.read(buf.data(), sz);
  return buf;
}

void strip_comments(std::string& src) {
  size_t i = 0, n = src.size();
  size_t w = 0;
  while (i < n) {
    if (i + 1 < n && src[i] == '/' && src[i + 1] == '/') {

      while (i < n && src[i] != '\n') src[w++] = ' ', ++i;
    } else if (i + 1 < n && src[i] == '/' && src[i + 1] == '*') {

      i += 2;
      while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
        src[w++] = (src[i] == '\n') ? '\n' : ' ';
        ++i;
      }
      if (i + 1 < n) { src[w++] = ' '; src[w++] = ' '; i += 2; }
    } else {
      src[w++] = src[i++];
    }
  }
  src.resize(w);
}

std::vector<std::string_view> split_statements(const std::string& src) {
  std::vector<std::string_view> stmts;
  size_t start = 0, n = src.size();
  int paren = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = src[i];
    if (c == '(') ++paren;
    else if (c == ')') { if (paren > 0) --paren; }
    else if (c == ';' && paren == 0) {
      stmts.emplace_back(src.data() + start, i - start);
      start = i + 1;
    } else if (paren == 0 && c == 'e' && i + 9 <= n &&
               src.compare(i, 9, "endmodule") == 0 &&
               (i + 9 == n || !std::isalnum(static_cast<unsigned char>(src[i + 9])))) {

      stmts.emplace_back(src.data() + start, i - start);

      stmts.emplace_back(src.data() + i, 9);
      i += 9;
      start = i;
      --i;
    }
  }

  if (start < n) stmts.emplace_back(src.data() + start, n - start);
  return stmts;
}

std::string_view trim_sv(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
  return sv;
}

std::string trim_str(std::string_view sv) {
  sv = trim_sv(sv);
  return std::string(sv);
}

std::string parse_identifier(std::string_view sv, size_t& pos) {
  while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos]))) ++pos;
  if (pos >= sv.size()) return {};
  if (sv[pos] == '\\') {

    size_t start = pos + 1;
    ++pos;
    while (pos < sv.size() && !std::isspace(static_cast<unsigned char>(sv[pos]))) ++pos;
    return std::string(sv.substr(start, pos - start));
  }

  size_t start = pos;
  while (pos < sv.size() && (std::isalnum(static_cast<unsigned char>(sv[pos]))
         || sv[pos] == '_' || sv[pos] == '$' || sv[pos] == '[' || sv[pos] == ']'
         || sv[pos] == ':' || sv[pos] == '\'')) {
    ++pos;
  }
  return std::string(sv.substr(start, pos - start));
}

std::string parse_net_expr(std::string_view sv) {
  auto s = trim_sv(sv);

  return std::string(s);
}

enum class StmtKind { MODULE, PORT, WIRE, INSTANCE, ENDMODULE, OTHER };

StmtKind classify(std::string_view sv) {
  sv = trim_sv(sv);
  if (sv.substr(0, 6) == "module" && (sv.size() <= 6 || !std::isalnum(static_cast<unsigned char>(sv[6])))) return StmtKind::MODULE;
  if (sv.substr(0, 9) == "endmodule") return StmtKind::ENDMODULE;
  if (sv.substr(0, 5) == "input" && (sv.size() <= 5 || !std::isalnum(static_cast<unsigned char>(sv[5])))) return StmtKind::PORT;
  if (sv.substr(0, 6) == "output" && (sv.size() <= 6 || !std::isalnum(static_cast<unsigned char>(sv[6])))) return StmtKind::PORT;
  if (sv.substr(0, 5) == "inout" && (sv.size() <= 5 || !std::isalnum(static_cast<unsigned char>(sv[5])))) return StmtKind::PORT;
  if (sv.substr(0, 4) == "wire" && (sv.size() <= 4 || !std::isalnum(static_cast<unsigned char>(sv[4])))) return StmtKind::WIRE;

  if (sv.find('(') != std::string_view::npos) return StmtKind::INSTANCE;
  return StmtKind::OTHER;
}

std::string parse_module_stmt(std::string_view sv) {
  sv = trim_sv(sv);

  size_t pos = 6;
  return parse_identifier(sv, pos);
}

std::string_view module_port_list(std::string_view sv) {
  auto lp = sv.find('(');
  auto rp = sv.rfind(')');
  if (lp == std::string_view::npos || rp == std::string_view::npos || rp <= lp) return {};
  return sv.substr(lp + 1, rp - lp - 1);
}

bool port_list_has_direction_keyword(std::string_view sv) {
  for (std::string_view kw : {std::string_view("input"), std::string_view("output"),
                              std::string_view("inout")}) {
    size_t pos = 0;
    while ((pos = sv.find(kw, pos)) != std::string_view::npos) {
      const size_t after = pos + kw.size();
      auto ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
      };
      const bool left_ok  = (pos == 0) || !ident_char(sv[pos - 1]);
      const bool right_ok = (after >= sv.size()) || !ident_char(sv[after]);
      if (left_ok && right_ok) return true;
      pos = after;
    }
  }
  return false;
}

struct BusRange { int width = 1, msb = 0, lsb = 0; };
BusRange parse_bus_range(std::string_view sv, size_t& pos) {
  while (pos < sv.size() && std::isspace(static_cast<unsigned char>(sv[pos]))) ++pos;
  if (pos >= sv.size() || sv[pos] != '[') return {};
  ++pos;

  size_t colon = sv.find(':', pos);
  size_t rb = sv.find(']', pos);
  if (rb == std::string_view::npos) return {};
  if (colon != std::string_view::npos && colon < rb) {
    int msb = std::stoi(std::string(sv.substr(pos, colon - pos)));
    int lsb = std::stoi(std::string(sv.substr(colon + 1, rb - colon - 1)));
    pos = rb + 1;
    return {std::abs(msb - lsb) + 1, msb, lsb};
  }

  pos = rb + 1;
  return {};
}

void parse_port_stmt(std::string_view sv, std::vector<PortDecl>& out) {
  sv = trim_sv(sv);
  PortDir dir;
  size_t pos = 0;
  if (sv.substr(0, 6) == "output") { dir = PortDir::OUTPUT; pos = 6; }
  else if (sv.substr(0, 5) == "inout") { dir = PortDir::INOUT; pos = 5; }
  else { dir = PortDir::INPUT; pos = 5; }

  auto range = parse_bus_range(sv, pos);

  while (pos < sv.size()) {
    auto name = parse_identifier(sv, pos);
    if (name.empty()) break;
    out.push_back({std::move(name), dir, range.width, range.msb, range.lsb});
    while (pos < sv.size() && (sv[pos] == ',' || std::isspace(static_cast<unsigned char>(sv[pos])))) ++pos;
  }
}

void parse_wire_stmt(std::string_view sv, std::unordered_map<std::string, int>& out) {
  sv = trim_sv(sv);
  size_t pos = 4;
  auto range = parse_bus_range(sv, pos);
  while (pos < sv.size()) {
    auto name = parse_identifier(sv, pos);
    if (name.empty()) break;
    out[std::move(name)] = range.width;
    while (pos < sv.size() && (sv[pos] == ',' || std::isspace(static_cast<unsigned char>(sv[pos])))) ++pos;
  }
}

Instance parse_instance_stmt(std::string_view sv) {
  Instance inst;
  sv = trim_sv(sv);
  size_t pos = 0;
  inst.master = parse_identifier(sv, pos);
  inst.name = parse_identifier(sv, pos);

  auto lp = sv.find('(', pos);
  if (lp == std::string_view::npos) return inst;

  auto rp = sv.rfind(')');
  if (rp == std::string_view::npos || rp <= lp) return inst;

  auto pins_sv = sv.substr(lp + 1, rp - lp - 1);

  size_t i = 0, n = pins_sv.size();
  while (i < n) {

    while (i < n && std::isspace(static_cast<unsigned char>(pins_sv[i]))) ++i;
    if (i >= n) break;

    if (pins_sv[i] != '.') { ++i; continue; }
    ++i;

    std::string port;
    while (i < n && pins_sv[i] != '(' && pins_sv[i] != ',' && !std::isspace(static_cast<unsigned char>(pins_sv[i]))) {
      port += pins_sv[i++];
    }

    while (i < n && pins_sv[i] != '(') ++i;
    if (i >= n) break;
    ++i;

    int depth = 1;
    size_t net_start = i;
    while (i < n && depth > 0) {
      if (pins_sv[i] == '(') ++depth;
      else if (pins_sv[i] == ')') --depth;
      if (depth > 0) ++i;
    }
    auto net_expr = parse_net_expr(pins_sv.substr(net_start, i - net_start));
    if (i < n) ++i;

    if (!port.empty()) inst.pins.push_back({std::move(port), std::move(net_expr)});

    while (i < n && pins_sv[i] != ',') ++i;
    if (i < n) ++i;
  }
  return inst;
}

}

ParsedNetlist parse(const std::string& filepath, const std::string& top_name) {
  std::string src = read_file(filepath);
  strip_comments(src);
  auto stmts = split_statements(src);

  const int nstmts = static_cast<int>(stmts.size());

  std::vector<StmtKind> kinds(nstmts);
  #pragma omp parallel for schedule(static)
  for (int i = 0; i < nstmts; ++i) {
    kinds[i] = classify(stmts[i]);
  }

  int mod_start = -1, mod_end = -1;
  for (int i = 0; i < nstmts; ++i) {
    if (kinds[i] == StmtKind::MODULE) {
      auto name = parse_module_stmt(stmts[i]);
      if (top_name.empty() || name == top_name) {
        mod_start = i;

        for (int j = i + 1; j < nstmts; ++j) {
          if (kinds[j] == StmtKind::ENDMODULE) { mod_end = j; break; }
        }
        if (!top_name.empty()) break;
      }
    }
  }
  if (mod_start < 0) throw std::runtime_error("gate_parser: no module found in " + filepath);
  if (mod_end < 0) mod_end = nstmts;

  ParsedNetlist result;
  result.top_name = parse_module_stmt(stmts[mod_start]);

  const auto port_list = module_port_list(trim_sv(stmts[mod_start]));
  if (port_list_has_direction_keyword(port_list)) {
    throw std::runtime_error("gate_parser: module '" + result.top_name + "' in " + filepath +
                             " uses an ANSI-style (Verilog-2001) header with port directions inside"
                             " the port list, which this parser cannot read; re-run with --flatten"
                             " to use the Yosys path");
  }

  int inst_count = 0;
  for (int i = mod_start + 1; i < mod_end; ++i) {
    if (kinds[i] == StmtKind::INSTANCE) ++inst_count;
  }

  std::vector<int> inst_indices;
  inst_indices.reserve(inst_count);
  for (int i = mod_start + 1; i < mod_end; ++i) {
    if (kinds[i] == StmtKind::INSTANCE) inst_indices.push_back(i);
  }

  std::vector<Instance> instances(inst_indices.size());
  #pragma omp parallel for schedule(dynamic, 256)
  for (int k = 0; k < static_cast<int>(inst_indices.size()); ++k) {
    instances[k] = parse_instance_stmt(stmts[inst_indices[k]]);
  }

  for (int i = mod_start + 1; i < mod_end; ++i) {
    if (kinds[i] == StmtKind::PORT) {
      parse_port_stmt(stmts[i], result.ports);
    } else if (kinds[i] == StmtKind::WIRE) {
      parse_wire_stmt(stmts[i], result.wire_widths);
    }
  }

  if (result.ports.empty() && !trim_sv(port_list).empty()) {
    throw std::runtime_error("gate_parser: module '" + result.top_name + "' in " + filepath +
                             " declares a port list but no input/output/inout declaration was"
                             " parsed inside the module body");
  }

  result.instances = std::move(instances);
  return result;
}

}
