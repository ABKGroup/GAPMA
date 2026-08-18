#include "logic_minor/pattern.hpp"
#include "logic_minor/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace {

double cell_dist(const Cell* a, const Cell* b) {
  if (!a || !b || std::isnan(a->x) || std::isnan(a->y) || std::isnan(b->x) || std::isnan(b->y))
    return 0.0;
  double dx = a->x - b->x, dy = a->y - b->y;
  return std::sqrt(dx * dx + dy * dy);
}

double axis_dx(const Cell* a, const Cell* b) {
  if (!a || !b || std::isnan(a->x) || std::isnan(b->x)) return 0.0;
  return std::abs(a->x - b->x);
}

double axis_dy(const Cell* a, const Cell* b) {
  if (!a || !b || std::isnan(a->y) || std::isnan(b->y)) return 0.0;
  return std::abs(a->y - b->y);
}

std::pair<Cell*, std::string> next_pin(const Pattern& p, Cell* pred) {
  for (auto* c : p.cells) {
    for (auto& [pn, net] : c->fanins) {
      auto* drv = net ? net->driver : nullptr;
      auto* prev = drv ? drv->cell : nullptr;
      if (prev == pred) return {c, pn};
    }
  }
  return {nullptr, ""};
}

}

bool is_buffer(const Cell* c) {
  if (!c || !c->master || c->master->functions.size() != 1 || c->master->input_terms.size() != 1) return false;
  auto in = *c->master->input_terms.begin();
  return trim(c->master->functions.begin()->second) == trim(in);
}

bool is_inv(const Cell* c) {
  if (!c || !c->master || c->master->functions.size() != 1 || c->master->input_terms.size() != 1) return false;
  auto in = *c->master->input_terms.begin();
  auto expr = trim(c->master->functions.begin()->second);
  auto nospace = [](std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); }), s.end());
    return s;
  };
  expr = nospace(expr);
  in = nospace(in);
  return expr == ("!" + in) || expr == (in + "'");
}

std::vector<Pin*> boundary_inputs(const Pattern& p) {
  std::vector<Pin*> out;
  for (auto* c : p.cells) {
    for (auto& [pn, pin] : c->in_pins) {
      auto* n = pin->net;
      auto* drv = n ? n->driver : nullptr;
      auto* prev = drv ? drv->cell : nullptr;
      if (!prev || p.cells.count(prev) == 0) out.push_back(pin);
    }
  }
  return out;
}

int input_count(const Pattern& p) {
  std::unordered_set<std::string> s;
  for (auto* pin : boundary_inputs(p)) {
    if (!pin || !pin->net) continue;
    s.insert(pin->net->name.empty() ? pin->full_name : pin->net->name);
  }
  return (int)s.size();
}

std::vector<std::string> collect_boundary_input_keys(const Pattern& p) {
  std::vector<std::string> keys;
  keys.reserve(p.cells.size() * 2);
  std::unordered_set<std::string> seen;
  seen.reserve(p.cells.size() * 2);
  for (auto* pin : boundary_inputs(p)) {
    if (!pin || !pin->net) continue;
    auto key = pin->net->name.empty() ? pin->full_name : pin->net->name;
    if (seen.insert(key).second) keys.push_back(std::move(key));
  }
  return keys;
}

bool has_no_external_output(const Pattern& p) {
  for (auto* c : p.cells) {
    if (!c || c == p.root) continue;
    for (const auto& [pn, nets] : c->fanouts) {
      for (auto* n : nets) {
        if (!n || n->loads.empty()) return false;
        for (auto* l : n->loads) {
          if (!l || !l->cell || p.cells.count(l->cell) == 0) return false;
        }
      }
    }
  }
  return true;
}

int boundary_output_count(const Pattern& p, bool ignore_nonroot_outputs) {
  int count = 0;

  if (p.root) {
    for (const auto& [pn, pin] : p.root->out_pins) {
      if (pin) count++;
    }
  }
  if (ignore_nonroot_outputs) return count;

  for (auto* c : p.cells) {
    if (!c || c == p.root) continue;
    for (const auto& [pn, nets] : c->fanouts) {
      for (auto* n : nets) {
        if (!n) continue;
        bool external = n->loads.empty();
        if (!external) {
          for (auto* l : n->loads) {
            if (!l || !l->cell || p.cells.count(l->cell) == 0) { external = true; break; }
          }
        }
        if (external) count++;
      }
    }
  }
  return count;
}

bool has_external_output_exceeding_budget(const Pattern& p, const Opt& opt) {
  const int remaining_budget = opt.max_cells - p.num_cells;
  for (auto* c : p.cells) {
    if (!c || c == p.root) continue;
    for (const auto& [_, nets] : c->fanouts) {
      for (auto* n : nets) {
        if (!n) return true;
        bool external = n->loads.empty();
        if (!external) {
          for (auto* l : n->loads) {
            if (!l || !l->cell || p.cells.count(l->cell) == 0) { external = true; break; }
          }
        }
        if (!external) continue;
        std::unordered_set<Cell*> external_load_cells;
        for (auto* l : n->loads) {
          if (!l || !l->cell) continue;
          if (p.cells.count(l->cell) != 0) continue;
          external_load_cells.insert(l->cell);
        }
        if (static_cast<int>(external_load_cells.size()) > remaining_budget) return true;
      }
    }
  }
  return false;
}

Pattern init_pattern(Cell* root) {
  Pattern p;
  p.root = root;
  p.cells.insert(root);
  p.num_cells = 1;
  p.dist[root] = 0.0;
  p.depth[root] = 1;
  p.max_root_dx = 0.0;
  p.max_root_dy = 0.0;
  for (auto& [pin, net] : root->fanins) {
    auto* drv = net ? net->driver : nullptr;
    auto* prev = drv ? drv->cell : nullptr;
    if (prev) p.frontier.insert(prev);
  }
  return p;
}

Pattern add_pred(Pattern p, Cell* pred) {
  p.frontier.erase(pred);
  if (p.cells.count(pred)) return p;

  auto [nextc, nextp] = next_pin(p, pred);

  p.cells.insert(pred);
  p.num_cells += 1;
  if (nextc) {
    double nd = p.dist[nextc] + cell_dist(pred, nextc);
    p.dist[pred] = nd;
    p.max_dist = std::max(p.max_dist, nd);
    p.max_root_dx = std::max(p.max_root_dx, axis_dx(pred, p.root));
    p.max_root_dy = std::max(p.max_root_dy, axis_dy(pred, p.root));

    int nde = 1;
    for (auto* c : p.cells) {
      for (auto& [pn2, net2] : c->fanins) {
        auto* drv2 = net2 ? net2->driver : nullptr;
        if (drv2 && drv2->cell == pred) {
          auto dit = p.depth.find(c);
          if (dit != p.depth.end()) nde = std::max(nde, dit->second + 1);
        }
      }
    }
    p.depth[pred] = nde;
    p.max_depth = std::max(p.max_depth, nde);
  }
  for (auto& [pn, net] : pred->fanins) {
    auto* drv = net ? net->driver : nullptr;
    auto* prev = drv ? drv->cell : nullptr;
    if (prev && p.cells.count(prev) == 0) p.frontier.insert(prev);
  }
  return p;
}
