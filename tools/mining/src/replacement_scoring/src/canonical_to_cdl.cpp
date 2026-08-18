

#include "runtime_logger.hpp"
#include "cell_metrics.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <climits>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <chrono>
#include <omp.h>
#include <mutex>
#include <atomic>

using Term = std::string;
using SOP = std::vector<Term>;

static std::string g_nmos_extra = "";
static std::string g_pmos_extra = "";
static std::string g_pdk = "";

static bool g_complementary = false;

static bool g_series_parallel = true;

static bool g_validate_p_canonical = false;

static double g_wmax_n_nm = 46.0;
static double g_wmax_p_nm = 46.0;

static int g_search_depth = -1;

struct Transistor {
    std::string name;
    std::string gate;
    std::string drain;
    std::string source;
    std::string bulk;
    bool is_pmos;
    std::string width;
    std::string length;
};

static void adjust_sizing(std::vector<Transistor>& nmos, std::vector<Transistor>& pmos) {
    if (g_pdk.empty()) return;

    auto compute_series_depth = [](std::vector<Transistor>& trs, const std::string& supply) {

        std::unordered_map<std::string, std::vector<int>> node_to_tr;
        for (int i = 0; i < (int)trs.size(); i++) {
            if (trs[i].drain != "VDD" && trs[i].drain != "VSS")
                node_to_tr[trs[i].drain].push_back(i);
            if (trs[i].source != "VDD" && trs[i].source != "VSS")
                node_to_tr[trs[i].source].push_back(i);
        }

        std::vector<int> depths(trs.size(), 1);
        for (int i = 0; i < (int)trs.size(); i++) {
            std::string node = trs[i].source;
            int depth = 1;
            std::set<int> seen = {i};
            while (node != supply && depth < 10) {
                bool found = false;
                for (int j : node_to_tr[node]) {
                    if (seen.count(j)) continue;
                    seen.insert(j);

                    if (trs[j].drain == node) node = trs[j].source;
                    else node = trs[j].drain;
                    depth++;
                    found = true;
                    break;
                }
                if (!found) break;
            }
            depths[i] = depth;
        }
        return depths;
    };

    auto nmos_depths = compute_series_depth(nmos, "VSS");
    auto pmos_depths = compute_series_depth(pmos, "VDD");

    (void)nmos_depths;
    (void)pmos_depths;
}

static bool verify_cdl(const std::vector<Transistor>& pmos, const std::vector<Transistor>& nmos,
                        int n_vars, const std::string& canonical, const std::string& output_pin = "Y") {

    int total = 1 << n_vars;
    for (int input = 0; input < total; input++) {
        std::unordered_map<std::string, int> vals;
        vals["VDD"] = 1; vals["VSS"] = 0;
        for (int v = 0; v < n_vars; v++) {
            std::string nm = (n_vars <= 6) ? std::string(1, 'A' + v) : ("A" + std::to_string(v));
            vals[nm] = (input >> (n_vars - 1 - v)) & 1;
            vals[nm + "_bar"] = 1 - vals[nm];
        }

        bool converged = false;
        for (int iter = 0; iter < 100; iter++) {
            bool changed = false;
            for (auto& t : nmos) {
                auto git = vals.find(t.gate);
                if (git == vals.end() || git->second != 1) continue;
                auto sit = vals.find(t.source);
                if (sit != vals.end() && sit->second == 0 && t.drain != "VDD" && t.drain != "VSS") {
                    auto dit = vals.find(t.drain);
                    if (dit == vals.end() || dit->second != 0) { vals[t.drain] = 0; changed = true; }
                }
                auto dit = vals.find(t.drain);
                if (dit != vals.end() && dit->second == 0 && t.source != "VDD" && t.source != "VSS") {
                    auto sit2 = vals.find(t.source);
                    if (sit2 == vals.end() || sit2->second != 0) { vals[t.source] = 0; changed = true; }
                }
            }
            for (auto& t : pmos) {
                auto git = vals.find(t.gate);
                if (git == vals.end() || git->second != 0) continue;
                auto sit = vals.find(t.source);
                if (sit != vals.end() && sit->second == 1 && t.drain != "VDD" && t.drain != "VSS") {
                    auto dit = vals.find(t.drain);
                    if (dit == vals.end() || dit->second != 1) { vals[t.drain] = 1; changed = true; }
                }
                auto dit = vals.find(t.drain);
                if (dit != vals.end() && dit->second == 1 && t.source != "VDD" && t.source != "VSS") {
                    auto sit2 = vals.find(t.source);
                    if (sit2 == vals.end() || sit2->second != 1) { vals[t.source] = 1; changed = true; }
                }
            }
            if (!changed) { converged = true; break; }
        }
        if (!converged) return false;
        int sim_out = vals.count(output_pin) ? vals[output_pin] : -1;
        int expected = (canonical[2 + input] == '1') ? 1 : 0;
        if (sim_out != expected) return false;
    }
    return true;
}

static bool verify_multi_output_cdl(const std::vector<Transistor>& pmos,
                                    const std::vector<Transistor>& nmos,
                                    int n_vars,
                                    const std::vector<unsigned>& output_tts,
                                    const std::vector<std::string>& output_names) {
    int total = 1 << n_vars;
    for (int input = 0; input < total; input++) {
        std::unordered_map<std::string, int> vals;
        vals["VDD"] = 1;
        vals["VSS"] = 0;
        for (int v = 0; v < n_vars; v++) {
            std::string nm = (n_vars <= 6) ? std::string(1, 'A' + v) : ("A" + std::to_string(v));
            vals[nm] = (input >> (n_vars - 1 - v)) & 1;
            vals[nm + "_bar"] = 1 - vals[nm];
        }
        for (int iter = 0; iter < 100; iter++) {
            bool changed = false;
            for (auto& t : nmos) {
                auto git = vals.find(t.gate);
                if (git == vals.end() || git->second != 1) continue;
                auto sit = vals.find(t.source);
                if (sit != vals.end() && sit->second == 0 && t.drain != "VDD" && t.drain != "VSS") {
                    auto dit = vals.find(t.drain);
                    if (dit == vals.end() || dit->second != 0) { vals[t.drain] = 0; changed = true; }
                }
                auto dit = vals.find(t.drain);
                if (dit != vals.end() && dit->second == 0 && t.source != "VDD" && t.source != "VSS") {
                    auto sit2 = vals.find(t.source);
                    if (sit2 == vals.end() || sit2->second != 0) { vals[t.source] = 0; changed = true; }
                }
            }
            for (auto& t : pmos) {
                auto git = vals.find(t.gate);
                if (git == vals.end() || git->second != 0) continue;
                auto sit = vals.find(t.source);
                if (sit != vals.end() && sit->second == 1 && t.drain != "VDD" && t.drain != "VSS") {
                    auto dit = vals.find(t.drain);
                    if (dit == vals.end() || dit->second != 1) { vals[t.drain] = 1; changed = true; }
                }
                auto dit = vals.find(t.drain);
                if (dit != vals.end() && dit->second == 1 && t.source != "VDD" && t.source != "VSS") {
                    auto sit2 = vals.find(t.source);
                    if (sit2 == vals.end() || sit2->second != 1) { vals[t.source] = 1; changed = true; }
                }
            }
            if (!changed) break;
        }
        for (size_t oi = 0; oi < output_tts.size(); oi++) {
            int sim_out = vals.count(output_names[oi]) ? vals[output_names[oi]] : -1;
            int expected = (output_tts[oi] >> input) & 1u;
            if (sim_out != expected) return false;
        }
    }
    return true;
}

enum class NodeKind { AND, OR, LIT };

struct FNode {
    NodeKind kind;
    int var = -1;
    std::vector<std::unique_ptr<FNode>> children;

    int literal_count() const {
        if (kind == NodeKind::LIT) return 1;
        int s = 0;
        for (auto& c : children) s += c->literal_count();
        return s;
    }
};

using FNodePtr = std::unique_ptr<FNode>;

static FNodePtr make_lit(int v) {
    auto n = std::make_unique<FNode>();
    n->kind = NodeKind::LIT;
    n->var = v;
    return n;
}

static FNodePtr make_node(NodeKind k, std::vector<FNodePtr> ch) {
    auto n = std::make_unique<FNode>();
    n->kind = k;
    n->children = std::move(ch);
    return n;
}

static std::string input_name(int var, int n_vars) {
    if (n_vars <= 6) return std::string(1, 'A' + var);
    return "A" + std::to_string(var);
}

static std::pair<int, std::set<int>> parse_canonical(const std::string& canon) {
    if (canon.size() < 3 || canon.substr(0, 2) != "0b")
        throw std::runtime_error("Expected '0b...' format: " + canon);
    std::string bits = canon.substr(2);
    int n = (int)bits.size();
    int nv = 0;
    while ((1 << nv) < n) nv++;
    if ((1 << nv) != n)
        throw std::runtime_error("Truth table length not power of 2: " + canon);
    std::set<int> onset;
    for (int i = 0; i < n; i++)
        if (bits[i] == '1') onset.insert(i);
    return {nv, onset};
}

static bool sop_has_complement(const SOP& sop, int n_vars) {
    for (auto& t : sop)
        for (int i = 0; i < n_vars; i++)
            if (t[i] == '0') return true;
    return false;
}

static int sop_literal_count(const SOP& sop) {
    int c = 0;
    for (auto& t : sop)
        for (char ch : t)
            if (ch != '-') c++;
    return c;
}

static std::string qm_combine(const std::string& a, const std::string& b) {
    int diff = 0;
    std::string result(a.size(), ' ');
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            diff++;
            result[i] = '-';
        } else {
            result[i] = a[i];
        }
        if (diff > 1) return "";
    }
    return diff == 1 ? result : "";
}

static bool qm_covers(const std::string& imp, int minterm, int n_vars) {
    for (int i = 0; i < n_vars; i++) {
        if (imp[i] == '-') continue;
        int bit = (minterm >> (n_vars - 1 - i)) & 1;
        if ((imp[i] == '1') != (bit == 1)) return false;
    }
    return true;
}

static SOP quine_mccluskey(int n_vars, const std::set<int>& onset) {
    if (onset.empty()) return {};
    if ((int)onset.size() == (1 << n_vars)) return {std::string(n_vars, '-')};

    std::set<std::string> implicants;
    for (int m : onset) {
        std::string s(n_vars, '0');
        for (int i = n_vars - 1; i >= 0; i--) {
            s[i] = ((m >> (n_vars - 1 - i)) & 1) ? '1' : '0';
        }
        implicants.insert(s);
    }

    std::set<std::string> prime;
    while (!implicants.empty()) {
        std::set<std::string> used, next;
        std::vector<std::string> imp_vec(implicants.begin(), implicants.end());
        for (size_t i = 0; i < imp_vec.size(); i++) {
            for (size_t j = i + 1; j < imp_vec.size(); j++) {
                auto c = qm_combine(imp_vec[i], imp_vec[j]);
                if (!c.empty()) {
                    next.insert(c);
                    used.insert(imp_vec[i]);
                    used.insert(imp_vec[j]);
                }
            }
        }
        for (auto& s : implicants)
            if (!used.count(s)) prime.insert(s);
        implicants = next;
    }

    std::vector<std::string> prime_vec(prime.begin(), prime.end());
    std::set<int> uncovered = onset;
    SOP selected;
    while (!uncovered.empty()) {
        int best_idx = 0, best_count = -1, best_comp = 9999;
        for (size_t i = 0; i < prime_vec.size(); i++) {
            int cnt = 0;
            for (int m : uncovered)
                if (qm_covers(prime_vec[i], m, n_vars)) cnt++;
            int comp = 0;
            for (char c : prime_vec[i]) if (c == '0') comp++;
            if (cnt > best_count || (cnt == best_count && comp < best_comp)) {
                best_count = cnt; best_idx = (int)i; best_comp = comp;
            }
        }
        selected.push_back(prime_vec[best_idx]);
        std::set<int> still;
        for (int m : uncovered)
            if (!qm_covers(prime_vec[best_idx], m, n_vars)) still.insert(m);
        uncovered = still;
    }
    return selected;
}

static SOP qm_with_dc(int n_vars, const std::set<int>& target, const std::set<int>& dc) {
    if (target.empty()) return {};
    std::set<int> care = target;
    care.insert(dc.begin(), dc.end());
    if ((int)care.size() == (1 << n_vars)) return {std::string(n_vars, '-')};

    std::set<std::string> implicants;
    for (int m : care)
        implicants.insert([&]{ std::string s(n_vars,'0'); for(int i=n_vars-1;i>=0;i--) s[i]=((m>>(n_vars-1-i))&1)?'1':'0'; return s; }());

    std::set<std::string> prime;
    while (!implicants.empty()) {
        std::set<std::string> used, next;
        std::vector<std::string> v(implicants.begin(), implicants.end());
        for (size_t i = 0; i < v.size(); i++)
            for (size_t j = i+1; j < v.size(); j++) {
                auto c = qm_combine(v[i], v[j]);
                if (!c.empty()) { next.insert(c); used.insert(v[i]); used.insert(v[j]); }
            }
        for (auto& s : implicants) if (!used.count(s)) prime.insert(s);
        implicants = next;
    }

    std::vector<std::string> pv(prime.begin(), prime.end());
    std::set<int> uncovered = target;
    SOP selected;
    while (!uncovered.empty()) {
        int bi = 0, bc = -1, best_comp = 9999;
        for (size_t i = 0; i < pv.size(); i++) {
            int cnt = 0;
            for (int m : uncovered) if (qm_covers(pv[i], m, n_vars)) cnt++;

            int comp = 0;
            for (char c : pv[i]) if (c == '0') comp++;

            if (cnt > bc || (cnt == bc && comp < best_comp)) {
                bc = cnt; bi = (int)i; best_comp = comp;
            }
        }
        selected.push_back(pv[bi]);
        std::set<int> still;
        for (int m : uncovered) if (!qm_covers(pv[bi], m, n_vars)) still.insert(m);
        uncovered = still;
    }
    return selected;
}

static FNodePtr flatten(FNodePtr n) {
    if (n->kind == NodeKind::LIT) return n;
    std::vector<FNodePtr> new_ch;
    for (auto& c : n->children) {
        c = flatten(std::move(c));
        if (c->kind == n->kind) {
            for (auto& gc : c->children)
                new_ch.push_back(std::move(gc));
        } else {
            new_ch.push_back(std::move(c));
        }
    }
    if (new_ch.size() == 1) return std::move(new_ch[0]);
    return make_node(n->kind, std::move(new_ch));
}

static FNodePtr term_to_tree(const Term& term, int n_vars) {
    std::vector<FNodePtr> lits;
    for (int i = 0; i < n_vars; i++)
        if (term[i] == '1') lits.push_back(make_lit(i));
    if (lits.empty()) return make_lit(-1);
    if (lits.size() == 1) return std::move(lits[0]);
    return make_node(NodeKind::AND, std::move(lits));
}

static FNodePtr term_to_tree_with_comp(const Term& term, int n_vars) {
    std::vector<FNodePtr> lits;
    for (int i = 0; i < n_vars; i++) {
        if (term[i] == '1') lits.push_back(make_lit(i));
        else if (term[i] == '0') lits.push_back(make_lit(i + n_vars));
    }
    if (lits.empty()) return make_lit(-1);
    if (lits.size() == 1) return std::move(lits[0]);
    return make_node(NodeKind::AND, std::move(lits));
}

static FNodePtr factor_sop_with_comp(const SOP& sop, int n_vars) {
    if (sop.empty()) return make_lit(-1);
    if (sop.size() == 1) return term_to_tree_with_comp(sop[0], n_vars);

    std::vector<FNodePtr> terms;
    for (auto& t : sop) terms.push_back(term_to_tree_with_comp(t, n_vars));
    if (terms.size() == 1) return std::move(terms[0]);
    return make_node(NodeKind::OR, std::move(terms));
}

static FNodePtr factor_sop(const SOP& sop, int n_vars) {
    if (sop.empty()) return make_lit(-1);
    if (sop.size() == 1) return term_to_tree(sop[0], n_vars);

    std::map<int, int> freq;
    for (auto& t : sop)
        for (int i = 0; i < n_vars; i++)
            if (t[i] == '1') freq[i]++;

    if (freq.empty()) return make_lit(-1);

    int best_var = -1, best_count = 0;
    for (auto& [v, c] : freq)
        if (c > best_count) { best_count = c; best_var = v; }

    if (best_count <= 1) {
        std::vector<FNodePtr> ch;
        for (auto& t : sop) ch.push_back(term_to_tree(t, n_vars));
        return flatten(make_node(NodeKind::OR, std::move(ch)));
    }

    SOP with_var, without_var;
    for (auto& t : sop) {
        if (t[best_var] == '1') with_var.push_back(t);
        else without_var.push_back(t);
    }

    SOP quotient;
    bool has_bare = false;
    for (auto& t : with_var) {
        Term q = t;
        q[best_var] = '-';
        bool all_dc = true;
        for (char c : q) if (c != '-') { all_dc = false; break; }
        if (all_dc) has_bare = true;
        else quotient.push_back(q);
    }

    FNodePtr factored_with;
    if (quotient.empty() || has_bare) {

        if (quotient.empty()) {
            factored_with = make_lit(best_var);
        } else {

            factored_with = make_lit(best_var);
        }
    } else {
        auto fq = factor_sop(quotient, n_vars);
        std::vector<FNodePtr> and_ch;
        and_ch.push_back(make_lit(best_var));
        and_ch.push_back(std::move(fq));
        factored_with = flatten(make_node(NodeKind::AND, std::move(and_ch)));
    }

    if (!without_var.empty()) {
        auto fw = factor_sop(without_var, n_vars);
        std::vector<FNodePtr> or_ch;
        or_ch.push_back(std::move(factored_with));
        or_ch.push_back(std::move(fw));
        return flatten(make_node(NodeKind::OR, std::move(or_ch)));
    }
    return factored_with;
}

static std::string serialize(const FNode& n) {
    if (n.kind == NodeKind::LIT) return "L" + std::to_string(n.var);
    std::vector<std::string> parts;
    for (auto& c : n.children) parts.push_back(serialize(*c));
    std::sort(parts.begin(), parts.end());
    std::string op = (n.kind == NodeKind::AND) ? "&" : "|";
    std::string r = op + "(";
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) r += ",";
        r += parts[i];
    }
    return r + ")";
}

static FNodePtr clone(const FNode& n) {
    if (n.kind == NodeKind::LIT) return make_lit(n.var);
    std::vector<FNodePtr> ch;
    for (auto& c : n.children) ch.push_back(clone(*c));
    return make_node(n.kind, std::move(ch));
}

static FNodePtr merge_common_quotients(FNodePtr node) {
    if (!node || node->kind == NodeKind::LIT) return node;

    for (auto& c : node->children)
        c = merge_common_quotients(std::move(c));

    if (node->kind != NodeKind::OR || node->children.size() < 2)
        return node;

    bool changed = true;
    while (changed) {
        changed = false;
        node = flatten(std::move(node));
        if (node->kind != NodeKind::OR) break;

        std::map<std::string, std::set<size_t>> factor_to_children;
        for (size_t i = 0; i < node->children.size(); i++) {
            auto& ch = node->children[i];
            if (ch->kind == NodeKind::AND) {
                for (auto& fc : ch->children) {
                    factor_to_children[serialize(*fc)].insert(i);
                }
            }
        }

        std::string best_key;
        size_t best_count = 0;
        for (auto& [key, indices] : factor_to_children) {
            if (indices.size() > best_count) {
                best_count = indices.size();
                best_key = key;
            }
        }

        if (best_count < 2) break;

        auto& matching = factor_to_children[best_key];

        FNodePtr common_factor;
        std::vector<FNodePtr> remainders;

        for (size_t i : matching) {
            auto& child = node->children[i];
            std::vector<FNodePtr> remaining;
            bool found = false;
            for (auto& fc : child->children) {
                if (!found && serialize(*fc) == best_key) {
                    if (!common_factor) common_factor = clone(*fc);
                    found = true;
                } else {
                    remaining.push_back(clone(*fc));
                }
            }
            if (remaining.empty()) {

                remainders.clear();
                break;
            } else if (remaining.size() == 1) {
                remainders.push_back(std::move(remaining[0]));
            } else {
                remainders.push_back(make_node(NodeKind::AND, std::move(remaining)));
            }
        }

        FNodePtr merged;
        if (remainders.empty()) {

            merged = std::move(common_factor);
        } else {
            FNodePtr new_or;
            if (remainders.size() == 1) {
                new_or = std::move(remainders[0]);
            } else {
                new_or = make_node(NodeKind::OR, std::move(remainders));
            }
            std::vector<FNodePtr> and_ch;
            and_ch.push_back(std::move(common_factor));
            and_ch.push_back(std::move(new_or));
            merged = make_node(NodeKind::AND, std::move(and_ch));
        }

        std::vector<FNodePtr> new_children;
        for (size_t i = 0; i < node->children.size(); i++) {
            if (!matching.count(i))
                new_children.push_back(std::move(node->children[i]));
        }
        new_children.push_back(std::move(merged));

        if (new_children.size() == 1) {
            node = flatten(std::move(new_children[0]));
        } else {
            node = flatten(make_node(NodeKind::OR, std::move(new_children)));
        }
        changed = true;
    }

    if (node && node->kind != NodeKind::LIT) {
        for (auto& c : node->children)
            c = merge_common_quotients(std::move(c));
        node = flatten(std::move(node));
    }

    return node;
}

