

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "canonical_to_cdl.hpp"
#include "cell_metrics.hpp"

namespace fs = std::filesystem;

struct PdkWidths {
    double wmax_n;
    double wmax_p;
};

static PdkWidths pdk_widths(const std::string& pdk) {
    std::string p = pdk;
    for (char& c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (p == "SO3")   return {46e-9,   46e-9};
    throw std::runtime_error("unknown PDK: " + pdk + " (expected SO3)");
}

static int pdk_unit_nfin(const std::string& pdk) {
    std::string p = pdk;
    for (char& c : p) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (p == "SO3")   return 2;
    throw std::runtime_error("unknown PDK for unit_nfin: " + pdk);
}

static std::string bits_to_so3_hex(const std::string& bits) {
    std::string b = bits;
    while (b.size() % 4 != 0) b.insert(b.begin(), '0');
    std::string hex;
    hex.reserve(b.size() / 4);
    for (size_t i = 0; i < b.size(); i += 4) {
        int v = 0;
        for (int j = 0; j < 4; ++j) v = (v << 1) | (b[i + j] - '0');
        hex += "0123456789abcdef"[v];
    }
    auto first = hex.find_first_not_of('0');
    return (first == std::string::npos) ? std::string("0") : hex.substr(first);
}

static std::string cell_name_for(const std::string& key,
                                  const std::string& ds = "X1") {
    const std::string bits = key.substr(2);
    size_t nbits = bits.size();
    int n = 0;
    while ((1u << n) < nbits) ++n;
    return std::to_string(n) + "input0x" + bits_to_so3_hex(bits) + "_" + ds;
}

static std::string cdl_lookup_name(const std::string& key) {
    return cell_name_for(key, "X1");
}

static std::string feature_name_for(const std::string& key,
                                    const std::string& ds = "X1") {
    const std::string bits = key.substr(2);
    size_t nbits = bits.size();
    int n = 0;
    while ((1u << n) < nbits) ++n;
    return std::to_string(n) + "input0x" + bits_to_so3_hex(bits) + "_" + ds;
}

static int ds_nfin_scale(const std::string& ds) {
    if (ds == "X1") return 1;
    if (ds == "X2") return 2;
    if (ds == "X4") return 4;
    if (ds == "X8") return 8;
    throw std::runtime_error("unknown drive strength '" + ds +
                             "' (supported: X1, X2, X4, X8)");
}

static std::string rename_ds(const std::string& name, const std::string& ds) {
    for (const auto& old_sfx : {"_X1", "_X2"}) {
        const size_t sfx_len = std::string(old_sfx).size();
        auto pos = name.rfind(old_sfx);
        if (pos != std::string::npos)
            return name.substr(0, pos) + "_" + ds + name.substr(pos + sfx_len);
    }
    return name + "_" + ds;
}

static std::vector<std::string> parse_drive_strengths(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty())
            throw std::runtime_error("--drive-strengths: empty token in '" + s + "'");
        ds_nfin_scale(tok);
        result.push_back(tok);
    }
    if (result.empty())
        throw std::runtime_error("--drive-strengths must not be empty");
    return result;
}

static int lf_popcount(int p) {
    return __builtin_popcount(static_cast<unsigned>(p));
}

static int n_inputs_from_key(const std::string& key) {
    if (key.size() < 3 || key[0] != '0' || key[1] != 'b')
        throw std::runtime_error("canonical_key must start with '0b': " + key);
    size_t nbits = key.size() - 2;
    int n = 0;
    while ((1u << n) < nbits) ++n;
    if ((1u << n) != nbits)
        throw std::runtime_error("canonical_key body length not a power of 2: " + key);
    return n;
}

static std::vector<int> tt_bits(const std::string& key, int n) {
    int sz = 1 << n;
    std::vector<int> bits;
    bits.reserve(sz);
    for (int i = 0; i < sz; ++i) bits.push_back(key[2 + i] - '0');
    return bits;
}

