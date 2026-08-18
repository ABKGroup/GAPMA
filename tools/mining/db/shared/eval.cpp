#include "eval.hpp"
#include "canonical.hpp"
#include "pattern.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

class Expr {
 public:
  explicit Expr(std::string s) : s_(std::move(s)) {}

  bool eval(const std::function<bool(const std::string&)>& lookup) {
    i_ = 0;
    lookup_ = lookup;
    next();
    return parse_or();
  }

 private:
  enum T { ID, NOT, AND, OR, XOR, LP, RP, END } t_ = END;
  std::string tok_;
  std::string s_;
  size_t i_ = 0;
  std::function<bool(const std::string&)> lookup_;

  void next() {
    while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    if (i_ >= s_.size()) { t_ = END; tok_.clear(); return; }
    char c = s_[i_];
    if (c == '!') { ++i_; t_ = NOT; tok_ = "!"; return; }
    if (c == '&' || c == '*') { ++i_; if (c == '&' && i_ < s_.size() && s_[i_] == '&') ++i_; t_ = AND; tok_ = "&"; return; }
    if (c == '|' || c == '+') { ++i_; if (c == '|' && i_ < s_.size() && s_[i_] == '|') ++i_; t_ = OR; tok_ = "|"; return; }
    if (c == '^') { ++i_; t_ = XOR; tok_ = "^"; return; }
    if (c == '(') { ++i_; t_ = LP; tok_ = "("; return; }
    if (c == ')') { ++i_; t_ = RP; tok_ = ")"; return; }
    if (std::isalnum((unsigned char)c) || c == '_' || c == '/' || c == '$') {
      size_t b = i_;
      while (i_ < s_.size()) {
        char d = s_[i_];
        if (std::isalnum((unsigned char)d) || d == '_' || d == '/' || d == '$') ++i_;
        else break;
      }
      tok_ = s_.substr(b, i_ - b);
      if (i_ < s_.size() && s_[i_] == '\'') { ++i_; t_ = ID; tok_ = "!" + tok_; }
      else { t_ = ID; }
      return;
    }
    ++i_;
    next();
  }

  bool parse_or() { bool v = parse_xor(); while (t_ == OR) { next(); bool r = parse_xor(); v = v | r; } return v; }
  bool parse_xor() { bool v = parse_and(); while (t_ == XOR) { next(); bool r = parse_and(); v = (v != r); } return v; }
  bool parse_and() { bool v = parse_unary(); while (t_ == AND) { next(); bool r = parse_unary(); v = v & r; } return v; }
  bool parse_unary() {
    if (t_ == NOT) { next(); return !parse_unary(); }
    if (t_ == LP) { next(); bool v = parse_or(); if (t_ == RP) next(); return v; }
    if (t_ == ID) {
      auto id = tok_; next();
      if (id.rfind("!", 0) == 0) return !lookup_(id.substr(1));
      auto u = upper(id);
      if (u == "1" || u == "TRUE") return true;
      if (u == "0" || u == "FALSE") return false;
      return lookup_(id);
    }
    return false;
  }
};

bool eval_out_pin(const Pin* op, EvalCtx& ec);

bool resolve_tok(const Cell* c, const std::string& tok, EvalCtx& ec) {
  auto u = upper(tok);
  if (u == "1" || u == "TRUE") return true;
  if (u == "0" || u == "FALSE") return false;

  auto it = c->in_pins.find(tok);
  if (it != c->in_pins.end()) {
    auto* pin = it->second;
    auto mt = ec.memo.find(pin);
    if (mt != ec.memo.end()) return mt->second;
    auto* net = pin->net;
    auto* drv = net ? net->driver : nullptr;
    auto* prev = drv ? drv->cell : nullptr;
    if (!prev || ec.p->cells.count(prev) == 0) {
      auto key = net ? net->name : pin->full_name;
      bool v = false;
      auto a = ec.assign.find(key);
      if (a != ec.assign.end()) v = a->second;
      ec.memo[pin] = v;
      return v;
    }
    bool v = eval_out_pin(drv, ec);
    ec.memo[pin] = v;
    return v;
  }

  auto ot = c->out_pins.find(tok);
  if (ot != c->out_pins.end()) return eval_out_pin(ot->second, ec);
  return false;
}

