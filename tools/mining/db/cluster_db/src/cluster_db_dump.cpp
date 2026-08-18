

#include "runtime_logger.hpp"
#include "cluster_db_reader.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

static void print_summary(const ClusterDB& db) {
  std::cout << "clusters: " << db.clusters.size() << "\n";
  std::cout << "logic_functions: " << db.logic_functions.size() << "\n";
  std::cout << "overlap_groups: " << db.overlap_groups.size() << "\n";
  std::cout << "subset_pairs: " << db.subset_pairs.size() << "\n";
}

static void print_cluster(const ClusterEntry& c) {
  std::cout << "cluster " << c.id << ":\n";
  std::cout << "  canonical: " << c.canonical << "\n";
  std::cout << "  cells:";
  for (auto id : c.cell_ids) std::cout << " " << id;
  std::cout << "\n";
  std::cout << "  inputs:";
  for (const auto& s : c.inputs) std::cout << " " << s;
  std::cout << "\n";
  std::cout << "  outputs:";
  for (const auto& s : c.outputs) std::cout << " " << s;
  std::cout << "\n";
  if (c.total_area > 0) std::cout << "  total_area: " << c.total_area << "\n";
  if (c.depth > 0) std::cout << "  depth: " << c.depth << "\n";
  if (!c.truth_table.empty())
    std::cout << "  truth_table: " << c.truth_table << "\n";
  if (!c.input_order.empty()) {
    std::cout << "  input_order:";
    for (const auto& s : c.input_order) std::cout << " " << s;
    std::cout << "\n";
  }
  if (!c.internal_nets.empty()) {
    std::cout << "  internal_nets:";
    for (const auto& s : c.internal_nets) std::cout << " " << s;
    std::cout << "\n";
  }
  if (!c.boundary_input_pins.empty()) {
    std::cout << "  boundary_input_pins:\n";
    for (const auto& bp : c.boundary_input_pins)
      std::cout << "    cell=" << bp.cell_id << " pin=" << bp.pin_name << " net=" << bp.net_name << "\n";
  }
  if (!c.boundary_output_pins.empty()) {
    std::cout << "  boundary_output_pins:\n";
    for (const auto& bp : c.boundary_output_pins)
      std::cout << "    cell=" << bp.cell_id << " pin=" << bp.pin_name << " net=" << bp.net_name << "\n";
  }
  if (!c.overlap_cluster_ids.empty()) {
    std::cout << "  overlaps:";
    for (auto id : c.overlap_cluster_ids) std::cout << " " << id;
    std::cout << "\n";
  }
  if (!c.superset_cluster_ids.empty()) {
    std::cout << "  supersets:";
    for (auto id : c.superset_cluster_ids) std::cout << " " << id;
    std::cout << "\n";
  }
  if (!c.subset_cluster_ids.empty()) {
    std::cout << "  subsets:";
    for (auto id : c.subset_cluster_ids) std::cout << " " << id;
    std::cout << "\n";
  }
}

static void write_freq_csv(const ClusterDB& db, const std::string& out_path) {
  auto lfs = db.logic_functions;
  std::sort(lfs.begin(), lfs.end(),
            [](const LogicFunctionEntry& a, const LogicFunctionEntry& b) {
              return a.count > b.count;
            });

  std::ostream* out = &std::cout;
  std::ofstream fout;
  if (!out_path.empty()) {
    fout.open(out_path);
    if (!fout) throw std::runtime_error("cannot open output: " + out_path);
    out = &fout;
  }

  *out << "canonical,count,library_match\n";
  for (const auto& lf : lfs) {
    *out << lf.canonical << "," << lf.count << "," << lf.library_match << "\n";
  }
}

static void print_all(const ClusterDB& db) {
  print_summary(db);
  std::cout << "\n=== clusters ===\n";
  for (const auto& c : db.clusters) print_cluster(c);
  if (!db.logic_functions.empty()) {
    std::cout << "\n=== logic_functions ===\n";
    for (const auto& p : db.logic_functions)
      std::cout << "  " << p.canonical << " count=" << p.count
                << " expr=" << p.expr << " lib=" << p.library_match << "\n";
  }
  if (!db.overlap_groups.empty()) {
    std::cout << "\n=== overlap_groups ===\n";
    for (size_t i = 0; i < db.overlap_groups.size(); ++i) {
      std::cout << "  group " << i << ":";
      for (auto id : db.overlap_groups[i]) std::cout << " " << id;
      std::cout << "\n";
    }
  }
}