static std::string detect_class(const std::vector<int>& bits, int n) {
    int sz = 1 << n;
    bool all0 = true, all1 = true;
    for (int b : bits) {
        if (b) all0 = false;
        else   all1 = false;
    }
    if (all0) return "CONST0";
    if (all1) return "CONST1";

    if (n == 1) {
        if (bits[0] == 0 && bits[1] == 1) return "BUF";
        if (bits[0] == 1 && bits[1] == 0) return "INV";
        return "";
    }

    int full = sz - 1;

    {
        int ones = 0, pos = -1;
        for (int i = 0; i < sz; i++) if (bits[i]) { ones++; pos = i; }
        if (ones == 1 && pos == full) return "AND";
    }
    {
        int zeros = 0, pos = -1;
        for (int i = 0; i < sz; i++) if (!bits[i]) { zeros++; pos = i; }
        if (zeros == 1 && pos == full) return "NAND";
    }
    {
        int zeros = 0, pos = -1;
        for (int i = 0; i < sz; i++) if (!bits[i]) { zeros++; pos = i; }
        if (zeros == 1 && pos == 0) return "OR";
    }
    {
        int ones = 0, pos = -1;
        for (int i = 0; i < sz; i++) if (bits[i]) { ones++; pos = i; }
        if (ones == 1 && pos == 0) return "NOR";
    }

    {
        bool ok = true;
        for (int p = 0; p < sz && ok; p++)
            if (bits[p] != (lf_popcount(p) & 1)) ok = false;
        if (ok) return "XOR";
    }
    {
        bool ok = true;
        for (int p = 0; p < sz && ok; p++)
            if (bits[p] != (1 - (lf_popcount(p) & 1))) ok = false;
        if (ok) return "XNOR";
    }

    if (n == 3) {
        {
            bool ok = true;
            for (int p = 0; p < sz && ok; p++)
                if (bits[p] != (lf_popcount(p) >= 2 ? 1 : 0)) ok = false;
            if (ok) return "MAJ";
        }
        {
            bool ok = true;
            for (int p = 0; p < sz && ok; p++)
                if (bits[p] != (lf_popcount(p) >= 2 ? 0 : 1)) ok = false;
            if (ok) return "MAJI";
        }
    }

    auto is_monotone = [&]() {
        for (int p = 0; p < sz; p++)
            for (int q = p + 1; q < sz; q++)
                if ((p & q) == p && bits[p] > bits[q]) return false;
        return true;
    };
    auto is_antimonotone = [&]() {
        for (int p = 0; p < sz; p++)
            for (int q = p + 1; q < sz; q++)
                if ((p & q) == p && bits[p] < bits[q]) return false;
        return true;
    };

    if (is_monotone()) {
        if (bits[0] == 0 && bits[full] == 1) return "AO_OR_OA";
        return "";
    }
    if (is_antimonotone()) {
        if (bits[0] == 1 && bits[full] == 0) return "AOI_OR_OAI";
        return "";
    }

    return "";
}

static int fet_cost(const std::string& cls, int n) {
    if (cls == "CONST0" || cls == "CONST1") return 0;
    if (cls == "BUF")  return 4;
    if (cls == "INV")  return 2;
    if (cls == "NAND" || cls == "NOR") return 2 * n;
    if (cls == "AND"  || cls == "OR")  return 2 * n + 2;
    if (cls == "AOI_OR_OAI") return 2 * n;
    if (cls == "AO_OR_OA")   return 2 * n + 2;
    if (cls == "XOR" || cls == "XNOR") {
        if (n < 2) throw std::runtime_error("XOR/XNOR requires n_inputs >= 2");
        return 10 + 8 * (n - 2);
    }
    if (cls == "MAJ")  { if (n != 3) throw std::runtime_error("MAJ only for n=3"); return 12; }
    if (cls == "MAJI") { if (n != 3) throw std::runtime_error("MAJI only for n=3"); return 10; }
    throw std::runtime_error("no FET cost formula for class: " + cls);
}

