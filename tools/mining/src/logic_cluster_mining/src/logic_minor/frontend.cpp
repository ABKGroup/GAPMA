

#include "logic_minor/frontend.hpp"

#include "logic_minor/canonical.hpp"
#include "logic_minor/util.hpp"
#include "cell_db_reader.hpp"
#include "netlist_db_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

std::string family_name(const std::string& n) {
  auto p = n.find('x');
  if (p == std::string::npos) p = n.find('_');
  return p == std::string::npos ? n : n.substr(0, p);
}

std::string canonicalize_name(std::string s) {
  s = trim(s);
  if (!s.empty() && s.front() == '\\') s.erase(s.begin());
  return s;
}

std::string lower_copy(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string sanitize_component(std::string s) {
  for (char& ch : s) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) ch = '_';
  }
  return s;
}

void erase_load_pin_local(Net* net, Pin* pin) {
  if (!net || !pin) return;
  auto& loads = net->loads;
  loads.erase(std::remove(loads.begin(), loads.end(), pin), loads.end());
}

Master* get_master(Graph& g,
                   const std::string& name,
                   const std::unordered_map<std::string, std::unique_ptr<Master>>& lm) {
  auto it = g.masters.find(name);
  if (it != g.masters.end()) return it->second.get();
  auto m = std::make_unique<Master>();
  m->name = name;
  m->family = family_name(name);
  auto lit = lm.find(name);
  if (lit != lm.end()) {
    m->is_seq = lit->second->is_seq;
    m->functions = lit->second->functions;
    m->pin_directions = lit->second->pin_directions;
    m->input_terms = lit->second->input_terms;
  } else if (name.empty() || name[0] != '$') {
    auto upper_name = upper(name);
    bool is_seq_by_name = upper_name.find("DFF") != std::string::npos ||
                          upper_name.find("LATCH") != std::string::npos;
    if (is_seq_by_name) {

      throw std::runtime_error("sequential cell not in cell.cdb: \"" + name +
                               "\" — add it to the library or check the cell.cdb path");
    }
    throw std::runtime_error("cell not in cell.cdb: \"" + name + "\" — add it to the library or check the cell.cdb path");
  }
  Master* p = m.get();
  g.masters[name] = std::move(m);
  return p;
}

Cell* get_cell(Graph& g, const std::string& n, Master* m) {
  auto it = g.cells.find(n);
  if (it != g.cells.end()) return it->second.get();
  auto c = std::make_unique<Cell>();
  c->id = static_cast<int>(g.cell_list.size());
  c->name = n;
  c->master = m;
  Cell* p = c.get();
  g.cell_list.push_back(p);
  g.cells[n] = std::move(c);
  return p;
}

Pin* get_pin(Graph& g, const std::string& n) {
  auto it = g.pins.find(n);
  if (it != g.pins.end()) return it->second.get();
  auto p = std::make_unique<Pin>();
  p->full_name = n;
  Pin* r = p.get();
  g.pins[n] = std::move(p);
  return r;
}

Net* get_net(Graph& g, const std::string& n) {
  auto it = g.nets.find(n);
  if (it != g.nets.end()) return it->second.get();
  auto net = std::make_unique<Net>();
  net->name = n;
  Net* r = net.get();
  g.nets[n] = std::move(net);
  return r;
}

std::string tie_canonical_for_family(const std::string& family) {
  auto f = upper(family);
  if (f == "TIEHI" || f == "TIEH") return "1#0x1";
  if (f == "TIELO" || f == "TIEL") return "1#0x0";
  return "";
}

}

void record_master_canonicals(const Cfg& cfg, Graph& g, Master* master) {
  std::vector<std::string> input_pins(master->input_terms.begin(), master->input_terms.end());
  std::sort(input_pins.begin(), input_pins.end());

  if (master->is_seq) {
    g.lib_output_signature_for_master[master->name] =
        canonical_set_signature(g.lib_canonicals_for_master[master->name]);
    return;
  }

  int n = static_cast<int>(input_pins.size());
  if (n > cfg.opt.max_inputs) {
    g.lib_output_signature_for_master[master->name] =
        canonical_set_signature(g.lib_canonicals_for_master[master->name]);
    return;
  }

  if (n == 0 && master->functions.empty()) {
    auto ck = tie_canonical_for_family(master->family);
    if (!ck.empty()) {
      g.lib_canonicals_for_master[master->name].insert(ck);
    }
    g.lib_output_signature_for_master[master->name] =
        canonical_set_signature(g.lib_canonicals_for_master[master->name]);
    return;
  }

  for (const auto& [pin, expr] : master->functions) {
    auto ck = normalize_target_canonical(canonical_from_expr(expr, input_pins));
    g.lib_canonicals_for_master[master->name].insert(ck);
  }
  g.lib_output_signature_for_master[master->name] =
      canonical_set_signature(g.lib_canonicals_for_master[master->name]);
}

