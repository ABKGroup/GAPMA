

#include "cell_db_reader.hpp"
#include "runtime_logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static const char* dir_str(uint8_t d) {
    switch (d) {
        case 0: return "in";
        case 1: return "out";
        case 2: return "inout";
        default: return "?";
    }
}

static void print_summary(const CellDb& db) {
    size_t total_pins = 0, total_funcs = 0, seq = 0;
    for (const auto& m : db.masters) {
        total_pins  += m.pins.size();
        total_funcs += m.functions.size();
        if (m.is_seq) ++seq;
    }
    std::cout << "masters: "       << db.masters.size() << "\n";
    std::cout << "total_pins: "    << total_pins        << "\n";
    std::cout << "total_functions: " << total_funcs     << "\n";
    std::cout << "sequential: "    << seq               << "\n";
    std::cout << "combinational: " << (db.masters.size() - seq) << "\n";
}

static void print_master(const MasterEntry& m) {
    std::cout << "master: "       << m.name         << "\n";
    std::cout << "  family: "     << m.family        << "\n";
    std::cout << "  area: "       << m.area          << "\n";
    std::cout << "  sequential: " << (m.is_seq ? "True" : "False") << "\n";
    std::cout << "  output_count: " << m.output_count << "\n";
    for (const auto& p : m.pins)
        std::cout << "  pin: " << p.name << " (" << dir_str(p.direction) << ")\n";
    for (const auto& fn : m.functions) {
        std::cout << "  function: " << fn.pin_name << " = " << fn.expr << "\n";
        if (!fn.canonical.empty())
            std::cout << "    canonical: " << fn.canonical << "\n";
    }
}

static void print_all(const CellDb& db) {
    print_summary(db);
    std::cout << "\n";
    for (const auto& m : db.masters) {
        print_master(m);
        std::cout << "\n";
    }
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

static void print_json(const CellDb& db) {
    std::cout << "{\n";
    std::cout << "  \"masters\": " << db.masters.size() << ",\n";
    std::cout << "  \"master_list\": [\n";
    for (size_t i = 0; i < db.masters.size(); ++i) {
        const auto& m = db.masters[i];
        std::cout << "    {\n";
        std::cout << "      \"name\": \""   << json_escape(m.name)   << "\",\n";
        std::cout << "      \"family\": \"" << json_escape(m.family) << "\",\n";
        std::cout << "      \"area\": "     << m.area                << ",\n";
        std::cout << "      \"is_seq\": "   << (m.is_seq ? "true" : "false") << ",\n";
        std::cout << "      \"output_count\": " << m.output_count    << ",\n";
        std::cout << "      \"pins\": [";
        for (size_t j = 0; j < m.pins.size(); ++j) {
            if (j > 0) std::cout << ", ";
            std::cout << "{\"name\": \"" << json_escape(m.pins[j].name)
                      << "\", \"direction\": \"" << dir_str(m.pins[j].direction) << "\"}";
        }
        std::cout << "],\n";
        std::cout << "      \"functions\": [";
        for (size_t j = 0; j < m.functions.size(); ++j) {
            if (j > 0) std::cout << ", ";
            std::cout << "{\"pin\": \"" << json_escape(m.functions[j].pin_name)
                      << "\", \"expr\": \"" << json_escape(m.functions[j].expr)
                      << "\", \"canonical\": \"" << json_escape(m.functions[j].canonical) << "\"}";
        }
        std::cout << "]\n";
        std::cout << "    }";
        if (i + 1 < db.masters.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

static void export_to_dir(const CellDb& db, const std::string& out_dir) {
    fs::create_directories(out_dir);

    {
        std::ofstream f(out_dir + "/masters.csv");
        f << "name,family,area,is_seq,output_count\n";
        for (const auto& m : db.masters)
            f << m.name << "," << m.family << "," << m.area << ","
              << (m.is_seq ? "True" : "False") << "," << m.output_count << "\n";
    }

    {
        std::ofstream f(out_dir + "/pins.csv");
        f << "master,pin_name,direction\n";
        for (const auto& m : db.masters)
            for (const auto& p : m.pins)
                f << m.name << "," << p.name << "," << dir_str(p.direction) << "\n";
    }

    {
        std::ofstream f(out_dir + "/functions.csv");
        f << "master,pin_name,expr,canonical\n";
        for (const auto& m : db.masters)
            for (const auto& fn : m.functions)
                f << m.name << "," << fn.pin_name << "," << fn.expr << "," << fn.canonical << "\n";
    }
    std::cout << "Exported " << db.masters.size() << " masters to " << out_dir << "\n";
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <cell.cdb> [--summary | --master NAME | --json | --export DIR]\n";
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    RuntimeLogger rl("cell_db_dump");

    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string path = argv[1];
    bool do_summary = false, do_json = false;
    std::string master_name, export_dir;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--summary") { do_summary = true; }
        else if (arg == "--json") { do_json = true; }
        else if (arg == "--master" && i + 1 < argc) { master_name = argv[++i]; }
        else if (arg == "--export" && i + 1 < argc) { export_dir = argv[++i]; }
        else { std::cerr << "ERROR: unknown argument: " << arg << "\n"; print_usage(argv[0]); return 1; }
    }

    CellDb db;
    {
        auto s = rl.step("read cell.cdb");
        try { db = read_cell_db(path); }
        catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; return 1; }
    }

    {
        auto s = rl.step("output");
        if (do_summary) {
            print_summary(db);
        } else if (!master_name.empty()) {
            auto it = db.master_by_name.find(master_name);
            if (it != db.master_by_name.end()) {
                print_master(*it->second);
            } else {
                std::cerr << "master '" << master_name << "' not found\n";
                return 1;
            }
        } else if (do_json) {
            print_json(db);
        } else if (!export_dir.empty()) {
            export_to_dir(db, export_dir);
        } else {
            print_all(db);
        }
    }

    rl.done();
    return 0;
}
