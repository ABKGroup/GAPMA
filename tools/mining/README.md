# mining

Logic-function mining pipeline: takes a gate-level netlist and a standard-cell
library, finds recurring sub-circuit patterns, and canonicalizes them into
candidate cell functions.

## Build

```bash
cd tools/mining
./build.sh
```

Requires a C++17 compiler and CMake. `lib2cdb`/`design2cdb` also need Yosys
(`YOSYS_ROOT`). OpenDB is optional for LEF/DEF support.

## Usage

```bash
./mine.sh --libs <*.lib> --netlist <design.v> --out-dir <dir>
./scripts/extract.sh --mining-dir <dir> --out-dir <dir>
```

`mine.sh` also accepts pre-built CDB files (`--cell-db`, `--netlist-db`); run
`./mine.sh --help` for the full flag list.

## Merged Liberty for ABC

```bash
python3 scripts/merge_liberty.py --lib-dir ../../pdk/SO3/lib --out so3_merged.lib
yosys -p 'read_liberty -lib so3_merged.lib'
```
