cmake_minimum_required(VERSION 3.16)
project(cluster_postprocess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include roots
set(CLUSTER_DB_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../db/cluster_db/src")  #
set(MERGE_DB_INCLUDE   "${CMAKE_CURRENT_SOURCE_DIR}/../../db/merge_db/src")    #
set(NETLIST_DB_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../db/netlist_db/src")     #
set(UTILS_INCLUDE      "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils")                   #

add_library(multi_output_grouping_lib STATIC src/multi_output_grouping.cpp)
target_include_directories(multi_output_grouping_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CLUSTER_DB_INCLUDE} ${NETLIST_DB_INCLUDE} ${MERGE_DB_INCLUDE} ${UTILS_INCLUDE})
target_compile_options(multi_output_grouping_lib PRIVATE -O2 -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(multi_output_grouping_lib PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(multi_output_grouping_lib PUBLIC stdc++fs)
endif()

add_executable(merge_single_to_multi_cpp src/merge_single_to_multi.cpp)
add_executable(select_overlap_cells src/select_overlap_cells.cpp)

target_link_libraries(merge_single_to_multi_cpp PRIVATE multi_output_grouping_lib)
target_include_directories(merge_single_to_multi_cpp PRIVATE
  ${CLUSTER_DB_INCLUDE} ${NETLIST_DB_INCLUDE} ${MERGE_DB_INCLUDE} ${UTILS_INCLUDE})
target_include_directories(select_overlap_cells PRIVATE
  ${CLUSTER_DB_INCLUDE} ${UTILS_INCLUDE})

foreach(tgt merge_single_to_multi_cpp select_overlap_cells)
  target_compile_options(${tgt} PRIVATE -O2 -Wall -Wextra)
  find_package(Threads REQUIRED)
  target_link_libraries(${tgt} PRIVATE Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(${tgt} PRIVATE stdc++fs)
  endif()
endforeach()
