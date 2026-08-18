

#include "runtime_logger.hpp"
#include "gate_netlist_parser.hpp"
#include "netlist_db_writer.hpp"

#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "kernel/yosys.h"
#include "passes/techmap/libparse.h"

#ifdef LOGIC_MINOR_USE_OPENDB
#include "opendb/db.h"
#include "opendb/defin.h"
#endif

static std::string canonicalize_name(std::string s) {
  s = trim(s);
  if (!s.empty() && s.front() == '\\') s.erase(s.begin());
  return s;
}

static std::string sigbit_to_net_name(const Yosys::RTLIL::SigBit& bit) {
  if (bit.wire == nullptr) {
    if (bit.data == Yosys::RTLIL::State::S0) return "$const$0";
    if (bit.data == Yosys::RTLIL::State::S1) return "$const$1";
    return "$const$x";
  }
  std::string base = canonicalize_name(Yosys::RTLIL::unescape_id(bit.wire->name));
  if (bit.wire->width == 1) return base;
  int index = bit.wire->upto ? bit.wire->start_offset - bit.offset : bit.wire->start_offset + bit.offset;
  return base + "[" + std::to_string(index) + "]";
}

struct DefInfo {
  double dbu_per_um = 0.0;
  float die_llx = 0.0f, die_lly = 0.0f, die_urx = 0.0f, die_ury = 0.0f;
};

static bool is_def_comment(const std::string& line) {
  auto first = line.find_first_not_of(" \t");
  return first != std::string::npos && line[first] == '#';
}