static int output_inv_count_for(const std::string& cls) {
    static const std::unordered_map<std::string, int> TABLE = {
        {"CONST0", 0}, {"CONST1", 0}, {"INV", 0}, {"BUF", 2},
        {"NAND", 0}, {"NOR", 0}, {"AND", 1}, {"OR", 1},
        {"AOI_OR_OAI", 0}, {"AO_OR_OA", 1},
        {"XOR", 0}, {"XNOR", 0}, {"MAJ", 1}, {"MAJI", 0},
    };
    auto it = TABLE.find(cls);
    if (it == TABLE.end())
        throw std::runtime_error("no output_inv_count for class: " + cls);
    return it->second;
}

static int anf_degree(const std::vector<int>& tt, int n) {
    int sz = 1 << n;
    std::vector<int> a = tt;
    for (int i = 0; i < n; i++) {
        int step = 1 << i;
        for (int j = 0; j < sz; j++)
            if ((j >> i) & 1) a[j] ^= a[j ^ step];
    }
    int deg = 0;
    for (int s = 0; s < sz; s++)
        if (a[s]) deg = std::max(deg, lf_popcount(s));
    return deg;
}

static bool is_symmetric(const std::vector<int>& tt, int n) {
    int sz = 1 << n;
    for (int x = 0; x < sz; x++)
        for (int y = x + 1; y < sz; y++)
            if (lf_popcount(x) == lf_popcount(y) && tt[x] != tt[y]) return false;
    return true;
}

static bool is_monotone(const std::vector<int>& tt, int n) {
    int sz = 1 << n;
    for (int p = 0; p < sz; p++)
        for (int q = p + 1; q < sz; q++)
            if ((p & q) == p && tt[p] > tt[q]) return false;
    return true;
}

static bool is_antimonotone(const std::vector<int>& tt, int n) {
    int sz = 1 << n;
    for (int p = 0; p < sz; p++)
        for (int q = p + 1; q < sz; q++)
            if ((p & q) == p && tt[p] < tt[q]) return false;
    return true;
}

static int input_inv_count(const std::vector<int>& tt, int n) {
    int sz = 1 << n;
    int count = 0;
    for (int var = 0; var < n; var++) {
        int flip = 1 << (n - 1 - var);
        bool neg_unate = true;
        for (int p = 0; p < sz && neg_unate; p++) {
            if (p & flip) continue;
            int q = p | flip;
            if (tt[p] < tt[q]) neg_unate = false;
        }
        if (neg_unate) count++;
    }
    return count;
}

struct CdlFeats {
    int    n_nmos        = 0;
    int    n_pmos        = 0;
    double nfin_n_total  = 0.0;
    double nfin_p_total  = 0.0;
    double nfin_ratio    = 0.0;
    int    max_stack_n   = 0;
    int    max_stack_p   = 0;
    int    n_internal_nets = 0;
    int    cpp_min       = 0;

    int    cpp_min_ds    = 0;
};

static int max_stack_depth(const std::vector<cell_metrics::Device>& devices, bool use_pmos,
                           const std::string& start, const std::string& rail) {
    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& d : devices) {
        if (d.is_pmos != use_pmos) continue;
        adj[d.drain].push_back(d.source);
        adj[d.source].push_back(d.drain);
    }
    if (!adj.count(start)) return 0;

    int best = 0;
    std::unordered_set<std::string> visited;
    visited.insert(start);

    std::function<void(const std::string&, int)> dfs = [&](const std::string& cur, int hops) {
        if (cur == rail) { best = std::max(best, hops); return; }
        auto it = adj.find(cur);
        if (it == adj.end()) return;
        for (const auto& nb : it->second) {
            if (!visited.count(nb)) {
                visited.insert(nb);
                dfs(nb, hops + 1);
                visited.erase(nb);
            }
        }
    };

    dfs(start, 0);
    return best;
}