static void tree_to_nmos(const FNode& node, const std::string& drain,
                         const std::string& source, int n_vars,
                         const std::string& w, const std::string& l,
                         const std::string& prefix, int& tid,
                         const std::map<int, std::string>& var_map,
                         std::vector<Transistor>& out) {
    if (node.kind == NodeKind::LIT) {
        std::string gate;
        auto it = var_map.find(node.var);
        gate = (it != var_map.end()) ? it->second : input_name(node.var, n_vars);
        out.push_back({"M" + prefix + "N" + std::to_string(tid++), gate, drain, source, "VSS", false, w, l});
        return;
    }
    if (node.kind == NodeKind::AND) {

        std::vector<std::string> nodes;
        nodes.push_back(drain);
        int n_inter_n = (int)node.children.size() - 1;
        for (int i = 0; i < n_inter_n; i++)
            nodes.push_back("ns" + prefix + std::to_string(tid + i));
        tid += n_inter_n;
        nodes.push_back(source);
        for (size_t i = 0; i < node.children.size(); i++)
            tree_to_nmos(*node.children[i], nodes[i], nodes[i + 1], n_vars, w, l, prefix, tid, var_map, out);
    } else {
        for (auto& c : node.children)
            tree_to_nmos(*c, drain, source, n_vars, w, l, prefix, tid, var_map, out);
    }
}

static void tree_to_pmos(const FNode& node, const std::string& drain,
                         const std::string& source, int n_vars,
                         const std::string& w, const std::string& l,
                         const std::string& prefix, int& tid,
                         const std::map<int, std::string>& var_map,
                         std::vector<Transistor>& out) {
    if (node.kind == NodeKind::LIT) {
        std::string gate;
        auto it = var_map.find(node.var);
        gate = (it != var_map.end()) ? it->second : input_name(node.var, n_vars);
        out.push_back({"M" + prefix + "P" + std::to_string(tid++), gate, drain, source, "VDD", true, w, l});
        return;
    }
    if (node.kind == NodeKind::AND) {

        for (auto& c : node.children)
            tree_to_pmos(*c, drain, source, n_vars, w, l, prefix, tid, var_map, out);
    } else {
        std::vector<std::string> nodes;
        nodes.push_back(source);
        int n_inter_p = (int)node.children.size() - 1;
        for (int i = 0; i < n_inter_p; i++) {

            std::string node_name = g_complementary
                ? ("ns" + prefix + std::to_string(tid + i))
                : ("np" + prefix + std::to_string(tid + i));
            nodes.push_back(node_name);
        }
        tid += n_inter_p;
        nodes.push_back(drain);
        for (size_t i = 0; i < node.children.size(); i++)
            tree_to_pmos(*node.children[i], nodes[i + 1], nodes[i], n_vars, w, l, prefix, tid, var_map, out);
    }
}

static std::string tree_to_expr(const FNode& node, int n_vars) {
    if (node.kind == NodeKind::LIT)
        return (node.var < 0) ? "1" : input_name(node.var, n_vars);
    std::string op = (node.kind == NodeKind::AND) ? "·" : "+";
    std::string s;
    for (size_t i = 0; i < node.children.size(); i++) {
        if (i > 0) s += op;
        auto sub = tree_to_expr(*node.children[i], n_vars);
        if (node.kind == NodeKind::AND && node.children[i]->kind == NodeKind::OR)
            s += "(" + sub + ")";
        else
            s += sub;
    }
    return s;
}

static std::string emit_cdl(const std::string& name, int n_vars,
                             const std::vector<Transistor>& pmos,
                             const std::vector<Transistor>& nmos,
                             const std::string& canonical, const std::string& comment,
                             const std::string& nmos_model, const std::string& pmos_model,
                             const std::vector<std::string>& extra_outputs = {}) {
    std::ostringstream os;

    os << ".SUBCKT " << name;
    for (int i = 0; i < n_vars; i++) os << " " << input_name(i, n_vars);
    os << " VDD VSS";
    if (extra_outputs.empty()) os << " Y";
    else for (auto& o : extra_outputs) os << " " << o;
    os << "\n";
    os << "* Canonical: " << canonical << "\n";
    os << "* Function: " << comment << "\n";
    os << "* Transistors: " << (pmos.size() + nmos.size()) << "\n";

    for (auto& t : pmos)
        os << t.name << " " << t.drain << " " << t.gate << " " << t.source << " " << t.bulk
           << " " << pmos_model << " W=" << t.width << " L=" << t.length << (g_pmos_extra.empty() ? "" : " " + g_pmos_extra) << "\n";
    for (auto& t : nmos)
        os << t.name << " " << t.drain << " " << t.gate << " " << t.source << " " << t.bulk
           << " " << nmos_model << " W=" << t.width << " L=" << t.length << (g_nmos_extra.empty() ? "" : " " + g_nmos_extra) << "\n";
    os << ".ENDS\n";
    return os.str();
}

struct BinateResult {
    std::vector<Transistor> pmos;
    std::vector<Transistor> nmos;
    std::string output_node;
    bool output_inverted = false;
    std::string compl_node;
    bool infeasible = false;

    std::string aux_gate_node;
    std::vector<bool> aux_gate_tt;
    std::vector<Transistor> aux_gate_pmos;
    std::vector<Transistor> aux_gate_nmos;
};

static std::pair<std::vector<Transistor>, std::vector<Transistor>>
finalize_binate_result(const BinateResult& br,
                       bool target_is_complement,
                       const std::string& w_n,
                       const std::string& w_p,
                       const std::string& l) {
    std::vector<Transistor> pmos = br.pmos;
    std::vector<Transistor> nmos = br.nmos;

    bool actual_is_target = !br.output_inverted;
    bool actual_is_f = target_is_complement ? !actual_is_target : actual_is_target;

    if (!actual_is_f) {
        pmos.push_back({"MP_inv", br.output_node, "Y", "VDD", "VDD", true, w_p, l});
        nmos.push_back({"MN_inv", br.output_node, "Y", "VSS", "VSS", false, w_n, l});
        return {std::move(pmos), std::move(nmos)};
    }

    if (br.output_node != "Y") {
        for (auto& t : pmos) {
            if (t.drain == br.output_node) t.drain = "Y";
            if (t.source == br.output_node) t.source = "Y";
        }
        for (auto& t : nmos) {
            if (t.drain == br.output_node) t.drain = "Y";
            if (t.source == br.output_node) t.source = "Y";
        }
    }
    return {std::move(pmos), std::move(nmos)};
}

static BinateResult build_nand_nand(
        const SOP& onset_sop, int n_vars,
        const std::map<int, std::string>& var_name_map,
        const std::string& node_prefix,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& global_tid) {

    BinateResult result;
    std::vector<std::string> nand_outs;

    for (size_t ti = 0; ti < onset_sop.size(); ti++) {
        auto& term = onset_sop[ti];
        std::vector<std::string> gates;
        for (int i = 0; i < n_vars; i++) {
            if (term[i] == '1' && var_name_map.count(i))
                gates.push_back(var_name_map.at(i));
        }
        if (gates.empty()) { nand_outs.push_back("VDD"); continue; }

        std::string nout = node_prefix + "nd" + std::to_string(ti);
        nand_outs.push_back(nout);

        if (gates.size() == 1) {
            result.pmos.push_back({"MP" + std::to_string(global_tid), gates[0], nout, "VDD", "VDD", true, w_p, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid), gates[0], nout, "VSS", "VSS", false, w_n, l});
            global_tid++;
        } else {
            for (auto& g : gates)
                result.pmos.push_back({"MP" + std::to_string(global_tid++), g, nout, "VDD", "VDD", true, w_p, l});
            std::string prev = nout;
            for (size_t j = 0; j < gates.size(); j++) {
                std::string next = (j + 1 < gates.size())
                    ? node_prefix + "ns" + std::to_string(ti) + "_" + std::to_string(j) : "VSS";
                result.nmos.push_back({"MN" + std::to_string(global_tid++), gates[j], prev, next, "VSS", false, w_n, l});
                prev = next;
            }
        }
    }

    if (nand_outs.size() == 1) {
        result.output_node = nand_outs[0];
        result.output_inverted = true;
        return result;
    }

    std::string out = node_prefix + "out";
    for (auto& g : nand_outs)
        result.pmos.push_back({"MP" + std::to_string(global_tid++), g, out, "VDD", "VDD", true, w_p, l});
    std::string prev = out;
    for (size_t j = 0; j < nand_outs.size(); j++) {
        std::string next = (j + 1 < nand_outs.size())
            ? node_prefix + "nso" + std::to_string(j) : "VSS";
        result.nmos.push_back({"MN" + std::to_string(global_tid++), nand_outs[j], prev, next, "VSS", false, w_n, l});
        prev = next;
    }
    result.output_node = out;
    result.output_inverted = false;
    return result;
}

enum class SmallGateType {
    INV,
    NAND2,
    NOR2,
    NAND3,
    NOR3,
    AOI21,
    OAI21,
    AOI22,
    OAI22,
    ONEHOT3,
    NOT_ONEHOT3,
    TWOHOT3,
};

struct SmallGateInst {
    SmallGateType type;
    std::vector<int> inputs;
    int output_id = -1;
    unsigned tt = 0;
    int cost = 0;
};

struct SmallSynthSolution {
    bool found = false;
    int cost = INT_MAX;
    int output_id = -1;
    std::vector<SmallGateInst> gates;

    bool timed_out = false;

    int deepest_depth_attempted = 0;
};

struct CandidateNet {
    std::vector<Transistor> pmos;
    std::vector<Transistor> nmos;
    std::string desc;
    int cpp_min = INT_MAX;
};

struct ExactMultiStep2 {
    SmallGateType type;
    unsigned in0 = 0;
    unsigned in1 = 0;
    unsigned out = 0;
};

struct ExactMultiResult2 {
    bool found = false;
    int tr = INT_MAX;
    std::vector<ExactMultiStep2> steps;
    std::vector<Transistor> pmos;
    std::vector<Transistor> nmos;
};

static unsigned onset_to_tt(const std::set<int>& onset, int n_vars) {
    unsigned tt = 0;
    for (int m : onset) {
        if (m >= 0 && m < (1 << n_vars)) tt |= (1u << m);
    }
    return tt;
}

static unsigned eval_small_gate_tt(SmallGateType type, const std::vector<unsigned>& args, unsigned inv_mask) {
    auto inv8 = [inv_mask](unsigned x) { return x ^ inv_mask; };
    switch (type) {
        case SmallGateType::INV: return inv8(args[0]);
        case SmallGateType::NAND2: return inv8(args[0] & args[1]);
        case SmallGateType::NOR2: return inv8(args[0] | args[1]);
        case SmallGateType::NAND3: return inv8(args[0] & args[1] & args[2]);
        case SmallGateType::NOR3: return inv8(args[0] | args[1] | args[2]);
        case SmallGateType::AOI21: return inv8(args[0] | (args[1] & args[2]));
        case SmallGateType::OAI21: return inv8(args[0] & (args[1] | args[2]));
        case SmallGateType::AOI22: return inv8((args[0] & args[1]) | (args[2] & args[3]));
        case SmallGateType::OAI22: return inv8((args[0] | args[1]) & (args[2] | args[3]));
        case SmallGateType::ONEHOT3: {
            unsigned na = inv_mask ^ args[0], nb = inv_mask ^ args[1], nc = inv_mask ^ args[2];
            return (args[0]&nb&nc) | (na&args[1]&nc) | (na&nb&args[2]);
        }
        case SmallGateType::NOT_ONEHOT3: {
            unsigned na = inv_mask ^ args[0], nb = inv_mask ^ args[1], nc = inv_mask ^ args[2];
            unsigned oh = (args[0]&nb&nc) | (na&args[1]&nc) | (na&nb&args[2]);
            return inv_mask ^ oh;
        }
        case SmallGateType::TWOHOT3: {
            unsigned na = inv_mask ^ args[0], nb = inv_mask ^ args[1], nc = inv_mask ^ args[2];
            return (args[0]&args[1]&nc) | (args[0]&nb&args[2]) | (na&args[1]&args[2]);
        }
    }
    return 0;
}

static void append_small_gate_transistors(
        SmallGateType type,
        const std::vector<std::string>& ins,
        const std::string& out,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& tid,
        std::vector<Transistor>& pmos,
        std::vector<Transistor>& nmos) {
    auto add_p = [&](const std::string& g, const std::string& d, const std::string& s) {
        pmos.push_back({"MP" + std::to_string(tid++), g, d, s, "VDD", true, w_p, l});
    };
    auto add_n = [&](const std::string& g, const std::string& d, const std::string& s) {
        nmos.push_back({"MN" + std::to_string(tid++), g, d, s, "VSS", false, w_n, l});
    };

    if (type == SmallGateType::INV) {
        add_p(ins[0], out, "VDD");
        add_n(ins[0], out, "VSS");
        return;
    }
    if (type == SmallGateType::NAND2 || type == SmallGateType::NAND3) {
        for (auto& in : ins) add_p(in, out, "VDD");
        std::string prev = out;
        for (size_t i = 0; i < ins.size(); i++) {
            std::string next = (i + 1 < ins.size()) ? (out + "_ns" + std::to_string(i)) : "VSS";
            add_n(ins[i], prev, next);
            prev = next;
        }
        return;
    }
    if (type == SmallGateType::NOR2 || type == SmallGateType::NOR3) {
        std::string prev = "VDD";
        for (size_t i = 0; i < ins.size(); i++) {
            std::string next = (i + 1 < ins.size()) ? (out + "_ps" + std::to_string(i)) : out;
            add_p(ins[i], next, prev);
            prev = next;
        }
        for (auto& in : ins) add_n(in, out, "VSS");
        return;
    }
    if (type == SmallGateType::AOI21) {
        add_n(ins[0], out, "VSS");
        std::string nmid = out + "_nmid";
        add_n(ins[1], out, nmid);
        add_n(ins[2], nmid, "VSS");
        std::string pmid = out + "_pmid";
        add_p(ins[0], pmid, "VDD");
        add_p(ins[1], out, pmid);
        add_p(ins[2], out, pmid);
        return;
    }
    if (type == SmallGateType::OAI21) {
        std::string nmid = out + "_nmid";
        add_n(ins[0], out, nmid);
        add_n(ins[1], nmid, "VSS");
        add_n(ins[2], nmid, "VSS");
        add_p(ins[0], out, "VDD");
        std::string pmid = out + "_pmid";
        add_p(ins[1], out, pmid);
        add_p(ins[2], pmid, "VDD");
        return;
    }
    if (type == SmallGateType::AOI22) {
        std::string n0 = out + "_n0";
        std::string n1 = out + "_n1";
        add_n(ins[0], out, n0);
        add_n(ins[1], n0, "VSS");
        add_n(ins[2], out, n1);
        add_n(ins[3], n1, "VSS");
        std::string p0 = out + "_p0";
        add_p(ins[0], p0, "VDD");
        add_p(ins[1], p0, "VDD");
        add_p(ins[2], out, p0);
        add_p(ins[3], out, p0);
        return;
    }
    if (type == SmallGateType::OAI22) {
        std::string n0 = out + "_n0";
        add_n(ins[0], out, n0);
        add_n(ins[1], out, n0);
        add_n(ins[2], n0, "VSS");
        add_n(ins[3], n0, "VSS");
        std::string p0 = out + "_p0";
        std::string p1 = out + "_p1";
        add_p(ins[0], out, p0);
        add_p(ins[1], p0, "VDD");
        add_p(ins[2], out, p1);
        add_p(ins[3], p1, "VDD");
        return;
    }
    if (type == SmallGateType::ONEHOT3) {
        const auto& A = ins[0];
        const auto& B = ins[1];
        const auto& C = ins[2];
        std::string nor3 = out + "_nor3";
        std::string n1 = out + "_n1", n2 = out + "_n2";
        add_n(A, nor3, "VSS");
        add_n(B, nor3, "VSS");
        add_n(C, nor3, "VSS");
        add_p(A, n2, nor3);
        add_p(B, n1, n2);
        add_p(C, "VDD", n1);
        std::string n3 = out + "_n3", n4 = out + "_n4", n5 = out + "_n5";
        add_p(A, n4, n3);
        add_p(C, n4, n3);
        add_p(B, out, n3);
        add_p(C, n4, n5);
        add_p(A, n5, out);
        add_p(nor3, "VDD", n4);
        std::string n6 = out + "_n6", n7 = out + "_n7";
        add_n(nor3, out, "VSS");
        add_n(A, n6, "VSS");
        add_n(C, n6, "VSS");
        add_n(B, out, n6);
        add_n(A, n7, out);
        add_n(C, n7, "VSS");
        return;
    }
    if (type == SmallGateType::NOT_ONEHOT3) {
        const auto& A = ins[0];
        const auto& B = ins[1];
        const auto& C = ins[2];
        std::string planar = out + "_planar";
        std::string n1 = out + "_n1", n2 = out + "_n2", n3 = out + "_n3";
        add_n(C, n1, planar);
        add_n(B, n1, "VSS");
        add_n(A, n2, planar);
        add_n(B, n2, "VSS");
        add_n(C, n2, "VSS");
        add_p(C, n3, "VDD");
        add_p(B, n3, "VDD");
        add_p(A, planar, n3);
        std::string n4 = out + "_n4", n5 = out + "_n5", n6 = out + "_n6", n7 = out + "_n7";
        add_p(B, n4, "VDD");
        add_p(C, planar, n4);
        add_n(planar, n5, out);
        add_n(A, n5, "VSS");
        add_n(B, n5, "VSS");
        add_n(C, n5, "VSS");
        add_p(planar, out, "VDD");
        add_p(A, n6, "VDD");
        add_p(C, n6, n7);
        add_p(B, out, n7);
        return;
    }
    if (type == SmallGateType::TWOHOT3) {
        const auto& A = ins[0];
        const auto& B = ins[1];
        const auto& C = ins[2];
        std::string planar = out + "_planar";
        std::string n1 = out + "_n1", n2 = out + "_n2", n3 = out + "_n3";
        add_n(B, n1, planar);
        add_n(C, n1, "VSS");
        add_n(A, n2, planar);
        add_n(B, n2, "VSS");
        add_n(C, n2, "VSS");
        add_p(C, n3, "VDD");
        add_p(B, n3, "VDD");
        add_p(A, planar, n3);
        std::string n4 = out + "_n4", n5 = out + "_n5", n6 = out + "_n6", n7 = out + "_n7";
        add_p(C, n4, "VDD");
        add_p(B, planar, n4);
        add_p(planar, n5, out);
        add_p(A, n5, "VDD");
        add_p(B, n5, "VDD");
        add_p(C, n5, "VDD");
        add_n(planar, out, "VSS");
        add_n(A, n6, "VSS");
        add_n(B, n6, n7);
        add_n(C, out, n7);
        return;
    }
}