bool eval_cell_out(const Cell* c, const std::string& out_term, EvalCtx& ec) {
  if (!c || !c->master) return false;
  if (ec.active.count(c)) return false;
  auto fit = c->master->functions.find(out_term);
  if (fit == c->master->functions.end()) return false;
  ec.active.insert(c);
  Expr ex(fit->second);
  bool v = ex.eval([&](const std::string& t) { return resolve_tok(c, t, ec); });
  ec.active.erase(c);
  return v;
}

bool eval_out_pin(const Pin* op, EvalCtx& ec) {
  if (!op || !op->cell) return false;
  auto it = ec.memo.find(op);
  if (it != ec.memo.end()) return it->second;
  bool v = eval_cell_out(op->cell, op->term, ec);
  ec.memo[op] = v;
  return v;
}

}

bool has_single_root_output(const Pattern& p) {
  if (!p.root) return false;
  int root_outputs = 0;
  for (const auto& kv : p.root->out_pins) {
    if (kv.second) root_outputs += 1;
  }
  return root_outputs == 1;
}

PatternEvalSummary evaluate_pattern_outputs(const Pattern& p) {
  PatternEvalSummary out;
  if (!p.root) return out;

  auto keys = collect_boundary_input_keys(p);
  if (keys.empty()) return out;

  std::vector<Pin*> boundary_outputs;
  for (const auto& [pn, pin] : p.root->out_pins) {
    if (pin) boundary_outputs.push_back(pin);
  }

  std::vector<Pin*> nonroot_outputs;
  for (auto* c : p.cells) {
    if (!c || c == p.root) continue;
    for (const auto& [pn, pin] : c->out_pins) {
      if (!pin || !pin->net) continue;
      auto* net = pin->net;
      bool external = net->loads.empty();
      if (!external) {
        for (auto* l : net->loads) {
          if (!l || !l->cell || p.cells.count(l->cell) == 0) { external = true; break; }
        }
      }
      if (external) nonroot_outputs.push_back(pin);
    }
  }
  std::sort(nonroot_outputs.begin(), nonroot_outputs.end(), [](const Pin* a, const Pin* b) {
    int aid = a->cell ? a->cell->id : -1, bid = b->cell ? b->cell->id : -1;
    if (aid != bid) return aid < bid;
    return a->term < b->term;
  });
  boundary_outputs.insert(boundary_outputs.end(), nonroot_outputs.begin(), nonroot_outputs.end());
  if (boundary_outputs.empty()) return out;

  const int n = static_cast<int>(keys.size());
  const int rows = 1 << n;
  std::vector<std::string> output_bits(boundary_outputs.size());
  for (auto& bits : output_bits) bits.reserve(rows);

  EvalCtx ec;
  ec.p = &p;
  ec.assign.reserve(keys.size());
  ec.memo.reserve(std::max<size_t>(16, p.cells.size() * 8));
  ec.active.reserve(std::max<size_t>(16, p.cells.size()));
  for (const auto& key : keys) ec.assign.emplace(key, false);

  for (int a = 0; a < rows; ++a) {
    for (int i = 0; i < n; ++i) ec.assign[keys[static_cast<size_t>(i)]] = (((a >> (n - 1 - i)) & 1) != 0);
    ec.memo.clear();
    ec.active.clear();
    for (size_t oi = 0; oi < boundary_outputs.size(); ++oi) {
      bool v = eval_out_pin(boundary_outputs[oi], ec);
      output_bits[oi].push_back(v ? '1' : '0');
    }
  }

  out.has_output_vector = true;
  out.vec_key = std::to_string(n) + "|" + output_bits.front();
  out.input_net_order = keys;

  if (boundary_outputs.size() == 1) {

    std::unordered_set<std::string> canonicals;
    canonicals.insert(bits_to_key(canonical_bits(output_bits[0], n)));
    out.canonical_key = vec_to_canon(out.vec_key);
    out.signature = canonical_set_signature(canonicals);
    out.raw_truth_table = output_bits[0];
  } else {

    auto joint = canonical_bits_joint(output_bits, n);
    std::string sig;
    for (size_t ji = 0; ji < joint.size(); ++ji) {
      if (ji) sig += ";";
      sig += "0b" + joint[ji];
    }
    out.canonical_key = sig;
    out.signature = sig;

    for (size_t oi = 0; oi < output_bits.size(); ++oi) {
      if (oi) out.raw_truth_table += ";";
      out.raw_truth_table += output_bits[oi];
    }
  }

  return out;
}
