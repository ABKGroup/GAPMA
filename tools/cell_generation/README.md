# cell_generation

Turns a candidate cell's transistor-level CDL netlist into a full set of PDK
views: GDS, LEF, PEX netlist, Liberty, and .db.

## Setup

```bash
cd SO3-Cell
git apply ../scripts/so3-cell.patch
cd Framework/PostCellGen/inputs
bash unzip_stdqrc.sh
```

## Run

```bash
./single_cell.sh <CELL_NAME>
```

Reads `$PDK_ROOT/cdl/<CELL_NAME>.cdl` and installs the generated views into
`$PDK_ROOT`. `flow/autorun.sh` calls this for every selected candidate cell that
is not already in the PDK. Each step is skipped when its output already exists,
so a failed run resumes by invoking the same command again.

Tool executables are taken from `PATH` and can be overridden individually
(`KLAYOUT`, `PEGASUS`, `QUANTUS`, `LIBERATE`, `LC_SHELL`, `PYTHON`).
