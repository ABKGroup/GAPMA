#!/usr/bin/env python3
"""
Run under KLayout in batch mode (its `pya` module provides the layout engine):

  # single cell
  klayout -b -r run.py -rd config=examples/so3/input/so3.json -rd gds=cell.gds [-rd out=cell.lef]

  # a whole directory of *.gds
  klayout -b -r run.py -rd config=examples/so3/input/so3.json -rd gds_dir=cells/ [-rd out_dir=out/]

  # a merged library GDS (many cells) -> one merged LEF library with out=, or one
  # LEF per cell in a dir with out_dir=
  klayout -b -r run.py -rd config=examples/gt2n/input/gt2n.json -rd gds=stdcells.gds -rd out=stdcells.lef

Optional: -rd site=NAME  override the SITE name written on each MACRO.
          -rd cell=NAME  extract only this cell (default: every top cell).
"""

import os
import re
import sys
import glob
import json
import pya


def _arg(name, default=None):
    return globals().get(name, default)

CONFIG          = _arg("config")
GDS             = _arg("gds")
GDS_DIR         = _arg("gds_dir")
OUT             = _arg("out")
OUT_DIR         = _arg("out_dir")
SITE_OVERRIDE   = _arg("site")
CELL            = _arg("cell")

if not CONFIG:
    print("ERROR: -rd config=<pdk.json> is required", file=sys.stderr)
    sys.exit(1)
if not os.path.isfile(CONFIG):
    print(f"ERROR: config not found: {CONFIG}", file=sys.stderr)
    sys.exit(1)
if not (GDS or GDS_DIR):
    print("ERROR: give -rd gds=<file> or -rd gds_dir=<dir>", file=sys.stderr)
    sys.exit(1)

with open(CONFIG) as f:
    CFG = json.load(f)


def deep_merge(base, override):
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def cell_config(cell_name):
    ov = CFG.get("overrides", {}).get(cell_name)
    return deep_merge(CFG, ov) if ov else CFG


def require(d, k, ctx):
    if k not in d:
        print(f"ERROR: config missing '{k}' in {ctx} ({CONFIG})", file=sys.stderr)
        sys.exit(1)
    return d[k]


def build_header(cfg):
    lef = cfg.get("lef", {})
    out = ["VERSION 5.8 ;", 'BUSBITCHARS "[]" ;', 'DIVIDERCHAR "/" ;',
           "CLEARANCEMEASURE EUCLIDEAN ;", ""]
    s = require(lef, "header_site", "lef")
    w, h = s["size"]
    out += [f"SITE {s['name']}",
            f"    SIZE {w:.4f} BY {h:.4f} ;",
            f"    CLASS {s.get('class', 'CORE')} ;",
            f"    SYMMETRY {s.get('symmetry', 'X Y')} ;",
            f"END {s['name']}", ""]
    return "\n".join(out)


def top_cells(gds_path):
    ly = pya.Layout()
    ly.read(gds_path)
    return [c.name for c in ly.each_cell() if c.is_top()]


