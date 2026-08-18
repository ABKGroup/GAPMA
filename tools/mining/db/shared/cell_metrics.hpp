#pragma once

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cell_metrics {

inline std::string cm_trim(const std::string& s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

inline std::string cm_upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

inline std::string cm_lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline std::vector<std::string> cm_split_ws(const std::string& s) {
  std::istringstream iss(s);
  std::vector<std::string> out;
  std::string tok;
  while (iss >> tok) out.push_back(tok);
  return out;
}

inline bool cm_is_supply_name(const std::string& raw) {
  const std::string s = cm_upper(raw);
  static const std::unordered_set<std::string> kSupply = {
      "VDD", "VSS", "VCC", "GND", "VPWR", "VGND", "VNW", "VPB"};
  return kSupply.count(s) > 0;
}

inline double cm_parse_spice_number(const std::string& raw) {
  const std::string s = cm_trim(raw);
  if (s.empty()) throw std::runtime_error("empty numeric token");

  size_t pos = 0;
  double value = 0.0;
  try {
    value = std::stod(s, &pos);
  } catch (const std::exception&) {
    throw std::runtime_error("cell_metrics: not a numeric token: '" + raw + "'");
  }
  if (!std::isfinite(value)) {
    throw std::runtime_error("cell_metrics: non-finite numeric token: '" + raw + "'");
  }
  const std::string suffix_raw = s.substr(pos);
  const std::string suffix = cm_lower(suffix_raw);
  if (suffix.empty()) return value;
  if (suffix == "meg") return value * 1e6;
  if (suffix.size() == 1) {
    switch (suffix[0]) {
      case 'f': return value * 1e-15;
      case 'p': return value * 1e-12;
      case 'n': return value * 1e-9;
      case 'u': return value * 1e-6;
      case 'm': return value * 1e-3;
      case 'k': return value * 1e3;
      case 'g': return value * 1e9;
      default: break;
    }
  }
  throw std::runtime_error("cell_metrics: unsupported numeric suffix '" + suffix_raw +
      "' in token '" + raw + "' (expected one of f p n u m meg k g, or no suffix)");
}

inline std::string cm_json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default:   out += c; break;
    }
  }
  return out;
}

struct Device {
  std::string name;
  std::string drain;
  std::string gate;
  std::string source;
  std::string bulk;
  std::string model;
  bool is_pmos = false;
  bool is_nmos = false;
  double width = 0.0;
  double length = 0.0;
  double mult = 1.0;
};

struct CellData {
  std::string name;
  std::vector<std::string> pins;
  std::vector<Device> devices;
};

struct PinInfo {
  std::vector<std::string> input_pins;
  std::vector<std::string> output_pins;
};

struct PathMetrics {
  int path_count = 0;
  int max_depth = 0;
  double max_resistance_proxy = 0.0;
};

struct StageMetric {
  std::vector<std::string> input_nets;
  int pu_path_count = 0;
  int pd_path_count = 0;
  double local_pu_resistance_proxy_max = 0.0;
  double local_pd_resistance_proxy_max = 0.0;
  double local_capacitance_proxy = 0.0;
};

struct PathProxyRange {
  double min_value = 0.0;
  double max_value = 0.0;
};

struct OutputMetric {
  std::string logic_function_expr_internal;
  std::string logic_function_canonical;
  int stage_count = 0;
  int max_pu_stack_depth = 0;
  int max_pd_stack_depth = 0;
  double input_to_output_resistance_proxy_min = 0.0;
  double input_to_output_resistance_proxy_max = 0.0;
  double input_to_output_capacitance_proxy_min = 0.0;
  double input_to_output_capacitance_proxy_max = 0.0;
};

struct CellMetricResult {
  PinInfo pins;
  double transistor_count_nmos = 0.0;
  double transistor_count_pmos = 0.0;
  int cpp_nmos = 0;
  int cpp_pmos = 0;
  int cpp_min  = 0;
  std::unordered_map<std::string, OutputMetric> outputs;
};

inline std::string read_text(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) throw std::runtime_error("failed to open: " + path);
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

