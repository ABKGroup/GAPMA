

#include "netlist_db_reader.hpp"
#include "runtime_logger.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;

static std::string fmt_coord(float v) {
    if (std::isnan(v)) return "NaN";
    std::ostringstream oss;
    oss << v;
    return oss.str();
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

static void print_summary(const NetlistDb& db) {
    size_t placed = 0;
    for (const auto& inst : db.instances)
        if (!std::isnan(inst.x)) ++placed;
    std::set<std::string> masters;
    for (const auto& inst : db.instances)
        masters.insert(inst.master);

    std::cout << "top: "            << db.top_name        << "\n";
    std::cout << "instances: "      << db.instances.size() << "\n";
    std::cout << "nets: "           << db.nets.size()      << "\n";
    std::cout << "placed: "         << placed              << "\n";
    std::cout << "unique_masters: " << masters.size()      << "\n";
}

static void print_instance(const InstanceEntry& inst) {
    std::cout << "instance " << inst.id << ": " << inst.name << "\n";
    std::cout << "  master: " << inst.master << "\n";
    std::cout << "  x: " << fmt_coord(inst.x) << "  y: " << fmt_coord(inst.y) << "\n";
}

static void print_net(const NetlistNetEntry& net) {
    std::cout << "net: " << net.name << "\n";
    for (const auto& p : net.pins) {
        std::cout << "  " << (p.is_driver ? "driver" : "load")
                  << ": inst=" << p.instance_id << " pin=" << p.pin_name << "\n";
    }
}

static void print_all(const NetlistDb& db) {
    print_summary(db);
    std::cout << "\n=== instances ===\n";
    size_t limit = std::min(db.instances.size(), (size_t)50);
    for (size_t i = 0; i < limit; ++i) {
        const auto& inst = db.instances[i];
        std::cout << "  " << inst.id << ": " << inst.name
                  << " (" << inst.master << ") x=" << fmt_coord(inst.x)
                  << " y=" << fmt_coord(inst.y) << "\n";
    }
    if (db.instances.size() > 50)
        std::cout << "  ... (" << (db.instances.size() - 50) << " more)\n";

    std::cout << "\n=== nets (" << db.nets.size() << ") ===\n";
    size_t net_limit = std::min(db.nets.size(), (size_t)20);
    for (size_t i = 0; i < net_limit; ++i) {
        const auto& net = db.nets[i];
        std::cout << "  " << net.name << ": ";
        for (size_t j = 0; j < net.pins.size(); ++j) {
            if (j > 0) std::cout << ", ";
            std::cout << (net.pins[j].is_driver ? "D" : "L")
                      << ":" << net.pins[j].instance_id;
        }
        std::cout << "\n";
    }
    if (db.nets.size() > 20)
        std::cout << "  ... (" << (db.nets.size() - 20) << " more)\n";
}

static void print_json(const NetlistDb& db) {
    std::cout << "{\n";
    std::cout << "  \"top\": \""       << json_escape(db.top_name)    << "\",\n";
    std::cout << "  \"instances\": "   << db.instances.size()          << ",\n";
    std::cout << "  \"nets\": "        << db.nets.size()               << ",\n";
    std::cout << "  \"instance_list\": [\n";
    for (size_t i = 0; i < db.instances.size(); ++i) {
        const auto& inst = db.instances[i];
        std::cout << "    {\"id\": " << inst.id
                  << ", \"name\": \""   << json_escape(inst.name)   << "\""
                  << ", \"master\": \"" << json_escape(inst.master) << "\""
                  << ", \"x\": " << fmt_coord(inst.x)
                  << ", \"y\": " << fmt_coord(inst.y) << "}";
        if (i + 1 < db.instances.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"net_list\": [\n";
    for (size_t i = 0; i < db.nets.size(); ++i) {
        const auto& net = db.nets[i];
        std::cout << "    {\"name\": \"" << json_escape(net.name) << "\", \"pins\": [";
        for (size_t j = 0; j < net.pins.size(); ++j) {
            if (j > 0) std::cout << ", ";
            std::cout << "{\"instance_id\": " << net.pins[j].instance_id
                      << ", \"pin_name\": \""  << json_escape(net.pins[j].pin_name) << "\""
                      << ", \"is_driver\": "   << (net.pins[j].is_driver ? "true" : "false") << "}";
        }
        std::cout << "]}";
        if (i + 1 < db.nets.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ]\n}\n";
}

static void export_to_dir(const NetlistDb& db, const std::string& out_dir) {
    fs::create_directories(out_dir);

    {
        std::ofstream f(out_dir + "/instances.csv");
        f << "id,name,master,x,y\n";
        for (const auto& inst : db.instances)
            f << inst.id << "," << inst.name << "," << inst.master << ","
              << fmt_coord(inst.x) << "," << fmt_coord(inst.y) << "\n";
    }

    {
        std::ofstream f(out_dir + "/nets.csv");
        f << "net_name,instance_id,pin_name,is_driver\n";
        for (const auto& net : db.nets)
            for (const auto& p : net.pins)
                f << net.name << "," << p.instance_id << "," << p.pin_name << ","
                  << (p.is_driver ? "True" : "False") << "\n";
    }
    std::cout << "Exported " << db.instances.size() << " instances, "
              << db.nets.size() << " nets to " << out_dir << "\n";
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <netlist.cdb> [--summary | --instance NAME | --net NAME | --json | --export DIR]\n";
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    RuntimeLogger rl("netlist_db_dump");

    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string path = argv[1];
    bool do_summary = false, do_json = false;
    std::string inst_name, net_name, export_dir;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--summary") { do_summary = true; }
        else if (arg == "--json") { do_json = true; }
        else if (arg == "--instance" && i + 1 < argc) { inst_name = argv[++i]; }
        else if (arg == "--net"      && i + 1 < argc) { net_name  = argv[++i]; }
        else if (arg == "--export"   && i + 1 < argc) { export_dir = argv[++i]; }
        else { std::cerr << "ERROR: unknown argument: " << arg << "\n"; print_usage(argv[0]); return 1; }
    }

    NetlistDb db;
    {
        auto s = rl.step("read netlist.cdb");
        try { db = read_netlist_db(path); }
        catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; return 1; }
    }

    {
        auto s = rl.step("output");
        if (do_summary) {
            print_summary(db);
        } else if (!inst_name.empty()) {
            auto it = db.instance_by_name.find(inst_name);
            if (it != db.instance_by_name.end()) {
                print_instance(*it->second);
            } else {
                std::cerr << "instance '" << inst_name << "' not found\n";
                return 1;
            }
        } else if (!net_name.empty()) {
            auto it = db.net_by_name.find(net_name);
            if (it != db.net_by_name.end()) {
                print_net(*it->second);
            } else {
                std::cerr << "net '" << net_name << "' not found\n";
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
