#!/usr/bin/env python3
import argparse
import difflib
import re
import sys
from pathlib import Path

W_VALID = re.compile(r"^\s*M\S+.*[wW]\s*=\s*46(?:\.0+)?n", re.MULTILINE)
NFIN_VALID = re.compile(r"^\s*M\S+.*\bnfin\s*=\s*2\b", re.MULTILINE)
ANY_TRANSISTOR = re.compile(r"^\s*M\S+\s.*", re.MULTILINE)

W_SCALE = re.compile(r"([wW])\s*=\s*46(\.0+)?n")
NFIN_SCALE = re.compile(r"\bnfin\s*=\s*2\b")
SUBCKT_NAME = re.compile(r"^(\.SUBCKT\s+)(\S+)_X1\b", re.MULTILINE)
SUBCKT_ANY = re.compile(r"^\.SUBCKT\s+(\S+)", re.MULTILINE | re.IGNORECASE)

def validate_x1(text):
    subckts = SUBCKT_ANY.findall(text)
    if not subckts:
        raise ValueError("No .SUBCKT line found in CDL.")
    bad_names = [n for n in subckts if not n.endswith("_X1")]
    if bad_names:
        raise ValueError(
            "X1 CDL .SUBCKT cell name must end with _X1; found: "
            + ", ".join(bad_names)
        )

    tlines = [ln for ln in text.splitlines() if re.match(r"^\s*M\S+\s", ln)]
    if not tlines:
        raise ValueError("No MOSFET (M*) lines found in CDL.")
    bad = []
    for ln in tlines:
        ok_w = re.search(r"[wW]\s*=\s*46(?:\.0+)?n", ln)
        ok_nfin = re.search(r"\bnfin\s*=\s*2\b", ln)
        if not (ok_w and ok_nfin):
            bad.append(ln.rstrip())
    if bad:
        raise ValueError(
            f"X1 CDL has non-standard W or nfin on {len(bad)} transistor(s):\n  "
            + "\n  ".join(bad[:10])
        )

def scale(text):
    out, n_renamed = SUBCKT_NAME.subn(lambda m: f"{m.group(1)}{m.group(2)}_X2", text)
    if n_renamed == 0:
        raise ValueError(
            "no .SUBCKT <name>_X1 line matched; refusing to emit an X2 CDL whose "
            ".SUBCKT still carries the X1 name"
        )
    def repl_w(m):
        case = m.group(1)
        zeros = m.group(2) or ""
        return f"{case}=92{zeros}n"
    out = W_SCALE.sub(repl_w, out)
    out = NFIN_SCALE.sub("nfin=4", out)
    return out

def self_verify(x1_path, x2_ref_path):
    x1_text = Path(x1_path).read_text()
    x2_ref = Path(x2_ref_path).read_text()
    validate_x1(x1_text)
    x2_gen = scale(x1_text)
    diff = list(
        difflib.unified_diff(
            x2_ref.splitlines(keepends=True),
            x2_gen.splitlines(keepends=True),
            fromfile=str(x2_ref_path),
            tofile=f"{x1_path}.scaled",
            n=2,
        )
    )
    return diff

def main():
    ap = argparse.ArgumentParser(description="Scale X1 CDL to X2 (framework rule).")
    ap.add_argument("--src", help="Single X1 CDL path.")
    ap.add_argument("--out", help="Output X2 CDL path.")
    ap.add_argument(
        "--self-verify",
        nargs=2,
        metavar=("X1_CDL", "X2_REF_CDL"),
        help="Verify the scaling rule by comparing scaled X1 against existing X2 reference.",
    )
    args = ap.parse_args()

    if args.self_verify:
        diff = self_verify(*args.self_verify)
        if not diff:
            print(f"[self-verify] OK — scaled {args.self_verify[0]} matches {args.self_verify[1]} byte-for-byte.")
            return 0
        print(f"[self-verify] DIFF ({len(diff)} lines):")
        sys.stdout.writelines(diff)
        return 1

    if not args.src or not args.out:
        ap.error("--src and --out are required (or use --self-verify).")

    text = Path(args.src).read_text()
    validate_x1(text)
    out_text = scale(text)
    out_path = Path(args.out)
    if out_path.exists():
        raise FileExistsError(f"Refusing to overwrite existing: {out_path}")
    out_path.write_text(out_text)
    print(f"[scale] {args.src} -> {args.out}  ({len(out_text)} bytes)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