inline CellData parse_subckt(const std::string& text, const std::string& cell_name) {
  std::istringstream iss(text);
  std::string line;
  bool in_target = false;
  CellData cell;
  std::string current;
  while (std::getline(iss, line)) {
    const std::string t = cm_trim(line);
    if (t.empty() || t[0] == '*') continue;
    if (!current.empty() && !t.empty() && t[0] == '+') {
      current += " " + cm_trim(t.substr(1));
      continue;
    }
    if (!current.empty()) {
      const std::string stmt = current;
      current.clear();
      const std::string up = cm_upper(stmt);
      if (!in_target) {
        if (up.rfind(".SUBCKT ", 0) == 0) {
          auto toks = cm_split_ws(stmt);
          if (toks.size() >= 2 && toks[1] == cell_name) {
            in_target = true;
            cell.name = toks[1];
            for (size_t i = 2; i < toks.size(); ++i) cell.pins.push_back(toks[i]);
          }
        }
      } else {
        if (up == ".ENDS" || up.rfind(".ENDS ", 0) == 0) break;
        auto toks = cm_split_ws(stmt);
        if (toks.empty()) continue;
        const char lead = static_cast<char>(std::toupper(static_cast<unsigned char>(toks[0][0])));

        if (lead != 'M') {
          throw std::runtime_error(
              "cell_metrics: non-MOSFET element '" + toks[0] + "' inside .SUBCKT '" +
              cell_name + "' (only flat transistor-level cells are supported): " + stmt);
        }
        if (toks.size() < 6) throw std::runtime_error("malformed MOS line: " + stmt);
        Device d;
        d.name = toks[0];
        d.drain = toks[1];
        d.gate = toks[2];
        d.source = toks[3];
        d.bulk = toks[4];
        d.model = toks[5];
        const std::string model_l = cm_lower(d.model);
        d.is_pmos = model_l.find("pmos") != std::string::npos;
        d.is_nmos = model_l.find("nmos") != std::string::npos;
        if (!d.is_pmos && !d.is_nmos) {
          throw std::runtime_error("unknown MOS model type: " + d.model);
        }
        bool has_nfin = false;
        for (size_t i = 6; i < toks.size(); ++i) {
          const std::string tok = toks[i];
          const auto eq = tok.find('=');
          if (eq == std::string::npos) continue;
          const std::string key = cm_lower(tok.substr(0, eq));
          const std::string val = tok.substr(eq + 1);
          if (key == "w") d.width = cm_parse_spice_number(val);
          else if (key == "l") d.length = cm_parse_spice_number(val);

          else if (key == "m") { if (!has_nfin) d.mult = std::stod(val); }
          else if (key == "nfin") { d.mult = std::stod(val); has_nfin = true; }
        }
        if (d.width <= 0.0) throw std::runtime_error("device missing width: " + stmt);
        if (d.length <= 0.0) throw std::runtime_error("device missing length: " + stmt);
        cell.devices.push_back(d);
      }
    }
    current = t;
  }
  if (!current.empty() && in_target) {
    const std::string up = cm_upper(current);
    if (!(up == ".ENDS" || up.rfind(".ENDS ", 0) == 0)) {
      auto toks = cm_split_ws(current);
      if (!toks.empty() && std::toupper(static_cast<unsigned char>(toks[0][0])) == 'M') {
        Device d;
        if (toks.size() < 6) throw std::runtime_error("malformed MOS line: " + current);
        d.name = toks[0];
        d.drain = toks[1];
        d.gate = toks[2];
        d.source = toks[3];
        d.bulk = toks[4];
        d.model = toks[5];
        const std::string model_l = cm_lower(d.model);
        d.is_pmos = model_l.find("pmos") != std::string::npos;
        d.is_nmos = model_l.find("nmos") != std::string::npos;
        for (size_t i = 6; i < toks.size(); ++i) {
          const auto eq = toks[i].find('=');
          if (eq == std::string::npos) continue;
          const std::string key = cm_lower(toks[i].substr(0, eq));
          const std::string val = toks[i].substr(eq + 1);
          if (key == "w") d.width = cm_parse_spice_number(val);
          else if (key == "l") d.length = cm_parse_spice_number(val);
          else if (key == "m") d.mult = std::stod(val);
          else if (key == "nfin") d.mult = std::stod(val);
        }
        cell.devices.push_back(d);
      }
    }
  }
  if (cell.name.empty()) throw std::runtime_error("subckt not found: " + cell_name);
  return cell;
}