static SmallSynthSolution search_small_gate_library(unsigned target_tt, int n_vars) {
    struct Spec {
        SmallGateType type;
        int cost;
        int arity;
        bool ordered;
        bool allow_repeat;
    };
    const std::vector<Spec> specs = {
        {SmallGateType::INV, 2, 1, true, true},
        {SmallGateType::NAND2, 4, 2, false, false},
        {SmallGateType::NOR2, 4, 2, false, false},
        {SmallGateType::NAND3, 6, 3, false, false},
        {SmallGateType::NOR3, 6, 3, false, false},
        {SmallGateType::AOI21, 6, 3, true, false},
        {SmallGateType::OAI21, 6, 3, true, false},
        {SmallGateType::AOI22, 8, 4, true, true},
        {SmallGateType::OAI22, 8, 4, true, true},
        {SmallGateType::ONEHOT3, 18, 3, false, false},
        {SmallGateType::NOT_ONEHOT3, 18, 3, false, false},
        {SmallGateType::TWOHOT3, 18, 3, false, false},
    };

    struct SignalState { unsigned tt; int id; };

    // Deeper recursion levels (inside a branch already owned by one thread)

    SmallSynthSolution best;
    std::mutex best_mutex;
    std::atomic<int> best_cost{INT_MAX};
    auto search_start = std::chrono::steady_clock::now();
    std::atomic<long> node_count{0};
    std::atomic<bool> timed_out{false};
    unsigned inv_mask = (1u << (1u << n_vars)) - 1u;

    const bool lb_enabled = (std::getenv("NLCELL_DISABLE_LB") == nullptr);

    std::function<void(const std::vector<SignalState>&, const std::vector<SmallGateInst>&,
                        int, int, std::unordered_map<std::string, int>&,
                        std::unordered_map<unsigned, int>&, bool)> dfs;
    dfs = [&](const std::vector<SignalState>& signals,
              const std::vector<SmallGateInst>& gates,
              int depth_left,
              int cost,
              std::unordered_map<std::string, int>& seen_cost,
              std::unordered_map<unsigned, int>& min_cost_for_tt,
              bool is_top_level) {
        if (cost >= best_cost.load(std::memory_order_relaxed)) return;
        if (timed_out.load(std::memory_order_relaxed)) return;
        if ((node_count.fetch_add(1, std::memory_order_relaxed) & 0x3FF) == 0) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - search_start).count();
            if (elapsed_ms > 900000) { timed_out.store(true, std::memory_order_relaxed); return; }
        }

        std::vector<unsigned> key_tts;
        key_tts.reserve(signals.size());
        bool target_present = false;
        unsigned target_inv = target_tt ^ inv_mask;
        bool inv_present = false;
        for (const auto& s : signals) {
            key_tts.push_back(s.tt);
            if (s.tt == target_tt) {
                target_present = true;
                std::lock_guard<std::mutex> lock(best_mutex);
                if (cost < best.cost) {
                    best.found = true;
                    best.cost = cost;
                    best.output_id = s.id;
                    best.gates = gates;
                    best_cost.store(cost, std::memory_order_relaxed);
                }
            }
            if (s.tt == target_inv) inv_present = true;
        }
        if (depth_left == 0) return;

        if (lb_enabled) {
            int h = target_present ? 0 : (inv_present ? 2 : 4);
            if (cost + h >= best_cost.load(std::memory_order_relaxed)) return;
        }

        std::sort(key_tts.begin(), key_tts.end());
        std::string memo_key;
        memo_key.reserve(key_tts.size() * 4 + 8);
        memo_key += std::to_string(depth_left);
        memo_key += ":";
        for (unsigned tt : key_tts) {
            memo_key += std::to_string(tt);
            memo_key += ",";
        }
        auto it = seen_cost.find(memo_key);
        if (it != seen_cost.end() && it->second <= cost) return;
        seen_cost[memo_key] = cost;

        std::vector<unsigned> present_tts;
        present_tts.reserve(signals.size());
        for (const auto& s : signals) present_tts.push_back(s.tt);

        auto has_tt = [&](unsigned tt) {
            return std::find(present_tts.begin(), present_tts.end(), tt) != present_tts.end();
        };

        auto explore_spec = [&](const Spec& spec, std::unordered_map<std::string, int>& local_seen,
                                 std::unordered_map<unsigned, int>& local_min_cost_for_tt) {
            std::vector<int> combo(spec.arity, 0);
            std::vector<bool> used(signals.size(), false);
            std::unordered_set<unsigned> local_outs;

            std::function<void(int, int)> gen_combo = [&](int pos, int start) {
                if (pos == spec.arity) {
                    std::vector<unsigned> args;
                    args.reserve(combo.size());
                    std::vector<int> arg_ids;
                    arg_ids.reserve(combo.size());
                    for (int idx : combo) {
                        args.push_back(signals[idx].tt);
                        arg_ids.push_back(signals[idx].id);
                    }
                    unsigned out_tt = eval_small_gate_tt(spec.type, args, inv_mask);
                    if (has_tt(out_tt) || !local_outs.insert(out_tt).second) return;

                    int new_cost = cost + spec.cost;

                    auto dp_it = local_min_cost_for_tt.find(out_tt);
                    if (dp_it != local_min_cost_for_tt.end() && dp_it->second <= new_cost) return;
                    local_min_cost_for_tt[out_tt] = new_cost;

                    SmallGateInst inst;
                    inst.type = spec.type;
                    inst.inputs = std::move(arg_ids);
                    inst.output_id = static_cast<int>(signals.size() + gates.size());
                    inst.tt = out_tt;
                    inst.cost = spec.cost;

                    auto next_signals = signals;
                    next_signals.push_back({out_tt, inst.output_id});
                    auto next_gates = gates;
                    next_gates.push_back(std::move(inst));
                    dfs(next_signals, next_gates, depth_left - 1, new_cost, local_seen,
                        local_min_cost_for_tt, false);
                    return;
                }

                int begin = spec.ordered ? 0 : start;
                for (int i = begin; i < (int)signals.size(); i++) {
                    if (!spec.allow_repeat && spec.ordered && used[i]) continue;
                    combo[pos] = i;
                    bool was_used = used[i];
                    used[i] = true;
                    gen_combo(pos + 1,
                              spec.ordered ? 0 : (spec.allow_repeat ? i : i + 1));
                    used[i] = was_used;
                }
            };
            gen_combo(0, 0);
        };

        if (is_top_level) {

            #pragma omp parallel for schedule(dynamic) if(depth_left > 1)
            for (size_t si = 0; si < specs.size(); si++) {
                std::unordered_map<std::string, int> thread_seen = seen_cost;
                std::unordered_map<unsigned, int> thread_min_cost_for_tt = min_cost_for_tt;
                explore_spec(specs[si], thread_seen, thread_min_cost_for_tt);
            }
        } else {
            for (const auto& spec : specs) {
                explore_spec(spec, seen_cost, min_cost_for_tt);
            }
        }
    };

    std::vector<SignalState> init_signals;
    int total_minterms = 1 << n_vars;
    for (int v = 0; v < n_vars; v++) {
        unsigned tt = 0;
        for (int m = 0; m < total_minterms; m++)
            if ((m >> (n_vars - 1 - v)) & 1) tt |= (1u << m);
        init_signals.push_back({tt, v});
    }

    int max_depth = (g_search_depth > 0) ? g_search_depth : (n_vars <= 3 ? 5 : 6);
    int deepest_depth_attempted = 0;
    std::unordered_map<std::string, int> seen_cost;
    std::unordered_map<unsigned, int> min_cost_for_tt;

    int plateau_patience = -1;
    if (const char* pp = std::getenv("NLCELL_PLATEAU_PATIENCE")) {
        try { plateau_patience = std::stoi(pp); } catch (...) { plateau_patience = -1; }
    }
    int prev_best_cost = INT_MAX;
    int stall = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        deepest_depth_attempted = depth;
        dfs(init_signals, {}, depth, 0, seen_cost, min_cost_for_tt, true);
        if (timed_out.load(std::memory_order_relaxed)) break;
        if (plateau_patience >= 1 && best.found) {
            int cur = best.cost;
            if (cur >= prev_best_cost) {
                if (++stall >= plateau_patience) break;
            } else {
                stall = 0;
            }
            prev_best_cost = cur;
        }
    }
    best.timed_out = timed_out.load(std::memory_order_relaxed);
    best.deepest_depth_attempted = deepest_depth_attempted;
    return best;
}

static CandidateNet synthesize_small_gate_solution(
        const SmallSynthSolution& sol,
        int n_vars,
        const std::string& w_n, const std::string& w_p, const std::string& l) {
    CandidateNet cand;
    cand.desc = "Y = binate [small-gate search, ";
    if (!sol.found) return cand;

    std::unordered_map<int, std::string> id_to_name;
    for (int v = 0; v < n_vars; v++)
        id_to_name[v] = input_name(v, n_vars);

    int tid = 0;
    for (const auto& gate : sol.gates) {
        std::vector<std::string> ins;
        ins.reserve(gate.inputs.size());
        for (int id : gate.inputs) ins.push_back(id_to_name.at(id));
        std::string out = "sg" + std::to_string(gate.output_id);
        append_small_gate_transistors(gate.type, ins, out, w_n, w_p, l, tid, cand.pmos, cand.nmos);
        id_to_name[gate.output_id] = out;
    }

    std::string out_name = id_to_name.at(sol.output_id);
    if (out_name != "Y") {
        for (auto& t : cand.pmos) {
            if (t.drain == out_name) t.drain = "Y";
            if (t.source == out_name) t.source = "Y";
        }
        for (auto& t : cand.nmos) {
            if (t.drain == out_name) t.drain = "Y";
            if (t.source == out_name) t.source = "Y";
        }
    }
    return cand;
}

struct ABCGateActive {
    std::string name;
    std::string type;
    std::vector<std::string> inputs;
};

static std::string abc_binary_path() {
    return "abc";
}

static std::string run_abc_active(const std::string& canonical) {
    std::string bits = canonical.substr(2);
    std::string hex;
    for (size_t i = 0; i < bits.size(); i += 4) {
        int nibble = 0;
        for (size_t j = 0; j < 4 && i + j < bits.size(); j++) {
            if (bits[i + j] == '1') nibble |= (1 << j);
        }
        hex += "0123456789ABCDEF"[nibble];
    }

    std::string cmd = "echo 'read_truth " + hex +
        "; balance; rewrite; refactor; balance; rewrite; rewrite -z; balance; refactor -z; rewrite -z; balance"
        "; write_eqn /dev/stdout; quit' | timeout 30 " + abc_binary_path();
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("popen failed for ABC invocation (truth-table hex='" + hex +
            "'): " + std::string(std::strerror(errno)));
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    int rc = pclose(pipe);
    if (rc == -1)
        throw std::runtime_error("pclose failed for ABC pipe (truth-table hex='" + hex +
            "'): " + std::string(std::strerror(errno)));
    if (WEXITSTATUS(rc) != 0)
        throw std::runtime_error("ABC exited non-zero (status=" + std::to_string(WEXITSTATUS(rc)) +
            ") for truth-table hex='" + hex + "' — see ABC stderr above for cause");
    return result;
}

static std::vector<ABCGateActive> parse_abc_eqn_active(const std::string& eqn) {

    bool debug_parse = std::getenv("CANONICAL_TO_CDL_DEBUG") != nullptr;
    std::vector<ABCGateActive> gates;
    std::istringstream iss(eqn);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#' || line.find("INORDER") != std::string::npos ||
            line.find("OUTORDER") != std::string::npos) continue;
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t;");
            size_t b = s.find_last_not_of(" \t;");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };

        std::string lhs = line.substr(0, eq_pos);
        std::string rhs = line.substr(eq_pos + 1);
        trim(lhs);
        trim(rhs);
        if (lhs.empty() || rhs.empty()) continue;

        bool has_or = (rhs.find('+') != std::string::npos);
        bool has_and = (rhs.find('*') != std::string::npos);
        char delim = has_or ? '+' : '*';
        std::vector<std::string> parts;
        std::istringstream rss(rhs);
        std::string part;
        while (std::getline(rss, part, delim)) {
            trim(part);
            while (!part.empty() && part.front() == '(' && part.back() == ')') {
                part = part.substr(1, part.size() - 2);
                trim(part);
            }
            if (!part.empty()) parts.push_back(part);
        }
        if (parts.empty()) {
            if (debug_parse) std::cerr << "[abc-parse] empty term list for line: " << line << "\n";
            return {};
        }

        bool all_inverted = true;
        bool none_inverted = true;
        std::vector<std::pair<std::string, bool>> parsed_inputs;
        for (auto& p : parts) {
            bool inv = (!p.empty() && p[0] == '!');
            std::string sig = inv ? p.substr(1) : p;
            trim(sig);
            parsed_inputs.push_back({sig, inv});
            if (inv) none_inverted = false;
            else all_inverted = false;
        }

        ABCGateActive g;
        g.name = lhs;
        if (parsed_inputs.size() == 1) {
            g.inputs.push_back(parsed_inputs[0].first);
            g.type = parsed_inputs[0].second ? "INV" : "BUF";
        } else if (has_and && !has_or) {
            if (all_inverted) {
                g.type = "NOR";
                for (auto& [n, _] : parsed_inputs) g.inputs.push_back(n);
            } else if (none_inverted) {
                g.type = "AND";
                for (auto& [n, _] : parsed_inputs) g.inputs.push_back(n);
            } else {
                g.type = "AND_MIXED";
                for (auto& [n, inv] : parsed_inputs) g.inputs.push_back(inv ? ("!" + n) : n);
            }
        } else if (has_or && !has_and) {
            if (all_inverted) {
                g.type = "NAND";
                for (auto& [n, _] : parsed_inputs) g.inputs.push_back(n);
            } else if (none_inverted) {
                g.type = "OR";
                for (auto& [n, _] : parsed_inputs) g.inputs.push_back(n);
            } else {
                g.type = "OR_MIXED";
                for (auto& [n, inv] : parsed_inputs) g.inputs.push_back(inv ? ("!" + n) : n);
            }
        } else {
            if (debug_parse) std::cerr << "[abc-parse] unrecognized mixed AND/OR line: " << line << "\n";
            return {};
        }
        gates.push_back(std::move(g));
    }
    return gates;
}

static CandidateNet synthesize_abc_solution(
        const std::string& canonical,
        int n_vars,
        const std::string& w_n, const std::string& w_p, const std::string& l) {
    CandidateNet cand;
    cand.desc = "Y = abc [";
    std::string eqn = run_abc_active(canonical);
    if (eqn.empty()) return cand;
    auto gates = parse_abc_eqn_active(eqn);
    if (gates.empty()) return cand;

    std::unordered_map<std::string, std::string> aliases;

    auto map_signal = [&](const std::string& sig) -> std::string {
        if (sig.size() == 1 && sig[0] >= 'a' && sig[0] < 'a' + n_vars) {
            return input_name(n_vars - 1 - (sig[0] - 'a'), n_vars);
        }
        return sig;
    };
    auto resolve_signal = [&](std::string sig) -> std::string {
        sig = map_signal(sig);
        while (aliases.count(sig)) sig = aliases[sig];
        return sig;
    };

    std::set<std::string> inv_signals;
    int tid = 0;
    for (const auto& g : gates) {
        std::string out = (g.name == "F0") ? "Y" : g.name;
        auto emit_inv = [&](const std::string& in, const std::string& node) {
            cand.pmos.push_back({"MP" + std::to_string(tid), in, node, "VDD", "VDD", true, w_p, l});
            cand.nmos.push_back({"MN" + std::to_string(tid), in, node, "VSS", "VSS", false, w_n, l});
            tid++;
        };

        if (g.type == "INV") {
            emit_inv(resolve_signal(g.inputs[0]), out);
        } else if (g.type == "BUF") {
            aliases[out] = resolve_signal(g.inputs[0]);
        } else if (g.type == "NAND" || g.type == "NOR") {
            std::vector<std::string> ins;
            for (const auto& inp : g.inputs) ins.push_back(resolve_signal(inp));
            if (g.type == "NAND") {
                for (auto& in : ins) cand.pmos.push_back({"MP" + std::to_string(tid++), in, out, "VDD", "VDD", true, w_p, l});
                std::string prev = out;
                for (size_t i = 0; i < ins.size(); i++) {
                    std::string next = (i + 1 < ins.size()) ? (out + "_ns" + std::to_string(i)) : "VSS";
                    cand.nmos.push_back({"MN" + std::to_string(tid++), ins[i], prev, next, "VSS", false, w_n, l});
                    prev = next;
                }
            } else {
                std::string prev = "VDD";
                for (size_t i = 0; i < ins.size(); i++) {
                    std::string next = (i + 1 < ins.size()) ? (out + "_ps" + std::to_string(i)) : out;
                    cand.pmos.push_back({"MP" + std::to_string(tid++), ins[i], next, prev, "VDD", true, w_p, l});
                    prev = next;
                }
                for (auto& in : ins) cand.nmos.push_back({"MN" + std::to_string(tid++), in, out, "VSS", "VSS", false, w_n, l});
            }
        } else {
            std::vector<std::string> actual_inputs;
            for (const auto& inp : g.inputs) {
                if (!inp.empty() && inp[0] == '!') {
                    std::string base = resolve_signal(inp.substr(1));
                    std::string inv_name = base + "_inv";
                    if (!inv_signals.count(base)) {
                        emit_inv(base, inv_name);
                        inv_signals.insert(base);
                    }
                    actual_inputs.push_back(inv_name);
                } else {
                    actual_inputs.push_back(resolve_signal(inp));
                }
            }
            if (g.type == "AND" || g.type == "AND_MIXED") {
                std::string mid = out + "_n";
                for (auto& in : actual_inputs) cand.pmos.push_back({"MP" + std::to_string(tid++), in, mid, "VDD", "VDD", true, w_p, l});
                std::string prev = mid;
                for (size_t i = 0; i < actual_inputs.size(); i++) {
                    std::string next = (i + 1 < actual_inputs.size()) ? (mid + "_ns" + std::to_string(i)) : "VSS";
                    cand.nmos.push_back({"MN" + std::to_string(tid++), actual_inputs[i], prev, next, "VSS", false, w_n, l});
                    prev = next;
                }
                emit_inv(mid, out);
            } else if (g.type == "OR" || g.type == "OR_MIXED") {
                std::string mid = out + "_n";
                std::string prev = "VDD";
                for (size_t i = 0; i < actual_inputs.size(); i++) {
                    std::string next = (i + 1 < actual_inputs.size()) ? (mid + "_ps" + std::to_string(i)) : mid;
                    cand.pmos.push_back({"MP" + std::to_string(tid++), actual_inputs[i], next, prev, "VDD", true, w_p, l});
                    prev = next;
                }
                for (auto& in : actual_inputs) cand.nmos.push_back({"MN" + std::to_string(tid++), in, mid, "VSS", "VSS", false, w_n, l});
                emit_inv(mid, out);
            } else {
                cand.pmos.clear();
                cand.nmos.clear();
                return cand;
            }
        }
    }
    return cand;
}

static ExactMultiResult2 synthesize_exact_multi_output_2input(
        const std::vector<unsigned>& output_tts,
        const std::vector<std::string>& output_names,
        const std::string& w_n, const std::string& w_p, const std::string& l) {
    ExactMultiResult2 result;
    if (output_tts.size() != 2 || output_names.size() != 2) return result;
    if (output_tts[0] == output_tts[1]) return result;

    constexpr unsigned kATt = 0xCu;
    constexpr unsigned kBTt = 0xAu;
    constexpr int kStateCount = 1 << 16;

    struct Parent {
        bool valid = false;
        unsigned prev_mask = 0;
        ExactMultiStep2 step;
    };

    auto goal_reached = [&](unsigned mask) {
        return (mask & (1u << output_tts[0])) && (mask & (1u << output_tts[1]));
    };
    auto relax = [&](std::priority_queue<std::pair<int, unsigned>,
                                         std::vector<std::pair<int, unsigned>>,
                                         std::greater<std::pair<int, unsigned>>>& pq,
                     std::vector<int>& dist,
                     std::vector<Parent>& parent,
                     unsigned prev_mask,
                     unsigned next_mask,
                     int next_cost,
                     const ExactMultiStep2& step) {
        if (next_cost >= dist[next_mask]) return;
        dist[next_mask] = next_cost;
        parent[next_mask] = {true, prev_mask, step};
        pq.push({next_cost, next_mask});
    };

    std::vector<int> dist(kStateCount, INT_MAX);
    std::vector<Parent> parent(kStateCount);
    std::priority_queue<std::pair<int, unsigned>,
                        std::vector<std::pair<int, unsigned>>,
                        std::greater<std::pair<int, unsigned>>> pq;
    dist[0] = 0;
    pq.push({0, 0});

    unsigned goal_mask = 0;
    while (!pq.empty()) {
        auto [cost, mask] = pq.top();
        pq.pop();
        if (cost != dist[mask]) continue;
        if (goal_reached(mask)) {
            goal_mask = mask;
            break;
        }

        std::vector<unsigned> avail = {kATt, kBTt};
        for (unsigned tt = 0; tt < 16; tt++) {
            if (mask & (1u << tt)) avail.push_back(tt);
        }

        for (unsigned a : avail) {
            unsigned inv = (~a) & 0xFu;
            if (!(mask & (1u << inv))) {
                relax(pq, dist, parent, mask, mask | (1u << inv), cost + 2,
                      {SmallGateType::INV, a, 0, inv});
            }
        }

        for (size_t i = 0; i < avail.size(); i++) {
            for (size_t j = i; j < avail.size(); j++) {
                unsigned a = avail[i];
                unsigned b = avail[j];
                unsigned nand_tt = (~(a & b)) & 0xFu;
                if (!(mask & (1u << nand_tt))) {
                    relax(pq, dist, parent, mask, mask | (1u << nand_tt), cost + 4,
                          {SmallGateType::NAND2, a, b, nand_tt});
                }
                unsigned nor_tt = (~(a | b)) & 0xFu;
                if (!(mask & (1u << nor_tt))) {
                    relax(pq, dist, parent, mask, mask | (1u << nor_tt), cost + 4,
                          {SmallGateType::NOR2, a, b, nor_tt});
                }
            }
        }
    }

    if (!goal_mask) return result;

    for (unsigned cur = goal_mask; parent[cur].valid; cur = parent[cur].prev_mask)
        result.steps.push_back(parent[cur].step);
    std::reverse(result.steps.begin(), result.steps.end());

    std::unordered_map<unsigned, std::string> tt_to_name;
    tt_to_name[kATt] = "A";
    tt_to_name[kBTt] = "B";

    std::vector<bool> output_assigned(output_tts.size(), false);
    int tid = 0;
    int node_id = 0;
    for (const auto& step : result.steps) {
        std::string out_name;
        for (size_t oi = 0; oi < output_tts.size(); oi++) {
            if (!output_assigned[oi] && output_tts[oi] == step.out) {
                out_name = output_names[oi];
                output_assigned[oi] = true;
                break;
            }
        }
        if (out_name.empty()) out_name = "mo2_n" + std::to_string(node_id++);

        std::vector<std::string> ins = {tt_to_name.at(step.in0)};
        if (step.type != SmallGateType::INV) ins.push_back(tt_to_name.at(step.in1));
        append_small_gate_transistors(step.type, ins, out_name, w_n, w_p, l, tid,
                                      result.pmos, result.nmos);
        tt_to_name[step.out] = out_name;
    }

    if (!std::all_of(output_assigned.begin(), output_assigned.end(), [](bool v) { return v; }))
        return ExactMultiResult2{};

    result.found = true;
    result.tr = (int)(result.pmos.size() + result.nmos.size());
    return result;
}

