cmake_minimum_required(VERSION 3.16)
project(logic_func_minor_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(logic_func_minor_cpp
  src/main.cpp
  src/logic_minor/util.cpp
  src/logic_minor/frontend.cpp
  src/logic_minor/graph_prep.cpp
  src/logic_minor/pattern.cpp
  src/logic_minor/canonical.cpp
  src/logic_minor/eval.cpp
  src/logic_minor/mining.cpp
  src/logic_minor/reporting.cpp
  src/logic_minor/verification.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../cluster_postprocess/src/multi_output_grouping.cpp
)

target_include_directories(logic_func_minor_cpp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../cluster_postprocess/src     # multi_output_grouping.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../db/cluster_db/src        #
  ${CMAKE_CURRENT_SOURCE_DIR}/../../db/merge_db/src          #
  ${CMAKE_CURRENT_SOURCE_DIR}/../../db/cell_db/src            #
  ${CMAKE_CURRENT_SOURCE_DIR}/../../db/netlist_db/src         #
)

target_link_libraries(logic_func_minor_cpp PRIVATE stdc++fs)
find_package(Threads REQUIRED)
target_link_libraries(logic_func_minor_cpp PRIVATE Threads::Threads)

add_executable(npn_p_class_enum
  src/npn_p_class_enum.cpp
  src/logic_minor/canonical.cpp
  src/logic_minor/util.cpp
)
target_include_directories(npn_p_class_enum PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