inline std::unordered_map<std::string, CellData> parse_all_subckts(const std::string& text) {
  std::unordered_map<std::string, CellData> result;
  std::istringstream iss(text);
  std::string line;
  CellData cell;
  bool in_subckt = false;
  std::string current;
  auto process_stmt = [&](const std::string& stmt) {
    const std::string up = cm_upper(stmt);
    if (!in_subckt) {
      if (up.rfind(".SUBCKT ", 0) == 0) {
        auto toks = cm_split_ws(stmt);
        if (toks.size() >= 2) {
          in_subckt = true;
          cell = CellData{};
          cell.name = toks[1];
          for (size_t i = 2; i < toks.size(); ++i) cell.pins.push_back(toks[i]);
        }
      }
    } else {
      if (up == ".ENDS" || up.rfind(".ENDS ", 0) == 0) {
        if (!cell.name.empty()) {
          result[cell.name] = std::move(cell);
        }
        cell = CellData{};
        in_subckt = false;
        return;
      }
      auto toks = cm_split_ws(stmt);
      if (toks.empty()) return;
      const char lead = static_cast<char>(std::toupper(static_cast<unsigned char>(toks[0][0])));

      if (lead != 'M') {
        throw std::runtime_error(
            "cell_metrics: non-MOSFET element '" + toks[0] + "' inside .SUBCKT '" +
            cell.name + "' (only flat transistor-level cells are supported): " + stmt);
      }
      if (toks.size() < 6) {
        throw std::runtime_error(
            "cell_metrics: malformed MOSFET line in .SUBCKT '" + cell.name +
            "' (expected name d g s b model, got " + std::to_string(toks.size()) +
            " tokens): " + stmt);
      }
      Device d;
      d.name = toks[0];
      d.drain = toks[1];
      d.gate = toks[2];
      d.source = toks[3];
      d.bulk = toks[4];
      d.model = toks[5];
      const std::string model_l = cm_lower(d.model);
      d.is_pmos = model_l.find("pmos") != std::string::npos;
      d.is_nmos = model_l.find("nmos") != std::string::npos;
      if (!d.is_pmos && !d.is_nmos) {
        throw std::runtime_error(
            "cell_metrics: MOSFET '" + d.name + "' in .SUBCKT '" + cell.name +
            "' has model '" + d.model + "' that is neither nmos nor pmos");
      }
      bool has_nfin = false;
      for (size_t i = 6; i < toks.size(); ++i) {
        const std::string tok = toks[i];
        const auto eq = tok.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = cm_lower(tok.substr(0, eq));
        const std::string val = tok.substr(eq + 1);

        try {
          if (key == "w") d.width = cm_parse_spice_number(val);
          else if (key == "l") d.length = cm_parse_spice_number(val);

          else if (key == "m") { if (!has_nfin) d.mult = std::stod(val); }
          else if (key == "nfin") { d.mult = std::stod(val); has_nfin = true; }
        } catch (const std::exception& e) {
          throw std::runtime_error(
              "cell_metrics: cannot parse parameter '" + tok + "' of MOSFET '" +
              d.name + "' in .SUBCKT '" + cell.name + "': " + e.what());
        }
      }

      if (!(d.width > 0.0 && d.length > 0.0)) {
        throw std::runtime_error(
            "cell_metrics: MOSFET '" + d.name + "' in .SUBCKT '" + cell.name +
            "' has no usable w/l (w=" + std::to_string(d.width) +
            " l=" + std::to_string(d.length) + ")");
      }
      cell.devices.push_back(std::move(d));
    }
  };

  while (std::getline(iss, line)) {
    const std::string t = cm_trim(line);
    if (t.empty() || t[0] == '*') continue;
    if (!current.empty() && !t.empty() && t[0] == '+') {
      current += " " + cm_trim(t.substr(1));
      continue;
    }
    if (!current.empty()) {
      process_stmt(current);
      current.clear();
    }
    current = t;
  }
  if (!current.empty()) {
    process_stmt(current);
  }

  if (in_subckt) {
    throw std::runtime_error(
        "cell_metrics: subckt '" + cell.name +
        "' reached end of file without a closing .ENDS (truncated CDL)");
  }
  return result;
}

inline void enumerate_paths_dfs_metrics(
    const std::string& cur,
    const std::string& target,
    const CellData& cell,
    bool pmos,
    std::unordered_set<std::string>& visited_nodes,
    std::unordered_set<std::string>& used_devs,
    int depth,
    double resistance_proxy,
    PathMetrics& metrics) {
  if (cur == target) {
    ++metrics.path_count;
    metrics.max_depth = std::max(metrics.max_depth, depth);
    metrics.max_resistance_proxy = std::max(metrics.max_resistance_proxy, resistance_proxy);
    return;
  }
  visited_nodes.insert(cur);
  for (const auto& d : cell.devices) {
    if ((pmos != d.is_pmos) || used_devs.count(d.name) > 0) continue;
    std::string next;
    bool touches = false;
    if (d.drain == cur) {
      next = d.source;
      touches = true;
    } else if (d.source == cur) {
      next = d.drain;
      touches = true;
    }
    if (!touches || visited_nodes.count(next) > 0) continue;
    used_devs.insert(d.name);
    enumerate_paths_dfs_metrics(
        next, target, cell, pmos, visited_nodes, used_devs, depth + 1,
        resistance_proxy + ((d.width > 0.0) ? ((d.length * d.mult) / d.width) : 0.0),
        metrics);
    used_devs.erase(d.name);
  }
  visited_nodes.erase(cur);
}

inline PathMetrics path_metrics(const CellData& cell, const std::string& start,
                                const std::string& target, bool pmos) {
  std::unordered_set<std::string> visited_nodes;
  std::unordered_set<std::string> used_devs;
  PathMetrics metrics;
  enumerate_paths_dfs_metrics(start, target, cell, pmos, visited_nodes, used_devs, 0, 0.0, metrics);
  return metrics;
}

inline PinInfo infer_pins(const CellData& cell) {
  std::unordered_set<std::string> gate_nodes;
  std::unordered_set<std::string> cond_nodes;
  for (const auto& d : cell.devices) {
    if (!cm_is_supply_name(d.gate)) gate_nodes.insert(d.gate);
    if (!cm_is_supply_name(d.drain)) cond_nodes.insert(d.drain);
    if (!cm_is_supply_name(d.source)) cond_nodes.insert(d.source);
  }
  PinInfo info;
  for (const auto& pin : cell.pins) {
    if (cm_is_supply_name(pin)) continue;
    const bool in_gate = gate_nodes.count(pin) > 0;
    const bool in_cond = cond_nodes.count(pin) > 0;
    const auto pu = path_metrics(cell, pin, "VDD", true);
    const auto pd = path_metrics(cell, pin, "VSS", false);
    const bool drives_stage = (pu.path_count > 0) || (pd.path_count > 0);
    if (drives_stage) {
      info.output_pins.push_back(pin);
    } else if (in_gate && !in_cond) {
      info.input_pins.push_back(pin);
    } else if (!in_gate && in_cond) {
      info.output_pins.push_back(pin);
    } else {
      throw std::runtime_error("ambiguous signal pin role for `" + pin + "` in " + cell.name);
    }
  }
  std::sort(info.input_pins.begin(), info.input_pins.end());
  std::sort(info.output_pins.begin(), info.output_pins.end());
  return info;
}

