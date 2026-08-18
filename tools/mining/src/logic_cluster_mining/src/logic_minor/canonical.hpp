#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

std::string canonical_bits(const std::string& bits, int n);
struct NpnCanonical {
  std::string bits;
  int erased_input_inverters = 0;
  int erased_output_inverters = 0;
  int erased_total_inverters = 0;
  bool exact = true;
};

std::vector<NpnCanonical> npn_canonical_infos(const std::string& bits, int n);
std::string bits_to_key(const std::string& bits);
std::string key_to_binary(const std::string& k);
std::string vec_to_canon(const std::string& vec_key);
std::string canonical_set_signature(const std::unordered_set<std::string>& canonicals);
std::string signature_to_binary(const std::string& signature);
std::string exprs_from_signature(const std::string& signature);

std::vector<std::string> canonical_bits_joint(const std::vector<std::string>& output_bits, int n);

std::string expr_from_canon(const std::string& k);
std::string canonical_from_expr(const std::string& expr, std::vector<std::string> ins);
std::vector<std::string> detect_expr_inputs(const std::string& expr);
std::string normalize_target_canonical(const std::string& raw);
std::optional<int> input_count_from_canonical(const std::string& canonical);
