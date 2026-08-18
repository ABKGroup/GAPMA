cmake_minimum_required(VERSION 3.16)
project(replacement_scoring_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

find_package(OpenMP)

# canonical_to_cdl library (ported from tools/cell_generation).
# Linked into both the standalone `canonical_to_cdl` binary and
# `compare_cluster_to_single_cpp` so scoring can emit CDL in-process.
# Batch CDL generation runs in parallel (OpenMP).
add_library(canonical_to_cdl_lib STATIC src/canonical_to_cdl.cpp)
target_include_directories(canonical_to_cdl_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${REPO_ROOT}/tools/utils
)
if(OpenMP_CXX_FOUND)
  target_link_libraries(canonical_to_cdl_lib PUBLIC OpenMP::OpenMP_CXX)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(canonical_to_cdl_lib PUBLIC stdc++fs)
endif()

add_executable(canonical_to_cdl src/canonical_to_cdl_standalone.cpp)
target_link_libraries(canonical_to_cdl PRIVATE canonical_to_cdl_lib)

add_executable(extract_features_cpp src/extract_features.cpp)
target_include_directories(extract_features_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(extract_features_cpp PRIVATE canonical_to_cdl_lib)
if(OpenMP_CXX_FOUND)
  target_link_libraries(extract_features_cpp PRIVATE OpenMP::OpenMP_CXX)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  target_link_libraries(extract_features_cpp PRIVATE stdc++fs)
endif()