inline std::unordered_set<std::string> stage_reachable_gate_nets(const CellData& cell,
                                                                  const std::string& start) {
  std::unordered_set<std::string> gate_nets;
  std::vector<std::string> stack = {start};
  std::unordered_set<std::string> visited;
  while (!stack.empty()) {
    const std::string cur = stack.back();
    stack.pop_back();
    if (!visited.insert(cur).second) continue;
    for (const auto& d : cell.devices) {
      bool touches = false;
      std::string next;
      if (d.drain == cur) { touches = true; next = d.source; }
      else if (d.source == cur) { touches = true; next = d.drain; }
      if (!touches) continue;
      if (!cm_is_supply_name(d.gate)) gate_nets.insert(d.gate);
      if (!cm_is_supply_name(next) && visited.count(next) == 0) stack.push_back(next);
    }
  }
  gate_nets.erase(start);
  return gate_nets;
}

inline std::unordered_set<std::string> stage_output_candidates(const CellData& cell,
                                                                const PinInfo& pins) {
  std::unordered_set<std::string> candidates(pins.output_pins.begin(), pins.output_pins.end());
  std::unordered_set<std::string> gate_nodes;
  std::unordered_set<std::string> cond_nodes;
  for (const auto& d : cell.devices) {
    if (!cm_is_supply_name(d.gate)) gate_nodes.insert(d.gate);
    if (!cm_is_supply_name(d.drain)) cond_nodes.insert(d.drain);
    if (!cm_is_supply_name(d.source)) cond_nodes.insert(d.source);
  }
  for (const auto& net : gate_nodes) {
    if (cond_nodes.count(net) > 0) candidates.insert(net);
  }
  return candidates;
}

inline std::unordered_map<std::string, StageMetric> build_stage_metrics(
    const CellData& cell, const PinInfo& pins, double diff_cap_ratio = 1.0) {
  std::unordered_map<std::string, StageMetric> metrics;
  const auto candidates = stage_output_candidates(cell, pins);
  for (const auto& node : candidates) {
    const auto pu = path_metrics(cell, node, "VDD", true);
    const auto pd = path_metrics(cell, node, "VSS", false);
    if (pu.path_count == 0 && pd.path_count == 0) continue;
    auto gate_nets = stage_reachable_gate_nets(cell, node);
    StageMetric m;
    m.pu_path_count = pu.path_count;
    m.pd_path_count = pd.path_count;
    m.local_pu_resistance_proxy_max = pu.max_resistance_proxy;
    m.local_pd_resistance_proxy_max = pd.max_resistance_proxy;
    for (const auto& d : cell.devices) {
      if (d.drain == node || d.source == node) {
        m.local_capacitance_proxy += diff_cap_ratio * d.width * d.length * d.mult;
      }
      if (d.gate == node) {
        m.local_capacitance_proxy += d.width * d.length * d.mult;
      }
    }
    m.input_nets.assign(gate_nets.begin(), gate_nets.end());
    std::sort(m.input_nets.begin(), m.input_nets.end());
    metrics[node] = std::move(m);
  }
  return metrics;
}

inline int stage_count_for_node_impl(
    const std::string& node,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    std::unordered_map<std::string, int>& memo,
    std::unordered_set<std::string>& active) {
  const auto mit = memo.find(node);
  if (mit != memo.end()) return mit->second;
  const auto sit = stage_metrics.find(node);
  if (sit == stage_metrics.end()) {
    throw std::runtime_error("stage_count requested for non-stage node: " + node);
  }
  if (!active.insert(node).second) {
    throw std::runtime_error("combinational cycle detected during stage-count evaluation at " + node);
  }
  int best_pred = 0;
  for (const auto& pred : sit->second.input_nets) {
    if (stage_metrics.count(pred) == 0) continue;
    best_pred = std::max(best_pred, stage_count_for_node_impl(pred, stage_metrics, memo, active));
  }
  active.erase(node);
  const int count = best_pred + 1;
  memo[node] = count;
  return count;
}

inline int stage_count_for_node(
    const std::string& node,
    const std::unordered_map<std::string, StageMetric>& stage_metrics) {
  std::unordered_map<std::string, int> memo;
  std::unordered_set<std::string> active;
  return stage_count_for_node_impl(node, stage_metrics, memo, active);
}

inline bool device_is_on(const Device& d, const std::unordered_map<std::string, bool>& values) {
  const auto it = values.find(d.gate);
  if (it == values.end()) throw std::runtime_error("missing input assignment for " + d.gate);
  return d.is_nmos ? it->second : !it->second;
}

inline bool evaluate_stage_value(
    const std::string& node,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& primary_values,
    std::unordered_map<std::string, bool>& memo,
    std::unordered_set<std::string>& active);

inline bool device_is_on_dynamic(
    const Device& d,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& primary_values,
    std::unordered_map<std::string, bool>& memo,
    std::unordered_set<std::string>& active) {
  const bool gate_value = evaluate_stage_value(d.gate, cell, pins, stage_metrics, primary_values, memo, active);
  return d.is_nmos ? gate_value : !gate_value;
}

