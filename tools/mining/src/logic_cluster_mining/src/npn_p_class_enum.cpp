

#include "logic_minor/canonical.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--bits") {
    std::string bits = argv[2];
    if (bits.rfind("0b", 0) == 0) bits = bits.substr(2);
    if (bits.empty() || (bits.size() & (bits.size() - 1)) != 0 ||
        bits.find_first_not_of("01") != std::string::npos) {
      std::cerr << "ERROR: --bits requires a nonempty binary truth table with power-of-two length\n";
      return 2;
    }
    int n = 0;
    for (size_t rows = bits.size(); rows > 1; rows >>= 1) ++n;
    auto reps = npn_canonical_infos(bits, n);
    if (reps.empty()) {
      std::cerr << "ERROR: npn_canonical_infos returned no PM-NPN representative\n";
      return 1;
    }
    const auto& r = reps.front();
    std::cout << "np_canonical_bits,erased_input_inv,erased_output_inv,erased_total_inv,exact\n";
    std::cout << r.bits << "," << r.erased_input_inverters << ","
              << r.erased_output_inverters << "," << r.erased_total_inverters
              << "," << (r.exact ? 1 : 0) << "\n";
    return 0;
  }
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <n> | --bits <binary_truth_table>\n";
    return 2;
  }
  const int n = std::atoi(argv[1]);
  if (n < 1 || n > 4) {
    std::cerr << "ERROR: n must be in [1,4] (got " << n << ")\n";
    return 2;
  }
  const int rows = 1 << n;
  if (rows >= 31) {
    std::cerr << "ERROR: n too large for uint32 enumeration\n";
    return 2;
  }
  const std::uint64_t total = std::uint64_t(1) << rows;

  std::map<std::string, std::string> p_to_input;
  for (std::uint64_t f = 0; f < total; ++f) {
    std::string bits(rows, '0');
    for (int i = 0; i < rows; ++i)
      bits[i] = ((f >> i) & 1ULL) ? '1' : '0';
    std::string p = canonical_bits(bits, n);
    if (p_to_input.find(p) == p_to_input.end()) p_to_input[p] = p;
  }

  std::cout << "p_canonical_bits,np_canonical_bits,erased_input_inv,"
               "erased_output_inv,erased_total_inv,exact,p_expr,np_expr\n";
  std::uint64_t p_classes = 0;
  std::uint64_t emitted_rows = 0;
  for (const auto& [p, raw] : p_to_input) {
    auto reps = npn_canonical_infos(raw, n);
    if (reps.empty()) {
      throw std::runtime_error("npn_canonical_infos returned empty for p=" + p);
    }
    std::string p_expr = expr_from_canon("0b" + p);
    for (const auto& r : reps) {
      std::string np_expr = expr_from_canon("0b" + r.bits);
      std::cout << p << "," << r.bits << "," << r.erased_input_inverters
                << "," << r.erased_output_inverters << ","
                << r.erased_total_inverters << "," << (r.exact ? 1 : 0)
                << ",\"" << p_expr << "\",\"" << np_expr << "\"\n";
      ++emitted_rows;
    }
    ++p_classes;
  }
  std::cerr << "[info] n=" << n << " total_truth_tables=" << total
            << " distinct_p_classes=" << p_classes
            << " emitted_rows=" << emitted_rows << "\n";
  return 0;
}