static bool parse_def_coords(const std::string& def_file,
                      std::unordered_map<std::string, std::pair<float, float>>& out,
                      DefInfo& info) {
  if (def_file.empty()) return false;
  std::ifstream ifs(def_file);
  if (!ifs.is_open()) throw std::runtime_error("failed to open DEF: " + def_file);

  std::vector<std::string> all_lines;
  {
    std::string line;
    while (std::getline(ifs, line)) all_lines.push_back(std::move(line));
  }

  double dbu = 0.0;
  bool have_units = false, have_diearea = false;
  double dllx = 0.0, dlly = 0.0, durx = 0.0, dury = 0.0;
  int comp_start = -1, comp_end = -1;
  for (int i = 0; i < static_cast<int>(all_lines.size()); ++i) {
    if (is_def_comment(all_lines[i])) continue;
    auto uline = upper(all_lines[i]);
    if (!have_units && uline.find("UNITS DISTANCE MICRONS") != std::string::npos) {
      std::stringstream ss(all_lines[i]);
      std::string t0, t1, t2;
      double val = 0.0;

      if (!(ss >> t0 >> t1 >> t2 >> val) || val <= 0.0)
        throw std::runtime_error("DEF 'UNITS DISTANCE MICRONS' has no positive value: " + def_file);
      dbu = val;
      have_units = true;
    }
    if (!have_diearea) {
      auto dpos = uline.find("DIEAREA");
      if (dpos != std::string::npos) {

        std::string stmt = all_lines[i].substr(dpos + 7);
        for (int j = i + 1;
             stmt.find(';') == std::string::npos && j < static_cast<int>(all_lines.size()); ++j) {
          stmt += " ";
          stmt += all_lines[j];
        }
        auto semi = stmt.find(';');
        if (semi != std::string::npos) stmt.resize(semi);
        std::vector<double> xs, ys;
        for (size_t p = 0; (p = stmt.find('(', p)) != std::string::npos;) {
          auto q = stmt.find(')', p);
          if (q == std::string::npos) break;
          std::stringstream ps(stmt.substr(p + 1, q - p - 1));
          double px = 0.0, py = 0.0;
          if (ps >> px >> py) { xs.push_back(px); ys.push_back(py); }
          p = q + 1;
        }
        if (xs.size() < 2)
          throw std::runtime_error("DEF DIEAREA does not declare at least two points: " + def_file);
        dllx = *std::min_element(xs.begin(), xs.end());
        durx = *std::max_element(xs.begin(), xs.end());
        dlly = *std::min_element(ys.begin(), ys.end());
        dury = *std::max_element(ys.begin(), ys.end());
        have_diearea = true;
      }
    }
    if (comp_start < 0 && uline.find("COMPONENTS") != std::string::npos) comp_start = i + 1;
    if (comp_start >= 0 && uline.find("END COMPONENTS") != std::string::npos) { comp_end = i; break; }
  }

  if (!have_units)
    throw std::runtime_error("DEF has no 'UNITS DISTANCE MICRONS' statement: " + def_file);
  if (!have_diearea)
    throw std::runtime_error("DEF has no DIEAREA statement: " + def_file);
  info.dbu_per_um = dbu;
  info.die_llx = static_cast<float>(dllx / dbu);
  info.die_lly = static_cast<float>(dlly / dbu);
  info.die_urx = static_cast<float>(durx / dbu);
  info.die_ury = static_cast<float>(dury / dbu);
  if (comp_start < 0 || comp_end < 0)
    throw std::runtime_error("DEF has no COMPONENTS section: " + def_file);

  const int nlines = comp_end - comp_start;
  struct CoordEntry { std::string name; float x, y; bool valid; };
  std::vector<CoordEntry> entries(nlines);

  #pragma omp parallel for schedule(static)
  for (int i = 0; i < nlines; ++i) {
    entries[i].valid = false;
    const std::string& t = all_lines[comp_start + i];
    if (t.empty()) continue;

    size_t first = t.find_first_not_of(" \t");
    if (first == std::string::npos || t[first] != '-') continue;
    std::stringstream ss(t);
    std::string dash, inst, master;
    ss >> dash >> inst >> master;
    if (inst.empty()) continue;
    auto p = t.find("PLACED");
    if (p == std::string::npos) p = t.find("FIXED");
    if (p == std::string::npos) continue;
    auto lp = t.find('(', p);
    auto rp = (lp == std::string::npos) ? std::string::npos : t.find(')', lp);
    if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1) continue;
    std::string xy = t.substr(lp + 1, rp - lp - 1);
    std::stringstream xys(xy);
    double x = 0.0, y = 0.0;
    if (!(xys >> x >> y)) continue;
    entries[i].name = canonicalize_name(inst);
    entries[i].x = static_cast<float>(x / dbu);
    entries[i].y = static_cast<float>(y / dbu);
    entries[i].valid = true;
  }

  for (auto& e : entries) {
    if (e.valid) out[std::move(e.name)] = {e.x, e.y};
  }

  if (out.empty())
    throw std::runtime_error("DEF COMPONENTS section yielded no PLACED/FIXED coordinates: " + def_file);
  return true;
}

using PinDirMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

static const std::unordered_map<std::string, std::string> kNoPinDirs;