inline bool is_connected_under_assignment_dynamic(
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::string& start,
    const std::string& target,
    const std::unordered_map<std::string, bool>& primary_values,
    std::unordered_map<std::string, bool>& memo,
    std::unordered_set<std::string>& active,
    bool pmos) {
  std::vector<std::string> stack = {start};
  std::unordered_set<std::string> visited;
  while (!stack.empty()) {
    std::string cur = stack.back();
    stack.pop_back();
    if (cur == target) return true;
    if (!visited.insert(cur).second) continue;
    for (const auto& d : cell.devices) {
      if (pmos != d.is_pmos) continue;
      bool touches = false;
      std::string next_a, next_b;
      if (d.drain == cur) { touches = true; next_a = d.source; }
      if (d.source == cur) { touches = true; next_b = d.drain; }
      if (!touches) continue;
      if (!device_is_on_dynamic(d, cell, pins, stage_metrics, primary_values, memo, active)) continue;
      if (!next_a.empty() && visited.count(next_a) == 0) stack.push_back(next_a);
      if (!next_b.empty() && visited.count(next_b) == 0) stack.push_back(next_b);
    }
  }
  return false;
}

inline bool evaluate_stage_value(
    const std::string& node,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& primary_values,
    std::unordered_map<std::string, bool>& memo,
    std::unordered_set<std::string>& active) {
  const auto pit = primary_values.find(node);
  if (pit != primary_values.end()) return pit->second;
  const auto mit = memo.find(node);
  if (mit != memo.end()) return mit->second;
  if (stage_metrics.count(node) == 0) {
    throw std::runtime_error("missing input assignment for " + node);
  }
  if (!active.insert(node).second) {
    throw std::runtime_error("combinational cycle detected during logic evaluation at " + node);
  }
  const bool to_vdd = is_connected_under_assignment_dynamic(
      cell, pins, stage_metrics, node, "VDD", primary_values, memo, active, true);
  const bool to_vss = is_connected_under_assignment_dynamic(
      cell, pins, stage_metrics, node, "VSS", primary_values, memo, active, false);
  active.erase(node);
  if (to_vdd == to_vss) {
    throw std::runtime_error("non-static or ambiguous CMOS evaluation for output " + node);
  }
  memo[node] = to_vdd;
  return to_vdd;
}

inline std::optional<bool> stage_value_for_assignment(
    const std::string& node,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& primary_values) {
  if (stage_metrics.count(node) == 0 && primary_values.count(node) == 0) return std::nullopt;
  std::unordered_map<std::string, bool> memo;
  std::unordered_set<std::string> active;
  return evaluate_stage_value(node, cell, pins, stage_metrics, primary_values, memo, active);
}

inline std::optional<PathProxyRange> active_path_proxy_range_under_assignment(
    const std::string& start,
    const std::string& target,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& values,
    bool pmos,
    const std::function<double(const Device&)>& device_value) {
  std::unordered_map<std::string, bool> memo;
  std::unordered_set<std::string> active_stage_eval;
  std::unordered_set<std::string> visited_nodes;
  std::unordered_set<std::string> used_devs;
  PathProxyRange range;
  range.min_value = std::numeric_limits<double>::infinity();
  range.max_value = -1.0;
  bool found = false;

  std::function<void(const std::string&, double)> dfs = [&](const std::string& cur, double acc) {
    if (cur == target) {
      found = true;
      range.min_value = std::min(range.min_value, acc);
      range.max_value = std::max(range.max_value, acc);
      return;
    }
    visited_nodes.insert(cur);
    for (const auto& d : cell.devices) {
      if (pmos != d.is_pmos || used_devs.count(d.name) > 0) continue;
      std::string next;
      bool touches = false;
      if (d.drain == cur) { next = d.source; touches = true; }
      else if (d.source == cur) { next = d.drain; touches = true; }
      if (!touches || visited_nodes.count(next) > 0) continue;
      if (!device_is_on_dynamic(d, cell, pins, stage_metrics, values, memo, active_stage_eval)) continue;
      used_devs.insert(d.name);
      dfs(next, acc + device_value(d));
      used_devs.erase(d.name);
    }
    visited_nodes.erase(cur);
  };

  dfs(start, 0.0);
  if (!found) return std::nullopt;
  return range;
}