static CdlFeats extract_cdl_features(const cell_metrics::CellData& cell,
                                     double wmax_n, double wmax_p,
                                     int nfin_scale = 1, int unit_nfin = 0) {
    static const double EPS = 1e-9;
    CdlFeats f;

    std::unordered_set<std::string> port_set(cell.pins.begin(), cell.pins.end());

    std::string out_pin;
    for (const char* cand : {"Y", "ZN"}) {
        if (port_set.count(cand)) { out_pin = cand; break; }
    }
    if (out_pin.empty())
        throw std::runtime_error(
            "CDL cell '" + cell.name + "': cannot auto-detect output pin"
            " (neither 'Y' nor 'ZN' in ports)");

    for (const auto& d : cell.devices) {
        if (d.is_nmos) {
            if (unit_nfin > 0)
                f.n_nmos += static_cast<int>(std::round(d.mult / unit_nfin));
            else
                f.n_nmos += static_cast<int>(std::ceil(d.width / wmax_n - EPS));
            f.nfin_n_total += d.mult * nfin_scale;
        } else {
            if (unit_nfin > 0)
                f.n_pmos += static_cast<int>(std::round(d.mult / unit_nfin));
            else
                f.n_pmos += static_cast<int>(std::ceil(d.width / wmax_p - EPS));
            f.nfin_p_total += d.mult * nfin_scale;
        }
    }
    f.nfin_ratio = (f.nfin_n_total > 0) ? f.nfin_p_total / f.nfin_n_total : 0.0;

    f.max_stack_n = max_stack_depth(cell.devices, false, out_pin, "VSS");
    f.max_stack_p = max_stack_depth(cell.devices, true,  out_pin, "VDD");

    std::unordered_set<std::string> ds_nets;
    for (const auto& d : cell.devices) {
        ds_nets.insert(d.drain);
        ds_nets.insert(d.source);
    }
    for (const auto& net : ds_nets)
        if (!port_set.count(net)) f.n_internal_nets++;

    f.cpp_min = compute_cell_cpp(cell, wmax_n * 1e9, wmax_p * 1e9);

    if (nfin_scale == 1) {
        f.cpp_min_ds = f.cpp_min;
    } else {
        cell_metrics::CellData scaled = cell;
        for (auto& d : scaled.devices) d.width *= nfin_scale;
        f.cpp_min_ds = compute_cell_cpp(scaled, wmax_n * 1e9, wmax_p * 1e9);
    }

    return f;
}

static std::unordered_map<std::string, double>
parse_cell_fractions(const std::string& netlist_path) {
    static const std::unordered_set<std::string> VERILOG_KW = {
        "module", "endmodule", "input", "output", "inout", "wire", "reg",
        "assign", "always", "begin", "end", "if", "else", "case", "endcase",
        "parameter", "localparam", "integer", "supply0", "supply1", "tri",
        "posedge", "negedge", "initial", "generate", "endgenerate",
    };

    std::ifstream ifs(netlist_path);
    if (!ifs) throw std::runtime_error("failed to open netlist: " + netlist_path);

    auto is_id_start = [](char c) { return std::isalpha(c) || c == '_'; };
    auto is_id_cont  = [](char c) { return std::isalnum(c) || c == '_'; };

    std::unordered_map<std::string, int> counts;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || !std::isspace(static_cast<unsigned char>(line[0]))) continue;

        size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || !is_id_start(line[i])) continue;

        size_t id_start = i;
        while (i < line.size() && is_id_cont(line[i])) ++i;
        std::string cell_type = line.substr(id_start, i - id_start);

        if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) continue;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || (!std::isalnum(static_cast<unsigned char>(line[i])) && line[i] != '_'))
            continue;

        std::string lower = cell_type;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (VERILOG_KW.count(lower)) continue;

        counts[cell_type]++;
    }

    int total = 0;
    for (const auto& [k, v] : counts) total += v;

    std::unordered_map<std::string, double> fracs;
    if (total > 0)
        for (const auto& [k, v] : counts) fracs[k] = static_cast<double>(v) / total;
    return fracs;
}