static void print_json(const ClusterDB& db) {
  auto esc = [](const std::string& s) -> std::string {
    std::string out;
    for (char c : s) {
      if (c == '"') out += "\\\"";
      else if (c == '\\') out += "\\\\";
      else out += c;
    }
    return out;
  };

  std::cout << "{\n";
  std::cout << "  \"clusters\": " << db.clusters.size() << ",\n";
  std::cout << "  \"logic_functions\": " << db.logic_functions.size() << ",\n";

  std::cout << "  \"overlap_groups\": [\n";
  for (size_t i = 0; i < db.overlap_groups.size(); ++i) {
    std::cout << "    [";
    for (size_t j = 0; j < db.overlap_groups[i].size(); ++j) {
      if (j) std::cout << ",";
      std::cout << db.overlap_groups[i][j];
    }
    std::cout << "]";
    if (i + 1 < db.overlap_groups.size()) std::cout << ",";
    std::cout << "\n";
  }
  std::cout << "  ],\n";

  std::cout << "  \"subset_pairs\": [\n";
  for (size_t i = 0; i < db.subset_pairs.size(); ++i) {
    std::cout << "    [" << db.subset_pairs[i].first << "," << db.subset_pairs[i].second << "]";
    if (i + 1 < db.subset_pairs.size()) std::cout << ",";
    std::cout << "\n";
  }
  std::cout << "  ],\n";

  std::cout << "  \"cluster_list\": [\n";
  for (size_t i = 0; i < db.clusters.size(); ++i) {
    const auto& c = db.clusters[i];
    std::cout << "    {\"id\":" << c.id
              << ",\"canonical\":\"" << esc(c.canonical) << "\"";
    std::cout << ",\"cells\":[";
    for (size_t j = 0; j < c.cell_ids.size(); ++j) {
      if (j) std::cout << ",";
      std::cout << c.cell_ids[j];
    }
    std::cout << "]";
    if (!c.truth_table.empty())
      std::cout << ",\"truth_table\":\"" << c.truth_table << "\"";
    std::cout << ",\"total_area\":" << c.total_area << ",\"depth\":" << c.depth;
    std::cout << "}";
    if (i + 1 < db.clusters.size()) std::cout << ",";
    std::cout << "\n";
  }
  std::cout << "  ]\n";
  std::cout << "}\n";
}

static void print_usage(const char* prog) {
  std::cerr << "usage: " << prog
            << " <db.cdb> [--summary|--cluster N|--json|--freq-csv <out.csv>]\\n";
}

int main(int argc, char** argv) {
  if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    print_usage(argv[0]);
    return 0;
  }
  RuntimeLogger rl("cluster_db_dump");

  std::string db_path, mode, freq_csv_out;
  int target_cluster = -1;
  { auto s = rl.step("parse args");
    if (argc < 2) {
      print_usage(argv[0]);
      return 1;
    }
    db_path = argv[1];
    mode = argc >= 3 ? argv[2] : "";
    if (mode == "--cluster") {
      if (argc != 4) { std::cerr << "ERROR: --cluster requires an integer id\n"; return 1; }
      try {
        target_cluster = std::stoi(argv[3]);
      } catch (const std::exception&) {
        std::cerr << "ERROR: --cluster requires an integer id: " << argv[3] << "\n";
        return 1;
      }
    } else if (mode == "--freq-csv") {
      if (argc != 4) { std::cerr << "ERROR: --freq-csv requires an output path\n"; return 1; }
      freq_csv_out = argv[3];
    } else if (!mode.empty() && mode != "--summary" && mode != "--json") {
      std::cerr << "ERROR: unknown argument: " << mode << "\n";
      print_usage(argv[0]);
      return 1;
    } else if (argc != 3 && !mode.empty()) {
      std::cerr << "ERROR: unexpected argument count\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  { auto s = rl.step("load and dump");
    ClusterDB db;
    try {
      db = read_cluster_db(db_path);
    } catch (const std::exception& e) {
      std::cerr << "ERROR: " << e.what() << "\n";
      return 1;
    }

    if (mode == "--summary") {
      print_summary(db);
    } else if (mode == "--cluster") {
      for (const auto& c : db.clusters) {
        if (c.id == target_cluster) { rl.done(); print_cluster(c); return 0; }
      }
      std::cerr << "cluster " << target_cluster << " not found\n";
      rl.done();
      return 1;
    } else if (mode == "--json") {
      print_json(db);
    } else if (mode == "--freq-csv") {
      write_freq_csv(db, freq_csv_out);
    } else {
      print_all(db);
    }
  }
  rl.done();
  return 0;
}