inline std::optional<PathProxyRange> sensitized_transition_proxy_range(
    const std::string& node,
    const std::string& source_input,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::unordered_map<std::string, bool>& before_values,
    const std::unordered_map<std::string, bool>& after_values,
    const std::function<double(const StageMetric&, bool, const Device&)>& local_value) {
  std::unordered_map<std::string, PathProxyRange> memo;
  std::unordered_set<std::string> active;

  std::function<std::optional<PathProxyRange>(const std::string&)> solve =
      [&](const std::string& cur) -> std::optional<PathProxyRange> {
    const auto mit = memo.find(cur);
    if (mit != memo.end()) return mit->second;
    const auto sit = stage_metrics.find(cur);
    if (sit == stage_metrics.end()) return std::nullopt;
    if (!active.insert(cur).second) {
      throw std::runtime_error("combinational cycle detected in sensitized path at " + cur);
    }
    const auto before_val = stage_value_for_assignment(cur, cell, pins, stage_metrics, before_values);
    const auto after_val = stage_value_for_assignment(cur, cell, pins, stage_metrics, after_values);
    if (!before_val.has_value() || !after_val.has_value() || before_val.value() == after_val.value()) {
      active.erase(cur);
      return std::nullopt;
    }
    double min_pred = std::numeric_limits<double>::infinity();
    double max_pred = -1.0;
    bool has_pred = false;
    for (const auto& pred : sit->second.input_nets) {
      if (pred == source_input) {
        has_pred = true;
        min_pred = std::min(min_pred, 0.0);
        max_pred = std::max(max_pred, 0.0);
      } else {
        const auto pred_range = solve(pred);
        if (pred_range.has_value()) {
          has_pred = true;
          min_pred = std::min(min_pred, pred_range->min_value);
          max_pred = std::max(max_pred, pred_range->max_value);
        }
      }
    }
    active.erase(cur);
    if (!has_pred) return std::nullopt;
    const auto local_range_opt = active_path_proxy_range_under_assignment(
        cur,
        after_val.value() ? "VDD" : "VSS",
        cell, pins, stage_metrics, after_values, after_val.value(),
        [&](const Device& d) { return local_value(sit->second, after_val.value(), d); });
    if (!local_range_opt.has_value()) return std::nullopt;
    PathProxyRange range;
    range.min_value = min_pred + local_range_opt->min_value;
    range.max_value = max_pred + local_range_opt->max_value;
    memo[cur] = range;
    return range;
  };

  return solve(node);
}

inline PathProxyRange sensitized_output_proxy_range(
    const std::string& output_pin,
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::function<double(const StageMetric&, bool, const Device&)>& local_value) {
  double global_min = std::numeric_limits<double>::infinity();
  double global_max = -1.0;
  const int n_inputs = static_cast<int>(pins.input_pins.size());
  for (const auto& source_input : pins.input_pins) {
    const auto src_it = std::find(pins.input_pins.begin(), pins.input_pins.end(), source_input);
    const int src_idx = static_cast<int>(std::distance(pins.input_pins.begin(), src_it));
    for (int mask = 0; mask < (1 << n_inputs); ++mask) {
      std::unordered_map<std::string, bool> before_values;
      std::unordered_map<std::string, bool> after_values;
      for (int i = 0; i < n_inputs; ++i) {
        const bool bit = ((mask >> (n_inputs - 1 - i)) & 1) != 0;
        before_values[pins.input_pins[i]] = bit;
        after_values[pins.input_pins[i]] = bit;
      }
      before_values[pins.input_pins[src_idx]] = false;
      after_values[pins.input_pins[src_idx]] = true;
      const auto rise_range = sensitized_transition_proxy_range(
          output_pin, source_input, cell, pins, stage_metrics, before_values, after_values, local_value);
      if (rise_range.has_value()) {
        global_min = std::min(global_min, rise_range->min_value);
        global_max = std::max(global_max, rise_range->max_value);
      }
      const auto fall_range = sensitized_transition_proxy_range(
          output_pin, source_input, cell, pins, stage_metrics, after_values, before_values, local_value);
      if (fall_range.has_value()) {
        global_min = std::min(global_min, fall_range->min_value);
        global_max = std::max(global_max, fall_range->max_value);
      }
    }
  }
  if (!std::isfinite(global_min) || global_max < 0.0) {
    throw std::runtime_error("no sensitized input-to-output path found for output " + output_pin);
  }
  return {global_min, global_max};
}

struct Implicant {
  std::string bits;
  std::set<int> covered;
  bool used = false;
};

inline bool differ_by_one_bit(const std::string& a, const std::string& b, std::string& merged) {
  int diff = 0;
  merged = a;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == b[i]) continue;
    if (a[i] == '-' || b[i] == '-') return false;
    ++diff;
    merged[i] = '-';
  }
  return diff == 1;
}

inline std::vector<Implicant> prime_implicants_from_minterms(const std::vector<int>& minterms, int n_inputs) {
  std::vector<Implicant> current;
  for (int m : minterms) {
    std::string bits;
    for (int i = n_inputs - 1; i >= 0; --i) bits.push_back(((m >> i) & 1) ? '1' : '0');
    current.push_back({bits, {m}, false});
  }
  std::vector<Implicant> primes;
  while (!current.empty()) {
    std::vector<Implicant> next;
    std::set<std::string> seen;
    for (auto& imp : current) imp.used = false;
    for (size_t i = 0; i < current.size(); ++i) {
      for (size_t j = i + 1; j < current.size(); ++j) {
        std::string merged;
        if (!differ_by_one_bit(current[i].bits, current[j].bits, merged)) continue;
        current[i].used = true;
        current[j].used = true;
        std::set<int> cov = current[i].covered;
        cov.insert(current[j].covered.begin(), current[j].covered.end());
        if (seen.insert(merged).second) next.push_back({merged, cov, false});
      }
    }
    for (const auto& imp : current) {
      if (!imp.used) primes.push_back(imp);
    }
    current = next;
  }
  return primes;
}

inline bool implicant_covers(const Implicant& imp, int minterm, int n_inputs) {
  for (int i = 0; i < n_inputs; ++i) {
    const char want = imp.bits[i];
    if (want == '-') continue;
    const bool bit = ((minterm >> (n_inputs - 1 - i)) & 1) != 0;
    if ((want == '1') != bit) return false;
  }
  return true;
}

