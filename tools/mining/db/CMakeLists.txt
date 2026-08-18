cmake_minimum_required(VERSION 3.16)
project(design_data_compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Common include directories ──────────────────────────────────────────────
set(SHARED_INCLUDE  ${CMAKE_CURRENT_SOURCE_DIR}/shared)
set(UTILS_INCLUDE   ${CMAKE_CURRENT_SOURCE_DIR}/../../utils)
set(CELL_DB_INCLUDE    ${CMAKE_CURRENT_SOURCE_DIR}/cell_db/src)
set(NETLIST_DB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/netlist_db/src)
set(CLUSTER_DB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/cluster_db/src)
set(MERGE_DB_INCLUDE   ${CMAKE_CURRENT_SOURCE_DIR}/merge_db/src)

find_package(Threads REQUIRED)

# ─── Dump tools (no external deps) ──────────────────────────────────────────
add_executable(cell_db_dump cell_db/src/cell_db_dump.cpp)
target_include_directories(cell_db_dump PRIVATE ${CELL_DB_INCLUDE} ${UTILS_INCLUDE})

add_executable(netlist_db_dump netlist_db/src/netlist_db_dump.cpp)
target_include_directories(netlist_db_dump PRIVATE ${NETLIST_DB_INCLUDE} ${UTILS_INCLUDE})

add_executable(cluster_db_dump cluster_db/src/cluster_db_dump.cpp)
target_include_directories(cluster_db_dump PRIVATE ${CLUSTER_DB_INCLUDE} ${UTILS_INCLUDE})

include(CTest)
if(BUILD_TESTING)
  add_test(NAME cluster_db_dump_missing_input
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cluster_db_dump_missing_input.sh $<TARGET_FILE:cluster_db_dump>)
endif()

add_executable(merge_db_dump merge_db/src/merge_db_dump.cpp)
target_include_directories(merge_db_dump PRIVATE ${MERGE_DB_INCLUDE} ${UTILS_INCLUDE})

foreach(tgt cell_db_dump netlist_db_dump cluster_db_dump merge_db_dump)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(${tgt} PRIVATE stdc++fs)
  endif()
endforeach()

# ─── Optional: lib2cdb and design2cdb (require Yosys) ───────────────────────
option(DDC_USE_YOSYS "Build lib2cdb and design2cdb (requires Yosys)" OFF)
set(YOSYS_ROOT "" CACHE PATH "Path to Yosys source root")
set(YOSYS_TCL_INCLUDE "" CACHE PATH "Path containing tcl.h")

option(DDC_USE_OPENDB "Enable OpenDB for LEF parsing in lib2cdb" OFF)
set(OPENDB_SRC_ROOT "" CACHE PATH "Path to OpenDB source root")
set(OPENDB_BUILD_ROOT "" CACHE PATH "Path to prebuilt OpenDB build root")

if(DDC_USE_YOSYS)
  if(NOT EXISTS "${YOSYS_ROOT}/Makefile")
    message(FATAL_ERROR "YOSYS_ROOT not found: ${YOSYS_ROOT}")
  endif()

  find_package(Boost REQUIRED)
  find_package(OpenMP)
  find_library(TCL86_LIBRARY NAMES tcl8.6 tcl)

  set(YOSYS_COMPILE_DEFS
    LOGIC_MINOR_USE_YOSYS=1
    _YOSYS_ YOSYS_VER=\"0.62\" YOSYS_MAJOR=0 YOSYS_MINOR=62 YOSYS_COMMIT=\"0.62\"
    YOSYS_ENABLE_READLINE YOSYS_ENABLE_PLUGINS YOSYS_ENABLE_GLOB
    YOSYS_ENABLE_ZLIB YOSYS_ENABLE_TCL YOSYS_ENABLE_THREADS YOSYS_ENABLE_ABC
  )

  # Build libyosys.so if not already built.
  add_custom_command(
    OUTPUT "${YOSYS_ROOT}/libyosys.so"
    COMMAND ${CMAKE_MAKE_PROGRAM} -C "${YOSYS_ROOT}" -j8
            ENABLE_LIBYOSYS=1
            TCL_INCLUDE=${YOSYS_TCL_INCLUDE}
            libyosys.so
    WORKING_DIRECTORY "${YOSYS_ROOT}"
    COMMENT "Building libyosys.so"
    VERBATIM
  )
  add_custom_target(yosys_lib DEPENDS "${YOSYS_ROOT}/libyosys.so")
  add_library(yosys_shared SHARED IMPORTED GLOBAL)
  set_target_properties(yosys_shared PROPERTIES IMPORTED_LOCATION "${YOSYS_ROOT}/libyosys.so")
  add_dependencies(yosys_shared yosys_lib)

  set(COMMON_INCLUDE_DIRS
    ${SHARED_INCLUDE} ${UTILS_INCLUDE} ${CELL_DB_INCLUDE} ${NETLIST_DB_INCLUDE}
    ${Boost_INCLUDE_DIRS} "${YOSYS_ROOT}" "${YOSYS_TCL_INCLUDE}"
  )

  # ── lib2cdb ──
  add_executable(lib2cdb
    cell_db/src/lib2cdb.cpp
    shared/canonical.cpp
    shared/eval.cpp
    shared/pattern.cpp
    shared/util.cpp
  )
  target_include_directories(lib2cdb PRIVATE ${COMMON_INCLUDE_DIRS})
  target_compile_definitions(lib2cdb PRIVATE ${YOSYS_COMPILE_DEFS})
  target_compile_options(lib2cdb PRIVATE -fpermissive)
  add_dependencies(lib2cdb yosys_lib)
  target_link_libraries(lib2cdb PRIVATE yosys_shared dl stdc++fs Threads::Threads)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(lib2cdb PRIVATE OpenMP::OpenMP_CXX)
  endif()
  if(TCL86_LIBRARY)
    target_link_libraries(lib2cdb PRIVATE "${TCL86_LIBRARY}")
  endif()
  set_target_properties(lib2cdb PROPERTIES BUILD_RPATH "$ORIGIN")
  add_custom_command(TARGET lib2cdb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YOSYS_ROOT}/libyosys.so" "${CMAKE_BINARY_DIR}/libyosys.so"
    COMMENT "Copying libyosys.so to build directory"
  )

  # ── design2cdb ──
  add_executable(design2cdb
    netlist_db/src/design2cdb.cpp
    netlist_db/src/gate_netlist_parser.cpp
    shared/util.cpp
  )
  target_include_directories(design2cdb PRIVATE ${COMMON_INCLUDE_DIRS})
  target_compile_definitions(design2cdb PRIVATE ${YOSYS_COMPILE_DEFS})
  add_dependencies(design2cdb yosys_lib)
  target_link_libraries(design2cdb PRIVATE yosys_shared dl stdc++fs Threads::Threads)
  if(OpenMP_CXX_FOUND)
    target_link_libraries(design2cdb PRIVATE OpenMP::OpenMP_CXX)
  endif()
  if(TCL86_LIBRARY)
    target_link_libraries(design2cdb PRIVATE "${TCL86_LIBRARY}")
  endif()
  set_target_properties(design2cdb PROPERTIES BUILD_RPATH "$ORIGIN")
  add_custom_command(TARGET design2cdb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YOSYS_ROOT}/libyosys.so" "${CMAKE_BINARY_DIR}/libyosys.so"
    COMMENT "Copying libyosys.so to build directory"
  )

  # ── OpenDB support ──
  if(DDC_USE_OPENDB)
    set(OPENDB_MAIN_LIB "${OPENDB_BUILD_ROOT}/src/db/libopendb.so")
    set(OPENDB_LEFIN_LIB "${OPENDB_BUILD_ROOT}/src/lefin/liblefin.so")
    set(OPENDB_DEFIN_LIB "${OPENDB_BUILD_ROOT}/src/defin/libdefin.so")

    if(EXISTS "${OPENDB_SRC_ROOT}/include/opendb/db.h" AND
       EXISTS "${OPENDB_MAIN_LIB}" AND EXISTS "${OPENDB_LEFIN_LIB}" AND EXISTS "${OPENDB_DEFIN_LIB}")
      add_library(opendb_shared SHARED IMPORTED GLOBAL)
      set_target_properties(opendb_shared PROPERTIES IMPORTED_LOCATION "${OPENDB_MAIN_LIB}")
      add_library(lefin_shared SHARED IMPORTED GLOBAL)
      set_target_properties(lefin_shared PROPERTIES IMPORTED_LOCATION "${OPENDB_LEFIN_LIB}")
      add_library(defin_shared SHARED IMPORTED GLOBAL)
      set_target_properties(defin_shared PROPERTIES IMPORTED_LOCATION "${OPENDB_DEFIN_LIB}")

      set(OPENDB_SO_DIRS
        "${OPENDB_BUILD_ROOT}/src/db" "${OPENDB_BUILD_ROOT}/src/lefin"
        "${OPENDB_BUILD_ROOT}/src/defin" "${OPENDB_BUILD_ROOT}/src/def"
        "${OPENDB_BUILD_ROOT}/src/lef" "${OPENDB_BUILD_ROOT}/src/zutil"
        "${OPENDB_BUILD_ROOT}/src/zlib" "${OPENDB_BUILD_ROOT}/src/tm"
      )
      # Collect OpenDB .so files at configure time (pre-built, must exist).
      set(OPENDB_SO_FILES "")
      foreach(so_dir IN LISTS OPENDB_SO_DIRS)
        file(GLOB _found "${so_dir}/*.so*")
        list(APPEND OPENDB_SO_FILES ${_found})
      endforeach()

      foreach(tgt lib2cdb design2cdb)
        target_compile_definitions(${tgt} PRIVATE LOGIC_MINOR_USE_OPENDB=1)
        target_include_directories(${tgt} PRIVATE
          "${OPENDB_SRC_ROOT}/include" "${OPENDB_SRC_ROOT}/compat"
          "${OPENDB_BUILD_ROOT}/include")
        target_link_libraries(${tgt} PRIVATE opendb_shared lefin_shared defin_shared)
        # Copy all OpenDB .so files next to the binary; use $ORIGIN so no
        # absolute path is baked into the executable's RPATH.
        set_property(TARGET ${tgt} PROPERTY BUILD_RPATH "$ORIGIN")
        foreach(so_file IN LISTS OPENDB_SO_FILES)
          add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${so_file}" "${CMAKE_BINARY_DIR}/"
          )
        endforeach()
      endforeach()
      message(STATUS "OpenDB enabled from ${OPENDB_BUILD_ROOT}")
    else()
      message(WARNING "OpenDB libraries not found — disabling")
      set(DDC_USE_OPENDB OFF)
    endif()
  endif()

  message(STATUS "Yosys-based tools enabled: lib2cdb, design2cdb")
else()
  message(STATUS "Yosys disabled — only dump tools will be built")
endif()
