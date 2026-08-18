# third_party

External dependencies: `OpenDB/` (LEF/DEF parsing) and `yosys/` (Verilog/library
parsing), plus `patches/`.

## OpenDB patch

```bash
cd OpenDB
git apply ../patches/opendb-lef-null-guard.patch
```