void split_multi_output_cells(Graph& g) {
  std::vector<std::string> to_split;
  to_split.reserve(g.cells.size());
  for (const auto& [name, cup] : g.cells) {
    if (!cup) continue;
    if (cup->out_pins.size() > 1) to_split.push_back(name);
  }

  for (const auto& cell_name : to_split) {
    auto it = g.cells.find(cell_name);
    if (it == g.cells.end() || !it->second) continue;
    Cell* orig = it->second.get();
    Master* orig_master = orig->master;
    if (!orig_master) {
      throw std::runtime_error("multi-output cell missing master: " + cell_name);
    }

    std::vector<std::pair<std::string, Pin*>> outputs;
    outputs.reserve(orig->out_pins.size());
    for (const auto& kv : orig->out_pins) {
      if (kv.second) outputs.push_back(kv);
    }
    if (outputs.size() <= 1) continue;

    for (const auto& [out_term, out_pin] : outputs) {
      const std::string split_tag = sanitize_component(out_term);
      const std::string split_master_name = orig_master->name + "__split__" + split_tag;
      Master* split_master = nullptr;
      auto mit = g.masters.find(split_master_name);
      if (mit != g.masters.end()) {
        split_master = mit->second.get();
      } else {
        auto m = std::make_unique<Master>();
        m->name = split_master_name;
        m->family = orig_master->family;
        m->is_seq = orig_master->is_seq;
        m->is_hierarchy_boundary = orig_master->is_hierarchy_boundary;
        m->input_terms = orig_master->input_terms;
        for (const auto& [pin_name, dir] : orig_master->pin_directions) {
          if (dir == "in" || dir == "inout") m->pin_directions[pin_name] = dir;
        }
        auto fit = orig_master->functions.find(out_term);
        if (fit != orig_master->functions.end()) m->functions[out_term] = fit->second;
        m->pin_directions[out_term] = "out";
        split_master = m.get();
        g.masters[split_master_name] = std::move(m);
      }

      const std::string split_cell_name = cell_name + "__split__" + split_tag;
      Cell* split_cell = get_cell(g, split_cell_name, split_master);
      split_cell->x = orig->x;
      split_cell->y = orig->y;

      for (const auto& [in_term, in_pin] : orig->in_pins) {
        if (!in_pin) continue;
        auto* split_pin = get_pin(g, split_cell_name + "/" + in_term);
        split_pin->cell = split_cell;
        split_pin->term = in_term;
        split_pin->dir = in_pin->dir;
        split_pin->net = in_pin->net;
        split_cell->in_pins[in_term] = split_pin;
        split_cell->fanins[in_term] = in_pin->net;
        if (in_pin->net) in_pin->net->loads.push_back(split_pin);
      }

      auto* split_out_pin = get_pin(g, split_cell_name + "/" + out_term);
      split_out_pin->cell = split_cell;
      split_out_pin->term = out_term;
      split_out_pin->dir = "out";
      split_out_pin->net = out_pin ? out_pin->net : nullptr;
      split_cell->out_pins[out_term] = split_out_pin;
      if (out_pin && out_pin->net) {
        split_cell->fanouts[out_term].push_back(out_pin->net);
        if (out_pin->net->driver == out_pin) {
          out_pin->net->driver = split_out_pin;
        } else {
          auto& loads = out_pin->net->loads;
          std::replace(loads.begin(), loads.end(), out_pin, split_out_pin);
        }
      }
    }

    if (orig->id >= 0 && orig->id < static_cast<int>(g.cell_list.size())) g.cell_list[orig->id] = nullptr;

    for (const auto& [term, pin] : orig->in_pins) {
      if (!pin) continue;
      if (pin->net) erase_load_pin_local(pin->net, pin);
      g.pins.erase(pin->full_name);
    }
    for (const auto& [term, pin] : orig->out_pins) {
      if (!pin) continue;
      if (pin->net && pin->net->driver == pin) {
        pin->net->driver = nullptr;
      } else if (pin->net) {
        erase_load_pin_local(pin->net, pin);
      }
      g.pins.erase(pin->full_name);
    }
    g.cells.erase(it);
  }
}