inline std::string implicant_to_expr(const Implicant& imp, const std::vector<std::string>& inputs) {
  std::vector<std::string> lits;
  for (size_t i = 0; i < imp.bits.size(); ++i) {
    if (imp.bits[i] == '-') continue;
    lits.push_back(imp.bits[i] == '1' ? inputs[i] : "!" + inputs[i]);
  }
  if (lits.empty()) return "1";
  if (lits.size() == 1) return lits[0];
  std::ostringstream oss;
  for (size_t i = 0; i < lits.size(); ++i) {
    if (i) oss << " * ";
    oss << lits[i];
  }
  return "(" + oss.str() + ")";
}

inline std::string simplify_truth_table(const std::vector<int>& ones, int n_inputs,
                                         const std::vector<std::string>& inputs) {
  if (ones.empty()) return "0";
  if (static_cast<int>(ones.size()) == (1 << n_inputs)) return "1";
  auto primes = prime_implicants_from_minterms(ones, n_inputs);
  std::vector<int> minterms = ones;
  std::vector<Implicant> essential;
  std::set<int> covered;
  for (int m : minterms) {
    int cover_count = 0;
    int cover_idx = -1;
    for (size_t i = 0; i < primes.size(); ++i) {
      if (implicant_covers(primes[i], m, n_inputs)) {
        ++cover_count;
        cover_idx = static_cast<int>(i);
      }
    }
    if (cover_count == 1 && cover_idx >= 0) {
      essential.push_back(primes[cover_idx]);
    }
  }
  std::sort(essential.begin(), essential.end(), [](const auto& a, const auto& b) { return a.bits < b.bits; });
  essential.erase(std::unique(essential.begin(), essential.end(),
                              [](const auto& a, const auto& b) { return a.bits == b.bits; }),
                  essential.end());
  for (const auto& imp : essential) {
    for (int m : minterms) {
      if (implicant_covers(imp, m, n_inputs)) covered.insert(m);
    }
  }
  std::vector<Implicant> chosen = essential;
  while (covered.size() < minterms.size()) {
    int best_idx = -1;
    int best_gain = -1;
    for (size_t i = 0; i < primes.size(); ++i) {
      int gain = 0;
      for (int m : minterms) {
        if (covered.count(m) == 0 && implicant_covers(primes[i], m, n_inputs)) ++gain;
      }
      if (gain > best_gain) { best_gain = gain; best_idx = static_cast<int>(i); }
    }
    if (best_idx < 0 || best_gain <= 0) break;
    chosen.push_back(primes[best_idx]);
    for (int m : minterms) {
      if (implicant_covers(primes[best_idx], m, n_inputs)) covered.insert(m);
    }
  }
  std::vector<std::string> terms;
  for (const auto& imp : chosen) terms.push_back(implicant_to_expr(imp, inputs));
  std::sort(terms.begin(), terms.end());
  terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
  if (terms.size() == 1) return terms[0];
  std::ostringstream oss;
  for (size_t i = 0; i < terms.size(); ++i) {
    if (i) oss << " + ";
    oss << terms[i];
  }
  return oss.str();
}

inline std::string logic_expr_for_output(
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::string& output_pin) {
  const int n_inputs = static_cast<int>(pins.input_pins.size());
  std::vector<int> ones;
  for (int mask = 0; mask < (1 << n_inputs); ++mask) {
    std::unordered_map<std::string, bool> values;
    for (int i = 0; i < n_inputs; ++i) {
      const bool bit = ((mask >> (n_inputs - 1 - i)) & 1) != 0;
      values[pins.input_pins[i]] = bit;
    }
    std::unordered_map<std::string, bool> memo;
    std::unordered_set<std::string> active;
    if (evaluate_stage_value(output_pin, cell, pins, stage_metrics, values, memo, active)) ones.push_back(mask);
  }
  return simplify_truth_table(ones, n_inputs, pins.input_pins);
}

inline std::string canonical_bits_cm(std::string bits, int n_inputs) {
  if (n_inputs <= 1 || bits.size() <= 1) return bits;
  std::vector<std::vector<int>> assignments;
  assignments.reserve(static_cast<size_t>(1) << n_inputs);
  std::map<std::vector<int>, int> idx_by_assignment;
  for (int mask = 0; mask < (1 << n_inputs); ++mask) {
    std::vector<int> assignment;
    assignment.reserve(n_inputs);
    for (int i = 0; i < n_inputs; ++i) {
      assignment.push_back(((mask >> (n_inputs - 1 - i)) & 1) != 0 ? 1 : 0);
    }
    idx_by_assignment[assignment] = mask;
    assignments.push_back(std::move(assignment));
  }
  std::vector<int> perm(n_inputs);
  for (int i = 0; i < n_inputs; ++i) perm[i] = i;
  std::string best = bits;
  do {
    std::string permuted;
    permuted.reserve(bits.size());
    for (const auto& combo : assignments) {
      std::vector<int> base(n_inputs, 0);
      for (int new_pos = 0; new_pos < n_inputs; ++new_pos) {
        base[perm[new_pos]] = combo[new_pos];
      }
      permuted.push_back(bits.at(static_cast<size_t>(idx_by_assignment.at(base))));
    }
    if (permuted < best) best = std::move(permuted);
  } while (std::next_permutation(perm.begin(), perm.end()));
  return best;
}

