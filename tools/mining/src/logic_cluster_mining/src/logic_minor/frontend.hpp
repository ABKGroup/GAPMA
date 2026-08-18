#pragma once

#include "logic_minor/types.hpp"

void load_graph_from_cdbs(const Cfg& cfg, const std::string& cell_db_path, const std::string& netlist_db_path, Graph& g);
void split_multi_output_cells(Graph& g);