static void add_bundle_member_dirs(PinDirMap& dirs,
                                   const std::vector<std::string>& lib_files) {
  auto lower = [](std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  for (const auto& lib : lib_files) {
    std::ifstream ifs(lib);
    if (!ifs.is_open()) continue;
    Yosys::LibertyMergedCells merged;
    Yosys::LibertyParser parser(ifs);
    merged.merge(parser);
    for (auto* cell : merged.cells) {
      if (!cell || cell->args.size() != 1) continue;
      auto cname = canonicalize_name(cell->args[0]);
      for (const auto* node : cell->children) {
        if (!node || node->id != "bundle") continue;
        const auto* members = node->find("members");
        const auto* bdir    = node->find("direction");
        if (!members || !bdir) continue;
        std::vector<std::string> member_names = members->args;
        if (member_names.empty() && !members->value.empty()) {
          std::stringstream ms(members->value);
          std::string tok;
          while (std::getline(ms, tok, ',')) {
            tok.erase(0, tok.find_first_not_of(" \t"));
            tok.erase(tok.find_last_not_of(" \t") + 1);
            if (!tok.empty()) member_names.push_back(tok);
          }
        }
        std::string dv = lower(bdir->value);
        std::string dir = (dv == "input") ? "in" : (dv == "output") ? "out"
                        : (dv == "inout") ? "inout" : "";
        if (dir.empty()) continue;
        for (const auto& m : member_names) {
          auto pm = canonicalize_name(m);
          dirs[cname].emplace(pm, dir);
        }
      }
    }
  }
}

static PinDirMap load_liberty_pin_dirs(const std::vector<std::string>& lib_files) {
  Yosys::yosys_setup();
  Yosys::RTLIL::Design design;
  for (const auto& lib : lib_files) {
    Yosys::run_pass("read_liberty -lib " + yosys_quote(lib), &design);
  }
  PinDirMap dirs;
  for (auto* mod : design.modules()) {
    if (!mod) continue;
    auto mname = canonicalize_name(Yosys::RTLIL::unescape_id(mod->name));
    for (auto* wire : mod->wires()) {
      if (!wire || !wire->port_id) continue;
      auto pname = canonicalize_name(Yosys::RTLIL::unescape_id(wire->name));
      if (wire->port_input && !wire->port_output) dirs[mname][pname] = "in";
      else if (wire->port_output && !wire->port_input) dirs[mname][pname] = "out";
      else dirs[mname][pname] = "inout";
    }
  }
  Yosys::yosys_shutdown();
  add_bundle_member_dirs(dirs, lib_files);
  return dirs;
}

enum class NetExprKind { NAMED, CONSTANT, UNCONNECTED };

struct FastNetName {
  NetExprKind kind = NetExprKind::NAMED;
  std::string name;
};

static FastNetName fast_path_net_name(const std::string& expr,
                                      const std::string& inst_name,
                                      const std::string& master,
                                      const std::string& port,
                                      const gate_parser::ParsedNetlist& parsed) {
  const std::string e = trim(expr);
  auto reject = [&](const std::string& what) -> FastNetName {
    throw std::runtime_error("instance '" + inst_name + "' (master '" + master + "'): pin '" +
                             port + "' is connected to " + what + " (\"" + e +
                             "\"), which the fast gate-level parser cannot expand into"
                             " individual nets; re-run with --flatten to use the Yosys path");
  };

  if (e.empty()) return {NetExprKind::UNCONNECTED, ""};
  if (e.front() == '{') return reject("a concatenation");

  const auto tick = e.find('\'');
  if (tick != std::string::npos) {

    const std::string size_txt = trim(e.substr(0, tick));
    if (!size_txt.empty() && size_txt != "1") return reject("a multi-bit constant");
    std::string value = trim(e.substr(tick + 1));
    if (value.size() >= 1 && (value[0] == 's' || value[0] == 'S')) value.erase(0, 1);
    if (value.size() < 2) return reject("an unparseable constant literal");
    value.erase(0, 1);
    value = trim(value);
    if (value.size() != 1) return reject("a multi-bit constant");

    if (value == "0") return {NetExprKind::CONSTANT, "$const$0"};
    if (value == "1") return {NetExprKind::CONSTANT, "$const$1"};
    return {NetExprKind::CONSTANT, "$const$x"};
  }

  if (e.find('[') == std::string::npos) {
    auto wit = parsed.wire_widths.find(e);
    if (wit != parsed.wire_widths.end() && wit->second > 1) return reject("a whole multi-bit wire");
    for (const auto& pd : parsed.ports) {
      if (pd.name == e && pd.width > 1) return reject("a whole multi-bit port");
    }
  }
  return {NetExprKind::NAMED, e};
}

static int run_fast_path(const std::string& netlist, const std::string& def_file,
                         const std::string& top_name, const std::vector<std::string>& lib_files,
                         const std::string& out_path) {

  std::unordered_map<std::string, std::pair<float, float>> coords;
  DefInfo def_info;
  const bool def_read = parse_def_coords(def_file, coords, def_info);

  auto master_pin_dirs = load_liberty_pin_dirs(lib_files);

  auto parsed = gate_parser::parse(netlist, top_name);

  std::string resolved_top = parsed.top_name;

  std::unordered_map<std::string, gate_parser::PortDir> port_dir_map;
  for (const auto& p : parsed.ports) {
    port_dir_map[p.name] = p.dir;
  }

  struct NetInfo {
    std::string name;
    int driver_inst = 0;
    std::string driver_pin;
    std::vector<std::pair<int, std::string>> loads;
  };

  std::vector<netlist_db_writer::WInstance> instances;
  std::unordered_map<std::string, int> inst_id_map;
  std::unordered_map<std::string, NetInfo> net_map;
  int next_id = 1;

  for (const auto& gi : parsed.instances) {
    netlist_db_writer::WInstance wi;
    wi.id = next_id;
    wi.name = gi.name;
    wi.master = gi.master;
    auto cit = coords.find(gi.name);
    if (cit != coords.end()) { wi.x = cit->second.first; wi.y = cit->second.second; }
    instances.push_back(wi);
    inst_id_map[gi.name] = next_id;
    ++next_id;

    auto mit = master_pin_dirs.find(gi.master);
    const auto& dirs = (mit != master_pin_dirs.end()) ? mit->second : kNoPinDirs;
    for (const auto& pin : gi.pins) {
      auto dir_it = dirs.find(pin.port);
      if (dir_it == dirs.end()) {
        throw std::runtime_error(
            mit == master_pin_dirs.end()
                ? ("instance '" + gi.name + "': master '" + gi.master +
                   "' is not present in any Liberty file, so the direction of pin '" +
                   pin.port + "' is unknown")
                : ("instance '" + gi.name + "' (master '" + gi.master + "'): pin '" + pin.port +
                   "' has no direction in the Liberty pin map"));
      }
      const std::string& dir = dir_it->second;
      auto full_pin = gi.name + "/" + pin.port;

      auto resolved = fast_path_net_name(pin.net, gi.name, gi.master, pin.port, parsed);
      if (resolved.kind == NetExprKind::UNCONNECTED) continue;

      auto& ninfo = net_map[resolved.name];
      ninfo.name = resolved.name;

      if (dir == "out") {
        if (ninfo.driver_inst == 0) { ninfo.driver_inst = wi.id; ninfo.driver_pin = full_pin; }
        else ninfo.loads.push_back(std::make_pair(wi.id, full_pin));
      } else {
        ninfo.loads.push_back(std::make_pair(wi.id, full_pin));
      }
    }
  }

  for (const auto& pd : parsed.ports) {

    if (pd.width == 1) {
      auto& ninfo = net_map[pd.name];
      ninfo.name = pd.name;
      if (pd.dir == gate_parser::PortDir::INPUT) {
        if (ninfo.driver_inst == 0 && ninfo.driver_pin.empty()) {
          ninfo.driver_pin = pd.name;
        }
      } else if (pd.dir == gate_parser::PortDir::OUTPUT) {
        ninfo.loads.push_back(std::make_pair(0, pd.name));
      }
    } else {
      int lo = std::min(pd.msb, pd.lsb);
      int hi = std::max(pd.msb, pd.lsb);
      for (int b = lo; b <= hi; ++b) {
        auto bit_name = pd.name + "[" + std::to_string(b) + "]";
        auto& ninfo = net_map[bit_name];
        ninfo.name = bit_name;
        if (pd.dir == gate_parser::PortDir::INPUT) {
          if (ninfo.driver_inst == 0 && ninfo.driver_pin.empty()) {
            ninfo.driver_pin = bit_name;
          }
        } else if (pd.dir == gate_parser::PortDir::OUTPUT) {
          ninfo.loads.push_back(std::make_pair(0, bit_name));
        }
      }
    }
  }

  std::vector<netlist_db_writer::WNet> nets;
  for (auto& [_, ninfo] : net_map) {
    netlist_db_writer::WNet wn;
    wn.name = ninfo.name;
    if (ninfo.driver_inst > 0 || !ninfo.driver_pin.empty()) {
      wn.pins.push_back({ninfo.driver_inst, ninfo.driver_pin, true});
    }
    for (auto& [lid, lpin] : ninfo.loads) {
      wn.pins.push_back({lid, lpin, false});
    }
    if (!wn.pins.empty()) nets.push_back(std::move(wn));
  }

  std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

  netlist_db_writer::WDieArea die{};
  if (def_read) {
    die.llx = def_info.die_llx;
    die.lly = def_info.die_lly;
    die.urx = def_info.die_urx;
    die.ury = def_info.die_ury;
    die.dbu_per_um = static_cast<float>(def_info.dbu_per_um);
  }

  uint32_t src_flags = NETLIST_DB_SRC_NETLIST;
  if (def_read) src_flags |= NETLIST_DB_SRC_DEF;

  if (!netlist_db_writer::write_db(out_path, resolved_top, instances, nets, die, src_flags)) {
    throw std::runtime_error("failed to write layout database: " + out_path);
  }

  std::cout << "[done] netlist_db=" << out_path
            << " top=" << resolved_top
            << " instances=" << instances.size()
            << " nets=" << nets.size()
            << " (fast parser)\n";
  return 0;
}

static int run_yosys_path(const std::string& netlist, const std::string& def_file,
                          const std::string& top_name, const std::vector<std::string>& lib_files,
                          const std::string& out_path, bool do_flatten) {

  std::unordered_map<std::string, std::pair<float, float>> coords;
  DefInfo def_info;
  const bool def_read = parse_def_coords(def_file, coords, def_info);

  Yosys::yosys_setup();
  Yosys::RTLIL::Design design;
  for (const auto& lib : lib_files) {
    Yosys::run_pass("read_liberty -lib " + yosys_quote(lib), &design);
  }
  Yosys::run_pass("read_verilog " + yosys_quote(netlist), &design);
  if (!top_name.empty()) Yosys::run_pass("hierarchy -top " + top_name, &design);
  else Yosys::run_pass("hierarchy -auto-top", &design);

  if (do_flatten) {
    Yosys::run_pass("flatten", &design);
    Yosys::run_pass("hierarchy -auto-top", &design);
  }

  auto* top_mod = design.top_module();
  if (!top_mod) throw std::runtime_error("failed to resolve top module");
  std::string resolved_top = canonicalize_name(Yosys::RTLIL::unescape_id(top_mod->name));

  struct NetInfo {
    std::string name;
    int driver_inst = 0;
    std::string driver_pin;
    std::vector<std::pair<int, std::string>> loads;
  };

  std::vector<netlist_db_writer::WInstance> instances;
  std::unordered_map<std::string, int> inst_id_map;
  std::unordered_map<std::string, NetInfo> net_map;
  int next_id = 1;

  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> master_pin_dirs;
  for (auto* mod : design.modules()) {
    if (!mod) continue;
    auto mname = canonicalize_name(Yosys::RTLIL::unescape_id(mod->name));
    for (auto* wire : mod->wires()) {
      if (!wire || !wire->port_id) continue;
      auto pname = canonicalize_name(Yosys::RTLIL::unescape_id(wire->name));
      if (wire->port_input && !wire->port_output) master_pin_dirs[mname][pname] = "in";
      else if (wire->port_output && !wire->port_input) master_pin_dirs[mname][pname] = "out";
      else master_pin_dirs[mname][pname] = "inout";
    }
  }
  add_bundle_member_dirs(master_pin_dirs, lib_files);

  for (auto* ycell : top_mod->cells()) {
    if (!ycell) continue;
    auto inst_name = canonicalize_name(Yosys::RTLIL::unescape_id(ycell->name));
    auto master_name = canonicalize_name(Yosys::RTLIL::unescape_id(ycell->type));
    if (inst_name.empty() || master_name.empty()) continue;

    netlist_db_writer::WInstance wi;
    wi.id = next_id;
    wi.name = inst_name;
    wi.master = master_name;
    auto cit = coords.find(inst_name);
    if (cit != coords.end()) { wi.x = cit->second.first; wi.y = cit->second.second; }
    instances.push_back(wi);
    inst_id_map[inst_name] = next_id;
    ++next_id;

    auto mit = master_pin_dirs.find(master_name);
    const auto& dirs = (mit != master_pin_dirs.end()) ? mit->second : kNoPinDirs;
    for (const auto& conn : ycell->connections()) {
      auto port_name = canonicalize_name(Yosys::RTLIL::unescape_id(conn.first));
      const auto& sig = conn.second;
      auto dir_it = dirs.find(port_name);
      if (dir_it == dirs.end()) {
        throw std::runtime_error(
            mit == master_pin_dirs.end()
                ? ("instance '" + inst_name + "': master '" + master_name +
                   "' is not present in any Liberty file, so the direction of pin '" +
                   port_name + "' is unknown")
                : ("instance '" + inst_name + "' (master '" + master_name + "'): pin '" +
                   port_name + "' has no direction in the Liberty pin map"));
      }
      const std::string& dir = dir_it->second;

      for (int i = 0; i < Yosys::GetSize(sig); i++) {
        auto term = (Yosys::GetSize(sig) == 1) ? port_name : (port_name + "[" + std::to_string(i) + "]");
        auto net_name = sigbit_to_net_name(sig[i]);
        auto& ninfo = net_map[net_name];
        ninfo.name = net_name;
        auto full_pin = inst_name + "/" + term;

        if (dir == "out") {
          if (ninfo.driver_inst == 0) { ninfo.driver_inst = wi.id; ninfo.driver_pin = full_pin; }
          else ninfo.loads.push_back(std::make_pair(wi.id, full_pin));
        } else {
          ninfo.loads.push_back(std::make_pair(wi.id, full_pin));
        }
      }
    }
  }

  for (auto* wire : top_mod->wires()) {
    if (!wire || !wire->port_id) continue;
    for (int i = 0; i < wire->width; ++i) {
      Yosys::RTLIL::SigBit bit(wire, i);
      auto net_name = sigbit_to_net_name(bit);
      auto& ninfo = net_map[net_name];
      ninfo.name = net_name;
      auto port_name = canonicalize_name(Yosys::RTLIL::unescape_id(wire->name));
      auto term = (wire->width == 1) ? port_name : (port_name + "[" + std::to_string(i) + "]");
      if (wire->port_input) {

        if (ninfo.driver_inst == 0 && ninfo.driver_pin.empty()) {
          ninfo.driver_inst = 0;
          ninfo.driver_pin = term;
        }
      }
      if (wire->port_output) {

        ninfo.loads.push_back(std::make_pair(0, term));
      }
    }
  }

  Yosys::yosys_shutdown();

  std::vector<netlist_db_writer::WNet> nets;
  for (auto& [_, ninfo] : net_map) {
    netlist_db_writer::WNet wn;
    wn.name = ninfo.name;
    if (ninfo.driver_inst > 0 || !ninfo.driver_pin.empty()) {
      wn.pins.push_back({ninfo.driver_inst, ninfo.driver_pin, true});
    }
    for (auto& [lid, lpin] : ninfo.loads) {
      wn.pins.push_back({lid, lpin, false});
    }
    if (!wn.pins.empty()) nets.push_back(std::move(wn));
  }

  std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

  netlist_db_writer::WDieArea die{};
  if (def_read) {
    die.llx = def_info.die_llx;
    die.lly = def_info.die_lly;
    die.urx = def_info.die_urx;
    die.ury = def_info.die_ury;
    die.dbu_per_um = static_cast<float>(def_info.dbu_per_um);
  }

  uint32_t src_flags = NETLIST_DB_SRC_NETLIST;
  if (def_read) src_flags |= NETLIST_DB_SRC_DEF;

  if (!netlist_db_writer::write_db(out_path, resolved_top, instances, nets, die, src_flags)) {
    throw std::runtime_error("failed to write layout database: " + out_path);
  }

  std::cout << "[done] netlist_db=" << out_path
            << " top=" << resolved_top
            << " instances=" << instances.size()
            << " nets=" << nets.size()
            << " (yosys)\n";
  return 0;
}

int main(int argc, char** argv) {
  try {
    RuntimeLogger rl("design2cdb");

    std::string out_path, netlist, def_file, top_name;
    std::vector<std::string> lib_files;
    std::vector<std::string> lib_dirs;
    bool do_flatten = false;

    { auto s = rl.step("parse args");
    int i = 1;
    auto need_value = [&](const std::string& flag) -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + flag);
      return argv[++i];
    };
    for (; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--out" || a == "-o") out_path = need_value(a);
      else if (a == "--netlist") netlist = need_value(a);
      else if (a == "--def") def_file = need_value(a);
      else if (a == "--top") top_name = need_value(a);
      else if (a == "--lib-dir") lib_dirs.push_back(need_value(a));
      else if (a == "--flatten") do_flatten = true;
      else if (a == "--libs") {
        while (i + 1 < argc && argv[i + 1][0] != '-') lib_files.push_back(argv[++i]);
      } else if (a == "--help" || a == "-h") {
        std::cout << "usage: design2cdb --netlist <file.v> --libs <*.lib> --out <netlist.cdb> [--def <file.def>] [--top <name>] [--flatten]\n";
        return 0;
      } else if (!a.empty() && a[0] == '-') {
        throw std::runtime_error("unknown argument: " + a);
      } else {
        lib_files.push_back(a);
      }
    }

    for (const auto& lib_dir : lib_dirs) {
      auto expanded = glob_files(lib_dir, "*.lib");
      for (auto& p : expanded) lib_files.push_back(p.string());
    }

    if (netlist.empty()) { std::cerr << "ERROR: --netlist required\n"; return 1; }
    if (lib_files.empty()) { std::cerr << "ERROR: --libs or --lib-dir required\n"; return 1; }
    if (out_path.empty()) { std::cerr << "ERROR: --out required\n"; return 1; }

    if (def_file.empty()) {
      std::cerr << "[warning] no --def provided: placement coordinates will not be included in netlist.cdb\n";
    }

    if (!do_flatten) {
      std::ifstream vf(netlist);
      if (!vf.is_open()) { std::cerr << "ERROR: cannot open netlist: " << netlist << "\n"; return 1; }

      int module_count = 0;
      bool in_block_comment = false;
      std::string line;
      auto ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
      };
      while (std::getline(vf, line)) {
        for (size_t p = 0; p < line.size(); ++p) {
          if (in_block_comment) {
            if (line[p] == '*' && p + 1 < line.size() && line[p + 1] == '/') { in_block_comment = false; ++p; }
            continue;
          }
          if (line[p] == '/' && p + 1 < line.size() && line[p + 1] == '/') break;
          if (line[p] == '/' && p + 1 < line.size() && line[p + 1] == '*') { in_block_comment = true; ++p; continue; }
          if (line[p] != 'm' || line.compare(p, 6, "module") != 0) continue;

          const bool left_ok  = (p == 0) || !ident_char(line[p - 1]);
          const bool right_ok = (p + 6 >= line.size()) || !ident_char(line[p + 6]);
          if (left_ok && right_ok) ++module_count;
          p += 5;
        }
      }
      if (module_count == 0) {
        std::cerr << "ERROR: no module declaration found in netlist: " << netlist << "\n";
        return 1;
      }
      if (module_count > 1) {
        do_flatten = true;
        std::cerr << "[info] hierarchical netlist detected (" << module_count
                  << " modules) — using Yosys flatten path\n";
      }
    }
    }

    { auto s = rl.step("process design");
    int rc;
    if (do_flatten) {
      rc = run_yosys_path(netlist, def_file, top_name, lib_files, out_path, do_flatten);
    } else {
      rc = run_fast_path(netlist, def_file, top_name, lib_files, out_path);
    }
    if (rc != 0) { rl.done(); return rc; }
    }
    rl.done();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[error] " << e.what() << "\n";
    return 1;
  }
}
