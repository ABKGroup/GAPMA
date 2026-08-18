#include "logic_minor/graph_prep.hpp"
#include "logic_minor/pattern.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

void erase_load_pin(Net* net, Pin* pin) {
  if (!net || !pin) return;
  auto& loads = net->loads;
  loads.erase(std::remove(loads.begin(), loads.end(), pin), loads.end());
}

}

void remove_sequential_cells_from_graph(Graph& g) {
  std::vector<std::string> to_remove;
  to_remove.reserve(g.cells.size());
  for (auto& [name, cup] : g.cells) {
    if (!cup || !cup->master) continue;
    if (cup->master->is_seq) to_remove.push_back(name);
  }

  for (const auto& cname : to_remove) {
    auto cit = g.cells.find(cname);
    if (cit == g.cells.end() || !cit->second) continue;
    Cell* c = cit->second.get();
    if (c->id >= 0 && c->id < static_cast<int>(g.cell_list.size())) g.cell_list[c->id] = nullptr;

    for (auto& [pn, pp] : c->in_pins) {
      if (!pp) continue;

      if (pp->net) erase_load_pin(pp->net, pp);
      g.pins.erase(pp->full_name);
    }
    for (auto& [pn, pp] : c->out_pins) {
      if (!pp) continue;
      if (pp->net && pp->net->driver == pp) {
        pp->net->driver = nullptr;
        if (pp->net->loads.empty()) g.nets.erase(pp->net->name);
      }
      g.pins.erase(pp->full_name);
    }
    g.cells.erase(cit);
  }
}

void remove_buffer_cells_from_graph(Graph& g) {
  std::vector<std::string> to_remove;
  to_remove.reserve(g.cells.size());
  for (auto& [name, cup] : g.cells) {
    if (!cup || !cup->master) continue;
    if (is_buffer(cup.get())) to_remove.push_back(name);
  }

  for (const auto& cname : to_remove) {
    auto cit = g.cells.find(cname);
    if (cit == g.cells.end() || !cit->second) continue;
    Cell* c = cit->second.get();
    if (c->in_pins.empty() || c->out_pins.empty()) continue;

    Pin* in_pin = c->in_pins.begin()->second;
    Pin* out_pin = c->out_pins.begin()->second;
    Net* in_net = in_pin ? in_pin->net : nullptr;
    Net* out_net = out_pin ? out_pin->net : nullptr;

    if (in_net) erase_load_pin(in_net, in_pin);

    if (out_net) {
      std::vector<Pin*> moved_loads;
      moved_loads.reserve(out_net->loads.size());
      for (Pin* lp : out_net->loads) {
        if (!lp || lp == out_pin) continue;
        moved_loads.push_back(lp);
      }
      for (Pin* lp : moved_loads) {
        erase_load_pin(out_net, lp);
        if (in_net) {
          lp->net = in_net;
          in_net->loads.push_back(lp);
          if (lp->cell && !lp->term.empty()) lp->cell->fanins[lp->term] = in_net;
        }
      }
      if (out_net->driver == out_pin) out_net->driver = nullptr;
      if (!out_net->driver && out_net->loads.empty()) g.nets.erase(out_net->name);
    }

    for (auto& [pn, pp] : c->in_pins) { if (pp) g.pins.erase(pp->full_name); }
    for (auto& [pn, pp] : c->out_pins) { if (pp) g.pins.erase(pp->full_name); }
    if (c->id >= 0 && c->id < static_cast<int>(g.cell_list.size())) g.cell_list[c->id] = nullptr;
    g.cells.erase(cit);
  }
}