void load_graph_from_cdbs(const Cfg& cfg,
                          const std::string& cell_db_path,
                          const std::string& netlist_db_path,
                          Graph& g) {

  auto cdb = read_cell_db(cell_db_path);
  std::unordered_map<std::string, std::unique_ptr<Master>> lib_masters;

  for (const auto& me : cdb.masters) {
    auto m = std::make_unique<Master>();
    m->name = me.name;
    m->family = me.family;
    m->is_seq = me.is_seq;
    g.lib_master_area[me.name] = me.area;

    for (const auto& pin : me.pins) {
      if (pin.direction == 0) {
        m->pin_directions[pin.name] = "in";
        m->input_terms.insert(pin.name);
      } else if (pin.direction == 1) {
        m->pin_directions[pin.name] = "out";
      } else {
        m->pin_directions[pin.name] = "inout";
      }
    }
    for (const auto& fn : me.functions) {
      m->functions[fn.pin_name] = fn.expr;
      if (!fn.canonical.empty()) {
        auto ck = normalize_target_canonical(fn.canonical);
        g.lib_canonicals_for_master[me.name].insert(ck);
      }
    }

    if (!g.lib_canonicals_for_master[me.name].empty()) {
      g.lib_output_signature_for_master[me.name] =
          canonical_set_signature(g.lib_canonicals_for_master[me.name]);
    }

    lib_masters[me.name] = std::move(m);
  }

  auto ldb = read_netlist_db(netlist_db_path);

  {
    int32_t max_id = 0;
    for (const auto& inst : ldb.instances) {
      if (inst.id > max_id) max_id = inst.id;
    }
    g.cell_list.resize(static_cast<size_t>(max_id + 1), nullptr);
  }
  for (const auto& inst : ldb.instances) {
    auto* master = get_master(g, inst.master, lib_masters);
    auto it = g.cells.find(inst.name);
    Cell* cell;
    if (it != g.cells.end()) {
      cell = it->second.get();
    } else {
      auto c = std::make_unique<Cell>();
      c->id = inst.id;
      c->name = inst.name;
      c->master = master;
      cell = c.get();
      g.cell_list[static_cast<size_t>(inst.id)] = cell;
      g.cells[inst.name] = std::move(c);
    }
    if (!std::isnan(inst.x)) cell->x = inst.x;
    if (!std::isnan(inst.y)) cell->y = inst.y;
  }

  for (const auto& net : ldb.nets) {
    auto* gnet = get_net(g, net.name);
    for (const auto& pc : net.pins) {

      if (pc.instance_id == 0) continue;
      auto inst_it = ldb.instance_by_id.find(pc.instance_id);
      if (inst_it == ldb.instance_by_id.end() || !inst_it->second) {
        throw std::runtime_error("netlist.cdb: net \"" + net.name + "\" pin \"" + pc.pin_name +
                                 "\" references unknown instance id " + std::to_string(pc.instance_id));
      }
      const auto& inst = *inst_it->second;
      auto cell_it = g.cells.find(inst.name);
      if (cell_it == g.cells.end()) {
        throw std::runtime_error("netlist.cdb: net \"" + net.name + "\" pin \"" + pc.pin_name +
                                 "\" references instance \"" + inst.name + "\" that was never loaded");
      }
      Cell* cell = cell_it->second.get();

      std::string term = pc.pin_name;
      auto slash = term.rfind('/');
      if (slash != std::string::npos) term = term.substr(slash + 1);

      auto* pin = get_pin(g, pc.pin_name);
      pin->cell = cell;
      pin->term = term;
      pin->net = gnet;

      if (pc.is_driver) {
        pin->dir = "out";
        cell->out_pins[term] = pin;
        if (!gnet->driver) {
          gnet->driver = pin;
        } else if (gnet->driver != pin) {
          throw std::runtime_error("netlist.cdb: net \"" + net.name + "\" has multiple drivers: \"" +
                                   gnet->driver->full_name + "\" and \"" + pc.pin_name + "\"");
        }
        cell->fanouts[term].push_back(gnet);
      } else {
        pin->dir = "in";
        cell->in_pins[term] = pin;
        gnet->loads.push_back(pin);
        cell->fanins[term] = gnet;
      }
    }
  }

  for (auto& [name, net_up] : g.nets) {
    auto* net = net_up.get();
    if (!net->driver) {
      auto* dp = get_pin(g, "UNDRIVEN:" + name);
      dp->dir = "out";
      dp->net = net;
      net->driver = dp;
    }
  }
}
