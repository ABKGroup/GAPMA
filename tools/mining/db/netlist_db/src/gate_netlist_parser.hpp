#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace gate_parser {

struct Pin {
  std::string port;
  std::string net;
};

struct Instance {
  std::string name;
  std::string master;
  std::vector<Pin> pins;
};

enum class PortDir { INPUT, OUTPUT, INOUT };

struct PortDecl {
  std::string name;
  PortDir dir;
  int width = 1;
  int msb = 0, lsb = 0;
};

struct ParsedNetlist {
  std::string top_name;
  std::vector<PortDecl> ports;
  std::vector<Instance> instances;

  std::unordered_map<std::string, int> wire_widths;
};

ParsedNetlist parse(const std::string& filepath, const std::string& top_name = "");

}
