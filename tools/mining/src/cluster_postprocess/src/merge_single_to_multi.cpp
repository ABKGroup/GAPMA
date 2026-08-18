

#include "multi_output_grouping.hpp"
#include "cluster_db_reader.hpp"
#include "netlist_db_reader.hpp"
#include "runtime_logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void write_frequency_report(const std::string& path, const std::vector<MultiOutputRow>& rows) {
  std::ofstream f(path);
  if (!f) { throw std::runtime_error("cannot open frequency report: " + path); }
  f << "num_inputs,num_outputs,canonical,count,avg_input_overlap,avg_cell_overlap\n";
  f.precision(4);
  f << std::fixed;
  for (const auto& r : rows) {
    f << r.num_inputs << "," << r.num_outputs << "," << r.canonical << "," << r.count
      << "," << r.avg_input_overlap << "," << r.avg_cell_overlap << "\n";
  }
  std::cerr << "[done] " << path << " (" << rows.size() << " entries)\n";
}

int main(int argc, char** argv) {
  try {
  RuntimeLogger rl("merge_single_to_multi");
  std::string db_path, netlist_db_path, out_path, merge_db_path;

  MultiOutputParams params;
  params.max_outputs = 2;
  params.max_inputs = 6;
  params.num_cores = 8;
  params.max_distance = -1.0;
  params.input_sharing_mode = "shared";
  params.min_shared_inputs = 1;
  params.min_shared_cells = 0;

  { auto s = rl.step("parse args");
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](const std::string& flag) -> std::string {
      if (i + 1 >= argc) { std::cerr << "missing value for " << flag << "\n"; std::exit(1); }
      return argv[++i];
    };
    if (a == "--db") db_path = need(a);
    else if (a == "--netlist-db") netlist_db_path = need(a);
    else if (a == "--out") out_path = need(a);
    else if (a == "--merge-db") merge_db_path = need(a);
    else if (a == "--max-outputs") params.max_outputs = std::stoi(need(a));
    else if (a == "--max-inputs") params.max_inputs = std::stoi(need(a));
    else if (a == "--max-distance") params.max_distance = std::stod(need(a));
    else if (a == "--num-cores") params.num_cores = std::max(1, std::stoi(need(a)));
    else if (a == "--input-sharing-mode") {
      params.input_sharing_mode = need(a);
      if (params.input_sharing_mode != "shared" && params.input_sharing_mode != "exact") {
        std::cerr << "[error] --input-sharing-mode must be 'shared' or 'exact'\n"; return 1;
      }
    }
    else if (a == "--min-shared-inputs") {
      params.min_shared_inputs = std::stoi(need(a));
      if (params.min_shared_inputs < 1) { std::cerr << "[error] --min-shared-inputs must be >= 1\n"; return 1; }
    }
    else if (a == "--min-shared-cells") {
      params.min_shared_cells = std::stoi(need(a));
      if (params.min_shared_cells < 0) { std::cerr << "[error] --min-shared-cells must be >= 0\n"; return 1; }
    }
    else if (a == "--help" || a == "-h") {
      std::cout << "usage: merge_single_to_multi_cpp --db <db.cdb> --netlist-db <netlist.cdb> [--out <prefix>]\n"
                << "       [--max-outputs 2] [--max-inputs 6] [--max-distance <um>]\n"
                << "       [--input-sharing-mode shared|exact] [--min-shared-inputs N]\n"
                << "\n"
                << "Options:\n"
                << "  --db <path>               cluster.cdb (required)\n"
                << "  --netlist-db <path>       netlist.cdb (required)\n"
                << "  --out <prefix>            output CSV/instance file prefix\n"
                << "  --merge-db <path>         output merge.cdb binary (skips CSV output)\n"
                << "  --max-outputs N           max outputs per candidate: 2 or 3 (default: 2)\n"
                << "  --max-inputs N            max merged input count (default: 6)\n"
                << "  --max-distance <um>       max physical span between merged clusters\n"
                << "  --input-sharing-mode M    shared|exact (default: shared)\n"
                << "  --min-shared-inputs N     min shared inputs in 'shared' mode (default: 1)\n"
                << "  --min-shared-cells N      min shared cells between merged cluster pair (default: 0)\n";
      return 0;
    }
    else {
      std::cerr << "[error] unknown argument: " << a << "\n";
      return 1;
    }
  }
  if (db_path.empty()) { std::cerr << "[error] --db is required.\n"; return 1; }
  if (netlist_db_path.empty()) { std::cerr << "[error] --netlist-db is required.\n"; return 1; }
  }

  ClusterDB db;
  NetlistDb ldb;
  { auto s = rl.step("load databases");
  db = read_cluster_db(db_path);
  ldb = read_netlist_db(netlist_db_path);
  std::cerr << "[info] loaded DB: " << db.clusters.size() << " clusters, "
            << db.logic_functions.size() << " logic functions\n";
  std::cerr << "[info] input_sharing_mode=" << params.input_sharing_mode
            << " min_shared_inputs=" << params.min_shared_inputs
            << " min_shared_cells=" << params.min_shared_cells << "\n";
  }

  std::vector<MultiOutputRow> all_rows;

  for (const auto& p : db.logic_functions) {
    int n_in = 0;
    const auto& c = p.canonical;
    if (c.size() > 2 && c.substr(0, 2) == "0b") {
      int bits = static_cast<int>(c.size() - 2);
      while ((1 << n_in) < bits) ++n_in;
    }
    MultiOutputRow r;
    r.num_inputs = n_in;
    r.num_outputs = 1;
    r.canonical = p.canonical;
    r.count = p.count;
    r.avg_input_overlap = 1.0;
    r.avg_cell_overlap = 1.0;
    all_rows.push_back(std::move(r));
  }

  MultiOutputResult result;
  { auto s = rl.step("compute merges");
  result = run_multi_output_grouping(
      db, ldb, params, merge_db_path,
      merge_db_path.empty() ? out_path : std::string());
  }

  for (auto& r : result.rows) all_rows.push_back(std::move(r));

  { auto s = rl.step("write output");
  std::sort(all_rows.begin(), all_rows.end(),
            [](const MultiOutputRow& a, const MultiOutputRow& b) { return a.count > b.count; });
  if (merge_db_path.empty()) {
    std::string report_path = out_path.empty() ? "frequency_report.csv" : out_path + ".csv";
    write_frequency_report(report_path, all_rows);
  }
  }

  rl.done();
  return 0;
  } catch (const std::exception& e) {
    std::cerr << "[error] " << e.what() << "\n";
    return 1;
  }
}