def build_macro(gds_path, cell_name, cfg, site_override=None):
    OUTLINE_LD = tuple(require(cfg, "boundary_layer", "top"))
    LAYERS     = require(cfg, "layers", "top")
    PINS       = require(cfg, "pins", "top")
    POWER_NETS = {p["name"]: p["use"] for p in cfg.get("power_nets", [])}
    DIRCFG     = PINS.get("direction", {"output_regex": "Z|Y|Q"})
    LEFCFG     = cfg.get("lef", {})
    SITE_NAME  = site_override or LEFCFG.get("header_site", {}).get("name", "coresite")

    src = pya.Layout()
    src.read(gds_path)
    scell = None
    for c in src.each_cell():
        if c.name == cell_name:
            scell = c
            break
    if scell is None:
        print(f"ERROR: cell '{cell_name}' not found in {gds_path}", file=sys.stderr)
        return None

    dbu = src.dbu
    ly = pya.Layout()
    ly.dbu = dbu
    top = ly.create_cell(scell.name)
    flat = scell.dup()
    flat.flatten(-1, True)
    for li in src.layer_indexes():
        info = src.get_info(li)
        tl = ly.layer(info.layer, info.datatype)
        for s in flat.shapes(li).each():
            top.shapes(tl).insert(s)

    oli = ly.find_layer(OUTLINE_LD[0], OUTLINE_LD[1])
    if oli is None:
        print(f"ERROR: boundary layer {OUTLINE_LD} not found", file=sys.stderr)
        return None
    obox = None
    for s in top.shapes(oli).each():
        bb = s.bbox()
        obox = bb if obox is None else (obox + bb)
    if obox is None or obox.empty():
        print(f"ERROR: no shape on boundary layer in '{cell_name}'", file=sys.stderr)
        return None
    ox, oy = obox.left, obox.bottom
    width_dbu, height_dbu = obox.width(), obox.height()
    width_um = round(width_dbu * dbu, 4)
    height_um = round(height_dbu * dbu, 4)

    def box_to_rect(b):
        return (round((b.left - ox) * dbu, 4), round((b.bottom - oy) * dbu, 4),
                round((b.right - ox) * dbu, 4), round((b.top - oy) * dbu, 4))

    l2n = pya.LayoutToNetlist(pya.RecursiveShapeIterator(ly, top, []))
    regions = {}
    INPUTS = require(LAYERS, "layer_table", "layers")
    for name, ld in INPUTS.items():
        li = ly.find_layer(ld[0], ld[1])
        regions[name] = l2n.make_layer(li, name) if li is not None else l2n.make_layer(name)

    for d in LAYERS.get("derived_layers", []):
        expr = d["expr"]
        is_alias = expr.strip() in regions
        try:
            regions[d["name"]] = eval(expr, {"__builtins__": {}}, regions)
        except Exception as e:
            print(f"ERROR: derived layer '{d['name']}' expr '{expr}' failed: {e}", file=sys.stderr)
            return None
        if not is_alias:
            l2n.register(regions[d["name"]], d["name"])

    for r in regions:
        l2n.connect(regions[r])
    for a, b in LAYERS.get("connect_rules", []):
        l2n.connect(regions[a], regions[b])

    LABEL_LAYERS = PINS.get("text_layers", {})
    for metal_name, ld in LABEL_LAYERS.items():
        li = ly.find_layer(ld[0], ld[1])
        if li is None:
            continue
        lblreg = l2n.make_layer(li, metal_name + "_lbl")
        l2n.connect(regions[metal_name], lblreg)

    l2n.extract_netlist()
    circ = None
    for c in l2n.netlist().each_circuit():
        circ = c
        break
    if circ is None:
        print("ERROR: netlist extraction produced no circuit", file=sys.stderr)
        return None

    PIN_LAYERS = LAYERS.get("routing_layers", [])
    LEF_NAMES  = {r: r for r in PIN_LAYERS}

    def boxes_of(net, region):
        merged = pya.Region()
        for poly in l2n.shapes_of_net(net, region, True).each():
            merged.insert(poly)
        merged.merge()
        out = []
        for poly in merged.each():
            for trap in poly.decompose_trapezoids(pya.Polygon.TD_simple):
                out.append(trap.bbox())
        return out

    pin_data = {}
    power_pin_rects = {}
    obs_by_lef = {}

    ANT = cfg.get("antenna")
    pin_antenna = {}
    if ANT:
        a_gl = require(ANT, "gate_layer", "antenna")
        a_al = require(ANT, "active_layer", "antenna")
        a_dl = require(ANT, "diff_layer", "antenna")
        a_th = ANT.get("metal_thickness", {})
        for _key, _layer in (("gate_layer", a_gl), ("diff_layer", a_dl)):
            if _layer not in regions:
                print(f"ERROR: antenna {_key} '{_layer}' is not a layer defined in "
                      f"layer_table or derived_layers ({CONFIG}). antenna needs it to "
                      f"compute gate/diff area.", file=sys.stderr)
                sys.exit(1)
        if a_al not in INPUTS:
            print(f"ERROR: antenna active_layer '{a_al}' is not in layer_table ({CONFIG}).",
                  file=sys.stderr)
            sys.exit(1)
        _bad_th = [L for L in a_th if L not in PIN_LAYERS]
        if _bad_th:
            print(f"ERROR: antenna metal_thickness lists non-routing layer(s) {_bad_th}, "
                  f"must be in routing_layers {PIN_LAYERS} ({CONFIG}).", file=sys.stderr)
            sys.exit(1)
        _car = ANT.get("car")
        if _car:
            for _k in ("metal_stack", "cut_stack"):
                if _k not in _car:
                    print(f"ERROR: antenna car needs '{_k}' (bottom-to-top layer list) ({CONFIG}).",
                          file=sys.stderr)
                    sys.exit(1)
            _bad_car = [L for L in _car["metal_stack"] + _car["cut_stack"] if L not in regions]
            if _bad_car:
                print(f"ERROR: antenna car layer(s) {_bad_car} not defined in layer_table or "
                      f"derived_layers ({CONFIG}).", file=sys.stderr)
                sys.exit(1)
        active_full = pya.Region()
        _ali = ly.find_layer(*INPUTS[a_al]) if a_al in INPUTS else None
        if _ali is not None:
            for s in top.shapes(_ali).each():
                if not s.is_text():
                    active_full.insert(s.polygon if not s.is_box() else pya.Polygon(s.box))
        active_full.merge()

    def antenna_of(net):
        def net_region(rname):
            r = pya.Region()
            if rname in regions:
                for poly in l2n.shapes_of_net(net, regions[rname], True).each():
                    r.insert(poly)
            return r.merged()
        name = net.expanded_name()
        label_pts = []
        for lname, ld in LABEL_LAYERS.items():
            li = ly.find_layer(ld[0], ld[1])
            if li is None:
                continue
            for s in top.shapes(li).each():
                if s.is_text() and s.text.string == name:
                    label_pts.append((pya.Point(s.text.trans.disp.x, s.text.trans.disp.y), lname))
        def internal_region(rname):
            internal = pya.Region()
            for poly in net_region(rname).each():
                pr = pya.Region(poly)
                is_port = any(mlyr == rname and poly.bbox().contains(pt) and not
                              (pr & pya.Region(pya.Box(pt.x - 1, pt.y - 1, pt.x + 1, pt.y + 1))).is_empty()
                              for pt, mlyr in label_pts)
                if not is_port:
                    internal.insert(poly)
            return internal
        port_layers = {lname for _pt, lname in label_pts}
        out = {"metal": []}
        gch = (net_region(a_gl) & active_full).area()
        if gch > 0:
            out["gate"] = gch * dbu * dbu
        dif = net_region(a_dl).area()
        if dif > 0:
            out["diff"] = dif * dbu * dbu
        metal_layers = ANT["car"]["metal_stack"] if ANT.get("car") else PIN_LAYERS
        reached = [L for L in metal_layers if not net_region(L).is_empty()]
        out["gate_layers"] = [LEF_NAMES.get(L, L) for L in reached]
        for L in metal_layers:
            if L not in port_layers:
                continue
            ir = internal_region(L)
            if ir.is_empty():
                continue
            side = ir.perimeter() * dbu * a_th[L] if L in a_th else None
            out["metal"].append((LEF_NAMES.get(L, L), ir.area() * dbu * dbu, side))
        car = ANT.get("car")
        if car and gch > 0 and reached:
            total = sum(internal_region(L).area() * dbu * dbu for L in car["metal_stack"]) / out["gate"]
            if total > 0:
                out["car_area"] = [(LEF_NAMES.get(reached[-1], reached[-1]), total)]
            acc = 0.0
            car_cut = []
            cut_partial = []
            for L in car["cut_stack"]:
                a = internal_region(L).area() * dbu * dbu
                if a > 0:
                    acc += a / out["gate"]
                    car_cut.append((LEF_NAMES.get(L, L), acc))
                    cut_partial.append((LEF_NAMES.get(L, L), a))
            if car_cut:
                out["car_cut"] = car_cut
            if cut_partial:
                out["cut"] = cut_partial
        return out

    for net in circ.each_net():
        name = net.expanded_name()
        per_layer = {}
        for rname in PIN_LAYERS:
            rects = [box_to_rect(b) for b in boxes_of(net, regions[rname])]
            if rects:
                per_layer[LEF_NAMES.get(rname, rname)] = rects
        if not per_layer:
            labeled = name in POWER_NETS or (name and not name.startswith("$"))
            if labeled:
                print(f"  WARNING: net '{name}' in '{cell_name}' carries a port label but "
                      f"has no metal on any pin layer, NO PIN emitted "
                      f"(check routing_layers / label_layers).", file=sys.stderr)
            continue
        if name in POWER_NETS:
            dst = power_pin_rects.setdefault(name, {})
        elif name and not name.startswith("$"):
            dst = pin_data.setdefault(name, {})
            if ANT:
                pin_antenna[name] = antenna_of(net)
        else:
            for lef, rects in per_layer.items():
                obs_by_lef.setdefault(lef, []).extend(rects)
            continue
        for lef, rects in per_layer.items():
            dst.setdefault(lef, []).extend(rects)

    PIN_DIRS = PINS.get("pin_directions", {})
    _out_re = DIRCFG.get("output_regex")
    _in_re  = DIRCFG.get("input_regex")
    OUT_PAT = re.compile(_out_re) if _out_re else None
    IN_PAT  = re.compile(_in_re) if _in_re else None
    def pin_direction(nm):
        if nm in PIN_DIRS:
            return PIN_DIRS[nm]
        is_out = bool(OUT_PAT and OUT_PAT.fullmatch(nm))
        is_in  = bool(IN_PAT and IN_PAT.fullmatch(nm))
        if is_out and is_in:
            print(f"ERROR: ambiguous pin '{nm}' in cell '{cell_name}': matches both "
                  f"output_regex and input_regex. Make the patterns disjoint, or set "
                  f"this pin per-cell in pins.pin_directions.", file=sys.stderr)
            sys.exit(1)
        if is_out:
            return "OUTPUT"
        if is_in:
            return "INPUT"
        print(f"ERROR: unclassified pin '{nm}' in cell '{cell_name}': matches neither "
              f"output_regex nor input_regex. Add it to one of them, or set it per-cell "
              f"in pins.pin_directions.", file=sys.stderr)
        sys.exit(1)

    EMIT_ORDER = [LEF_NAMES.get(r, r) for r in PIN_LAYERS]
    def emit_layers(per_layer):
        out = []
        for lef_layer in EMIT_ORDER:
            rects = per_layer.get(lef_layer, [])
            if rects:
                out.append(f"      LAYER {lef_layer} ;")
                for lx, ly_, ux, uy in rects:
                    out.append(f"        RECT {lx:.4f} {ly_:.4f} {ux:.4f} {uy:.4f} ;")
        return out

    cc = cfg.get("cell_class", [])
    if isinstance(cc, str):
        cell_cls = cc
    else:
        cell_cls = "CORE"
        for rule in cc:
            if re.search(rule["match"], cell_name):
                cell_cls = rule["class"]
                break
    lines = [f"MACRO {cell_name}",
             f"  CLASS {cell_cls} ;",
             "  ORIGIN 0 0 ;",
             f"  FOREIGN {cell_name} 0 0 ;",
             f"  SIZE {width_um:.4f} BY {height_um:.4f} ;",
             "  SYMMETRY X Y ;",
             f"  SITE {SITE_NAME} ;"]

    USE_OF = {nm: use for use, names in PINS.get("pin_use", {}).items() for nm in names}

    def fmt(x):
        return f"{x:.6f}".rstrip("0").rstrip(".")

    emitted_dirs = []
    for net_name in sorted(pin_data):
        port_lines = emit_layers(pin_data[net_name])
        if not port_lines:
            continue
        d = pin_direction(net_name)
        emitted_dirs.append(d)
        use = USE_OF.get(net_name, "SIGNAL")
        lines.append(f"  PIN {net_name}")
        lines.append(f"    DIRECTION {d} ;")
        lines.append(f"    USE {use} ;")
        ant = pin_antenna.get(net_name)
        if ant:
            layers = ant.get("gate_layers", [])
            if ANT.get("partial_metal", True):
                for lef, marea, mside in ant["metal"]:
                    lines.append(f"    ANTENNAPARTIALMETALAREA {fmt(marea)} LAYER {lef} ;")
                    if mside is not None:
                        lines.append(f"    ANTENNAPARTIALMETALSIDEAREA {fmt(mside)} LAYER {lef} ;")
                for lef, carea in ant.get("cut", []):
                    lines.append(f"    ANTENNAPARTIALCUTAREA {fmt(carea)} LAYER {lef} ;")
            if ant.get("gate"):
                if ANT.get("model"):
                    lines.append(f"    ANTENNAMODEL {ANT['model']} ;")
                for lef in layers:
                    lines.append(f"    ANTENNAGATEAREA {fmt(ant['gate'])} LAYER {lef} ;")
            if ant.get("diff"):
                for lef in layers:
                    lines.append(f"    ANTENNADIFFAREA {fmt(ant['diff'])} LAYER {lef} ;")
            for lef, car_v in ant.get("car_area", []):
                lines.append(f"    ANTENNAMAXAREACAR {fmt(car_v)} LAYER {lef} ;")
            for lef, car_v in ant.get("car_cut", []):
                lines.append(f"    ANTENNAMAXCUTCAR {fmt(car_v)} LAYER {lef} ;")
        lines.append("    PORT")
        lines.extend(port_lines)
        lines.append("    END")
        lines.append(f"  END {net_name}")

    if len(emitted_dirs) >= 2 and "OUTPUT" not in emitted_dirs:
        print(f"  WARNING: cell '{cell_name}' has {len(emitted_dirs)} signal pins "
              f"but no OUTPUT pin, check pin directions.", file=sys.stderr)

    for p in cfg.get("power_nets", []):
        net_name, use = p["name"], p["use"]
        port_lines = emit_layers(power_pin_rects.get(net_name, {}))
        lines.append(f"  PIN {net_name}")
        lines.append("    DIRECTION INOUT ;")
        lines.append(f"    USE {use} ;")
        lines.append("    SHAPE ABUTMENT ;")
        lines.append("    PORT")
        if port_lines:
            lines.extend(port_lines)
        else:
            print(f"  WARNING: no geometry for power net '{net_name}'", file=sys.stderr)
        lines.append("    END")
        lines.append(f"  END {net_name}")

    if obs_by_lef:
        lines.append("  OBS")
        for lef_layer in EMIT_ORDER:
            rects = obs_by_lef.get(lef_layer, [])
            if rects:
                lines.append(f"    LAYER {lef_layer} ;")
                for lx, ly_, ux, uy in rects:
                    lines.append(f"      RECT {lx:.4f} {ly_:.4f} {ux:.4f} {uy:.4f} ;")
        lines.append("  END")

    lines.append(f"END {cell_name}")
    return "\n".join(lines)


