

#include "runtime_logger.hpp"
#include "cell_db_reader.hpp"
#include "cell_db_writer.hpp"

#include "canonical.hpp"
#include "util.hpp"

#include "cell_metrics.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
#include "opendb/lefin.h"
#endif

namespace {

std::string family_name(const std::string& name) {
  auto pos = name.find('x');
  if (pos == std::string::npos) pos = name.find('_');
  return pos == std::string::npos ? name : name.substr(0, pos);
}

std::string lower_copy(const std::string& s) {
  std::string r = s;
  for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

cell_db_writer::WMaster entry_to_wmaster(const MasterEntry& me) {
  cell_db_writer::WMaster wm;
  wm.name         = me.name;
  wm.family       = me.family;
  wm.area         = me.area;
  wm.is_seq       = me.is_seq;
  wm.output_count = me.output_count;
  wm.width        = me.width;
  wm.height       = me.height;
  wm.site         = me.site;
  wm.symmetry     = me.symmetry;
  wm.tr_nmos          = me.tr_nmos;
  wm.tr_pmos          = me.tr_pmos;
  wm.shared_diffusion   = me.shared_diffusion;
  for (const auto& pe : me.pins) {
    cell_db_writer::WPin wp;
    wp.name      = pe.name;
    wp.direction = pe.direction;
    wm.pins.push_back(std::move(wp));
  }
  for (const auto& fe : me.functions) {
    cell_db_writer::WFunction wf;
    wf.pin_name              = fe.pin_name;
    wf.expr                  = fe.expr;
    wf.canonical             = fe.canonical;
    wf.stage_count           = fe.stage_count;
    wf.max_pu_stack_depth    = fe.max_pu_stack_depth;
    wf.max_pd_stack_depth    = fe.max_pd_stack_depth;
    wf.resistance_proxy_max  = fe.resistance_proxy_max;
    wf.capacitance_proxy_max = fe.capacitance_proxy_max;
    wm.functions.push_back(std::move(wf));
  }
  return wm;
}

int apply_cdl(std::vector<cell_db_writer::WMaster>& masters,
              const std::string& cdl_path) {
  std::cerr << "[info] parsing CDL: " << cdl_path << "\n";
  const std::string cdl_text = cell_metrics::read_text(cdl_path);
  auto all_subckts = cell_metrics::parse_all_subckts(cdl_text);
  std::cerr << "[info] CDL: parsed " << all_subckts.size() << " subckts\n";

  std::atomic<int> matched{0}, overridden{0};
  const int n = static_cast<int>(masters.size());
  #pragma omp parallel for schedule(dynamic)
  for (int i = 0; i < n; ++i) {
    auto& wm = masters[i];
    auto it = all_subckts.find(wm.name);
    if (it == all_subckts.end()) continue;

    try {
      auto cm = cell_metrics::compute_cell_metrics(it->second);
      auto new_nmos = static_cast<uint16_t>(cm.transistor_count_nmos);
      auto new_pmos = static_cast<uint16_t>(cm.transistor_count_pmos);
      if (wm.tr_nmos > 0 && (wm.tr_nmos != new_nmos || wm.tr_pmos != new_pmos)) {
        #pragma omp critical
        std::cerr << "[warn] CDL override " << wm.name
                  << ": TR " << wm.tr_nmos << "+" << wm.tr_pmos
                  << " -> " << new_nmos << "+" << new_pmos << "\n";
        ++overridden;
      }
      wm.tr_nmos = new_nmos;
      wm.tr_pmos = new_pmos;

      for (auto& wf : wm.functions) {
        auto oit = cm.outputs.find(wf.pin_name);
        if (oit == cm.outputs.end()) continue;
        const auto& om = oit->second;
        wf.stage_count           = static_cast<int16_t>(om.stage_count);
        wf.max_pu_stack_depth    = static_cast<int16_t>(om.max_pu_stack_depth);
        wf.max_pd_stack_depth    = static_cast<int16_t>(om.max_pd_stack_depth);
        wf.resistance_proxy_max  = static_cast<float>(om.input_to_output_resistance_proxy_max);
        wf.capacitance_proxy_max = static_cast<float>(om.input_to_output_capacitance_proxy_max);
      }
      ++matched;
    } catch (const std::exception& e) {
      #pragma omp critical
      std::cerr << "[warn] CDL metrics failed for " << wm.name << ": " << e.what() << "\n";
    }
  }
  std::cerr << "[info] CDL: updated " << matched.load() << "/" << masters.size() << " masters";
  if (overridden.load() > 0) std::cerr << " (" << overridden.load() << " overridden)";
  std::cerr << "\n";

  if (matched.load() == 0) {
    throw std::runtime_error("CDL " + cdl_path + ": 0 of " + std::to_string(masters.size()) +
                             " masters matched any subckt (" + std::to_string(all_subckts.size()) +
                             " subckts parsed); transistor-level metrics would all be zero");
  }
  return matched.load();
}

}

int main(int argc, char** argv) {
  try {
    RuntimeLogger rl("lib2cdb");

    std::string out_path;
    std::string existing_path;
    std::vector<std::string> cdl_paths;
    std::string shared_diffusion_csv_path;
    std::vector<std::string> lib_files;
    std::vector<std::string> lef_files;
    std::vector<std::string> lib_dirs;
    int max_canonical_inputs = 8;

    { auto s = rl.step("parse args");
    int i = 1;
    auto need_value = [&](const std::string& flag) -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + flag);
      return argv[++i];
    };
    for (; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--out" || a == "-o") {
        out_path = need_value(a);
      } else if (a == "--existing") {
        existing_path = need_value(a);
      } else if (a == "--cdl") {
        cdl_paths.push_back(need_value(a));
      } else if (a == "--lib-dir") {
        lib_dirs.push_back(need_value(a));
      } else if (a == "--libs") {
        while (i + 1 < argc && argv[i + 1][0] != '-') lib_files.push_back(argv[++i]);
      } else if (a == "--lef") {
        while (i + 1 < argc && argv[i + 1][0] != '-') lef_files.push_back(argv[++i]);
      } else if (a == "--shared-diffusion-csv") {
        shared_diffusion_csv_path = need_value(a);
      } else if (a == "--max-canonical-inputs") {
        max_canonical_inputs = std::stoi(need_value(a));
      } else if (a == "--help" || a == "-h") {
        std::cout <<
          "usage: lib2cdb [--existing cell.cdb] [--libs *.lib] [--lib-dir <dir> ...]\n"
          "               [--lef <cell.lef ...>] [--cdl <file.cdl> ...] --out <cell.cdb>\n"
          "               [--max-canonical-inputs N]\n"
          "\n"
          "Incremental mode (--existing): load existing DB, merge new inputs on top.\n"
          "  LIB  — adds new cells; duplicate names replace the existing entry.\n"
          "  LEF  — updates width/height/site/symmetry on matching cells.\n"
          "         Cell/macro LEF only; tech LEF is not required.\n"
          "         Requires patched OpenDB (opendb-lef-null-guard.patch).\n"
          "  CDL  — updates tr_nmos/tr_pmos and per-output stage/stack/proxy metrics.\n"
          "\n"
          "  --max-canonical-inputs N  Max input count for canonical computation (default: 8).\n"
          "                            Cells with more inputs store function but not canonical.\n"
          "                            Canonical is O(n!): 8!=40320, 9!=362880, 10!=3628800.\n"
          "\n"
          "At least one of --existing / --libs / --lib-dir is required.\n";
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

    if (out_path.empty()) { std::cerr << "ERROR: --out required\n"; return 1; }
    if (existing_path.empty() && lib_files.empty()) {
      std::cerr << "ERROR: --existing or liberty files required\n"; return 1;
    }

    if (lef_files.empty()) {
      std::cerr << "[warning] no --lef provided: physical info (width, height, site, symmetry) will not be included in cell.cdb\n";
    }
    if (cdl_paths.empty()) {
      std::cerr << "[warning] no --cdl provided: transistor-level metrics (NMOS/PMOS counts, stage depth, resistance/capacitance proxy) will not be included in cell.cdb\n";
    }
    }

    std::vector<cell_db_writer::WMaster> masters;
    std::unordered_map<std::string, size_t> seen_masters;

    { auto s = rl.step("load existing db");
    if (!existing_path.empty()) {
      std::cerr << "[info] loading existing cell_db: " << existing_path << "\n";
      CellDb existing = read_cell_db(existing_path);
      masters.reserve(existing.masters.size());
      for (const auto& me : existing.masters) {
        seen_masters[me.name] = masters.size();
        masters.push_back(entry_to_wmaster(me));
      }
      std::cerr << "[info] loaded " << masters.size() << " existing masters\n";
    }
    }

    { auto s = rl.step("parse liberty");
    if (!lib_files.empty()) {
      Yosys::LibertyMergedCells merged;
      for (const auto& lib : lib_files) {
        std::ifstream ifs(lib);
        if (!ifs.is_open()) throw std::runtime_error("failed to open liberty: " + lib);
        Yosys::LibertyParser parser(ifs);
        merged.merge(parser);
      }

      std::vector<Yosys::LibertyAst*> valid_cells;
      for (auto* cell : merged.cells) {
        if (cell && cell->args.size() == 1)
          valid_cells.push_back(const_cast<Yosys::LibertyAst*>(cell));
      }

      const int ncells = static_cast<int>(valid_cells.size());
      std::vector<cell_db_writer::WMaster> new_masters(ncells);
      std::atomic<bool> area_parse_failed{false};
      std::string area_parse_error;
      std::atomic<bool> bus_group_found{false};
      std::string bus_group_error;
      #pragma omp parallel for schedule(dynamic)
      for (int ci = 0; ci < ncells; ++ci) {
        const auto* cell = valid_cells[ci];
        const std::string& cell_name = cell->args[0];

        cell_db_writer::WMaster wm;
        wm.name   = cell_name;
        wm.family = family_name(cell_name);

        if (const auto* area = cell->find("area")) {
          try { wm.area = std::stof(area->value); }
          catch (const std::exception& e) {
            #pragma omp critical
            {
              if (!area_parse_failed.load()) {
                area_parse_error = "area parse failed for cell '" + cell_name +
                                   "': value='" + area->value + "' (" + e.what() + ")";
                area_parse_failed.store(true);
              }
            }
          }
        }

        bool is_seq = false;
        std::map<std::string, std::string> pin_directions;
        std::map<std::string, std::string> functions;
        std::set<std::string> input_terms;

        for (const auto* node : cell->children) {
          if (!node) continue;
          if (node->id == "ff" || node->id == "ff_bank" || node->id == "latch" ||
              node->id == "latch_bank" || node->id == "statetable" || node->id == "sequential") {
            is_seq = true;
          }

          if (node->id == "bus" || node->id == "bundle") {
            const auto* members = node->find("members");
            const auto* bdir    = node->find("direction");
            std::vector<std::string> member_names;
            if (members) {
              if (!members->args.empty()) {
                member_names = members->args;
              } else if (!members->value.empty()) {
                std::stringstream ms(members->value);
                std::string tok;
                while (std::getline(ms, tok, ',')) {
                  tok.erase(0, tok.find_first_not_of(" \t"));
                  tok.erase(tok.find_last_not_of(" \t") + 1);
                  if (!tok.empty()) member_names.push_back(tok);
                }
              }
            }
            if (node->id == "bundle" && bdir && !member_names.empty()) {
              auto dir_value = lower_copy(bdir->value);
              const auto* bfunc = node->find("function");
              for (const auto& mname : member_names) {
                if (dir_value == "internal") continue;
                cell_db_writer::WPin wp;
                wp.name = mname;
                if (dir_value == "input") {
                  wp.direction = 0;
                  pin_directions[mname] = "in";
                  input_terms.insert(mname);
                } else if (dir_value == "output") {
                  wp.direction = 1;
                  pin_directions[mname] = "out";
                  if (bfunc) functions[mname] = bfunc->value;
                } else if (dir_value == "inout") {
                  wp.direction = 2;
                  pin_directions[mname] = "inout";
                } else {
                  continue;
                }
                wm.pins.push_back(std::move(wp));
              }
              continue;
            }
            #pragma omp critical
            {
              if (!bus_group_found.load()) {
                bus_group_error = "cell '" + cell_name + "': Liberty '" + node->id + "' group '" +
                                  (node->args.empty() ? std::string("<unnamed>") : node->args[0]) +
                                  "' cannot be expanded into individual pins; refusing to drop it"
                                  " silently (its ports would be missing from cell.cdb and"
                                  " output_count would be understated)";
                bus_group_found.store(true);
              }
            }
            continue;
          }
          if (node->id != "pin" || node->args.size() != 1) continue;

          auto* dir = node->find("direction");
          if (!dir) continue;

          auto pin_name  = node->args[0];
          auto dir_value = lower_copy(dir->value);
          if (dir_value == "internal") continue;

          cell_db_writer::WPin wp;
          wp.name = pin_name;
          if (dir_value == "input") {
            wp.direction = 0;
            pin_directions[pin_name] = "in";
            input_terms.insert(pin_name);
          } else if (dir_value == "output") {
            wp.direction = 1;
            pin_directions[pin_name] = "out";
            if (const auto* func = node->find("function")) functions[pin_name] = func->value;
          } else if (dir_value == "inout") {
            wp.direction = 2;
            pin_directions[pin_name] = "inout";
          } else {
            continue;
          }
          wm.pins.push_back(std::move(wp));
        }

        wm.is_seq = is_seq;
        int out_count = 0;
        for (const auto& [_, d] : pin_directions) { if (d == "out") out_count++; }
        wm.output_count = out_count;

        std::vector<std::string> sorted_inputs(input_terms.begin(), input_terms.end());
        std::sort(sorted_inputs.begin(), sorted_inputs.end());

        if (!is_seq && !functions.empty()) {
          for (const auto& [pin, expr] : functions) {
            cell_db_writer::WFunction wf;
            wf.pin_name = pin;
            wf.expr     = expr;

            if (!sorted_inputs.empty() && static_cast<int>(sorted_inputs.size()) <= max_canonical_inputs) {
              wf.canonical = normalize_target_canonical(canonical_from_expr(expr, sorted_inputs));
            }
            wm.functions.push_back(std::move(wf));
          }
        }

        new_masters[ci] = std::move(wm);
      }

      if (area_parse_failed.load()) {
        throw std::runtime_error(area_parse_error);
      }
      if (bus_group_found.load()) {
        throw std::runtime_error(bus_group_error);
      }

      int overridden = 0;
      for (int ci = 0; ci < ncells; ++ci) {
        auto& wm = new_masters[ci];
        auto it = seen_masters.find(wm.name);
        if (it != seen_masters.end()) {
          auto& existing_wm = masters[it->second];
          std::cerr << "[warn] overriding existing master: " << wm.name << "\n";
          ++overridden;
          existing_wm = std::move(wm);
        } else {
          seen_masters[wm.name] = masters.size();
          masters.push_back(std::move(wm));
        }
      }
      if (overridden > 0)
        std::cerr << "[info] " << overridden << " existing master(s) overridden by new Liberty data\n";
    }
    }

    bool lef_applied = false;
    bool cdl_applied = false;

    { auto s = rl.step("process LEF");
#ifdef LOGIC_MINOR_USE_OPENDB
    if (!lef_files.empty()) {
      std::unordered_map<std::string, size_t> master_idx;
      for (size_t i = 0; i < masters.size(); ++i) master_idx[masters[i].name] = i;

      odb::dbDatabase* odb_db = odb::dbDatabase::create();
      if (!odb_db) {
        throw std::runtime_error("OpenDB dbDatabase::create() failed; cannot read LEF files");
      }
      {
        odb::lefin lef_parser(odb_db, false);

        {
          std::list<std::string> lef_list(lef_files.begin(), lef_files.end());
          lef_parser.createTechAndLib("lef_lib", lef_list);
        }
        int lef_matched = 0, lef_overridden = 0;
        for (odb::dbLib* lib : odb_db->getLibs()) {
          for (odb::dbMaster* m : lib->getMasters()) {
            auto it = master_idx.find(m->getName());
            if (it == master_idx.end()) continue;
            auto& wm = masters[it->second];
            int dbu = odb_db->getTech() ? odb_db->getTech()->getLefUnits() : 1000;
            if (dbu <= 0) dbu = 1000;
            float new_w = static_cast<float>(m->getWidth())  / static_cast<float>(dbu);
            float new_h = static_cast<float>(m->getHeight()) / static_cast<float>(dbu);
            if (wm.width > 0 && (wm.width != new_w || wm.height != new_h)) {
              std::cerr << "[warn] LEF override " << wm.name
                        << ": size " << wm.width << "x" << wm.height
                        << " -> " << new_w << "x" << new_h << "\n";
              ++lef_overridden;
            }
            wm.width  = new_w;
            wm.height = new_h;
            odb::dbSite* site = m->getSite();
            if (site) wm.site = site->getName();
            auto sym = m->getSymmetryX() | (m->getSymmetryY() << 1) | (m->getSymmetryR90() << 2);
            wm.symmetry = static_cast<uint8_t>(sym);
            ++lef_matched;
          }
        }
        if (lef_overridden > 0)
          std::cerr << "[info] LEF: " << lef_overridden << " master(s) had size overridden\n";
        odb::dbDatabase::destroy(odb_db);
        std::cerr << "[info] LEF: matched " << lef_matched << "/" << masters.size() << " masters\n";

        if (lef_matched == 0) {
          throw std::runtime_error("LEF: 0 of " + std::to_string(masters.size()) +
                                   " masters matched any LEF macro; physical data would all be zero");
        }
        lef_applied = true;
      }
    }
#else
    if (!lef_files.empty()) {
      throw std::runtime_error("--lef specified but lib2cdb was built without OpenDB support "
                               "(rebuild with -DDDC_USE_OPENDB=ON); refusing to write a cell.cdb "
                               "that claims LEF coverage with no LEF data");
    }
#endif

    for (const auto& cdl_path : cdl_paths) {
      if (apply_cdl(masters, cdl_path) > 0) cdl_applied = true;
    }
    }

    { auto s = rl.step("write output");
    if (!shared_diffusion_csv_path.empty()) {
      std::ifstream bcsv(shared_diffusion_csv_path);
      if (!bcsv.is_open())
        throw std::runtime_error("failed to open shared_diffusion CSV: " + shared_diffusion_csv_path);

      std::unordered_map<std::string, uint8_t> bp_map;
      std::string line;
      while (std::getline(bcsv, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        std::string name = line.substr(0, comma);
        int val = std::stoi(line.substr(comma + 1));
        bp_map[name] = static_cast<uint8_t>(val);
      }

      int matched = 0;
      for (auto& wm : masters) {
        auto it = bp_map.find(wm.name);
        if (it != bp_map.end()) {
          wm.shared_diffusion = it->second;
          ++matched;
        }
      }
      std::cerr << "[info] shared_diffusion: updated " << matched << "/" << masters.size()
                << " masters from " << shared_diffusion_csv_path << "\n";
    }

    std::sort(masters.begin(), masters.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    uint32_t src_flags = CELL_DB_SRC_LIBERTY;
    if (lef_applied) src_flags |= CELL_DB_SRC_LEF;
    if (cdl_applied) src_flags |= CELL_DB_SRC_CDL;

    if (!cell_db_writer::write_db(out_path, masters, src_flags)) {
      throw std::runtime_error("failed to write cell database: " + out_path);
    }

    std::cout << "[done] cell_db=" << out_path << " masters=" << masters.size() << "\n";
    }
    rl.done();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[error] " << e.what() << "\n";
    return 1;
  }
}