static ExactMultiResult2 synthesize_seeded_output_2input(
        unsigned target_tt,
        const std::string& output_name,
        const std::unordered_map<unsigned, std::string>& seed_names,
        bool target_already_named,
        const std::string& w_n, const std::string& w_p, const std::string& l) {
    ExactMultiResult2 result;
    if (target_already_named) {
        result.found = true;
        result.tr = 0;
        return result;
    }

    constexpr unsigned kATt = 0xCu;
    constexpr unsigned kBTt = 0xAu;
    constexpr int kStateCount = 1 << 16;

    struct Parent {
        bool valid = false;
        unsigned prev_mask = 0;
        ExactMultiStep2 step;
    };

    std::vector<int> dist(kStateCount, INT_MAX);
    std::vector<Parent> parent(kStateCount);
    std::priority_queue<std::pair<int, unsigned>,
                        std::vector<std::pair<int, unsigned>>,
                        std::greater<std::pair<int, unsigned>>> pq;
    dist[0] = 0;
    pq.push({0, 0});

    while (!pq.empty()) {
        auto [cost, mask] = pq.top();
        pq.pop();
        if (cost != dist[mask]) continue;
        if (mask & (1u << target_tt)) break;

        std::vector<unsigned> avail = {kATt, kBTt};
        for (const auto& [tt, _] : seed_names) {
            if (tt != kATt && tt != kBTt) avail.push_back(tt);
        }
        for (unsigned tt = 0; tt < 16; tt++) {
            if (mask & (1u << tt)) avail.push_back(tt);
        }
        std::sort(avail.begin(), avail.end());
        avail.erase(std::unique(avail.begin(), avail.end()), avail.end());

        auto relax = [&](unsigned next_mask, int next_cost, const ExactMultiStep2& step) {
            if (next_cost >= dist[next_mask]) return;
            dist[next_mask] = next_cost;
            parent[next_mask] = {true, mask, step};
            pq.push({next_cost, next_mask});
        };

        for (unsigned a : avail) {
            unsigned inv = (~a) & 0xFu;
            if (!(mask & (1u << inv))) {
                relax(mask | (1u << inv), cost + 2, {SmallGateType::INV, a, 0, inv});
            }
        }
        for (size_t i = 0; i < avail.size(); i++) {
            for (size_t j = i; j < avail.size(); j++) {
                unsigned a = avail[i];
                unsigned b = avail[j];
                unsigned nand_tt = (~(a & b)) & 0xFu;
                if (!(mask & (1u << nand_tt))) {
                    relax(mask | (1u << nand_tt), cost + 4, {SmallGateType::NAND2, a, b, nand_tt});
                }
                unsigned nor_tt = (~(a | b)) & 0xFu;
                if (!(mask & (1u << nor_tt))) {
                    relax(mask | (1u << nor_tt), cost + 4, {SmallGateType::NOR2, a, b, nor_tt});
                }
            }
        }
    }

    unsigned goal_mask = 0;
    for (unsigned mask = 0; mask < (1u << 16); mask++) {
        if (dist[mask] != INT_MAX && (mask & (1u << target_tt))) {
            goal_mask = mask;
            break;
        }
    }
    if (!goal_mask) return result;

    for (unsigned cur = goal_mask; parent[cur].valid; cur = parent[cur].prev_mask)
        result.steps.push_back(parent[cur].step);
    std::reverse(result.steps.begin(), result.steps.end());

    std::unordered_map<unsigned, std::string> tt_to_name = seed_names;
    tt_to_name[kATt] = "A";
    tt_to_name[kBTt] = "B";

    int tid = 0;
    int node_id = 0;
    for (const auto& step : result.steps) {
        std::string out_name = (step.out == target_tt) ? output_name : ("seed2_n" + std::to_string(node_id++));
        std::vector<std::string> ins = {tt_to_name.at(step.in0)};
        if (step.type != SmallGateType::INV) ins.push_back(tt_to_name.at(step.in1));
        append_small_gate_transistors(step.type, ins, out_name, w_n, w_p, l, tid,
                                      result.pmos, result.nmos);
        tt_to_name[step.out] = out_name;
    }

    result.found = true;
    result.tr = (int)(result.pmos.size() + result.nmos.size());
    return result;
}

static void append_xor_shared_core_2input(
        bool xnor_main,
        const std::string& out_name,
        const std::string& a_bar_name,
        const std::string& b_bar_name,
        const std::string& mid_name,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& tid,
        std::vector<Transistor>& pmos,
        std::vector<Transistor>& nmos) {
    auto add_p = [&](const std::string& g, const std::string& d, const std::string& s) {
        pmos.push_back({"MP" + std::to_string(tid++), g, d, s, "VDD", true, w_p, l});
    };
    auto add_n = [&](const std::string& g, const std::string& d, const std::string& s) {
        nmos.push_back({"MN" + std::to_string(tid++), g, d, s, "VSS", false, w_n, l});
    };

    add_n("B", b_bar_name, "VSS");
    add_p("B", b_bar_name, "VDD");
    add_n("A", a_bar_name, "VSS");
    add_p("A", a_bar_name, "VDD");

    if (xnor_main) {
        add_n(a_bar_name, b_bar_name, out_name);
        add_n("A", mid_name, out_name);
        add_n(b_bar_name, "VSS", mid_name);

        add_p("A", b_bar_name, out_name);
        add_p(a_bar_name, mid_name, out_name);
        add_p(b_bar_name, "VDD", mid_name);
    } else {
        add_n("A", b_bar_name, out_name);
        add_n(a_bar_name, mid_name, out_name);
        add_n(b_bar_name, "VSS", mid_name);

        add_p(a_bar_name, b_bar_name, out_name);
        add_p("A", mid_name, out_name);
        add_p(b_bar_name, "VDD", mid_name);
    }
}

static ExactMultiResult2 synthesize_xor_seed_multi_output_2input(
        const std::vector<unsigned>& output_tts,
        const std::vector<std::string>& output_names,
        const std::string& w_n, const std::string& w_p, const std::string& l) {
    ExactMultiResult2 best;
    if (output_tts.size() != 2 || output_names.size() != 2) return best;

    auto consider_main = [&](size_t main_idx, bool xnor_main) {
        if (output_tts[main_idx] != (xnor_main ? 0x9u : 0x6u)) return;
        size_t other_idx = 1 - main_idx;
        unsigned other_tt = output_tts[other_idx];

        ExactMultiResult2 cand;
        int tid = 0;
        std::string out_name = output_names[main_idx];
        std::string a_bar_name = (other_tt == 0x3u) ? output_names[other_idx] : "xor_abar";
        std::string b_bar_name = (other_tt == 0x5u) ? output_names[other_idx] : "xor_bbar";
        std::string mid_name = "xor_mid";

        append_xor_shared_core_2input(xnor_main, out_name, a_bar_name, b_bar_name, mid_name,
                                      w_n, w_p, l, tid, cand.pmos, cand.nmos);

        std::unordered_map<unsigned, std::string> seed_names;
        seed_names[0x3u] = a_bar_name;
        seed_names[0x5u] = b_bar_name;
        seed_names[output_tts[main_idx]] = out_name;

        bool target_already_named = (other_tt == 0x3u && a_bar_name == output_names[other_idx]) ||
                                    (other_tt == 0x5u && b_bar_name == output_names[other_idx]);
        auto extra = synthesize_seeded_output_2input(other_tt, output_names[other_idx], seed_names,
                                                     target_already_named, w_n, w_p, l);
        if (!extra.found) return;

        cand.pmos.insert(cand.pmos.end(), extra.pmos.begin(), extra.pmos.end());
        cand.nmos.insert(cand.nmos.end(), extra.nmos.begin(), extra.nmos.end());
        cand.found = true;
        cand.tr = (int)(cand.pmos.size() + cand.nmos.size());

        if (!best.found || cand.tr < best.tr) best = std::move(cand);
    };

    consider_main(0, false);
    consider_main(0, true);
    consider_main(1, false);
    consider_main(1, true);
    return best;
}

static int count_cdl_transistors(const std::string& cdl) {
    std::istringstream iss(cdl);
    std::string line;
    int count = 0;
    while (std::getline(iss, line)) {
        size_t pos = line.find_first_not_of(" \t");
        if (pos != std::string::npos && pos < line.size() && line[pos] == 'M') count++;
    }
    return count;
}

static BinateResult try_dsd_decomp(
        const std::set<int>& onset, int n_vars,
        const std::vector<int>& var_map,
        const std::string& node_prefix,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& global_tid,
        int max_tr,
        const std::vector<std::string>& explicit_names = {});

