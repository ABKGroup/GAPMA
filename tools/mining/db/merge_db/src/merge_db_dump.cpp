

#include "merge_db_reader.hpp"
#include "runtime_logger.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <merge.cdb> [--summary | --json]\n";
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }
    RuntimeLogger rl("merge_db_dump");

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string path = argv[1];
    bool do_summary = false;
    bool do_json = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--summary") do_summary = true;
        else if (arg == "--json") do_json = true;
        else {
            std::cerr << "ERROR: unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    MergeDb db;
    {
        auto s = rl.step("read merge.cdb");
        try {
            db = read_merge_db(path);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }
    }

    {
        auto s = rl.step("output");

        if (do_summary) {
            int n2 = 0, n3 = 0, nother = 0;
            for (const auto& m : db.merges) {
                int n = static_cast<int>(m.outputs.size());
                if (n == 2) ++n2;
                else if (n == 3) ++n3;
                else ++nother;
            }
            std::cout << "merge entries: " << db.merges.size() << "\n";
            std::cout << "  2-output: " << n2 << "\n";
            std::cout << "  3-output: " << n3 << "\n";
            if (nother) std::cout << "  other: " << nother << "\n";

        } else if (do_json) {
            std::cout << "[\n";
            for (size_t i = 0; i < db.merges.size(); ++i) {
                const auto& m = db.merges[i];
                std::cout << "  {";
                std::cout << "\"canonical\": \"" << m.canonical << "\", ";

                std::cout << "\"cluster_ids\": [";
                for (size_t j = 0; j < m.cluster_ids.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << m.cluster_ids[j];
                }
                std::cout << "], ";

                std::cout << "\"shared_inputs\": [";
                for (size_t j = 0; j < m.shared_inputs.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << "\"" << m.shared_inputs[j] << "\"";
                }
                std::cout << "], ";

                std::cout << "\"outputs\": [";
                for (size_t j = 0; j < m.outputs.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << "\"" << m.outputs[j] << "\"";
                }
                std::cout << "]}";

                if (i + 1 < db.merges.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "]\n";

        } else {
            for (size_t i = 0; i < db.merges.size(); ++i) {
                const auto& m = db.merges[i];
                std::cout << "merge " << i << ": canonical=" << m.canonical << "\n";

                std::cout << "  cluster_ids: [";
                for (size_t j = 0; j < m.cluster_ids.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << m.cluster_ids[j];
                }
                std::cout << "]\n";

                std::cout << "  shared_inputs: [";
                for (size_t j = 0; j < m.shared_inputs.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << m.shared_inputs[j];
                }
                std::cout << "]\n";

                std::cout << "  outputs: [";
                for (size_t j = 0; j < m.outputs.size(); ++j) {
                    if (j > 0) std::cout << ", ";
                    std::cout << m.outputs[j];
                }
                std::cout << "]\n";
            }
        }
    }

    rl.done();
    return 0;
}
