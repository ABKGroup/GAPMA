

#include "cluster_db_reader.hpp"
#include "runtime_logger.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::vector<std::string> split_commas(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream ss(s);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    size_t b = tok.find_first_not_of(" \t");
    if (b != std::string::npos)
      out.push_back(tok.substr(b, tok.find_last_not_of(" \t") - b + 1));
  }
  return out;
}

}

int main(int argc, char** argv) {
  RuntimeLogger rl("select_overlap_cells");
  try {
    std::string db_path, primary_canonical;
    int max_secondary = 2;
    std::string library_only_str, exclude_str;
    std::unordered_set<std::string> library_set;
    std::unordered_set<std::string> exclude_set;

    { auto s = rl.step("parse args");
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto need = [&]() -> std::string {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
        return argv[++i];
      };
      if (a == "--db") db_path = need();
      else if (a == "--primary-canonical") primary_canonical = need();
      else if (a == "--max-secondary") max_secondary = std::stoi(need());
      else if (a == "--library-only") library_only_str = need();
      else if (a == "--exclude") exclude_str = need();
      else if (a == "--help" || a == "-h") {
        std::cout << "usage: select_overlap_cells --db clusters.cdb "
                     "--primary-canonical <canonical>\n"
                     "       [--max-secondary 2] [--library-only <c1,c2,...>] "
                     "[--exclude <c1,c2,...>]\n";
        return 0;
      } else {
        throw std::runtime_error("unknown argument: " + a);
      }
    }
    if (db_path.empty() || primary_canonical.empty()) {
      throw std::runtime_error("--db and --primary-canonical are required");
    }

    if (!library_only_str.empty()) {
      for (const auto& c : split_commas(library_only_str)) library_set.insert(c);
    }
    if (!exclude_str.empty()) {
      for (const auto& c : split_commas(exclude_str)) exclude_set.insert(c);
    }
    exclude_set.insert(primary_canonical);
    }

    ClusterDB db;
    { auto s = rl.step("load cluster db");
    db = read_cluster_db(db_path);
    }

    struct Candidate {
      std::string canonical;
      int max_overlap;
    };
    std::vector<Candidate> candidates;

    { auto s = rl.step("compute overlaps");

    std::unordered_map<int32_t, size_t> id_to_idx;
    for (size_t i = 0; i < db.clusters.size(); ++i) {
      id_to_idx[db.clusters[i].id] = i;
    }

    std::vector<size_t> primary_indices;
    for (size_t i = 0; i < db.clusters.size(); ++i) {
      if (db.clusters[i].canonical == primary_canonical)
        primary_indices.push_back(i);
    }

    if (primary_indices.empty()) {
      std::cerr << "[info] no clusters found for canonical: " << primary_canonical << "\n";
      return 0;
    }
    std::cerr << "[info] primary canonical: " << primary_canonical
              << " (" << primary_indices.size() << " clusters)\n";

    std::unordered_map<std::string, int> max_per_cluster_overlap;

    for (size_t pi : primary_indices) {
      const auto& pc = db.clusters[pi];

      std::unordered_map<std::string, int> local_count;
      for (int32_t ov_id : pc.overlap_cluster_ids) {
        auto it = id_to_idx.find(ov_id);
        if (it == id_to_idx.end()) continue;
        const auto& ov_cluster = db.clusters[it->second];
        if (ov_cluster.canonical == primary_canonical) continue;
        local_count[ov_cluster.canonical]++;
      }

      for (const auto& [canonical, count] : local_count) {
        auto& m = max_per_cluster_overlap[canonical];
        if (count > m) m = count;
      }
    }

    for (const auto& [canonical, max_ov] : max_per_cluster_overlap) {
      if (exclude_set.count(canonical)) continue;
      if (!library_set.empty() && !library_set.count(canonical)) continue;
      candidates.push_back({canonical, max_ov});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                if (a.max_overlap != b.max_overlap) return a.max_overlap > b.max_overlap;
                return a.canonical < b.canonical;
              });
    }

    { auto s = rl.step("output results");

    int n = std::min(max_secondary, static_cast<int>(candidates.size()));
    std::cerr << "[info] " << candidates.size() << " overlap candidates, selecting top-"
              << n << "\n";

    std::cout << "canonical,max_overlap\n";
    for (int i = 0; i < n; ++i) {
      std::cout << candidates[i].canonical << "," << candidates[i].max_overlap << "\n";
      std::cerr << "  " << (i + 1) << ". " << candidates[i].canonical
                << " (max_overlap=" << candidates[i].max_overlap << ")\n";
    }
    }

    rl.done();
    return 0;
  } catch (const std::exception& e) {
    rl.done();
    std::cerr << "[error] " << e.what() << "\n";
    return 1;
  }
}