static BinateResult generate_cmos_recursive(
        const std::set<int>& onset, int n_vars,
        const std::vector<int>& var_map,
        const std::string& node_prefix,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& global_tid,
        int max_tr = INT_MAX,
        const std::vector<std::string>& explicit_names = {}) {

    BinateResult result;
    int total = 1 << n_vars;
    std::set<int> all_mt;
    for (int i = 0; i < total; i++) all_mt.insert(i);
    std::set<int> offset;
    for (int i = 0; i < total; i++)
        if (!onset.count(i)) offset.insert(i);

    auto get_var_name = [&](int local_idx) -> std::string {
        if (!explicit_names.empty() && local_idx < (int)explicit_names.size())
            return explicit_names[local_idx];
        return input_name(var_map[local_idx], (int)var_map.size());
    };

    if (onset.empty()) {

        result.output_node = "VSS";
        result.output_inverted = false;
        return result;
    }
    if (offset.empty()) {
        result.output_node = "VDD";
        result.output_inverted = false;
        return result;
    }

    if (n_vars == 1) {
        std::string orig_name = get_var_name(0);

        if (onset.count(0) && !onset.count(1)) {

            std::string out = node_prefix;
            result.pmos.push_back({"MP" + std::to_string(global_tid), orig_name, out, "VDD", "VDD", true, w_p, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid), orig_name, out, "VSS", "VSS", false, w_n, l});
            global_tid++;
            result.output_node = out;
            result.output_inverted = true;

            result.output_inverted = false;
            return result;
        }
        if (!onset.count(0) && onset.count(1)) {

            result.output_node = orig_name;
            result.output_inverted = false;
            return result;
        }

        result.output_node = "VDD";
        return result;
    }

    auto offset_sop = quine_mccluskey(n_vars, offset);
    auto onset_sop = quine_mccluskey(n_vars, onset);

    std::map<int, std::string> var_name_map;
    for (int i = 0; i < n_vars; i++)
        var_name_map[i] = get_var_name(i);

    if (!sop_has_complement(offset_sop, n_vars)) {

        auto tree = merge_common_quotients(factor_sop(offset_sop, n_vars));
        std::string out = node_prefix;
        int tn = 0, tp = 0;
        tree_to_nmos(*tree, out, "VSS", n_vars, w_n, l, node_prefix, tn, var_name_map, result.nmos);
        tree_to_pmos(*tree, out, "VDD", n_vars, w_p, l, node_prefix, tp, var_name_map, result.pmos);
        global_tid += std::max(tn, tp);
        result.output_node = out;

        result.output_inverted = false;
        return result;
    }

    if (!sop_has_complement(onset_sop, n_vars) && !g_complementary) {

        auto tree = merge_common_quotients(factor_sop(onset_sop, n_vars));
        std::string out = node_prefix;
        int tn = 0, tp = 0;
        tree_to_nmos(*tree, out, "VSS", n_vars, w_n, l, node_prefix, tn, var_name_map, result.nmos);
        tree_to_pmos(*tree, out, "VDD", n_vars, w_p, l, node_prefix, tp, var_name_map, result.pmos);
        global_tid += std::max(tn, tp);
        result.output_node = out;
        result.output_inverted = true;
        return result;
    }

    struct Candidate {
        int gate_tr;
        SOP combining_sop;
        int combining_lits;
        std::string gate_type;
        std::vector<int> gate_inputs;
        int ext_var;
        bool output_inverted = false;
        std::vector<int> gate_tt_stored;
    };

    int best_tr = 999999;
    Candidate best;

    auto try_gate = [&](const std::string& gtype, const std::vector<int>& gvars) {

        int orig_total = 1 << n_vars;
        std::vector<int> gate_tt(orig_total);
        for (int m = 0; m < orig_total; m++) {
            if (gtype == "NAND") {
                int all_one = 1;
                for (int v : gvars) if (!((m >> (n_vars - 1 - v)) & 1)) all_one = 0;
                gate_tt[m] = 1 - all_one;
            } else if (gtype == "NOR") {
                int any_one = 0;
                for (int v : gvars) if ((m >> (n_vars - 1 - v)) & 1) any_one = 1;
                gate_tt[m] = 1 - any_one;
            } else if (gtype == "AOI21") {

                int s  = (m >> (n_vars - 1 - gvars[0])) & 1;
                int p0 = (m >> (n_vars - 1 - gvars[1])) & 1;
                int p1 = (m >> (n_vars - 1 - gvars[2])) & 1;
                gate_tt[m] = 1 - (s | (p0 & p1));
            } else {
                int s  = (m >> (n_vars - 1 - gvars[0])) & 1;
                int p0 = (m >> (n_vars - 1 - gvars[1])) & 1;
                int p1 = (m >> (n_vars - 1 - gvars[2])) & 1;
                gate_tt[m] = 1 - (s & (p0 | p1));
            }
        }

        int ext_vars = n_vars + 1;
        int ext_var_idx = n_vars;

        std::set<int> ext_offset, ext_dc;
        std::set<int> valid_ext;
        for (int m = 0; m < orig_total; m++) {
            int ext_m = (m << 1) | gate_tt[m];
            valid_ext.insert(ext_m);
            if (!onset.count(m)) ext_offset.insert(ext_m);
        }

        int ext_total = 1 << ext_vars;
        for (int em = 0; em < ext_total; em++)
            if (!valid_ext.count(em)) ext_dc.insert(em);

        int gate_tr = (int)gvars.size() * 2;

        std::set<int> ext_onset_valid;
        for (int m = 0; m < orig_total; m++) {
            int ext_m = (m << 1) | gate_tt[m];
            if (onset.count(m)) ext_onset_valid.insert(ext_m);
        }

        {
            SOP sop = qm_with_dc(ext_vars, ext_offset, ext_dc);
            if (!sop.empty() && !sop_has_complement(sop, ext_vars)) {

                bool covers_onset = false;
                for (int m : ext_onset_valid)
                    for (auto& t : sop)
                        if (qm_covers(t, m, ext_vars)) { covers_onset = true; break; }
                if (covers_onset) goto skip_path1;

                auto tree = factor_sop(sop, ext_vars);
                int total_tr = gate_tr + tree->literal_count() * 2;
                if (total_tr < best_tr) {
                    best_tr = total_tr;
                    best.gate_tr = gate_tr;
                    best.combining_sop = sop;
                    best.combining_lits = tree->literal_count();
                    best.gate_type = gtype;
                    best.gate_inputs = gvars;
                    best.ext_var = ext_var_idx;
                    best.output_inverted = false;
                    best.gate_tt_stored = gate_tt;
                }
            }
        }

        skip_path1:;

        {
            std::set<int> ext_onset;
            for (int m = 0; m < orig_total; m++) {
                int ext_m = (m << 1) | gate_tt[m];
                if (onset.count(m)) ext_onset.insert(ext_m);
            }
            SOP sop = qm_with_dc(ext_vars, ext_onset, ext_dc);
            if (!sop.empty() && !sop_has_complement(sop, ext_vars)) {

                bool covers_offset = false;
                for (int m : ext_offset)
                    for (auto& t : sop)
                        if (qm_covers(t, m, ext_vars)) { covers_offset = true; break; }
                if (!covers_offset) {
                auto tree = factor_sop(sop, ext_vars);
                int total_tr = gate_tr + tree->literal_count() * 2 + 2;
                if (total_tr < best_tr) {
                    best_tr = total_tr;
                    best.gate_tr = gate_tr;
                    best.combining_sop = sop;
                    best.combining_lits = tree->literal_count();
                    best.gate_type = gtype;
                    best.gate_inputs = gvars;
                    best.ext_var = ext_var_idx;
                    best.output_inverted = true;
                    best.gate_tt_stored = gate_tt;
                }
            }
            }
        }
    };

    for (int i = 0; i < n_vars; i++) {
        try_gate("NOR", {i});
    }

    for (int i = 0; i < n_vars; i++)
        for (int j = i + 1; j < n_vars; j++) {
            try_gate("NAND", {i, j});
            try_gate("NOR", {i, j});
        }

    if (n_vars >= 3)
        for (int i = 0; i < n_vars; i++)
            for (int j = i+1; j < n_vars; j++)
                for (int k = j+1; k < n_vars; k++) {
                    try_gate("NAND", {i, j, k});
                    try_gate("NOR", {i, j, k});
                }

    if (n_vars >= 3) {
        std::vector<int> all_vars(n_vars);
        std::iota(all_vars.begin(), all_vars.end(), 0);
        for (int si = 0; si < n_vars; si++) {
            std::vector<int> pairs;
            for (int v = 0; v < n_vars; v++) if (v != si) pairs.push_back(v);
            for (int pi = 0; pi < (int)pairs.size(); pi++)
                for (int pj = pi + 1; pj < (int)pairs.size(); pj++) {
                    try_gate("AOI21", {si, pairs[pi], pairs[pj]});
                    try_gate("OAI21", {si, pairs[pi], pairs[pj]});
                }
        }
    }

    if (n_vars <= 3 && best_tr < 999999) {
        unsigned ftt = onset_to_tt(onset, n_vars);
        unsigned inv_mask = (1u << (1u << n_vars)) - 1u;
        for (int inv = 0; inv <= 1; inv++) {
            unsigned target = (inv == 0) ? ftt : (inv_mask ^ ftt);
            SmallSynthSolution sol = search_small_gate_library(target, n_vars);
            if (!sol.found || sol.cost >= best_tr || sol.cost > max_tr) continue;

            BinateResult sg;
            std::unordered_map<int, std::string> imap;
            for (int v = 0; v < n_vars; v++) imap[v] = get_var_name(v);
            for (const auto& gate : sol.gates) {
                std::vector<std::string> ins;
                ins.reserve(gate.inputs.size());
                for (int id : gate.inputs) ins.push_back(imap.at(id));
                bool is_final = (gate.output_id == sol.output_id);
                std::string out_node = is_final ? node_prefix
                                     : (node_prefix + "sg" + std::to_string(gate.output_id));
                append_small_gate_transistors(gate.type, ins, out_node,
                                              w_n, w_p, l, global_tid,
                                              sg.pmos, sg.nmos);
                imap[gate.output_id] = out_node;
            }
            sg.output_node     = node_prefix;
            sg.output_inverted = (inv == 1);
            return sg;
        }
    }

    if (best_tr < 999999) {

        std::string int_node = node_prefix + "g";

        std::map<int, std::string> ext_var_names;
        for (int i = 0; i < n_vars; i++)
            ext_var_names[i] = get_var_name(i);
        ext_var_names[best.ext_var] = int_node;

        if (best.gate_type == "NAND") {

            for (int v : best.gate_inputs)
                result.pmos.push_back({"MP" + std::to_string(global_tid++),
                    ext_var_names[v], int_node, "VDD", "VDD", true, w_p, l});
            std::string prev = int_node;
            for (size_t i = 0; i < best.gate_inputs.size(); i++) {
                std::string next = (i + 1 < best.gate_inputs.size())
                    ? node_prefix + "gs" + std::to_string(i) : "VSS";
                result.nmos.push_back({"MN" + std::to_string(global_tid++),
                    ext_var_names[best.gate_inputs[i]], prev, next, "VSS", false, w_n, l});
                prev = next;
            }
        } else if (best.gate_type == "NOR") {
            std::string prev = "VDD";
            for (size_t i = 0; i < best.gate_inputs.size(); i++) {
                std::string next = (i + 1 < best.gate_inputs.size())
                    ? node_prefix + "gp" + std::to_string(i) : int_node;
                result.pmos.push_back({"MP" + std::to_string(global_tid++),
                    ext_var_names[best.gate_inputs[i]], next, prev, "VDD", true, w_p, l});
                prev = next;
            }
            for (int v : best.gate_inputs)
                result.nmos.push_back({"MN" + std::to_string(global_tid++),
                    ext_var_names[v], int_node, "VSS", "VSS", false, w_n, l});
        } else if (best.gate_type == "AOI21") {

            int s  = best.gate_inputs[0];
            int p0 = best.gate_inputs[1];
            int p1 = best.gate_inputs[2];

            std::string gnb = node_prefix + "gnb";
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[s], int_node, "VSS", "VSS", false, w_n, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[p0], int_node, gnb, "VSS", false, w_n, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[p1], gnb, "VSS", "VSS", false, w_n, l});

            std::string gpa = node_prefix + "gpa";
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[s], gpa, "VDD", "VDD", true, w_p, l});
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[p0], int_node, gpa, "VDD", true, w_p, l});
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[p1], int_node, gpa, "VDD", true, w_p, l});
        } else {

            int s  = best.gate_inputs[0];
            int p0 = best.gate_inputs[1];
            int p1 = best.gate_inputs[2];

            std::string gna = node_prefix + "gna";
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[s], int_node, gna, "VSS", false, w_n, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[p0], gna, "VSS", "VSS", false, w_n, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid++),
                ext_var_names[p1], gna, "VSS", "VSS", false, w_n, l});

            std::string gpb = node_prefix + "gpb";
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[s], int_node, "VDD", "VDD", true, w_p, l});
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[p0], int_node, gpb, "VDD", true, w_p, l});
            result.pmos.push_back({"MP" + std::to_string(global_tid++),
                ext_var_names[p1], gpb, "VDD", "VDD", true, w_p, l});
        }

        result.aux_gate_node = int_node;
        result.aux_gate_pmos = result.pmos;
        result.aux_gate_nmos = result.nmos;
        if (!best.gate_tt_stored.empty()) {
            result.aux_gate_tt.resize(best.gate_tt_stored.size());
            for (size_t _i = 0; _i < best.gate_tt_stored.size(); _i++)
                result.aux_gate_tt[_i] = (best.gate_tt_stored[_i] != 0);
        }

        int ext_vars = n_vars + 1;
        auto tree = factor_sop(best.combining_sop, ext_vars);
        std::string out = node_prefix;
        int tn = 0, tp = 0;
        tree_to_nmos(*tree, out, "VSS", ext_vars, w_n, l, node_prefix + "c", tn, ext_var_names, result.nmos);
        tree_to_pmos(*tree, out, "VDD", ext_vars, w_p, l, node_prefix + "c", tp, ext_var_names, result.pmos);
        global_tid += std::max(tn, tp);

        if (best.output_inverted) {

            result.output_node     = out;
            result.output_inverted = true;
        } else {
            result.output_node     = out;
            result.output_inverted = false;
        }
        return result;
    }

    {

        std::map<int, int> pos_c, neg_c;
        for (auto& t : onset_sop)
            for (int i = 0; i < n_vars; i++) {
                if (t[i] == '1') pos_c[i]++;
                if (t[i] == '0') neg_c[i]++;
            }

        BinateResult best_shannon;
        best_shannon.infeasible = true;
        int best_shannon_tr = INT_MAX;

        int sub_budget = (max_tr > 10) ? (max_tr - 10) : 0;
        int snv = n_vars - 1;

        for (int sv_cand = 0; sv_cand < n_vars; sv_cand++) {
            if (!pos_c.count(sv_cand) && !neg_c.count(sv_cand)) continue;

            std::set<int> c0on_c, c1on_c;
            std::vector<int> svm_c;
            for (int v = 0; v < n_vars; v++) if (v != sv_cand) svm_c.push_back(var_map[v]);

            for (int m : onset) {
                int bit = (m >> (n_vars - 1 - sv_cand)) & 1;
                int red = 0, pb = 0;
                for (int v = n_vars - 1; v >= 0; v--) {
                    if (v == sv_cand) continue;
                    if ((m >> (n_vars - 1 - v)) & 1) red |= (1 << pb);
                    pb++;
                }
                if (bit) c1on_c.insert(red); else c0on_c.insert(red);
            }

            std::string sp = node_prefix + "sv" + std::to_string(sv_cand);

            std::vector<std::string> sub_explicit;
            if (!explicit_names.empty()) {
                for (int v = 0; v < n_vars; v++) if (v != sv_cand) sub_explicit.push_back(get_var_name(v));
            }

            auto r1 = generate_cmos_recursive(c1on_c, snv, svm_c, sp + "h", w_n, w_p, l, global_tid, sub_budget, sub_explicit);
            if (r1.infeasible) continue;
            auto r0 = generate_cmos_recursive(c0on_c, snv, svm_c, sp + "l", w_n, w_p, l, global_tid, sub_budget, sub_explicit);
            if (r0.infeasible) continue;

            if (!r1.aux_gate_node.empty() && !r0.aux_gate_node.empty() &&
                !r1.aux_gate_tt.empty() && !r0.aux_gate_tt.empty()) {
                int snv_size = 1 << snv;
                if ((int)r0.aux_gate_tt.size() == snv_size && (int)r1.aux_gate_tt.size() == snv_size) {
                    bool nand_match = true;
                    for (int _m = 0; _m < snv_size && nand_match; _m++) {
                        int r0v = r0.aux_gate_tt[_m] ? 1 : 0;
                        int r1v = r1.aux_gate_tt[_m] ? 1 : 0;
                        int nand_val = 1 - (r0v & (1 - r1v));
                        int expected = c0on_c.count(_m) ? 1 : 0;
                        if (nand_val != expected) nand_match = false;
                    }
                    if (nand_match) {
                        int old_comb_tr = (int)(r0.pmos.size() + r0.nmos.size())
                                        - (int)(r0.aux_gate_pmos.size() + r0.aux_gate_nmos.size());
                        if (6 < old_comb_tr) {
                            BinateResult new_r0;
                            new_r0.pmos = r0.aux_gate_pmos;
                            new_r0.nmos = r0.aux_gate_nmos;
                            new_r0.aux_gate_node = r0.aux_gate_node;
                            new_r0.aux_gate_tt   = r0.aux_gate_tt;
                            new_r0.aux_gate_pmos = r0.aux_gate_pmos;
                            new_r0.aux_gate_nmos = r0.aux_gate_nmos;

                            std::string inv_r1  = sp + "libhg";
                            new_r0.pmos.push_back({"MP" + std::to_string(global_tid),
                                r1.aux_gate_node, inv_r1, "VDD", "VDD", true, w_p, l});
                            new_r0.nmos.push_back({"MN" + std::to_string(global_tid),
                                r1.aux_gate_node, inv_r1, "VSS", "VSS", false, w_n, l});
                            global_tid++;

                            std::string nand_out = sp + "lno";
                            std::string nand_mid = sp + "lnm";
                            new_r0.pmos.push_back({"MP" + std::to_string(global_tid++),
                                r0.aux_gate_node, nand_out, "VDD", "VDD", true, w_p, l});
                            new_r0.pmos.push_back({"MP" + std::to_string(global_tid++),
                                inv_r1, nand_out, "VDD", "VDD", true, w_p, l});
                            new_r0.nmos.push_back({"MN" + std::to_string(global_tid++),
                                r0.aux_gate_node, nand_out, nand_mid, "VSS", false, w_n, l});
                            new_r0.nmos.push_back({"MN" + std::to_string(global_tid++),
                                inv_r1, nand_mid, "VSS", "VSS", false, w_n, l});
                            new_r0.output_node     = nand_out;
                            new_r0.output_inverted = false;
                            r0 = std::move(new_r0);
                        }
                    }
                }
            }

            BinateResult cand;
            cand.pmos.insert(cand.pmos.end(), r1.pmos.begin(), r1.pmos.end());
            cand.nmos.insert(cand.nmos.end(), r1.nmos.begin(), r1.nmos.end());
            cand.pmos.insert(cand.pmos.end(), r0.pmos.begin(), r0.pmos.end());
            cand.nmos.insert(cand.nmos.end(), r0.nmos.begin(), r0.nmos.end());

            std::string n1, n0;
            if (r1.output_inverted) { n1 = r1.output_node; }
            else {
                n1 = sp + "i1";
                cand.pmos.push_back({"MP" + std::to_string(global_tid), r1.output_node, n1, "VDD", "VDD", true, w_p, l});
                cand.nmos.push_back({"MN" + std::to_string(global_tid), r1.output_node, n1, "VSS", "VSS", false, w_n, l});
                global_tid++;
            }
            if (r0.output_inverted) { n0 = r0.output_node; }
            else {
                n0 = sp + "i0";
                cand.pmos.push_back({"MP" + std::to_string(global_tid), r0.output_node, n0, "VDD", "VDD", true, w_p, l});
                cand.nmos.push_back({"MN" + std::to_string(global_tid), r0.output_node, n0, "VSS", "VSS", false, w_n, l});
                global_tid++;
            }

            std::string x  = get_var_name(sv_cand);
            std::string xb = sp + "xb";
            cand.pmos.push_back({"MP" + std::to_string(global_tid), x, xb, "VDD", "VDD", true, w_p, l});
            cand.nmos.push_back({"MN" + std::to_string(global_tid), x, xb, "VSS", "VSS", false, w_n, l});
            global_tid++;

            std::string out  = sp + "s";
            std::string mid  = sp + "sm";
            std::string csp0 = sp + "sp0";
            std::string csp1 = sp + "sp1";
            cand.nmos.push_back({"MN" + std::to_string(global_tid++), xb, out, mid,   "VSS", false, w_n, l});
            cand.nmos.push_back({"MN" + std::to_string(global_tid++), n1,  out, mid,   "VSS", false, w_n, l});
            cand.nmos.push_back({"MN" + std::to_string(global_tid++), x,   mid, "VSS", "VSS", false, w_n, l});
            cand.nmos.push_back({"MN" + std::to_string(global_tid++), n0,  mid, "VSS", "VSS", false, w_n, l});
            cand.pmos.push_back({"MP" + std::to_string(global_tid++), xb,  out, csp0,  "VDD", true,  w_p, l});
            cand.pmos.push_back({"MP" + std::to_string(global_tid++), n1,  csp0,"VDD", "VDD", true,  w_p, l});
            cand.pmos.push_back({"MP" + std::to_string(global_tid++), x,   out, csp1,  "VDD", true,  w_p, l});
            cand.pmos.push_back({"MP" + std::to_string(global_tid++), n0,  csp1,"VDD", "VDD", true,  w_p, l});

            cand.output_node     = out;
            cand.output_inverted = false;

            int cand_tr = (int)(cand.pmos.size() + cand.nmos.size());
            if (cand_tr <= max_tr && cand_tr < best_shannon_tr) {
                best_shannon_tr = cand_tr;
                best_shannon = std::move(cand);
            }
        }

        int direct_tr = sop_literal_count(onset_sop) * 2 + 2;
        std::set<int> cv;
        for (auto& t : onset_sop) for (int i = 0; i < n_vars; i++) if (t[i] == '0') cv.insert(i);
        direct_tr += (int)cv.size() * 2;

        auto dsd = try_dsd_decomp(onset, n_vars, var_map, node_prefix, w_n, w_p, l, global_tid, max_tr, explicit_names);
        int dsd_tr = dsd.infeasible ? INT_MAX : (int)(dsd.pmos.size() + dsd.nmos.size());

        if (!dsd.infeasible && dsd_tr < best_shannon_tr && dsd_tr <= direct_tr && dsd_tr <= max_tr)
            return dsd;
        if (!best_shannon.infeasible && best_shannon_tr <= direct_tr && best_shannon_tr <= max_tr)
            return best_shannon;
        if (direct_tr > max_tr) {
            BinateResult fail; fail.infeasible = true; return fail;
        }
    }

    {
        std::map<int, std::string> vnames;
        for (int i = 0; i < n_vars; i++)
            vnames[i] = get_var_name(i);

        std::string out = node_prefix;

        std::set<int> comp_vars;
        for (auto& t : onset_sop)
            for (int i = 0; i < n_vars; i++)
                if (t[i] == '0') comp_vars.insert(i);

        for (int vi : comp_vars) {
            std::string bar = vnames[vi] + "_bar";
            result.pmos.push_back({"MP" + std::to_string(global_tid), vnames[vi], bar, "VDD", "VDD", true, w_p, l});
            result.nmos.push_back({"MN" + std::to_string(global_tid), vnames[vi], bar, "VSS", "VSS", false, w_n, l});
            global_tid++;
            vnames[vi + 100] = bar;
        }

        for (size_t ti = 0; ti < onset_sop.size(); ti++) {
            auto& term = onset_sop[ti];
            std::vector<std::string> gates;
            for (int i = 0; i < n_vars; i++) {
                if (term[i] == '-') continue;
                if (term[i] == '0') gates.push_back(vnames.count(i+100) ? vnames[i+100] : vnames[i] + "_bar");
                else gates.push_back(vnames[i]);
            }
            if (gates.size() == 1) {
                result.nmos.push_back({"MN" + std::to_string(global_tid++), gates[0], out, "VSS", "VSS", false, w_n, l});
            } else {
                std::string prev = out;
                for (size_t j = 0; j < gates.size(); j++) {
                    std::string next = (j+1 < gates.size()) ? node_prefix+"d"+std::to_string(ti)+"_"+std::to_string(j) : "VSS";
                    result.nmos.push_back({"MN" + std::to_string(global_tid++), gates[j], prev, next, "VSS", false, w_n, l});
                    prev = next;
                }
            }
        }

        if (onset_sop.size() == 1) {
            auto& term = onset_sop[0];
            for (int i = 0; i < n_vars; i++) {
                if (term[i] == '-') continue;
                std::string gate = (term[i] == '0') ? (vnames.count(i+100) ? vnames[i+100] : vnames[i]+"_bar") : vnames[i];
                result.pmos.push_back({"MP" + std::to_string(global_tid++), gate, out, "VDD", "VDD", true, w_p, l});
            }
        } else {
            std::vector<std::string> cn = {"VDD"};
            for (size_t ti = 0; ti+1 < onset_sop.size(); ti++) cn.push_back(node_prefix+"u"+std::to_string(ti));
            cn.push_back(out);
            for (size_t ti = 0; ti < onset_sop.size(); ti++) {
                auto& term = onset_sop[ti];
                for (int i = 0; i < n_vars; i++) {
                    if (term[i] == '-') continue;
                    std::string gate = (term[i] == '0') ? (vnames.count(i+100) ? vnames[i+100] : vnames[i]+"_bar") : vnames[i];
                    result.pmos.push_back({"MP" + std::to_string(global_tid++), gate, cn[ti+1], cn[ti], "VDD", true, w_p, l});
                }
            }
        }

        result.output_node = out;
        result.output_inverted = true;
        return result;
    }
}

static BinateResult build_combine_diff(
        int adj_combine,
        const std::string& g_node, const std::string& g_bar,
        const std::string& h_node, const std::string& h_bar,
        const std::string& node_prefix,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& global_tid) {

    BinateResult r;

    std::string gn = g_node, gb = g_bar, hn = h_node, hb = h_bar;
    if (gb.empty()) {
        gb = node_prefix + "cgb";
        r.pmos.push_back({"MP" + std::to_string(global_tid), gn, gb, "VDD", "VDD", true, w_p, l});
        r.nmos.push_back({"MN" + std::to_string(global_tid), gn, gb, "VSS", "VSS", false, w_n, l});
        global_tid++;
    }
    if (hb.empty()) {
        hb = node_prefix + "chb";
        r.pmos.push_back({"MP" + std::to_string(global_tid), hn, hb, "VDD", "VDD", true, w_p, l});
        r.nmos.push_back({"MN" + std::to_string(global_tid), hn, hb, "VSS", "VSS", false, w_n, l});
        global_tid++;
    }

    std::string out = node_prefix + "co";

    auto sig_g = [&](int gv) -> std::string { return gv ? gn : gb; };
    auto sig_h = [&](int hv) -> std::string { return hv ? hn : hb; };

    std::vector<std::pair<int,int>> off_terms, on_terms;
    for (int g = 0; g < 2; g++)
        for (int h = 0; h < 2; h++) {
            int f = (adj_combine >> (g*2+h)) & 1;
            if (f == 0) off_terms.push_back({g, h});
            else        on_terms.push_back({g, h});
        }

    for (auto& [gv, hv] : off_terms) {
        std::string mid = node_prefix + "pm" + std::to_string(gv) + std::to_string(hv);
        r.nmos.push_back({"MN" + std::to_string(global_tid++), sig_g(gv), out, mid, "VSS", false, w_n, l});
        r.nmos.push_back({"MN" + std::to_string(global_tid++), sig_h(hv), mid, "VSS", "VSS", false, w_n, l});
    }

    auto sig_g_p = [&](int gv) -> std::string { return gv ? gb : gn; };
    auto sig_h_p = [&](int hv) -> std::string { return hv ? hb : hn; };
    for (auto& [gv, hv] : on_terms) {
        std::string mid = node_prefix + "um" + std::to_string(gv) + std::to_string(hv);
        r.pmos.push_back({"MP" + std::to_string(global_tid++), sig_g_p(gv), out, mid, "VDD", true, w_p, l});
        r.pmos.push_back({"MP" + std::to_string(global_tid++), sig_h_p(hv), mid, "VDD", "VDD", true, w_p, l});
    }

    r.output_node = out;
    r.output_inverted = false;
    return r;
}

