#!/usr/bin/env python3
from __future__ import annotations

import csv
import os
from functools import lru_cache
from typing import Optional

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_FEATURES_CSV = os.path.join(_THIS_DIR, "features.csv")

INV_FET = 2

def n_inputs_from_key(key: str) -> int:
    if not isinstance(key, str) or not key.startswith("0b"):
        raise ValueError(f"canonical key must start with '0b' (got {key!r})")
    body = key[2:]
    n_bits = len(body)
    if n_bits == 0 or any(c not in "01" for c in body):
        raise ValueError(f"canonical key body must be non-empty binary (got {key!r})")
    if n_bits & (n_bits - 1) != 0:
        raise ValueError(f"canonical key length must be a power of two (got {n_bits} bits)")
    return n_bits.bit_length() - 1

@lru_cache(maxsize=1)
def _fet_table() -> dict:
    if not os.path.exists(_FEATURES_CSV):
        raise FileNotFoundError(
            f"fet_cost: features.csv not found at {_FEATURES_CSV} "
            f"(needed for the PDK-independent FET table)."
        )
    table: dict = {}
    with open(_FEATURES_CSV, newline="") as f:
        reader = csv.DictReader(f)
        cols = reader.fieldnames or []
        if "canonical" not in cols or "fet_count" not in cols:
            raise ValueError(
                f"fet_cost: features.csv missing 'canonical'/'fet_count' columns; "
                f"got {cols}"
            )
        for row in reader:
            key = (row["canonical"] or "").strip()
            raw = (row["fet_count"] or "").strip()
            if not key or not raw:
                continue
            fet = int(raw)
            if key in table and table[key] != fet:
                raise ValueError(
                    f"fet_cost: conflicting fet_count for {key}: {table[key]} vs {fet}"
                )
            table[key] = fet
    if not table:
        raise ValueError(f"fet_cost: no canonical/fet_count rows in {_FEATURES_CSV}")
    return table

def fet_cost(key: str, n_inputs: Optional[int] = None) -> int:
    table = _fet_table()
    if key not in table:
        raise ValueError(f"fet_cost: unknown canonical key {key!r} (no cell in features.csv)")
    return table[key]

def delta_fet(target_key: str, base_key: str, base_count: int, inv_count: int) -> int:
    indirect = base_count * fet_cost(base_key) + inv_count * INV_FET
    direct = fet_cost(target_key)
    return max(0, indirect - direct)