inline std::string bit_string_to_hex_cm(const std::string& bits) {
  if (bits.empty()) return "0";
  const int hex_width = std::max(1, static_cast<int>((bits.size() + 3) / 4));
  std::string padded = bits;
  const size_t rem = padded.size() % 4;
  if (rem != 0) padded = std::string(4 - rem, '0') + padded;
  static const char* kHex = "0123456789ABCDEF";
  std::string hex;
  hex.reserve(padded.size() / 4);
  for (size_t i = 0; i < padded.size(); i += 4) {
    int value = 0;
    for (size_t j = i; j < i + 4; ++j) value = (value << 1) | (padded[j] == '1' ? 1 : 0);
    hex.push_back(kHex[value]);
  }
  if (static_cast<int>(hex.size()) < hex_width) {
    hex = std::string(static_cast<size_t>(hex_width - static_cast<int>(hex.size())), '0') + hex;
  }
  return hex;
}

inline std::string canonical_function_for_output(
    const CellData& cell,
    const PinInfo& pins,
    const std::unordered_map<std::string, StageMetric>& stage_metrics,
    const std::string& output_pin) {
  const int n_inputs = static_cast<int>(pins.input_pins.size());
  if (n_inputs == 0) {
    std::unordered_map<std::string, bool> values;
    std::unordered_map<std::string, bool> memo;
    std::unordered_set<std::string> active;
    const bool value = evaluate_stage_value(output_pin, cell, pins, stage_metrics, values, memo, active);
    return value ? "1#0x1" : "1#0x0";
  }
  std::string bits;
  bits.reserve(static_cast<size_t>(1) << n_inputs);
  for (int mask = 0; mask < (1 << n_inputs); ++mask) {
    std::unordered_map<std::string, bool> values;
    for (int i = 0; i < n_inputs; ++i) {
      const bool bit = ((mask >> (n_inputs - 1 - i)) & 1) != 0;
      values[pins.input_pins[i]] = bit;
    }
    std::unordered_map<std::string, bool> memo;
    std::unordered_set<std::string> active;
    bits.push_back(evaluate_stage_value(output_pin, cell, pins, stage_metrics, values, memo, active) ? '1' : '0');
  }
  bits = canonical_bits_cm(std::move(bits), n_inputs);
  const int bit_count = static_cast<int>(bits.size());
  std::ostringstream oss;
  oss << bit_count << "#0x" << bit_string_to_hex_cm(bits);
  return oss.str();
}

inline CellMetricResult compute_cell_metrics(const CellData& cell, double diff_cap_ratio = 1.0,
                                              double wmax_nmos = 0.0, double wmax_pmos = 0.0) {
  CellMetricResult result;
  result.pins = infer_pins(cell);

  auto folded_count = [](double w, double mult, double wmax) -> double {
    if (wmax <= 0.0 || w <= 0.0) return mult;
    const double n = std::ceil(w / wmax);
    return 1.0 + 0.5 * (n - 1.0);
  };

  auto cpp_count = [](double w, double wmax) -> int {
    if (wmax <= 0.0 || w <= 0.0) return 1;
    return static_cast<int>(std::ceil(w / wmax - 1e-9));
  };

  for (const auto& d : cell.devices) {
    if (d.is_nmos) {
      result.transistor_count_nmos += folded_count(d.width, d.mult, wmax_nmos);
      result.cpp_nmos += cpp_count(d.width, wmax_nmos);
    }
    if (d.is_pmos) {
      result.transistor_count_pmos += folded_count(d.width, d.mult, wmax_pmos);
      result.cpp_pmos += cpp_count(d.width, wmax_pmos);
    }
  }
  result.cpp_min = std::max(result.cpp_nmos, result.cpp_pmos);

  const auto stage_metrics = build_stage_metrics(cell, result.pins, diff_cap_ratio);
  for (const auto& op : result.pins.output_pins) {
    const auto pu = path_metrics(cell, op, "VDD", true);
    const auto pd = path_metrics(cell, op, "VSS", false);
    const int sc = stage_count_for_node(op, stage_metrics);
    const std::string expr = logic_expr_for_output(cell, result.pins, stage_metrics, op);
    const std::string canonical = canonical_function_for_output(cell, result.pins, stage_metrics, op);
    const auto resistance_range = sensitized_output_proxy_range(
        op, cell, result.pins, stage_metrics,
        [](const StageMetric&, bool, const Device& d) {
          return (d.length * d.mult) / d.width;
        });
    const auto capacitance_range = sensitized_output_proxy_range(
        op, cell, result.pins, stage_metrics,
        [](const StageMetric&, bool, const Device& d) { return d.width * d.length * d.mult; });

    OutputMetric om;
    om.logic_function_expr_internal = expr;
    om.logic_function_canonical = canonical;
    om.stage_count = sc;
    om.max_pu_stack_depth = pu.max_depth;
    om.max_pd_stack_depth = pd.max_depth;
    om.input_to_output_resistance_proxy_min = resistance_range.min_value;
    om.input_to_output_resistance_proxy_max = resistance_range.max_value;
    om.input_to_output_capacitance_proxy_min = capacitance_range.min_value;
    om.input_to_output_capacitance_proxy_max = capacitance_range.max_value;
    result.outputs[op] = std::move(om);
  }
  return result;
}

}