static BinateResult try_dsd_decomp(
        const std::set<int>& onset, int n_vars,
        const std::vector<int>& var_map,
        const std::string& node_prefix,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        int& global_tid,
        int max_tr,
        const std::vector<std::string>& explicit_names) {

    if (n_vars < 2) { BinateResult f; f.infeasible = true; return f; }

    auto get_var_name = [&](int local_idx) -> std::string {
        if (!explicit_names.empty() && local_idx < (int)explicit_names.size())
            return explicit_names[local_idx];
        return input_name(var_map[local_idx], (int)var_map.size());
    };

    BinateResult best_result; best_result.infeasible = true;
    int best_tr = max_tr;

    for (int mask = 1; mask < (1 << n_vars) - 1; mask++) {

        if (!(mask & 1)) continue;

        int s1_size = __builtin_popcount(mask);
        int s2_size = n_vars - s1_size;

        std::vector<int> s1_vars, s2_vars;
        for (int i = 0; i < n_vars; i++) {
            if (mask & (1 << i)) s1_vars.push_back(i);
            else s2_vars.push_back(i);
        }

        int g_entries = 1 << s1_size;
        int h_entries = 1 << s2_size;

        for (int g_tt = 0; g_tt < (1 << g_entries); g_tt++) {
            for (int h_tt = 0; h_tt < (1 << h_entries); h_tt++) {

                int combine_tt = 0;
                bool consistent = true;
                int seen_mask = 0;

                for (int x = 0; x < (1 << n_vars); x++) {

                    int g_idx = 0;
                    for (int k = 0; k < s1_size; k++)
                        if ((x >> (n_vars - 1 - s1_vars[k])) & 1)
                            g_idx |= (1 << (s1_size - 1 - k));
                    int g_val = (g_tt >> g_idx) & 1;

                    int h_idx = 0;
                    for (int k = 0; k < s2_size; k++)
                        if ((x >> (n_vars - 1 - s2_vars[k])) & 1)
                            h_idx |= (1 << (s2_size - 1 - k));
                    int h_val = (h_tt >> h_idx) & 1;

                    int cidx = (g_val << 1) | h_val;
                    int f_val = onset.count(x) ? 1 : 0;

                    if (seen_mask & (1 << cidx)) {
                        if (((combine_tt >> cidx) & 1) != f_val) { consistent = false; break; }
                    } else {
                        seen_mask |= (1 << cidx);
                        if (f_val) combine_tt |= (1 << cidx);
                    }
                }

                if (!consistent) continue;

                int sub_budget = max_tr / 3;

                std::set<int> g_onset;
                for (int gi = 0; gi < g_entries; gi++) if ((g_tt >> gi) & 1) g_onset.insert(gi);
                std::vector<int> g_vmap;
                std::vector<std::string> g_names;
                for (int k : s1_vars) {
                    g_vmap.push_back(var_map[k]);
                    g_names.push_back(get_var_name(k));
                }

                std::set<int> h_onset;
                for (int hi = 0; hi < h_entries; hi++) if ((h_tt >> hi) & 1) h_onset.insert(hi);
                std::vector<int> h_vmap;
                std::vector<std::string> h_names;
                for (int k : s2_vars) {
                    h_vmap.push_back(var_map[k]);
                    h_names.push_back(get_var_name(k));
                }

                int saved_tid = global_tid;

                auto rg = generate_cmos_recursive(g_onset, s1_size, g_vmap, node_prefix + "dg",
                                                  w_n, w_p, l, global_tid, sub_budget, g_names);
                if (rg.infeasible) { global_tid = saved_tid; continue; }

                auto rh = generate_cmos_recursive(h_onset, s2_size, h_vmap, node_prefix + "dh",
                                                  w_n, w_p, l, global_tid, sub_budget, h_names);
                if (rh.infeasible) { global_tid = saved_tid; continue; }

                int adj_combine = combine_tt;
                if (rg.output_inverted) {

                    adj_combine = ((adj_combine & 0x3) << 2) | ((adj_combine & 0xC) >> 2);
                }
                if (rh.output_inverted) {

                    int tmp = 0;
                    for (int ci = 0; ci < 4; ci++) {
                        int other = ci ^ 1;
                        if ((adj_combine >> ci) & 1) tmp |= (1 << other);
                    }
                    adj_combine = tmp;
                }

                std::set<int> combining_onset_dc;
                for (int m = 0; m < 4; m++)
                    if ((adj_combine >> m) & 1) combining_onset_dc.insert(m);
                std::vector<int> combining_vmap_dc = {0, 1};
                std::vector<std::string> combining_names_dc = {rg.output_node, rh.output_node};
                int sub_tr_used_dc = (int)(rg.pmos.size() + rg.nmos.size()
                                         + rh.pmos.size() + rh.nmos.size());
                int combine_budget_dc = (max_tr == INT_MAX) ? INT_MAX : max_tr - sub_tr_used_dc;

                int tid_a_dc = global_tid;
                BinateResult rc_a_dc = generate_cmos_recursive(
                    combining_onset_dc, 2, combining_vmap_dc,
                    node_prefix + "dc", w_n, w_p, l,
                    tid_a_dc, combine_budget_dc, combining_names_dc);
                int tr_a_dc = rc_a_dc.infeasible ? INT_MAX
                            : (int)(rc_a_dc.pmos.size() + rc_a_dc.nmos.size());

                int tid_b_dc = global_tid;
                BinateResult rc_b_dc = build_combine_diff(adj_combine,
                                                          rg.output_node, rg.compl_node,
                                                          rh.output_node, rh.compl_node,
                                                          node_prefix + "dc", w_n, w_p, l, tid_b_dc);
                int tr_b_dc = (int)(rc_b_dc.pmos.size() + rc_b_dc.nmos.size());

                BinateResult rc;
                if (tr_a_dc <= tr_b_dc) { rc = std::move(rc_a_dc); global_tid = tid_a_dc; }
                else                    { rc = std::move(rc_b_dc); global_tid = tid_b_dc; }

                int total_tr = (int)(rg.pmos.size() + rg.nmos.size()
                                   + rh.pmos.size() + rh.nmos.size()
                                   + rc.pmos.size() + rc.nmos.size());
                if (total_tr > max_tr || total_tr >= best_tr) { global_tid = saved_tid; continue; }

                best_tr = total_tr;
                best_result.infeasible = false;
                best_result.pmos.clear(); best_result.nmos.clear();
                best_result.pmos.insert(best_result.pmos.end(), rg.pmos.begin(), rg.pmos.end());
                best_result.nmos.insert(best_result.nmos.end(), rg.nmos.begin(), rg.nmos.end());
                best_result.pmos.insert(best_result.pmos.end(), rh.pmos.begin(), rh.pmos.end());
                best_result.nmos.insert(best_result.nmos.end(), rh.nmos.begin(), rh.nmos.end());
                best_result.pmos.insert(best_result.pmos.end(), rc.pmos.begin(), rc.pmos.end());
                best_result.nmos.insert(best_result.nmos.end(), rc.nmos.begin(), rc.nmos.end());
                best_result.output_node = rc.output_node;
                best_result.output_inverted = rc.output_inverted;
                best_result.compl_node = rc.compl_node;
            }
        }
    }
    return best_result;
}

static double parse_width_nm(const std::string& w) {
    if (w.empty()) return 0.0;
    char unit = w.back();
    if (unit == 'n' || unit == 'N') {
        try { return std::stod(w.substr(0, w.size() - 1)); } catch (...) { return 0.0; }
    }
    if (unit == 'u' || unit == 'U') {
        try { return std::stod(w.substr(0, w.size() - 1)) * 1000.0; } catch (...) { return 0.0; }
    }
    try { return std::stod(w); } catch (...) { return 0.0; }
}

static int cpp_fingers(double width_nm, double wmax_nm) {
    if (wmax_nm <= 0.0) return 1;
    return static_cast<int>(std::ceil(width_nm / wmax_nm - 1e-9));
}

static int cpp_lcs(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    int m = (int)a.size(), n = (int)b.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; i++)
        for (int j = 1; j <= n; j++)
            dp[i][j] = (a[i-1] == b[j-1]) ? dp[i-1][j-1] + 1
                                            : std::max(dp[i-1][j], dp[i][j-1]);
    return dp[m][n];
}

static std::vector<std::vector<std::string>> weak_adj_gate_seqs(
        const std::vector<Transistor>& trs, double wmax_nm, int max_seqs = 256) {
    int n = (int)trs.size();
    if (n == 0) return {{}};

    std::vector<std::vector<bool>> adj(n, std::vector<bool>(n, false));
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            const auto& di = trs[i].drain;  const auto& si = trs[i].source;
            const auto& dj = trs[j].drain;  const auto& sj = trs[j].source;
            adj[i][j] = (di == dj || di == sj || si == dj || si == sj);
        }

    std::set<std::vector<std::string>> seen;
    std::vector<int> path;
    std::vector<bool> used(n, false);

    auto search_start = std::chrono::steady_clock::now();
    long node_count = 0;
    bool budget_exceeded = false;

    std::function<void(int)> dfs = [&](int prev) {
        if (budget_exceeded) return;
        if ((int)seen.size() >= max_seqs) return;
        if ((++node_count & 0xFFF) == 0) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - search_start).count();
            if (elapsed_ms > 2000) { budget_exceeded = true; return; }
        }
        if ((int)path.size() == n) {
            std::vector<std::string> seq;
            for (int idx : path) {
                int f = cpp_fingers(parse_width_nm(trs[idx].width), wmax_nm);
                for (int k = 0; k < f; k++) seq.push_back(trs[idx].gate);
            }
            seen.insert(seq);
            return;
        }
        for (int j = 0; j < n; j++) {
            if (used[j]) continue;
            if (prev == -1 || adj[prev][j]) {
                used[j] = true;  path.push_back(j);
                dfs(j);
                path.pop_back(); used[j] = false;
            }
        }
    };
    dfs(-1);

    if (seen.empty()) return {{}};
    return std::vector<std::vector<std::string>>(seen.begin(), seen.end());
}

static int compute_cpp_for_candidate(const std::vector<Transistor>& nmos,
                                     const std::vector<Transistor>& pmos) {
    if (nmos.empty() && pmos.empty()) return 0;
    auto n_seqs = weak_adj_gate_seqs(nmos, g_wmax_n_nm);
    auto p_seqs = weak_adj_gate_seqs(pmos, g_wmax_p_nm);
    int best_lcs = 0;
    for (const auto& ns : n_seqs)
        for (const auto& ps : p_seqs)
            best_lcs = std::max(best_lcs, cpp_lcs(ns, ps));
    int n_total = 0, p_total = 0;
    for (const auto& t : nmos) n_total += cpp_fingers(parse_width_nm(t.width), g_wmax_n_nm);
    for (const auto& t : pmos) p_total += cpp_fingers(parse_width_nm(t.width), g_wmax_p_nm);
    return n_total + p_total - best_lcs;
}

