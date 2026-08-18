#include "canonical.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

class Expr {
 public:
  explicit Expr(std::string s) : s_(std::move(s)) {}
  bool eval(const std::function<bool(const std::string&)>& lookup) {
    i_ = 0; lookup_ = lookup; next(); return parse_or();
  }
 private:
  enum T { ID, NOT, AND, OR, XOR, LP, RP, END } t_ = END;
  std::string tok_, s_;
  size_t i_ = 0;
  std::function<bool(const std::string&)> lookup_;
  void next() {
    while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
    if (i_ >= s_.size()) { t_ = END; tok_.clear(); return; }
    char c = s_[i_];
    if (c == '!') { ++i_; t_ = NOT; tok_ = "!"; return; }
    if (c == '&' || c == '*') { ++i_; if (c == '&' && i_ < s_.size() && s_[i_] == '&') ++i_; t_ = AND; tok_ = "&"; return; }
    if (c == '|' || c == '+') { ++i_; if (c == '|' && i_ < s_.size() && s_[i_] == '|') ++i_; t_ = OR; tok_ = "|"; return; }
    if (c == '^') { ++i_; t_ = XOR; tok_ = "^"; return; }
    if (c == '(') { ++i_; t_ = LP; tok_ = "("; return; }
    if (c == ')') { ++i_; t_ = RP; tok_ = ")"; return; }
    if (std::isalnum((unsigned char)c) || c == '_' || c == '/' || c == '$') {
      size_t b = i_;
      while (i_ < s_.size()) { char d = s_[i_]; if (std::isalnum((unsigned char)d) || d == '_' || d == '/' || d == '$') ++i_; else break; }
      tok_ = s_.substr(b, i_ - b);
      if (i_ < s_.size() && s_[i_] == '\'') { ++i_; t_ = ID; tok_ = "!" + tok_; } else { t_ = ID; }
      return;
    }
    ++i_; next();
  }

  bool parse_or() { bool v = parse_xor(); while (t_ == OR) { next(); bool r = parse_xor(); v = v | r; } return v; }
  bool parse_xor() { bool v = parse_and(); while (t_ == XOR) { next(); bool r = parse_and(); v = (v != r); } return v; }
  bool parse_and() { bool v = parse_unary(); while (t_ == AND) { next(); bool r = parse_unary(); v = v & r; } return v; }
  bool parse_unary() {
    if (t_ == NOT) { next(); return !parse_unary(); }
    if (t_ == LP) { next(); bool v = parse_or(); if (t_ == RP) next(); return v; }
    if (t_ == ID) {
      auto id = tok_; next();
      if (id.rfind("!", 0) == 0) return !lookup_(id.substr(1));
      auto u = upper(id);
      if (u == "1" || u == "TRUE") return true;
      if (u == "0" || u == "FALSE") return false;
      return lookup_(id);
    }
    return false;
  }
};

const char kHexDigits[] = "0123456789ABCDEF";

int hex_digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::vector<std::string> split_canonical_signature(const std::string& signature) {
  std::vector<std::string> parts;
  std::stringstream ss(signature);
  std::string tok;
  while (std::getline(ss, tok, ';')) {
    auto t = trim(tok);
    if (!t.empty()) parts.push_back(t);
  }
  return parts;
}

}

std::string canonical_bits(const std::string& bits, int n) {
  if (n <= 1) return bits;
  const int rows = 1 << n;

  std::vector<int> perm(n);
  for (int i = 0; i < n; ++i) perm[i] = i;

  std::vector<std::vector<int>> remaps;
  { int nfact = 1; for (int i = 2; i <= n; ++i) nfact *= i; remaps.reserve(nfact); }
  do {
    std::vector<int> remap(rows);
    for (int idx = 0; idx < rows; ++idx) {
      int bi = 0;
      for (int i = 0; i < n; ++i)
        bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i]);
      remap[idx] = bi;
    }
    remaps.push_back(std::move(remap));
  } while (std::next_permutation(perm.begin(), perm.end()));

  std::sort(remaps.begin(), remaps.end(), [&](const std::vector<int>& a, const std::vector<int>& b) {
    const int limit = std::min(rows, 8);
    for (int i = 0; i < limit; ++i) {
      char ca = bits[a[i]], cb = bits[b[i]];
      if (ca != cb) return ca < cb;
    }
    return false;
  });

  std::string best(rows, '0');
  for (int idx = 0; idx < rows; ++idx) best[idx] = bits[remaps[0][idx]];

  for (size_t pi = 1; pi < remaps.size(); ++pi) {
    const auto& remap = remaps[pi];
    if (bits[remap[0]] > best[0]) continue;
    for (int idx = 0; idx < rows; ++idx) {
      char c = bits[remap[idx]], b = best[idx];
      if (c > b) break;
      if (c < b) { best[idx] = c; for (int j = idx + 1; j < rows; ++j) best[j] = bits[remap[j]]; break; }
    }
  }
  return best;
}

