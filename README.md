# GAPMA

Code for the MLCAD 2026 paper "GAPMA: Graph-Attention Cell Usage Prediction with
Polarity-Minimized NPN Aggregation for Design-Specific Cell Composition".

If you use GAPMA in your work, please cite:

```
Chung-Kuan Cheng, Junyeong Jang, Andrew B. Kahng, Byeonggon Kang, and Jakang Lee. 2026.
GAPMA: Graph-Attention Cell Usage Prediction with Polarity-Minimized NPN Aggregation for
Design-Specific Cell Composition. In 2026 ACM/IEEE International Symposium on Machine
Learning for CAD (MLCAD '26), September 7-9, 2026, Jeju Island, Republic of Korea. ACM,
New York, NY, USA, 9 pages. https://doi.org/10.1145/3831599.3840351
```

An archived snapshot of this repository is available on Zenodo:
[10.5281/zenodo.21987869](https://doi.org/10.5281/zenodo.21987869).

## Repository layout

```
GAPMA/
├── rtl/                  # 10 RTL benchmarks (5 training + 5 evaluation designs)
├── pdk/SO3/               # Academic 3nm PDK (SO3) used for cell layout/enablement
├── tools/
│   ├── mining/            # Logic-cluster mining + PM-NPN aggregation (C++, links Yosys)
│   ├── cell_generation/   # SO3-Cell layout generation (submodule + local patch)
│   ├── third_party/       # Bundled OpenDB and Yosys submodules
│   └── utils/             # Shared runtime-logging helpers
├── flow/
│   ├── datagen.sh         # Stage: generate GATv2 training data from SP&R runs
│   ├── train_gat.sh       # Stage: train the GATv2 cell-usage-share predictor
│   ├── autorun.sh         # Stage: end-to-end candidate selection + SP&R loop
│   ├── config/            # *.conf files consumed by the scripts above
│   └── scripts/           # Per-stage driver scripts (datagen, fc_reference, iterate)
├── data/
│   ├── GATv2/             # Pretrained GATv2 checkpoint (model_pretrained.pt)
│   └── mining/            # Shared cell-feature / mining relation table
└── examples/graph_demo/   # Sample mining feature CSVs for one design
```

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