static std::string canonical_to_cdl(const std::string& canonical,
                                     const std::string& name,
                                     const std::string& w_n, const std::string& w_p,
                                     const std::string& l,
                                     const std::string& nmos_model,
                                     const std::string& pmos_model,
                                     int max_tr = INT_MAX) {
    auto [n_vars, onset] = parse_canonical(canonical);
    int total = 1 << n_vars;
    std::set<int> all_mt;
    for (int i = 0; i < total; i++) all_mt.insert(i);
    std::set<int> offset;
    for (int i = 0; i < total; i++)
        if (!onset.count(i)) offset.insert(i);

    if (onset.empty() || offset.empty()) {
        std::vector<Transistor> c_pmos, c_nmos;
        std::string func_str;
        if (onset.empty()) {
            c_nmos.push_back({"MN_tie0", "VDD", "Y", "VSS", "VSS", false, w_n, l});
            func_str = "Y = 0 [constant, tied to VSS]";
        } else {
            c_pmos.push_back({"MP_tie1", "VSS", "Y", "VDD", "VDD", true, w_p, l});
            func_str = "Y = 1 [constant, tied to VDD]";
        }
        adjust_sizing(c_nmos, c_pmos);
        return emit_cdl(name, n_vars, c_pmos, c_nmos, canonical, func_str,
                        nmos_model, pmos_model);
    }

    auto offset_sop = quine_mccluskey(n_vars, offset);
    auto onset_sop = quine_mccluskey(n_vars, onset);

    std::vector<Transistor> pmos_list, nmos_list;
    std::string func_str;

    if (!sop_has_complement(offset_sop, n_vars)) {

        auto tree = merge_common_quotients(factor_sop(offset_sop, n_vars));
        int tid_n = 0, tid_p = 0;
        std::map<int, std::string> vm;
        tree_to_nmos(*tree, "Y", "VSS", n_vars, w_n, l, "M", tid_n, vm, nmos_list);
        tree_to_pmos(*tree, "Y", "VDD", n_vars, w_p, l, "M", tid_p, vm, pmos_list);
        func_str = "Y = !(" + tree_to_expr(*tree, n_vars) + ") [" +
                   std::to_string(tree->literal_count()) + " lits, SOP was " +
                   std::to_string(sop_literal_count(offset_sop)) + "]";
    } else if (!sop_has_complement(onset_sop, n_vars) && !g_complementary) {

        auto tree = merge_common_quotients(factor_sop(onset_sop, n_vars));
        int tid_n = 0, tid_p = 0;
        std::map<int, std::string> vm;
        tree_to_nmos(*tree, "Y_bar", "VSS", n_vars, w_n, l, "M", tid_n, vm, nmos_list);
        tree_to_pmos(*tree, "Y_bar", "VDD", n_vars, w_p, l, "M", tid_p, vm, pmos_list);
        pmos_list.push_back({"MP_inv", "Y_bar", "Y", "VDD", "VDD", true, w_p, l});
        nmos_list.push_back({"MN_inv", "Y_bar", "Y", "VSS", "VSS", false, w_n, l});
        func_str = "Y = " + tree_to_expr(*tree, n_vars) + " [" +
                   std::to_string(tree->literal_count()) + " lits]";
    } else if (!sop_has_complement(onset_sop, n_vars) && g_complementary) {

        std::map<int, std::string> vm;
        for (int i = 0; i < n_vars; i++) vm[i] = input_name(i, n_vars);
        int gtid = 0;
        auto br = build_nand_nand(onset_sop, n_vars, vm, "M", w_n, w_p, l, gtid);
        pmos_list = std::move(br.pmos);
        nmos_list = std::move(br.nmos);
        if (br.output_inverted) {
            pmos_list.push_back({"MP_inv", br.output_node, "Y", "VDD", "VDD", true, w_p, l});
            nmos_list.push_back({"MN_inv", br.output_node, "Y", "VSS", "VSS", false, w_n, l});
        } else if (br.output_node != "Y") {
            for (auto& t : pmos_list) { if (t.drain==br.output_node) t.drain="Y"; if (t.source==br.output_node) t.source="Y"; }
            for (auto& t : nmos_list) { if (t.drain==br.output_node) t.drain="Y"; if (t.source==br.output_node) t.source="Y"; }
        }
        func_str = "Y = NAND-NAND [complementary, " + std::to_string(pmos_list.size()+nmos_list.size()) + " TR]";
    } else {

        std::vector<int> var_map_init;
        for (int i = 0; i < n_vars; i++) var_map_init.push_back(i);
        std::vector<CandidateNet> candidates;

        bool sp_search_timed_out = false;
        int sp_search_depth_reached = 0;
        {
            auto small_sol = search_small_gate_library(static_cast<unsigned>(onset_to_tt(onset, n_vars)), n_vars);
            sp_search_timed_out = small_sol.timed_out;
            sp_search_depth_reached = small_sol.deepest_depth_attempted;
            if (small_sol.found) {
                auto cand = synthesize_small_gate_solution(small_sol, n_vars, w_n, w_p, l);
                candidates.push_back(std::move(cand));
            }
        }

        if (g_series_parallel && candidates.empty()) {
            std::cerr << "[error] " << name << " (canonical " << canonical
                      << "): no series-parallel CMOS realization found"
                      << " (reached search depth=" << sp_search_depth_reached
                      << (sp_search_timed_out
                              ? "; search hit its 900s internal cap before exhausting the space — this may be a"
                                " genuine 'none exists' result OR an unexplored timeout, not confirmed either way"
                              : "; search space was fully exhausted, confirmed no SP realization exists within this depth")
                      << "); use --no-series-parallel for binate synthesis\n";
            return "";
        }

        if (!g_series_parallel) {
            int gtid_f = 0;
            auto br_f = generate_cmos_recursive(onset, n_vars, var_map_init, "b", w_n, w_p, l, gtid_f, max_tr);
            if (!br_f.infeasible) {
                auto nets = finalize_binate_result(br_f, false, w_n, w_p, l);
                candidates.push_back({std::move(nets.first), std::move(nets.second),
                                      "Y = binate [recursive NAND/NOR, "});
            }

            int gtid_c = 0;
            auto br_not_f = generate_cmos_recursive(offset, n_vars, var_map_init, "bc", w_n, w_p, l, gtid_c, max_tr);
            if (!br_not_f.infeasible) {
                auto nets = finalize_binate_result(br_not_f, true, w_n, w_p, l);
                candidates.push_back({std::move(nets.first), std::move(nets.second),
                                      "Y = binate [recursive NAND/NOR dual, "});
            }
        }

        if (candidates.empty()) {
            std::cerr << "ERROR: " << name << " (canonical " << canonical
                      << "): binate decomposition exceeds TR budget (" << max_tr
                      << " TR) — cell too complex for single-cell implementation\n";
            return "";
        }

        for (auto& cand : candidates)
            cand.cpp_min = compute_cpp_for_candidate(cand.nmos, cand.pmos);

        std::sort(candidates.begin(), candidates.end(),
                  [](const CandidateNet& a, const CandidateNet& b) {
                      int ta = (int)(a.pmos.size() + a.nmos.size());
                      int tb = (int)(b.pmos.size() + b.nmos.size());
                      if (ta != tb) return ta < tb;
                      return a.cpp_min < b.cpp_min;
                  });

        bool debug_candidates = std::getenv("CANONICAL_TO_CDL_DEBUG") != nullptr;
        size_t chosen_idx = 0;
        for (size_t i = 0; i < candidates.size(); i++) {
            auto& cand = candidates[i];
            bool ok = verify_cdl(cand.pmos, cand.nmos, n_vars, canonical);
            if (debug_candidates) {
                std::cerr << "[binate candidate] canonical=" << canonical
                          << " idx=" << i
                          << " tr=" << (cand.pmos.size() + cand.nmos.size())
                          << " ok=" << (ok ? "1" : "0")
                          << " desc=" << cand.desc << "\n";
                if (!ok && std::getenv("CANONICAL_TO_CDL_DUMP_FAIL") != nullptr) {
                    std::cerr << "  [PMOS]\n";
                    for (auto& t : cand.pmos)
                        std::cerr << "    " << t.name << " g=" << t.gate << " d=" << t.drain << " s=" << t.source << "\n";
                    std::cerr << "  [NMOS]\n";
                    for (auto& t : cand.nmos)
                        std::cerr << "    " << t.name << " g=" << t.gate << " d=" << t.drain << " s=" << t.source << "\n";
                }
            }
            if (ok) {
                chosen_idx = i;
                break;
            }
        }
        pmos_list = std::move(candidates[chosen_idx].pmos);
        nmos_list = std::move(candidates[chosen_idx].nmos);
        func_str = candidates[chosen_idx].desc +
                   std::to_string(pmos_list.size() + nmos_list.size()) + " TR]";

        if (sp_search_timed_out) {
            func_str += " [UNPROVEN: SP small-gate search hit its time cap at depth=" +
                        std::to_string(sp_search_depth_reached) +
                        "; a smaller series-parallel realization may exist but was not ruled out]";
        }

    }

    if (n_vars >= 3) {

        try {
            auto abc_cand = synthesize_abc_solution(canonical, n_vars, w_n, w_p, l);
            int cur_tr = (int)(pmos_list.size() + nmos_list.size());
            int abc_tr = (int)(abc_cand.pmos.size() + abc_cand.nmos.size());
            if (abc_tr > 0) {
                bool abc_better = false;
                if (abc_tr < cur_tr) {
                    abc_better = true;
                } else if (abc_tr == cur_tr) {
                    int abc_cpp = compute_cpp_for_candidate(abc_cand.nmos, abc_cand.pmos);
                    int cur_cpp = compute_cpp_for_candidate(nmos_list, pmos_list);
                    abc_better = (abc_cpp < cur_cpp);
                }
                if (abc_better && verify_cdl(abc_cand.pmos, abc_cand.nmos, n_vars, canonical)) {
                    pmos_list = std::move(abc_cand.pmos);
                    nmos_list = std::move(abc_cand.nmos);
                    func_str = abc_cand.desc + std::to_string(abc_tr) + " TR]";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[warn] " << name << " (canonical " << canonical
                      << "): ABC comparison skipped (" << e.what() << ")\n";
        }
    }

    adjust_sizing(nmos_list, pmos_list);

    if (!verify_cdl(pmos_list, nmos_list, n_vars, canonical)) {

        pmos_list.clear();
        nmos_list.clear();

        std::set<int> onset_retry;
        int total_retry = 1 << n_vars;
        for (int i = 0; i < total_retry; i++)
            if (canonical[2 + i] == '1') onset_retry.insert(i);
        auto onset_sop2 = quine_mccluskey(n_vars, onset_retry);
        std::set<int> comp_vars2;
        for (auto& t : onset_sop2)
            for (int i = 0; i < n_vars; i++)
                if (t[i] == '0') comp_vars2.insert(i);
        std::map<int, std::string> var_bar2;
        for (int vi : comp_vars2) {
            std::string bar = input_name(vi, n_vars) + "_bar";
            var_bar2[vi] = bar;
            pmos_list.push_back({"MPbar" + std::to_string(vi), input_name(vi, n_vars), bar, "VDD", "VDD", true, w_p, l});
            nmos_list.push_back({"MNbar" + std::to_string(vi), input_name(vi, n_vars), bar, "VSS", "VSS", false, w_n, l});
        }
        int tid2 = 0;
        for (size_t ti = 0; ti < onset_sop2.size(); ti++) {
            auto& term = onset_sop2[ti];
            std::vector<std::string> gates;
            for (int i = 0; i < n_vars; i++) {
                if (term[i] == '-') continue;
                if (term[i] == '0') gates.push_back(var_bar2.count(i) ? var_bar2.at(i) : input_name(i, n_vars) + "_bar");
                else gates.push_back(input_name(i, n_vars));
            }
            if (gates.size() == 1) {
                nmos_list.push_back({"MN" + std::to_string(tid2++), gates[0], "Y_fb", "VSS", "VSS", false, w_n, l});
            } else {
                std::string prev = "Y_fb";
                for (size_t j = 0; j < gates.size(); j++) {
                    std::string next = (j+1 < gates.size()) ? "nfb" + std::to_string(ti) + "_" + std::to_string(j) : "VSS";
                    nmos_list.push_back({"MN" + std::to_string(tid2++), gates[j], prev, next, "VSS", false, w_n, l});
                    prev = next;
                }
            }
        }
        if (onset_sop2.size() == 1) {
            auto& term = onset_sop2[0];
            for (int i = 0; i < n_vars; i++) {
                if (term[i] == '-') continue;
                std::string gate = (term[i] == '0') ? (var_bar2.count(i) ? var_bar2.at(i) : input_name(i, n_vars)+"_bar") : input_name(i, n_vars);
                pmos_list.push_back({"MP" + std::to_string(tid2++), gate, "Y_fb", "VDD", "VDD", true, w_p, l});
            }
        } else {
            std::vector<std::string> cn = {"VDD"};
            for (size_t ti = 0; ti+1 < onset_sop2.size(); ti++) cn.push_back("npu" + std::to_string(ti));
            cn.push_back("Y_fb");
            for (size_t ti = 0; ti < onset_sop2.size(); ti++) {
                auto& term = onset_sop2[ti];
                for (int i = 0; i < n_vars; i++) {
                    if (term[i] == '-') continue;
                    std::string gate = (term[i] == '0') ? (var_bar2.count(i) ? var_bar2.at(i) : input_name(i, n_vars)+"_bar") : input_name(i, n_vars);
                    pmos_list.push_back({"MP" + std::to_string(tid2++), gate, cn[ti+1], cn[ti], "VDD", true, w_p, l});
                }
            }
        }
        pmos_list.push_back({"MP_fbinv", "Y_fb", "Y", "VDD", "VDD", true, w_p, l});
        nmos_list.push_back({"MN_fbinv", "Y_fb", "Y", "VSS", "VSS", false, w_n, l});
        func_str = "Y = binate [direct fallback, " + std::to_string(pmos_list.size() + nmos_list.size()) + " TR]";
        adjust_sizing(nmos_list, pmos_list);

        if (!verify_cdl(pmos_list, nmos_list, n_vars, canonical)) {
            std::cerr << "ERROR: functional verification FAILED even after fallback for " << canonical << "\n";
            func_str += " [VERIFICATION FAILED]";
        }
    }

    return emit_cdl(name, n_vars, pmos_list, nmos_list, canonical, func_str, nmos_model, pmos_model);
}

static std::string multi_output_to_cdl_existing(
        const std::vector<std::string>& canonicals,
        const std::vector<std::string>& output_names,
        const std::string& cell_name,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        const std::string& nmos_model, const std::string& pmos_model,
        int max_tr = INT_MAX) {

    if (canonicals.size() == 1)
        return canonical_to_cdl(canonicals[0], cell_name, w_n, w_p, l, nmos_model, pmos_model, max_tr);

    {

        int mx = 0;
        std::vector<std::pair<int, std::set<int>>> all_parsed;
        for (auto& c : canonicals) {
            auto [nv, onset] = parse_canonical(c);
            mx = std::max(mx, nv);
            all_parsed.push_back({nv, std::set<int>(onset.begin(), onset.end())});
        }

        for (size_t i = 0; i < all_parsed.size(); i++) {
            if (all_parsed[i].first < mx) {
                std::set<int> padded;
                int reps = 1 << (mx - all_parsed[i].first);
                int block = 1 << all_parsed[i].first;
                for (int m : all_parsed[i].second)
                    for (int r = 0; r < reps; r++) padded.insert(m + r * block);
                all_parsed[i] = {mx, padded};
            }
        }

        int nv = mx;
        int total = 1 << nv;

        std::vector<bool> is_unate(canonicals.size(), false);
        std::vector<bool> is_binate(canonicals.size(), false);
        for (size_t i = 0; i < all_parsed.size(); i++) {
            auto& onset = all_parsed[i].second;
            std::set<int> offset;
            for (int m = 0; m < total; m++) if (!onset.count(m)) offset.insert(m);
            auto off_sop = offset.empty() ? SOP{} : quine_mccluskey(nv, offset);
            auto on_sop = onset.empty() ? SOP{} : quine_mccluskey(nv, onset);
            if ((!off_sop.empty() && !sop_has_complement(off_sop, nv)) ||
                (!on_sop.empty() && !sop_has_complement(on_sop, nv)))
                is_unate[i] = true;
            else
                is_binate[i] = true;
        }

        bool has_unate = false, has_binate = false;
        for (size_t i = 0; i < canonicals.size(); i++) {
            if (is_unate[i]) has_unate = true;
            if (is_binate[i]) has_binate = true;
        }

        if (has_unate && has_binate) {
            std::vector<Transistor> all_pmos, all_nmos;
            int gtid = 0;

            struct UnateOutput {
                size_t idx;
                std::string node;
                std::vector<int> tt;
            };
            std::vector<UnateOutput> unate_outputs;

            for (size_t i = 0; i < canonicals.size(); i++) {
                if (!is_unate[i]) continue;
                auto& onset = all_parsed[i].second;
                std::set<int> offset;
                for (int m = 0; m < total; m++) if (!onset.count(m)) offset.insert(m);
                auto off_sop = offset.empty() ? SOP{} : quine_mccluskey(nv, offset);
                auto on_sop = onset.empty() ? SOP{} : quine_mccluskey(nv, onset);

                std::string oname = (i < output_names.size()) ? output_names[i] : "Y" + std::to_string(i);
                std::map<int, std::string> vm;
                for (int v = 0; v < nv; v++) {
                    vm[v] = input_name(v, nv);
                    vm[v + nv] = input_name(v, nv) + "_bar";
                }

                if (!off_sop.empty() && !sop_has_complement(off_sop, nv)) {
                    auto tree = merge_common_quotients(factor_sop(off_sop, nv));
                    int tn = 0, tp = 0;
                    tree_to_nmos(*tree, oname, "VSS", nv, w_n, l, oname, tn, vm, all_nmos);
                    tree_to_pmos(*tree, oname, "VDD", nv, w_p, l, oname, tp, vm, all_pmos);
                    gtid += std::max(tn, tp);
                } else {

                    auto tree = sop_has_complement(on_sop, nv)
                        ? factor_sop_with_comp(on_sop, nv)
                        : merge_common_quotients(factor_sop(on_sop, nv));
                    std::string inode = oname + "_n";
                    int tn = 0, tp = 0;
                    tree_to_nmos(*tree, inode, "VSS", nv, w_n, l, oname, tn, vm, all_nmos);
                    tree_to_pmos(*tree, inode, "VDD", nv, w_p, l, oname, tp, vm, all_pmos);
                    all_pmos.push_back({"MP" + oname + "inv", inode, oname, "VDD", "VDD", true, w_p, l});
                    all_nmos.push_back({"MN" + oname + "inv", inode, oname, "VSS", "VSS", false, w_n, l});
                    gtid += std::max(tn, tp) + 1;
                }

                std::vector<int> tt(total);
                for (int m = 0; m < total; m++) tt[m] = onset.count(m) ? 1 : 0;
                unate_outputs.push_back({i, oname, tt});

                std::vector<int> tt_inv(total);
                for (int m = 0; m < total; m++) tt_inv[m] = 1 - tt[m];

                std::string inv_node = oname + "_n";

                bool onset_path = false;
                {
                    std::set<int> off2;
                    for (int m = 0; m < total; m++) if (!onset.count(m)) off2.insert(m);
                    auto os2 = off2.empty() ? SOP{} : quine_mccluskey(nv, off2);
                    onset_path = os2.empty() || sop_has_complement(os2, nv);
                }
                if (onset_path) {

                    unate_outputs.push_back({i, inv_node, tt_inv});
                } else {

                }
            }

            for (size_t i = 0; i < canonicals.size(); i++) {
                if (!is_binate[i]) continue;
                auto& onset = all_parsed[i].second;
                std::string oname = (i < output_names.size()) ? output_names[i] : "Y" + std::to_string(i);

                int best_tr = 999999;
                struct BestDecomp {
                    size_t unate_idx;
                    std::string signal_node;
                    SOP combining_sop;
                    int ext_vars;
                    bool inverted = false;
                } best_decomp{};
                bool found = false;

                for (auto& uo : unate_outputs) {
                    int ext_vars = nv + 1;
                    std::set<int> ext_offset, ext_onset_ext, ext_dc;
                    std::set<int> valid;
                    for (int m = 0; m < total; m++) {
                        int em = (m << 1) | uo.tt[m];
                        valid.insert(em);
                        if (!onset.count(m)) ext_offset.insert(em);
                        else ext_onset_ext.insert(em);
                    }
                    for (int em = 0; em < (1 << ext_vars); em++)
                        if (!valid.count(em)) ext_dc.insert(em);

                    {
                        auto sop = qm_with_dc(ext_vars, ext_offset, ext_dc);
                        if (!sop.empty() && !sop_has_complement(sop, ext_vars)) {
                            auto tree = merge_common_quotients(factor_sop(sop, ext_vars));
                            int tr = tree->literal_count() * 2;
                            if (tr < best_tr) {
                                best_tr = tr;
                                best_decomp = {uo.idx, uo.node, sop, ext_vars, false};
                                found = true;
                            }
                        }
                    }

                    {
                        auto sop = qm_with_dc(ext_vars, ext_onset_ext, ext_dc);
                        if (!sop.empty() && !sop_has_complement(sop, ext_vars)) {
                            auto tree = merge_common_quotients(factor_sop(sop, ext_vars));
                            int tr = tree->literal_count() * 2 + 2;
                            if (tr < best_tr) {
                                best_tr = tr;
                                best_decomp = {uo.idx, uo.node, sop, ext_vars, true};
                                found = true;
                            }
                        }
                    }
                }

                if (found) {

                    std::map<int, std::string> ext_vm;
                    for (int v = 0; v < nv; v++) {
                        ext_vm[v] = input_name(v, nv);
                        ext_vm[v + best_decomp.ext_vars] = input_name(v, nv) + "_bar";
                    }
                    ext_vm[nv] = best_decomp.signal_node;
                    ext_vm[nv + best_decomp.ext_vars] = best_decomp.signal_node + "_bar";

                    auto tree = merge_common_quotients(factor_sop(best_decomp.combining_sop, best_decomp.ext_vars));
                    std::string comb_out = best_decomp.inverted ? (oname + "_ci") : oname;
                    int tn = 0, tp = 0;
                    tree_to_nmos(*tree, comb_out, "VSS", best_decomp.ext_vars, w_n, l, oname, tn, ext_vm, all_nmos);
                    tree_to_pmos(*tree, comb_out, "VDD", best_decomp.ext_vars, w_p, l, oname, tp, ext_vm, all_pmos);
                    if (best_decomp.inverted) {
                        all_pmos.push_back({"MP" + oname + "cinv", comb_out, oname, "VDD", "VDD", true, w_p, l});
                        all_nmos.push_back({"MN" + oname + "cinv", comb_out, oname, "VSS", "VSS", false, w_n, l});
                    }
                } else {

                    std::vector<int> vm;
                    for (int v = 0; v < nv; v++) vm.push_back(v);
                    auto br = generate_cmos_recursive(onset, nv, vm, oname + "g", w_n, w_p, l, gtid, max_tr);
                    if (br.infeasible) {
                        std::cerr << "ERROR: " << cell_name << ": output " << oname
                                  << " binate decomposition exceeds TR budget (" << max_tr
                                  << " TR)\n";
                        return "";
                    }
                    all_pmos.insert(all_pmos.end(), br.pmos.begin(), br.pmos.end());
                    all_nmos.insert(all_nmos.end(), br.nmos.begin(), br.nmos.end());
                    if (br.output_inverted) {
                        all_pmos.push_back({"MP" + oname + "inv", br.output_node, oname, "VDD", "VDD", true, w_p, l});
                        all_nmos.push_back({"MN" + oname + "inv", br.output_node, oname, "VSS", "VSS", false, w_n, l});
                    } else if (br.output_node != oname) {
                        for (auto& t : all_pmos) { if (t.drain == br.output_node) t.drain = oname; if (t.source == br.output_node) t.source = oname; }
                        for (auto& t : all_nmos) { if (t.drain == br.output_node) t.drain = oname; if (t.source == br.output_node) t.source = oname; }
                    }
                }
            }

            std::vector<std::string> pins;
            for (int v = 0; v < nv; v++) pins.push_back(input_name(v, nv));
            pins.push_back("VDD"); pins.push_back("VSS");
            for (auto& o : output_names) pins.push_back(o);

            std::ostringstream os;
            os << ".SUBCKT " << cell_name;
            for (auto& p : pins) os << " " << p;
            os << "\n* multi-output [" << canonicals.size() << " outputs, cross-output optimization]\n";
            os << "* Transistors: " << (all_pmos.size() + all_nmos.size()) << "\n";
            for (auto& t : all_pmos)
                os << t.name << " " << t.drain << " " << t.gate << " " << t.source << " " << t.bulk << " " << pmos_model << " W=" << t.width << " L=" << t.length << (g_pmos_extra.empty() ? "" : " " + g_pmos_extra) << "\n";
            for (auto& t : all_nmos)
                os << t.name << " " << t.drain << " " << t.gate << " " << t.source << " " << t.bulk << " " << nmos_model << " W=" << t.width << " L=" << t.length << (g_nmos_extra.empty() ? "" : " " + g_nmos_extra) << "\n";
            os << ".ENDS\n";
            return os.str();
        }
    }

    int max_vars = 0;
    std::vector<std::pair<std::string, std::set<int>>> parsed;
    for (auto& c : canonicals) {
        auto [nv, onset] = parse_canonical(c);
        max_vars = std::max(max_vars, nv);
        parsed.push_back({c, onset});
    }
    int n_vars = max_vars;

    for (size_t i = 0; i < parsed.size(); i++) {
        auto [nv_unused, onset] = parse_canonical(canonicals[i]);
        int nv = nv_unused;
        if (nv < n_vars) {
            std::set<int> padded;
            int reps = 1 << (n_vars - nv);
            int block = 1 << nv;
            for (int m : onset)
                for (int r = 0; r < reps; r++)
                    padded.insert(m + r * block);
            parsed[i].second = padded;
        }
    }

    int total = 1 << n_vars;
    std::set<int> all_mt;
    for (int i = 0; i < total; i++) all_mt.insert(i);

    struct OutputInfo {
        SOP sop;
        bool needs_inv = false;
        std::set<int> comp_vars;
    };
    std::vector<OutputInfo> infos(canonicals.size());

    for (size_t oi = 0; oi < parsed.size(); oi++) {
        auto& onset = parsed[oi].second;
        std::set<int> offset;
        for (int i = 0; i < total; i++)
            if (!onset.count(i)) offset.insert(i);

        auto offset_sop = offset.empty() ? SOP{} : quine_mccluskey(n_vars, offset);
        auto onset_sop = onset.empty() ? SOP{} : quine_mccluskey(n_vars, onset);

        if (!offset_sop.empty() && !sop_has_complement(offset_sop, n_vars)) {
            infos[oi].sop = offset_sop;
            infos[oi].needs_inv = false;
        } else if (!onset_sop.empty() && !sop_has_complement(onset_sop, n_vars)) {
            infos[oi].sop = onset_sop;
            infos[oi].needs_inv = true;
        } else {
            infos[oi].sop = onset_sop;
            infos[oi].needs_inv = true;
            for (auto& t : onset_sop)
                for (int i = 0; i < n_vars; i++)
                    if (t[i] == '0') infos[oi].comp_vars.insert(i);
        }
    }

    std::vector<int> complement_source(infos.size(), -1);
    for (size_t i = 1; i < parsed.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (complement_source[j] >= 0) continue;
            bool is_compl = true;
            for (int m = 0; m < total; m++) {
                int vi = parsed[i].second.count(m) ? 1 : 0;
                int vj = parsed[j].second.count(m) ? 1 : 0;
                if (vi != (1 - vj)) { is_compl = false; break; }
            }
            if (is_compl) { complement_source[i] = (int)j; break; }
        }
    }

    std::map<Term, std::set<int>> term_to_outs;
    for (size_t oi = 0; oi < infos.size(); oi++) {
        if (complement_source[oi] >= 0) continue;
        for (auto& t : infos[oi].sop)
            term_to_outs[t].insert((int)oi);
    }

    std::map<Term, std::string> shared_nodes;
    int sh_idx = 0;
    for (auto& [t, outs] : term_to_outs)
        if (outs.size() > 1) shared_nodes[t] = "sh" + std::to_string(sh_idx++);

    std::set<int> all_comp;
    for (size_t oi = 0; oi < infos.size(); oi++)
        if (complement_source[oi] < 0)
            all_comp.insert(infos[oi].comp_vars.begin(), infos[oi].comp_vars.end());

    std::vector<Transistor> all_pmos, all_nmos;
    int tid_n = 0, tid_p = 0;

    std::map<int, std::string> var_map_comp;
    for (int vi = 0; vi < n_vars; vi++)
        var_map_comp[vi] = input_name(vi, n_vars);
    for (int vi : all_comp) {
        std::string bar = input_name(vi, n_vars) + "_bar";
        var_map_comp[vi + n_vars] = bar;
        all_pmos.push_back({"MPbar" + std::to_string(vi), input_name(vi, n_vars), bar, "VDD", "VDD", true, w_p, l});
        all_nmos.push_back({"MNbar" + std::to_string(vi), input_name(vi, n_vars), bar, "VSS", "VSS", false, w_n, l});
    }

    for (auto& [term, node_name] : shared_nodes) {
        std::string node_bar = node_name + "_bar";

        std::vector<std::string> gates;
        for (int i = 0; i < n_vars; i++) {
            if (term[i] == '-') continue;
            if (term[i] == '0') {
                gates.push_back(var_map_comp.count(i + n_vars) ? var_map_comp[i + n_vars] : input_name(i, n_vars) + "_bar");
            } else {
                gates.push_back(input_name(i, n_vars));
            }
        }
        if (gates.size() == 1) {
            all_nmos.push_back({"MNsh" + std::to_string(tid_n++), gates[0], node_name, "VSS", "VSS", false, w_n, l});
        } else {
            std::string prev = node_name;
            for (size_t j = 0; j < gates.size(); j++) {
                std::string next = (j + 1 < gates.size()) ? "nsh" + std::to_string(tid_n) + "_" + std::to_string(j) : "VSS";
                all_nmos.push_back({"MNsh" + std::to_string(tid_n++), gates[j], prev, next, "VSS", false, w_n, l});
                prev = next;
            }
        }

        for (auto& g : gates)
            all_pmos.push_back({"MPsh" + std::to_string(tid_p++), g, node_name, "VDD", "VDD", true, w_p, l});

        all_pmos.push_back({"MPshi" + std::to_string(tid_p++), node_name, node_bar, "VDD", "VDD", true, w_p, l});
        all_nmos.push_back({"MNshi" + std::to_string(tid_n++), node_name, node_bar, "VSS", "VSS", false, w_n, l});
    }

    for (size_t oi = 0; oi < infos.size(); oi++) {

        if (complement_source[oi] >= 0) continue;

        auto& info = infos[oi];
        std::string oname = output_names[oi];

        std::string stage_out = info.needs_inv ? (oname + "_bar") : oname;
        if (info.needs_inv) {
            for (size_t oi2 = oi + 1; oi2 < infos.size(); oi2++) {
                if (complement_source[oi2] == (int)oi) {
                    stage_out = output_names[oi2];
                    break;
                }
            }
        }

        SOP unique_terms;
        std::vector<Term> shared_in;
        for (auto& t : info.sop) {
            if (shared_nodes.count(t)) shared_in.push_back(t);
            else unique_terms.push_back(t);
        }

        std::string pmos_vdd_side = "VDD";
        if (!shared_in.empty() && !unique_terms.empty()) {
            pmos_vdd_side = "sp_" + oname + "_0";
        } else if (shared_in.empty()) {
            pmos_vdd_side = "VDD";
        }

        if (!unique_terms.empty()) {
            auto tree = factor_sop_with_comp(unique_terms, n_vars);
            tree_to_nmos(*tree, stage_out, "VSS", n_vars, w_n, l, oname, tid_n, var_map_comp, all_nmos);
            tree_to_pmos(*tree, stage_out, pmos_vdd_side, n_vars, w_p, l, oname, tid_p, var_map_comp, all_pmos);
        }

        for (auto& t : shared_in) {
            std::string nb = shared_nodes[t] + "_bar";
            all_nmos.push_back({"MN" + oname + std::to_string(tid_n++), nb, stage_out, "VSS", "VSS", false, w_n, l});
        }

        if (!shared_in.empty()) {
            std::string prev_node = unique_terms.empty() ? stage_out : pmos_vdd_side;
            for (size_t si = 0; si < shared_in.size(); si++) {
                std::string nb = shared_nodes[shared_in[si]] + "_bar";
                std::string next_node = (si + 1 < shared_in.size())
                    ? ("sp_" + oname + "_" + std::to_string(si + (unique_terms.empty() ? 0 : 1)))
                    : "VDD";

                all_pmos.push_back({"MP" + oname + std::to_string(tid_p++), nb, prev_node, next_node, "VDD", true, w_p, l});
                prev_node = next_node;
            }
        }

        if (info.needs_inv) {
            all_pmos.push_back({"MP" + oname + "inv", stage_out, oname, "VDD", "VDD", true, w_p, l});
            all_nmos.push_back({"MN" + oname + "inv", stage_out, oname, "VSS", "VSS", false, w_n, l});
        }
    }

    return emit_cdl(cell_name, n_vars, all_pmos, all_nmos,
                    canonicals[0], "multi-output [" + std::to_string(canonicals.size()) + " outputs]",
                    nmos_model, pmos_model, output_names);
}

static std::string multi_output_to_cdl(
        const std::vector<std::string>& canonicals,
        const std::vector<std::string>& output_names,
        const std::string& cell_name,
        const std::string& w_n, const std::string& w_p, const std::string& l,
        const std::string& nmos_model, const std::string& pmos_model,
        int max_tr = INT_MAX) {
    std::string baseline = multi_output_to_cdl_existing(
        canonicals, output_names, cell_name, w_n, w_p, l, nmos_model, pmos_model, max_tr);

    if (canonicals.size() != 2 || output_names.size() != 2) return baseline;

    int max_vars = 0;
    std::vector<std::pair<int, std::set<int>>> parsed;
    for (const auto& c : canonicals) {
        auto [nv, onset] = parse_canonical(c);
        max_vars = std::max(max_vars, nv);
        parsed.push_back({nv, onset});
    }
    if (max_vars != 2) return baseline;

    std::vector<unsigned> output_tts;
    output_tts.reserve(parsed.size());
    for (auto& [nv, onset] : parsed) {
        std::set<int> padded = onset;
        if (nv < max_vars) {
            padded.clear();
            int reps = 1 << (max_vars - nv);
            int block = 1 << nv;
            for (int m : onset)
                for (int r = 0; r < reps; r++)
                    padded.insert(m + r * block);
        }
        output_tts.push_back(onset_to_tt(padded, max_vars) & 0xFu);
    }

    auto exact = synthesize_exact_multi_output_2input(output_tts, output_names, w_n, w_p, l);
    int baseline_tr = count_cdl_transistors(baseline);
    ExactMultiResult2 best;
    std::string best_comment;

    auto consider = [&](ExactMultiResult2 cand, const std::string& comment) {
        if (!cand.found) return;
        if (!verify_multi_output_cdl(cand.pmos, cand.nmos, max_vars, output_tts, output_names)) return;
        if (!best.found || cand.tr < best.tr) {
            best = std::move(cand);
            best_comment = comment;
        }
    };

    consider(std::move(exact), "multi-output [2 outputs, exact 2-input primitive search]");
    if (!g_series_parallel)
        consider(synthesize_xor_seed_multi_output_2input(output_tts, output_names, w_n, w_p, l),
                 "multi-output [2 outputs, xor-seeded search]");

    if (!best.found) return baseline;
    if (!baseline.empty() && best.tr >= baseline_tr) return baseline;

    return emit_cdl(cell_name, max_vars, best.pmos, best.nmos,
                    canonicals[0], best_comment,
                    nmos_model, pmos_model, output_names);
}

static bool is_p_canonical(const std::string& c_0b) {
    if (c_0b.size() < 3 || c_0b[0] != '0' || c_0b[1] != 'b')
        throw std::runtime_error("is_p_canonical: expected 0b-prefix: " + c_0b);
    const std::string bits = c_0b.substr(2);
    const int total = (int)bits.size();
    int n = 0;
    while ((1 << n) < total) ++n;
    if ((1 << n) != total)
        throw std::runtime_error("is_p_canonical: bit length not power of 2: " + c_0b);
    if (n <= 1) return true;

    std::vector<int> sigma(n);
    std::iota(sigma.begin(), sigma.end(), 0);
    while (std::next_permutation(sigma.begin(), sigma.end())) {
        for (int j = 0; j < total; ++j) {
            int old_m = 0;
            for (int k = 0; k < n; ++k)
                old_m |= (((j >> (n - 1 - k)) & 1) << (n - 1 - sigma[k]));
            char pb = bits[old_m];
            char ob = bits[j];
            if (pb < ob) return false;
            if (pb > ob) break;
        }
    }
    return true;
}

static void check_p_canonical(const std::string& c_0b) {
    if (!g_validate_p_canonical) return;
    if (!is_p_canonical(c_0b))
        throw std::runtime_error(
            "P_canonical violation: '" + c_0b + "'\n"
            "  canonical_to_cdl requires P_canonical form: the lex-minimum truth\n"
            "  table over all n! input-variable permutations (0b-prefixed, MSB-first).\n"
            "  Use the mining tool canonical_key column, not an NPN class representative.");
}

static void batch_check_p_canonical(const std::vector<std::string>& lines) {
    for (const auto& L : lines) {
        std::size_t p = L.find_first_not_of(" \t");
        if (p == std::string::npos || L[p] == '#') continue;
        std::istringstream iss(L);
        std::string tok;
        while (std::getline(iss, tok, ',')) {
            std::size_t a = tok.find_first_not_of(" \t");
            std::size_t b = tok.find_last_not_of(" \t");
            std::string t = (a != std::string::npos) ? tok.substr(a, b - a + 1) : "";
            if (t.size() > 2 && t.substr(0, 2) == "0b") check_p_canonical(t);
        }
    }
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string tok;
    while (std::getline(iss, tok, delim)) {

        size_t a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
        if (a != std::string::npos) parts.push_back(tok.substr(a, b - a + 1));
    }
    return parts;
}

static std::string so3_hex_from_0b(const std::string& c_0b) {
    if (c_0b.size() < 3 || c_0b[0] != '0' || c_0b[1] != 'b')
        throw std::runtime_error("so3_hex_from_0b: expected 0b-prefixed string, got '" + c_0b + "'");
    std::string bits = c_0b.substr(2);
    while (bits.size() % 4 != 0) bits.insert(bits.begin(), '0');
    std::string hex;
    hex.reserve(bits.size() / 4);
    for (std::size_t i = 0; i < bits.size(); i += 4) {
        int v = 0;
        for (int j = 0; j < 4; ++j) {
            char ch = bits[i + j];
            if (ch != '0' && ch != '1')
                throw std::runtime_error("so3_hex_from_0b: non-binary digit in '" + c_0b + "'");
            v = (v << 1) | (ch - '0');
        }
        hex += "0123456789abcdef"[v];
    }
    std::size_t first_nz = hex.find_first_not_of('0');
    return (first_nz == std::string::npos) ? std::string("0") : hex.substr(first_nz);
}

static int so3_inputs_from_0b(const std::string& c_0b) {
    if (c_0b.size() < 3) throw std::runtime_error("so3_inputs_from_0b: too short: '" + c_0b + "'");
    std::size_t nbits = c_0b.size() - 2;
    int n = 0;
    while ((1u << n) < nbits) ++n;
    if ((1u << n) != nbits)
        throw std::runtime_error("so3_inputs_from_0b: canonical length " + std::to_string(nbits) +
                                 " is not a power of 2 in '" + c_0b + "'");
    return n;
}

static std::string so3_cell_name(const std::string& c_0b) {
    return std::to_string(so3_inputs_from_0b(c_0b))
         + "input0x" + so3_hex_from_0b(c_0b) + "_X1";
}

static std::string so3_multi_cell_name(const std::vector<std::string>& canons) {
    if (canons.empty()) return "MULTI_X1";
    std::string name = std::to_string(so3_inputs_from_0b(canons[0])) + "input";
    for (std::size_t i = 0; i < canons.size(); ++i)
        name += (i ? "__" : "") + std::string("0x") + so3_hex_from_0b(canons[i]);
    name += "_X1";
    return name;
}

int canonical_to_cdl_main(int argc, char** argv) {
    RuntimeLogger rl("canonical_to_cdl");
  try {

    g_pdk = "so3";
    std::string canonical, name, w_n = "46.0n", w_p = "46.0n", length = "16n";
    std::string nmos_model = "nmos_rvt", pmos_model = "pmos_rvt";
    std::string nmos_extra = "nfin=2", pmos_extra = "nfin=2";
    std::string multi_str, output_names_str, batch_file, out_file;
    int max_tr_budget = 56;

    { auto s = rl.step("parse args");
    int i = 1;
    auto need_value = [&](const std::string& flag) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + flag);
        return argv[++i];
    };
    for (; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--pdk") {
            std::string pdk = need_value(arg);
            g_pdk = pdk;
            if (pdk == "so3" || pdk == "SO3") {
                nmos_model = "nmos_rvt"; pmos_model = "pmos_rvt";
                w_n = "46.0n"; w_p = "46.0n"; length = "16n";
                nmos_extra = "nfin=2"; pmos_extra = "nfin=2";
                g_wmax_n_nm = 46.0; g_wmax_p_nm = 46.0;
            } else {
                throw std::runtime_error("unknown --pdk value: " + pdk + " (valid: so3)");
            }
        }
        else if (arg == "--name") name = need_value(arg);
        else if (arg == "--w-n") w_n = need_value(arg);
        else if (arg == "--w-p") w_p = need_value(arg);
        else if (arg == "--length") length = need_value(arg);
        else if (arg == "--nmos-model") nmos_model = need_value(arg);
        else if (arg == "--pmos-model") pmos_model = need_value(arg);
        else if (arg == "--nmos-extra") nmos_extra = need_value(arg);
        else if (arg == "--pmos-extra") pmos_extra = need_value(arg);
        else if (arg == "--multi") multi_str = need_value(arg);
        else if (arg == "--output-names") output_names_str = need_value(arg);
        else if (arg == "--batch") batch_file = need_value(arg);
        else if (arg == "--out") out_file = need_value(arg);
        else if (arg == "--max-tr") max_tr_budget = std::stoi(need_value(arg));
        else if (arg == "--search-depth") {
            int d = std::stoi(need_value(arg));
            if (d < 1) throw std::runtime_error("--search-depth must be >= 1, got " + std::to_string(d));
            g_search_depth = d;
        }
        else if (arg == "--complementary") g_complementary = true;
        else if (arg == "--시리즈_패러럴" || arg == "--series-parallel") g_series_parallel = true;
        else if (arg == "--no-시리즈_패러럴" || arg == "--no-series-parallel") g_series_parallel = false;
        else if (arg == "--validate-p-canonical") g_validate_p_canonical = true;
        else if (!arg.empty() && arg[0] != '-') canonical = arg;
        else throw std::runtime_error("unknown argument: " + arg);
    }

    g_nmos_extra = nmos_extra;
    g_pmos_extra = pmos_extra;

    }

    std::vector<std::string> results;
    { auto s = rl.step("generate CDL");

    if (!multi_str.empty()) {
        auto canons = split(multi_str, ',');
        for (const auto& c : canons) check_p_canonical(c);
        auto onames = output_names_str.empty() ? std::vector<std::string>{} : split(output_names_str, ',');
        if (onames.empty()) for (size_t i = 0; i < canons.size(); i++) onames.push_back("Y" + std::to_string(i));
        std::string cname = name.empty() ? "MULTI" : name;
        results.push_back(multi_output_to_cdl(canons, onames, cname, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget));
    } else if (!batch_file.empty()) {
        std::ifstream fin(batch_file);
        if (!fin) { std::cerr << "Cannot open: " << batch_file << "\n"; return 1; }
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(fin, line)) lines.push_back(std::move(line));

        batch_check_p_canonical(lines);
        std::vector<std::string> par_results(lines.size());
        std::cerr << "[info] parallel CDL gen: " << lines.size() << " entries, "
                  << omp_get_max_threads() << " threads\n";
        #pragma omp parallel for schedule(dynamic, 8)
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const std::string& L = lines[li];
            size_t p = L.find_first_not_of(" \t");
            if (p == std::string::npos || L[p] == '#') continue;
            auto parts = split(L, ',');
            bool is_multi = parts.size() > 1
                && parts[0].substr(0, 2) == "0b"
                && parts[1].size() > 2 && parts[1].substr(0, 2) == "0b";
            if (is_multi) {
                std::vector<std::string> canons;
                std::vector<std::string> onames;
                std::string mname;
                for (auto& pt : parts) {
                    auto trimmed = pt;
                    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
                    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
                    if (trimmed.size() > 2 && trimmed.substr(0, 2) == "0b") canons.push_back(trimmed);
                    else if (!trimmed.empty() && mname.empty()) mname = trimmed;
                }
                for (size_t i = 0; i < canons.size(); i++) onames.push_back("Y" + std::to_string(i));
                if (mname.empty()) mname = so3_multi_cell_name(canons);
                par_results[li] = multi_output_to_cdl(canons, onames, mname, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget);
            } else {
                std::string c = parts[0];
                std::string n = (parts.size() > 1) ? parts[1] : so3_cell_name(c);
                par_results[li] = canonical_to_cdl(c, n, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget);
            }
        }
        for (auto& r : par_results) if (!r.empty()) results.push_back(std::move(r));
    } else if (!canonical.empty()) {
        check_p_canonical(canonical);
        std::string n = name.empty() ? canonical.substr(2) : name;
        results.push_back(canonical_to_cdl(canonical, n, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget));
    } else {
        std::cerr << "Usage: canonical_to_cdl <0b...> [--name NAME] [--multi c1,c2 --output-names Y0,Y1]\n"
                  << "       canonical_to_cdl --batch file.txt --out cells.cdl\n"
                  << "       [--search-depth N]      max depth for small-gate DFS iterative deepening (default: auto = n_vars<=3?5:6, bounded by the 900s time cap)\n"
                  << "       [--validate-p-canonical] reject keys that are not lex-min over input permutations\n";
        return 1;
    }
    }

    for (auto& r : results) {
        if (r.empty()) {
            std::cerr << "ERROR: one or more cells could not be generated (TR budget exceeded)\n";
            rl.done();
            return 1;
        }
    }

    { auto s = rl.step("write output");
    std::ostringstream all;
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) all << "\n";
        all << results[i];
    }

    if (!out_file.empty()) {
        std::ofstream fout(out_file);
        if (!fout) { std::cerr << "Cannot write: " << out_file << "\n"; return 1; }
        fout << all.str();
        std::cerr << "Written " << results.size() << " subcircuit(s) to " << out_file << "\n";
    } else {
        std::cout << all.str();
    }
    }
    rl.done();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[error] " << e.what() << "\n";
    rl.done();
    return 1;
  }
}