std::string bits_to_key(const std::string& bits) {
  const size_t nbits = bits.size();
  const size_t width = (nbits + 3) / 4;
  std::ostringstream oss;
  oss << nbits << "#0x";
  if (width == 0) { oss << '0'; return oss.str(); }
  std::vector<unsigned char> nibbles(width, 0);
  for (size_t i = 0; i < nbits; ++i)
    if (bits[nbits - 1 - i] == '1')
      nibbles[width - 1 - i / 4] |= static_cast<unsigned char>(1u << (i % 4));
  for (size_t i = 0; i < width; ++i) oss << kHexDigits[nibbles[i]];
  return oss.str();
}

std::string key_to_binary(const std::string& k) {
  if (k.rfind("0b", 0) == 0) return k;
  auto p = k.find("#0x");
  if (p == std::string::npos) return "";
  int n = std::stoi(k.substr(0, p));
  if (n < 0)
    throw std::runtime_error("key_to_binary: negative bit count in key: '" + k + "'");
  auto h = k.substr(p + 3);
  if (h.empty())
    throw std::runtime_error("key_to_binary: canonical key has no hex digits: '" + k + "'");
  std::string b;
  b.reserve(h.size() * 4);
  for (char c : h) {
    const int d = hex_digit_value(c);
    if (d < 0)
      throw std::runtime_error("key_to_binary: invalid hex digit '" + std::string(1, c) +
                               "' in key: '" + k + "'");
    for (int s = 3; s >= 0; --s) b.push_back(((d >> s) & 1) ? '1' : '0');
  }
  const size_t want = static_cast<size_t>(n);
  if (b.size() < want) return "0b" + std::string(want - b.size(), '0') + b;
  const size_t extra = b.size() - want;
  if (b.substr(0, extra).find('1') != std::string::npos)
    throw std::runtime_error("key_to_binary: hex value does not fit in the declared bit count: '" +
                             k + "'");
  return "0b" + b.substr(extra);
}

std::string vec_to_canon(const std::string& vec_key) {
  auto p = vec_key.find('|');
  if (p == std::string::npos) return "0#0x0";
  int n = std::stoi(vec_key.substr(0, p));
  auto bits = vec_key.substr(p + 1);
  return bits_to_key(canonical_bits(bits, n));
}

std::string canonical_set_signature(const std::unordered_set<std::string>& canonicals) {
  if (canonicals.empty()) return "";
  std::vector<std::string> ordered(canonicals.begin(), canonicals.end());
  std::sort(ordered.begin(), ordered.end());
  std::ostringstream oss;
  for (size_t i = 0; i < ordered.size(); ++i) { if (i) oss << ";"; oss << ordered[i]; }
  return oss.str();
}

std::string signature_to_binary(const std::string& signature) {

  if (signature.rfind("0b", 0) == 0) return signature;

  auto parts = split_canonical_signature(signature);
  std::ostringstream oss;
  for (size_t i = 0; i < parts.size(); ++i) { if (i) oss << " | "; oss << key_to_binary(parts[i]); }
  return oss.str();
}

std::string exprs_from_signature(const std::string& signature) {
  auto parts = split_canonical_signature(signature);
  std::ostringstream oss;
  for (size_t i = 0; i < parts.size(); ++i) { if (i) oss << " | "; oss << expr_from_canon(parts[i]); }
  return oss.str();
}

std::vector<std::string> canonical_bits_joint(const std::vector<std::string>& output_bits, int n) {
  const int k = static_cast<int>(output_bits.size());
  if (k == 0) return {};
  if (k == 1) return {canonical_bits(output_bits[0], n)};
  if (n <= 0) return output_bits;

  const int rows = 1 << n;

  std::vector<int> perm(n);
  for (int i = 0; i < n; ++i) perm[i] = i;

  std::vector<std::vector<int>> remaps;
  { int nfact = 1; for (int i = 2; i <= n; ++i) nfact *= i; remaps.reserve(nfact); }
  do {
    std::vector<int> remap(rows);
    for (int idx = 0; idx < rows; ++idx) {
      int bi = 0;
      for (int i = 0; i < n; ++i)
        bi |= ((idx >> (n - 1 - i)) & 1) << (n - 1 - perm[i]);
      remap[idx] = bi;
    }
    remaps.push_back(std::move(remap));
  } while (std::next_permutation(perm.begin(), perm.end()));

  auto apply_perm = [&](const std::vector<int>& remap) -> std::vector<std::string> {
    std::vector<std::string> result(k);
    for (int oi = 0; oi < k; ++oi) {
      result[oi].resize(rows);
      for (int idx = 0; idx < rows; ++idx)
        result[oi][idx] = output_bits[oi][remap[idx]];
    }
    std::sort(result.begin(), result.end());
    return result;
  };

  auto vec_less = [&](const std::vector<std::string>& a, const std::vector<std::string>& b) -> bool {
    for (int oi = 0; oi < k; ++oi) {
      for (int idx = 0; idx < rows; ++idx) {
        if (a[oi][idx] < b[oi][idx]) return true;
        if (a[oi][idx] > b[oi][idx]) return false;
      }
    }
    return false;
  };

  std::vector<std::string> best = apply_perm(remaps[0]);
  for (size_t pi = 1; pi < remaps.size(); ++pi) {
    auto candidate = apply_perm(remaps[pi]);
    if (vec_less(candidate, best)) best = std::move(candidate);
  }
  return best;
}