def write_lef(gds_path, cell_name, out_path, header):
    macro = build_macro(gds_path, cell_name, cell_config(cell_name), SITE_OVERRIDE)
    if macro is None:
        return False
    with open(out_path, "w") as f:
        f.write(header + "\n" + macro + "\n\nEND LIBRARY\n")
    print(f"  OK  {cell_name} -> {out_path}  (pins={macro.count(chr(10) + '  PIN ')})")
    return True


_WRITTEN = {}


def _authority(gds_path, cell, ntop):
    name_match = 0 if os.path.splitext(os.path.basename(gds_path))[0] == cell else 1
    return (name_match, ntop)


def process_gds(gds_path, header, out, out_dir):
    cells = [CELL] if CELL else top_cells(gds_path)
    if not cells:
        print(f"  ERROR: no top cell in {gds_path}", file=sys.stderr)
        return 0, 1
    ntop = len(cells)
    npass = nfail = 0
    if out and not out_dir:
        macros = []
        for c in cells:
            macro = build_macro(gds_path, c, cell_config(c), SITE_OVERRIDE)
            if macro is None:
                nfail += 1
                continue
            macros.append(macro)
            print(f"  OK  {c} -> {out}  (pins={macro.count(chr(10) + '  PIN ')})")
            npass += 1
        if macros:
            with open(out, "w") as f:
                f.write(header + "\n" + "\n\n".join(macros) + "\n\nEND LIBRARY\n")
            _WRITTEN[out] = (gds_path, (0, ntop))
        return npass, nfail
    for c in cells:
        d = out_dir or os.path.dirname(gds_path) or "."
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, c + ".lef")
        score = _authority(gds_path, c, ntop)
        prev = _WRITTEN.get(dst)
        if prev and prev[0] != gds_path:
            keep_existing = prev[1] <= score
            kept, dropped = (prev[0], gds_path) if keep_existing else (gds_path, prev[0])
            print(f"  WARNING: cell '{c}' defined in two source GDS; keeping {kept}, "
                  f"ignoring {dropped} (copies may differ in DBU/geometry).", file=sys.stderr)
            if keep_existing:
                continue
        if write_lef(gds_path, c, dst, header):
            _WRITTEN[dst] = (gds_path, score)
            npass += 1
        else:
            nfail += 1
    return npass, nfail


HEADER = build_header(CFG)
npass = nfail = 0
gds_files = [GDS] if GDS else sorted(glob.glob(os.path.join(GDS_DIR, "*.gds")))
for g in gds_files:
    p, f = process_gds(g, HEADER, OUT, OUT_DIR or (None if GDS else GDS_DIR))
    npass += p
    nfail += f
print(f"DONE: {npass} passed, {nfail} failed")

sys.exit(0 if nfail == 0 else 1)
