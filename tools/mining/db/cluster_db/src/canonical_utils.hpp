

#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

inline std::string canonical_bits(const std::string& bits, int n) {
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

inline std::string bits_to_canonical_key(const std::string& bits, int n) {
  return "0b" + canonical_bits(bits, n);
}

inline std::vector<std::string> canonical_bits_joint(
    const std::vector<std::string>& output_bits, int n) {
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

  auto vec_less = [&](const std::vector<std::string>& a,
                      const std::vector<std::string>& b) -> int {
    for (int oi = 0; oi < k; ++oi) {
      for (int idx = 0; idx < rows; ++idx) {
        if (a[oi][idx] < b[oi][idx]) return -1;
        if (a[oi][idx] > b[oi][idx]) return 1;
      }
    }
    return 0;
  };

  std::vector<std::string> best = apply_perm(remaps[0]);
  for (size_t pi = 1; pi < remaps.size(); ++pi) {
    auto candidate = apply_perm(remaps[pi]);
    if (vec_less(candidate, best) < 0) best = std::move(candidate);
  }
  return best;
}

inline std::string canonical_bits_npn(const std::string& bits, int n) {
  if (n < 0) return bits;
  const int rows = 1 << n;
  if ((int)bits.size() != rows) {
    throw std::runtime_error("canonical_bits_npn: bits.size()=" +
                             std::to_string(bits.size()) +
                             " but expected 2^n=" + std::to_string(rows) +
                             " for n=" + std::to_string(n));
  }
  if (n == 0) {

    char b0 = bits[0];
    char neg = (b0 == '0') ? '1' : '0';
    return std::string(1, std::max(b0, neg));
  }

  std::vector<int> perm(n);
  for (int i = 0; i < n; ++i) perm[i] = i;

  std::string best;
  bool best_init = false;

  do {

    for (int neg_mask = 0; neg_mask < rows; ++neg_mask) {

      std::string base(rows, '0');
      for (int idx = 0; idx < rows; ++idx) {
        int src = 0;
        for (int i = 0; i < n; ++i) {
          int xi = (idx >> (n - 1 - i)) & 1;
          int neg_i = (neg_mask >> (n - 1 - i)) & 1;
          int yi = xi ^ neg_i;

          src |= yi << (n - 1 - perm[i]);
        }
        base[idx] = bits[src];
      }

      if (!best_init || base > best) { best = base; best_init = true; }
      std::string flipped(rows, '0');
      for (int idx = 0; idx < rows; ++idx)
        flipped[idx] = (base[idx] == '0') ? '1' : '0';
      if (flipped > best) best = flipped;
    }
  } while (std::next_permutation(perm.begin(), perm.end()));

  return best;
}

inline std::string bits_to_npn_canonical_key(const std::string& bits, int n) {
  return "0b" + canonical_bits_npn(bits, n);
}

inline std::string canonical_bits_np(const std::string& bits, int n) {
  if (n < 0) return bits;
  const int rows = 1 << n;
  if ((int)bits.size() != rows) {
    throw std::runtime_error("canonical_bits_np: bits.size()=" +
                             std::to_string(bits.size()) +
                             " but expected 2^n=" + std::to_string(rows) +
                             " for n=" + std::to_string(n));
  }
  if (n == 0) return bits;

  std::vector<int> perm(n);
  for (int i = 0; i < n; ++i) perm[i] = i;

  std::string best;
  bool best_init = false;

  do {
    for (int neg_mask = 0; neg_mask < rows; ++neg_mask) {
      std::string base(rows, '0');
      for (int idx = 0; idx < rows; ++idx) {
        int src = 0;
        for (int i = 0; i < n; ++i) {
          int xi = (idx >> (n - 1 - i)) & 1;
          int neg_i = (neg_mask >> (n - 1 - i)) & 1;
          int yi = xi ^ neg_i;
          src |= yi << (n - 1 - perm[i]);
        }
        base[idx] = bits[src];
      }
      if (!best_init || base > best) { best = base; best_init = true; }
    }
  } while (std::next_permutation(perm.begin(), perm.end()));

  return best;
}

inline std::string bits_to_np_canonical_key(const std::string& bits, int n) {
  return "0b" + canonical_bits_np(bits, n);
}

inline std::string canonical_bits_npn_no_full_neg(const std::string& bits, int n) {
  if (n < 0) return bits;
  const int rows = 1 << n;
  if ((int)bits.size() != rows) {
    throw std::runtime_error("canonical_bits_npn_no_full_neg: bits.size()=" +
                             std::to_string(bits.size()) +
                             " but expected 2^n=" + std::to_string(rows) +
                             " for n=" + std::to_string(n));
  }
  if (n == 0) {
    char b0 = bits[0];
    char neg = (b0 == '0') ? '1' : '0';
    return std::string(1, std::max(b0, neg));
  }

  const int full_neg_mask = rows - 1;

  std::vector<int> perm(n);
  for (int i = 0; i < n; ++i) perm[i] = i;

  std::string best;
  bool best_init = false;

  do {
    for (int neg_mask = 0; neg_mask < rows; ++neg_mask) {
      if (neg_mask == full_neg_mask) continue;
      std::string base(rows, '0');
      for (int idx = 0; idx < rows; ++idx) {
        int src = 0;
        for (int i = 0; i < n; ++i) {
          int xi = (idx >> (n - 1 - i)) & 1;
          int neg_i = (neg_mask >> (n - 1 - i)) & 1;
          int yi = xi ^ neg_i;
          src |= yi << (n - 1 - perm[i]);
        }
        base[idx] = bits[src];
      }
      if (!best_init || base > best) { best = base; best_init = true; }
      std::string flipped(rows, '0');
      for (int idx = 0; idx < rows; ++idx)
        flipped[idx] = (base[idx] == '0') ? '1' : '0';
      if (flipped > best) best = flipped;
    }
  } while (std::next_permutation(perm.begin(), perm.end()));

  if (!best_init) return bits;
  return best;
}
