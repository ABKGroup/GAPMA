# SO3 PDK Standard Cell Library

Academic 3nm PDK (SO3 / PROBE3.0) standard cells for synthesis and P&R with
Fusion Compiler. One subdirectory per view type: `cdl/`, `lib/`, `db/`, `lef/`,
`techlef/`, `gds/`, `tf/`, `tlup/`, `models/`.

This SO3 PDK subset is licensed under BSD-3-Clause. See `LICENSE`.

## Cell Naming

- Standard cells: `{CELLNAME}_X{DRIVE}`, e.g. `INV_X1`, `NAND2_X1`, `AND2_X1`
- Custom cells: `{n}input0x{TT}_X1[_DH_{N|P}]`, where `TT` is the mining
  P-canonical key and `_DH_N`/`_DH_P` marks a Double-Height layout variant.
