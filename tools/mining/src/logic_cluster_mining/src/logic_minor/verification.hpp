#pragma once

#include "logic_minor/types.hpp"

#include <string>

struct VerificationSummary {
  int included_entries = 0;
  int excluded_entries = 0;
  int branch_entries = 0;
  int errors = 0;
};

VerificationSummary verify_mining_logs(const Cfg& cfg);