static void apply_pdk_preset_for_batch(const std::string& pdk,
                                       std::string& w_n, std::string& w_p, std::string& length,
                                       std::string& nmos_model, std::string& pmos_model) {
    g_pdk.clear(); g_nmos_extra.clear(); g_pmos_extra.clear(); g_complementary = false;
    g_series_parallel = true;
    if (pdk.empty() || pdk == "so3" || pdk == "SO3") {
        g_pdk = "so3";
        nmos_model = "nmos_rvt"; pmos_model = "pmos_rvt";
        w_n = "46.0n"; w_p = "46.0n"; length = "16n";
        g_nmos_extra = "nfin=2"; g_pmos_extra = "nfin=2";
        g_wmax_n_nm = 46.0; g_wmax_p_nm = 46.0;
    } else {
        throw std::runtime_error("canonical_to_cdl: unknown pdk '" + pdk +
            "' (valid: SO3, or empty)");
    }
}

static std::vector<std::string> run_batch_parallel(
        const std::vector<std::string>& batch_lines,
        const std::string& w_n, const std::string& w_p,
        const std::string& length, const std::string& nmos_model,
        const std::string& pmos_model, int max_tr_budget,
        std::vector<std::string>* out_names = nullptr) {
    batch_check_p_canonical(batch_lines);
    std::vector<std::string> par_results(batch_lines.size());
    std::vector<std::string> par_names(batch_lines.size());
    std::cerr << "[info] parallel CDL gen: " << batch_lines.size() << " entries, "
              << omp_get_max_threads() << " threads\n";
    #pragma omp parallel for schedule(dynamic, 8)
    for (std::size_t li = 0; li < batch_lines.size(); ++li) {
        const std::string& line = batch_lines[li];
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line[p] == '#') continue;
        auto parts = split(line, ',');
        bool is_multi = parts.size() > 1
            && parts[0].substr(0, 2) == "0b"
            && parts[1].size() > 2 && parts[1].substr(0, 2) == "0b";
        if (is_multi) {
            std::vector<std::string> canons;
            std::vector<std::string> onames;
            std::string mname;
            for (auto& pt : parts) {
                auto trimmed = pt;
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(trimmed.begin());
                while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
                if (trimmed.size() > 2 && trimmed.substr(0, 2) == "0b") canons.push_back(trimmed);
                else if (!trimmed.empty() && mname.empty()) mname = trimmed;
            }
            for (size_t i = 0; i < canons.size(); i++) onames.push_back("Y" + std::to_string(i));
            if (mname.empty()) mname = so3_multi_cell_name(canons);
            par_names[li]   = mname;
            par_results[li] = multi_output_to_cdl(canons, onames, mname, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget);
        } else {
            std::string c = parts[0];
            std::string n = (parts.size() > 1) ? parts[1] : so3_cell_name(c);
            par_names[li]   = n;
            par_results[li] = canonical_to_cdl(c, n, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget);
        }
    }
    if (out_names) *out_names = std::move(par_names);
    return par_results;
}

int canonical_to_cdl_write_batch(const std::vector<std::string>& batch_lines,
                                 const std::string& out_path,
                                 const std::string& pdk) {
    RuntimeLogger rl("canonical_to_cdl_batch");
    try {
        std::string w_n, w_p, length, nmos_model, pmos_model;
        apply_pdk_preset_for_batch(pdk, w_n, w_p, length, nmos_model, pmos_model);
        const int max_tr_budget = 48;
        auto par_results = run_batch_parallel(batch_lines, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget);
        std::vector<std::size_t> failed_indices;
        std::vector<std::string> results;
        results.reserve(par_results.size());
        for (std::size_t i = 0; i < par_results.size(); ++i) {
            if (!par_results[i].empty()) { results.push_back(std::move(par_results[i])); continue; }
            const std::string& line = batch_lines[i];
            size_t p = line.find_first_not_of(" \t");
            if (p == std::string::npos || line[p] == '#') continue;
            failed_indices.push_back(i);
        }
        if (!failed_indices.empty()) {
            std::ostringstream oss;
            oss << "canonical_to_cdl_write_batch: synthesis failed for " << failed_indices.size() << " canonical(s). First: ";
            for (std::size_t k = 0; k < std::min(failed_indices.size(), std::size_t{5}); ++k) {
                if (k) oss << "; ";
                oss << "line " << (failed_indices[k] + 1) << "='" << batch_lines[failed_indices[k]] << "'";
            }
            throw std::runtime_error(oss.str());
        }
        { auto s = rl.step("write output");
          std::ofstream out(out_path);
          if (!out) throw std::runtime_error("cannot open output: " + out_path);
          for (const auto& r : results) out << r;
          std::cerr << "Written " << results.size() << " subcircuit(s) to " << out_path << "\n";
        }
        rl.done(); return 0;
    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n"; rl.done(); return 1;
    }
}

int canonical_to_cdl_write_batch_per_cell(const std::vector<std::string>& batch_lines,
                                          const std::string& out_dir,
                                          const std::string& pdk) {
    RuntimeLogger rl("canonical_to_cdl_batch_per_cell");
    try {
        std::string w_n, w_p, length, nmos_model, pmos_model;
        apply_pdk_preset_for_batch(pdk, w_n, w_p, length, nmos_model, pmos_model);
        const int max_tr_budget = 48;
        std::vector<std::string> par_names;
        auto par_results = run_batch_parallel(batch_lines, w_n, w_p, length, nmos_model, pmos_model, max_tr_budget, &par_names);
        { auto s = rl.step("write output");
          std::filesystem::create_directories(out_dir);
          std::vector<std::size_t> failed_indices;
          std::size_t written = 0;
          for (std::size_t i = 0; i < par_results.size(); ++i) {
              if (par_results[i].empty()) {
                  const std::string& line = batch_lines[i];
                  size_t p = line.find_first_not_of(" \t");
                  if (p == std::string::npos || line[p] == '#') continue;
                  failed_indices.push_back(i); continue;
              }
              const std::string& cname = par_names[i];
              if (cname.empty()) throw std::runtime_error(
                  "internal error: no cell name at index " + std::to_string(i));
              std::filesystem::path fp = std::filesystem::path(out_dir) / (cname + ".cdl");
              std::ofstream out(fp);
              if (!out) throw std::runtime_error("cannot open: " + fp.string());
              out << par_results[i]; ++written;
          }
          if (!failed_indices.empty()) {
              std::ostringstream oss;
              oss << "canonical_to_cdl_write_batch_per_cell: synthesis failed for " << failed_indices.size() << " canonical(s). First: ";
              for (std::size_t k = 0; k < std::min(failed_indices.size(), std::size_t{5}); ++k) {
                  if (k) oss << "; ";
                  oss << "line " << (failed_indices[k] + 1) << "='" << batch_lines[failed_indices[k]] << "'";
              }
              throw std::runtime_error(oss.str());
          }
          std::cerr << "Written " << written << " subcircuit file(s) to " << out_dir << "\n";
        }
        rl.done(); return 0;
    } catch (const std::exception& e) {
        std::cerr << "[error] " << e.what() << "\n"; rl.done(); return 1;
    }
}

int compute_cell_cpp(const cell_metrics::CellData& cell,
                     double wmax_n_nm, double wmax_p_nm) {
    std::vector<Transistor> nmos, pmos;
    for (const auto& d : cell.devices) {
        Transistor t;
        t.name   = d.name;
        t.gate   = d.gate;
        t.drain  = d.drain;
        t.source = d.source;
        t.bulk   = d.bulk;
        t.is_pmos = d.is_pmos;
        t.width  = std::to_string(d.width * 1e9);
        t.length = std::to_string(d.length * 1e9);
        if (d.is_pmos) pmos.push_back(t);
        else           nmos.push_back(t);
    }
    const double saved_n = g_wmax_n_nm, saved_p = g_wmax_p_nm;
    g_wmax_n_nm = wmax_n_nm;
    g_wmax_p_nm = wmax_p_nm;
    int cpp = compute_cpp_for_candidate(nmos, pmos);
    g_wmax_n_nm = saved_n;
    g_wmax_p_nm = saved_p;
    return cpp;
}
