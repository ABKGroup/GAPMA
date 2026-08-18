# GAPMA

Code for the MLCAD 2026 paper "GAPMA: Graph-Attention Cell Usage Prediction with
Polarity-Minimized NPN Aggregation for Design-Specific Cell Composition".

## Setup

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/ABKGroup/GAPMA.git
```

The artifact uses these submodule revisions:

| Path | Commit |
|---|---|
| `tools/third_party/OpenDB` | `32c13c6cd6894066b086356a7ff1f97894cafefb` |
| `tools/third_party/yosys` | `5cfe6a9c1ee66d810a5f40bedc57442dafd4b40d` |
| `tools/cell_generation/SO3-Cell` | `2af55b536333fc5ae31dc96f47a708b5a3824e4c` |

`tools/cell_generation/SO3-Cell` is pinned to a public upstream commit of
[ckchengucsd/SO3-Cell](https://github.com/ckchengucsd/SO3-Cell). Apply this
project's local patch after cloning:

```bash
cd tools/cell_generation/SO3-Cell
git apply ../scripts/so3-cell.patch
```

## Build

`lib2cdb` and `design2cdb` link against Yosys, so build the vendored
submodule first and point `YOSYS_ROOT` at it:

```bash
make -C tools/third_party/yosys -j"$(nproc)"
export YOSYS_ROOT=$PWD/tools/third_party/yosys
cd tools/mining
./build.sh
```

## Run

```bash
cd flow
./datagen.sh   --config config/datagen.conf    # generate training data
./train_gat.sh --config config/train_gat.conf  # train the GATv2 predictor
./autorun.sh   --config config/autorun.conf    # end-to-end candidate selection and SP&R
```
