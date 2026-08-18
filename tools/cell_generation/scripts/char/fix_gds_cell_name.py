import pya
import os
import sys

gds_path    = os.environ.get("gds_path")
expected    = os.environ.get("expected_name")

if not gds_path or not expected:
    raise RuntimeError("gds_path and expected_name must be provided via -rd")

layout = pya.Layout()
layout.read(gds_path)

top_cells = layout.top_cells()
if len(top_cells) != 1:
    raise RuntimeError(f"Expected 1 top cell, found {len(top_cells)}: {[c.name for c in top_cells]}")

top_cell = top_cells[0]
actual = top_cell.name

if actual == expected:
    print(f"OK: {gds_path} already has cell name '{expected}'")
    sys.exit(0)

print(f"Renaming '{actual}' -> '{expected}' in {gds_path}")
top_cell.name = expected

layout.write(gds_path)
print(f"Written: {gds_path}")