static std::vector<std::string> split_csv_n(const std::string& line, int max_fields) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (int f = 0; f < max_fields - 1; f++) {
        auto comma = line.find(',', start);
        if (comma == std::string::npos) { fields.push_back(line.substr(start)); return fields; }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    fields.push_back(line.substr(start));
    return fields;
}

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        auto comma = line.find(',', start);
        if (comma == std::string::npos) { fields.push_back(line.substr(start)); break; }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

static std::string csv_quote(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    out += '"';
    return out;
}

struct MiningRow {
    std::string canonical;
    long long   count;
    std::string logic_expr;
};

struct CellsRow {
    std::string name;
    std::string canonical;
};

int main(int argc, char** argv) try {

    std::string mining_dir, cells_csv_path, netlist_path;
    std::string pdk = "SO3", out_dir, out_file;
    int top_k1 = 10;
    std::vector<std::string> drive_strengths = {"X1"};

    for (int i = 1; i < argc; ) {
        std::string a = argv[i++];
        auto need = [&](const std::string& flag) -> std::string {
            if (i >= argc)
                throw std::runtime_error(flag + " requires a value");
            return argv[i++];
        };
        if      (a == "--mining-dir")      mining_dir      = need(a);
        else if (a == "--cells-csv")       cells_csv_path  = need(a);
        else if (a == "--netlist")         netlist_path    = need(a);
        else if (a == "--pdk")             pdk             = need(a);
        else if (a == "--top-k1")          top_k1          = std::stoi(need(a));
        else if (a == "--out-dir")         out_dir         = need(a);
        else if (a == "--out-file")        out_file        = need(a);
        else if (a == "--drive-strengths") drive_strengths = parse_drive_strengths(need(a));
        else if (a == "--help" || a == "-h") {
            std::cout <<
                "Usage:\n"
                "  extract_features_cpp --mining-dir <dir>   [--pdk SO3]\n"
                "                       [--top-k1 N] [--drive-strengths X1[,X2,X4,X8]]\n"
                "                       [--netlist <design.v>] [--out-dir <dir>]\n"
                "\n"
                "  extract_features_cpp --cells-csv <file>   [--pdk SO3]\n"
                "                       [--drive-strengths X1[,X2,X4,X8]]\n"
                "                       [--netlist <design.v>] [--out-dir <dir>]\n"
                "                       [--out-file <name>]\n";
            return 0;
        }
        else throw std::runtime_error("unknown argument: " + a);
    }

    if (mining_dir.empty() && cells_csv_path.empty())
        throw std::runtime_error(
            "one of --mining-dir or --cells-csv is required\n"
            "Run with --help for usage.");
    if (!mining_dir.empty() && !cells_csv_path.empty())
        throw std::runtime_error(
            "--mining-dir and --cells-csv are mutually exclusive");

    if (!mining_dir.empty() && !fs::is_directory(mining_dir))
        throw std::runtime_error("--mining-dir does not exist: " + mining_dir);
    if (!cells_csv_path.empty() && !fs::is_regular_file(cells_csv_path))
        throw std::runtime_error("--cells-csv does not exist: " + cells_csv_path);

    if (top_k1 <= 0)
        throw std::runtime_error("--top-k1 must be a positive integer");

    const PdkWidths widths = pdk_widths(pdk);

    if (out_dir.empty())
        out_dir = mining_dir.empty() ? fs::path(cells_csv_path).parent_path().string()
                                     : mining_dir;
    if (out_dir.empty()) out_dir = ".";
    fs::create_directories(out_dir);

    if (out_file.empty())
        out_file = mining_dir.empty() ? "features_l0.csv" : "features.csv";

    std::vector<MiningRow> mining_rows;
    std::vector<CellsRow>  cells_rows;
    bool is_mining_mode = !mining_dir.empty();

    if (is_mining_mode) {
        const std::string freq_path = mining_dir + "/pattern_frequency.csv";
        std::ifstream freq_file(freq_path);
        if (!freq_file)
            throw std::runtime_error("failed to open: " + freq_path);

        std::string line;
        bool header = true;
        while (std::getline(freq_file, line)) {
            if (header) { header = false; continue; }
            if (line.empty()) continue;

            auto fields = split_csv_n(line, 5);
            if (fields.size() < 4)
                throw std::runtime_error("malformed row in " + freq_path + ": " + line);
            MiningRow r;
            r.canonical  = fields[2];
            r.count      = std::stoll(fields[3]);
            r.logic_expr = (fields.size() >= 5) ? fields[4] : "";
            mining_rows.push_back(std::move(r));
        }

        std::stable_sort(mining_rows.begin(), mining_rows.end(),
            [](const MiningRow& a, const MiningRow& b) { return a.count > b.count; });
        if (static_cast<int>(mining_rows.size()) > top_k1)
            mining_rows.resize(top_k1);

        if (mining_rows.empty())
            std::cerr << "WARNING: pattern_frequency.csv has no data rows — "
                      << out_file << " will be empty\n";
    } else {

        std::ifstream f(cells_csv_path);
        if (!f) throw std::runtime_error("failed to open: " + cells_csv_path);

        std::string header_line;
        if (!std::getline(f, header_line))
            throw std::runtime_error("cells-csv is empty: " + cells_csv_path);

        auto header_fields = split_csv(header_line);
        int name_col = -1, key_col = -1;
        for (int c = 0; c < (int)header_fields.size(); c++) {
            if (header_fields[c] == "name")          name_col = c;
            if (header_fields[c] == "canonical_key") key_col  = c;
        }
        if (name_col < 0)
            throw std::runtime_error(
                "cells-csv '" + cells_csv_path + "' missing required column 'name'");
        if (key_col < 0)
            throw std::runtime_error(
                "cells-csv '" + cells_csv_path + "' missing required column 'canonical_key'");

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto fields = split_csv(line);
            int maxcol = std::max(name_col, key_col);
            if ((int)fields.size() <= maxcol)
                throw std::runtime_error(
                    "cells-csv row has too few columns: " + line);
            CellsRow r;
            r.name      = fields[name_col];
            r.canonical = fields[key_col];
            if (r.name.empty() || r.canonical.empty()) continue;
            cells_rows.push_back(std::move(r));
        }

        if (cells_rows.empty())
            std::cerr << "WARNING: cells-csv has no data rows — "
                      << out_file << " will be empty\n";
    }

    size_t n_base = is_mining_mode ? mining_rows.size() : cells_rows.size();

    std::vector<std::string> unique_canonicals;
    {
        std::unordered_set<std::string> seen;
        auto add = [&](const std::string& k) {
            if (seen.insert(k).second) unique_canonicals.push_back(k);
        };
        if (is_mining_mode) {
            for (const auto& r : mining_rows) add(r.canonical);
        } else {
            for (const auto& r : cells_rows) add(r.canonical);
        }
    }

    const std::string tmp_dir  = out_dir + "/.feat_tmp";
    fs::create_directories(tmp_dir);
    const std::string cdl_path = tmp_dir + "/generated.cdl";

    std::unordered_map<std::string, cell_metrics::CellData> cdl_cells;
    if (!unique_canonicals.empty()) {
        int rc = canonical_to_cdl_write_batch(unique_canonicals, cdl_path, pdk);
        if (rc != 0)
            throw std::runtime_error("canonical_to_cdl_write_batch failed (rc=" +
                                     std::to_string(rc) + ")");
        const std::string cdl_text = cell_metrics::read_text(cdl_path);
        cdl_cells = cell_metrics::parse_all_subckts(cdl_text);
    }

    std::unordered_map<std::string, double> cell_fracs;
    if (!netlist_path.empty()) {
        cell_fracs = parse_cell_fractions(netlist_path);
        if (is_mining_mode)
            std::cerr << "WARNING: --netlist given in mining mode, where cell_name is a "
                         "synthesized pattern name (e.g. <n>input0x<hex>_X<ds>) that "
                         "cannot match any real instance name parsed from the netlist -- "
                         "cell_fraction will be 0.0 for every row by construction.\n";
    }
    size_t cell_frac_hits = 0, cell_frac_rows = 0;

    const std::string out_path = out_dir + "/" + out_file;
    std::ofstream out(out_path);
    if (!out) throw std::runtime_error("failed to open output: " + out_path);

    out << "canonical,cell_name,count,logic_expr,"
        << "n_inputs,tt_weight,anf_degree,is_affine,is_symmetric,"
        << "is_monotone,is_anti_monotone,output_inv_count,input_inv_count,fet_count,"
        << "n_nmos,n_pmos,nfin_n_total,nfin_p_total,nfin_ratio,"
        << "max_stack_n,max_stack_p,n_internal_nets,total_transistors,"
        << "cpp_min,cell_fraction,cpp_min_ds\n";

    size_t rows_written = 0;

    auto emit_row = [&](const std::string& canonical,
                        const std::string& cell_name,
                        long long count,
                        const std::string& logic_expr,
                        const std::string& ds) {
        const int nfin_scale = ds_nfin_scale(ds);

        const int n   = n_inputs_from_key(canonical);
        const auto tt = tt_bits(canonical, n);
        const int sz  = 1 << n;

        int ones = 0;
        for (int b : tt) ones += b;
        const double tt_weight = static_cast<double>(ones) / sz;

        const int anf_deg   = anf_degree(tt, n);
        const int is_affine = (anf_deg <= 1) ? 1 : 0;
        const int is_sym    = is_symmetric(tt, n) ? 1 : 0;
        const int is_mono   = is_monotone(tt, n) ? 1 : 0;
        const int is_anti   = is_antimonotone(tt, n) ? 1 : 0;
        const int in_inv    = input_inv_count(tt, n);

        const std::string cls = detect_class(tt, n);
        int out_inv = -1, fet = -1;
        bool cls_unknown = cls.empty();
        if (!cls_unknown) {
            out_inv = output_inv_count_for(cls);
            fet     = fet_cost(cls, n);
        }

        const std::string lookup = cdl_lookup_name(canonical);
        auto it = cdl_cells.find(lookup);
        if (it == cdl_cells.end())
            throw std::runtime_error(
                "extract_features: CDL subckt '" + lookup +
                "' not found in generated CDL for canonical=" + canonical +
                ". Check that canonical_to_cdl supports this function.");
        const CdlFeats cdl = extract_cdl_features(
            it->second, widths.wmax_n, widths.wmax_p, nfin_scale, pdk_unit_nfin(pdk));

        if (cls_unknown) {
            const std::unordered_set<std::string> cell_ports(
                it->second.pins.begin(), it->second.pins.end());
            fet = cdl.n_nmos + cdl.n_pmos;
            out_inv = 0;
            for (const auto& d : it->second.devices) {
                if (!d.is_nmos || d.source != "VSS") continue;
                if (cell_ports.count(d.gate)) continue;
                if (d.drain == "Y" || d.drain == "ZN") ++out_inv;
            }
            std::cerr << "WARNING: unrecognized function class for canonical_key=" << canonical
                      << " (n_inputs=" << n << "); output_inv_count=" << out_inv
                      << " and fet_count=" << fet << " are derived from CDL device counts, "
                         "NOT the idealized static-CMOS formula used for classified cells "
                         "-- do not compare/average them with classified rows. logic_expr "
                         "for this row is tagged [UNCLASSIFIED_FALLBACK].\n";
        }
        std::string logic_expr_out = logic_expr;
        if (cls_unknown) logic_expr_out = "[UNCLASSIFIED_FALLBACK] " + logic_expr_out;

        const int total_transistors = cdl.n_nmos + cdl.n_pmos;
        {
            const int denom = std::max(1, std::max(fet, total_transistors));
            const double rel_diff = std::abs(fet - total_transistors) / static_cast<double>(denom);
            if (rel_diff > 0.30) {
                std::cerr << "WARNING: fet_count/total_transistors mismatch for canonical_key="
                          << canonical << " cell_name=" << cell_name
                          << " class=" << (cls_unknown ? "UNKNOWN" : cls)
                          << " fet_count=" << fet << " total_transistors=" << total_transistors
                          << " (relative diff=" << std::fixed << std::setprecision(1)
                          << (rel_diff * 100.0) << "%)\n";
            }
        }

        const double cell_frac = [&]() -> double {
            if (netlist_path.empty()) return 0.0;
            ++cell_frac_rows;
            auto jt = cell_fracs.find(cell_name);
            if (jt != cell_fracs.end()) { ++cell_frac_hits; return jt->second; }
            return 0.0;
        }();

        out << csv_quote(canonical) << ","
            << csv_quote(cell_name) << ","
            << count                << ","
            << csv_quote(logic_expr_out) << ","
            << n                    << ","
            << std::fixed << std::setprecision(6) << tt_weight << ","
            << anf_deg              << ","
            << is_affine            << ","
            << is_sym               << ","
            << is_mono              << ","
            << is_anti              << ","
            << out_inv              << ","
            << in_inv               << ","
            << fet                  << ","
            << cdl.n_nmos           << ","
            << cdl.n_pmos           << ","
            << static_cast<int>(std::round(cdl.nfin_n_total)) << ","
            << static_cast<int>(std::round(cdl.nfin_p_total)) << ","
            << std::fixed << std::setprecision(6) << cdl.nfin_ratio << ","
            << cdl.max_stack_n      << ","
            << cdl.max_stack_p      << ","
            << cdl.n_internal_nets  << ","
            << total_transistors    << ","
            << cdl.cpp_min          << ","
            << std::fixed << std::setprecision(6) << cell_frac << ","
            << cdl.cpp_min_ds
            << "\n";
        ++rows_written;
    };

    if (is_mining_mode) {
        for (const auto& row : mining_rows) {
            for (const auto& ds : drive_strengths) {
                const std::string cname = feature_name_for(row.canonical, ds);
                emit_row(row.canonical, cname, row.count, row.logic_expr, ds);
            }
        }
    } else {
        for (const auto& row : cells_rows) {

            const std::string& x1_name = row.name;
            emit_row(row.canonical, x1_name, 0, "", "X1");

            for (const auto& ds : drive_strengths) {
                if (ds == "X1") continue;
                const std::string ds_name = rename_ds(row.name, ds);
                emit_row(row.canonical, ds_name, 0, "", ds);
            }
        }
    }

    out.close();
    if (!out) throw std::runtime_error("write failed: " + out_path);

    std::cout << "[extract_features] wrote " << rows_written
              << " rows (" << n_base << " base × "
              << drive_strengths.size() << " drive strength(s)) to "
              << out_path << "\n";

    if (!netlist_path.empty() && cell_frac_hits == 0 && cell_frac_rows > 0)
        std::cerr << "WARNING: cell_fraction matched 0/" << cell_frac_rows
                   << " rows against --netlist " << netlist_path
                   << " -- the column is a constant 0.0 in this output.\n";
    return 0;

} catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
}
