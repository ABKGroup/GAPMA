#pragma once

#include "logic_minor/types.hpp"

#include <string>
#include <vector>

bool is_buffer(const Cell* c);
bool is_inv(const Cell* c);

Pattern init_pattern(Cell* root);
Pattern add_pred(Pattern p, Cell* pred);

std::vector<Pin*> boundary_inputs(const Pattern& p);
int input_count(const Pattern& p);
std::vector<std::string> collect_boundary_input_keys(const Pattern& p);

bool has_no_external_output(const Pattern& p);
bool has_external_output_exceeding_budget(const Pattern& p, const Opt& opt);
int boundary_output_count(const Pattern& p, bool ignore_nonroot_outputs = false);