std::string expr_from_canon(const std::string& k) {
  auto b = key_to_binary(k);
  if (b.rfind("0b", 0) != 0) return "";
  auto bits = b.substr(2);
  int rows = (int)bits.size();
  int n = 0;
  while ((1 << n) < rows) ++n;
  std::vector<std::string> terms;
  for (int r = 0; r < rows; ++r) {
    if (bits[r] != '1') continue;
    std::vector<std::string> lits;
    for (int i = 0; i < n; ++i) {
      bool bv = ((r >> (n - 1 - i)) & 1) != 0;
      std::string v = "A" + std::to_string(i + 1);
      lits.push_back(bv ? v : ("!" + v));
    }
    if (lits.empty()) terms.push_back("1");
    else { std::ostringstream t; for (size_t i = 0; i < lits.size(); ++i) { if (i) t << "*"; t << lits[i]; } terms.push_back(t.str()); }
  }
  if (terms.empty()) return "0";
  std::ostringstream out;
  for (size_t i = 0; i < terms.size(); ++i) { if (i) out << " + "; out << terms[i]; }
  return out.str();
}

std::string canonical_from_expr(const std::string& expr, std::vector<std::string> ins) {
  std::sort(ins.begin(), ins.end());
  int n = (int)ins.size();
  if (n == 0) {
    Expr ex(expr);
    bool v = ex.eval([&](const std::string&) { return false; });
    return v ? "1#0x1" : "1#0x0";
  }
  std::string bits;
  for (int a = 0; a < (1 << n); ++a) {
    std::unordered_map<std::string, bool> as;
    for (int i = 0; i < n; ++i) as[ins[i]] = (((a >> (n - 1 - i)) & 1) != 0);
    Expr ex(expr);
    bool v = ex.eval([&](const std::string& t) { auto it = as.find(t); return it != as.end() ? it->second : false; });
    bits.push_back(v ? '1' : '0');
  }
  return bits_to_key(canonical_bits(bits, n));
}

std::vector<std::string> detect_expr_inputs(const std::string& expr) {
  std::set<std::string> vars;
  for (size_t i = 0; i < expr.size();) {
    if (!(std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '/' || expr[i] == '$')) { ++i; continue; }
    size_t b = i++;
    while (i < expr.size()) { char c = expr[i]; if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '$')) break; ++i; }
    auto t = expr.substr(b, i - b);
    auto u = upper(t);
    if (!(u == "TRUE" || u == "FALSE")) vars.insert(t);
  }
  return std::vector<std::string>(vars.begin(), vars.end());
}

std::string normalize_target_canonical(const std::string& raw) {
  auto s = trim(raw);
  auto hash = s.find('#');
  if (hash == std::string::npos) throw std::runtime_error("target_canonical must match '<bit_count>#0x<hex>'");
  std::string lhs = trim(s.substr(0, hash));
  std::string rhs = trim(s.substr(hash + 1));
  if (lhs.empty() || rhs.size() < 3 || !(rhs[0] == '0' && (rhs[1] == 'x' || rhs[1] == 'X')))
    throw std::runtime_error("target_canonical must match '<bit_count>#0x<hex>'");
  int bit_count = std::stoi(lhs);
  if (bit_count <= 0) throw std::runtime_error("target_canonical bit_count must be > 0");
  const std::string hex = rhs.substr(2);
  if (hex.empty()) throw std::runtime_error("target_canonical hex parse failed");
  std::string digits;
  digits.reserve(hex.size());
  for (char c : hex) {
    const int d = hex_digit_value(c);
    if (d < 0) throw std::runtime_error("target_canonical hex parse failed");
    digits.push_back(kHexDigits[d]);
  }
  const size_t first = digits.find_first_not_of('0');
  digits = (first == std::string::npos) ? std::string("0") : digits.substr(first);
  const size_t width = static_cast<size_t>((bit_count + 3) / 4);
  std::ostringstream oss;
  oss << bit_count << "#0x";
  if (digits.size() < width) oss << std::string(width - digits.size(), '0');
  oss << digits;
  return oss.str();
}

std::optional<int> input_count_from_canonical(const std::string& canonical) {
  auto s = trim(canonical);
  auto hash = s.find('#');
  if (hash == std::string::npos) return std::nullopt;
  int bit_count = 0;
  try { bit_count = std::stoi(trim(s.substr(0, hash))); }
  catch (const std::exception& e) {
    throw std::runtime_error("canonical bit_count parse failed: '" + s + "' (" + e.what() + ")");
  }
  if (bit_count <= 0) return std::nullopt;
  if ((bit_count & (bit_count - 1)) != 0) return std::nullopt;
  int n = 0;
  while ((1 << n) < bit_count) ++n;
  return n;
}
