from __future__ import annotations

import argparse
import json
import csv
import math
import os
import random
import sys
from pathlib import Path

import torch
torch.multiprocessing.set_sharing_strategy("file_system")

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from train.dataset_diff import DiffPairDataset, DIFF_NODE_FEAT_DIM
from train.model_diff import DiffDemandGNN, demand_loss

try:
    from torch_geometric.data import Batch as _PyGBatch
    _HAVE_PYG_BATCH = True
except ImportError:
    _HAVE_PYG_BATCH = False

from torch.utils.data import DataLoader as _DataLoader

def _run_level_split(
    dataset: "DiffPairDataset",
    val_frac: float,
    seed: int,
    val_design: str | None = None,
) -> tuple[list[int], list[int]]:
    n_runs = len(dataset._samples)
    if val_design is not None:
        val_run_set = {
            idx for idx, s in enumerate(dataset._samples)
            if s["design"] == val_design
        }
        if not val_run_set:
            raise ValueError(
                f"--val-design={val_design!r} matched 0 runs. "
                f"Available designs: {sorted({s['design'] for s in dataset._samples})}"
            )
        train_run_set = set(range(n_runs)) - val_run_set
        print(f"[_run_level_split] design-level split: val_design={val_design!r}  "
              f"val_runs={len(val_run_set)}  train_runs={len(train_run_set)}", flush=True)
    else:
        run_indices = list(range(n_runs))
        random.Random(seed).shuffle(run_indices)
        n_val_runs = max(1, int(n_runs * val_frac))
        train_run_set = set(run_indices[n_val_runs:])
        val_run_set   = set(run_indices[:n_val_runs])

    train_idx = [
        idx for idx, (i, j) in enumerate(dataset._pairs)
        if i in train_run_set and j in train_run_set
    ]
    val_idx = [
        idx for idx, (i, j) in enumerate(dataset._pairs)
        if i in val_run_set and j in val_run_set
    ]
    if not val_idx:
        raise ValueError(
            f"Run-level val split produced 0 pairs "
            f"(n_val_runs={n_val_runs}, n_runs={n_runs}). "
            "Increase dataset size or reduce --val-frac."
        )
    return train_idx, val_idx

def _g_attr(g, key: str):
    return g[key] if isinstance(g, dict) else getattr(g, key)

class _PairSubset(torch.utils.data.Dataset):
    def __init__(self, dataset, indices: list[int]):
        self._ds  = dataset
        self._idx = indices

    def __len__(self) -> int:
        return len(self._idx)

    def __getitem__(self, i: int):
        return self._ds[self._idx[i]]

def _pyg_collate(batch):
    datas = []
    for d, y in batch:
        d.y = y
        datas.append(d)
    return _PyGBatch.from_data_list(datas)

def _worker_init_fn(worker_id: int) -> None:
    import torch.utils.data as _tud
    info = _tud.get_worker_info()
    if info is not None:
        info.dataset._ds._max_cache_size = 0

def _run_epoch(
    model: DiffDemandGNN,
    dataset: DiffPairDataset,
    indices: list[int],
    device: torch.device,
    optimizer=None,
    batch_size: int = 64,
    rank_weight: float = 0.0,
    num_workers: int = 0,
) -> tuple[float, float]:
    if not _HAVE_PYG_BATCH:
        raise RuntimeError("torch_geometric.data.Batch is required for batched training.")

    training = optimizer is not None
    model.train(training)
    total_loss_sum = 0.0
    total_loss_n   = 0
    total_mae_sum  = 0.0
    total_mae_n    = 0

    order = list(indices)
    if training:
        random.shuffle(order)

    def _make_batches():
        if num_workers > 0 and _HAVE_PYG_BATCH:
            subset = _PairSubset(dataset, order)
            loader = _DataLoader(
                subset, batch_size=batch_size, shuffle=False,
                num_workers=num_workers, collate_fn=_pyg_collate,
                pin_memory=(device.type == "cuda"),
                worker_init_fn=_worker_init_fn,
                persistent_workers=(num_workers > 0),
            )
            for b in loader:
                yield b.to(device)
        else:
            for i in range(0, len(order), batch_size):
                chunk = order[i:i + batch_size]
                datas = []
                for idx in chunk:
                    d, y = dataset[idx]
                    d.y = y
                    datas.append(d)
                yield _PyGBatch.from_data_list(datas).to(device)

    with torch.set_grad_enabled(training):
        for batch in _make_batches():
            keep = batch.loss_mask > 0.5
            if not keep.any():
                # Skipping means these pairs contribute nothing to the loss, so
                # the run trains on fewer samples than reported while looking
                # healthy. A pair with no B_lib node is malformed data.
                raise SystemExit(
                    "ERROR: batch has no B_lib nodes (loss_mask all zero); the "
                    "pair carries no training signal. batch_size="
                    f"{int(batch.batch.max().item())+1 if batch.batch.numel() > 0 else 0}"
                )

            dist = model(batch.x, batch.edge_index, batch.edge_attr,
                         batch=batch.batch, loss_mask=batch.loss_mask)
            loss, L_dist, L_rank = demand_loss(
                pred_dist   = dist,
                dist_label  = batch.dist_label,
                loss_mask   = batch.loss_mask,
                batch       = batch.batch,
                rank_weight = rank_weight,
            )
            with torch.no_grad():
                diff = (dist.detach()[keep] - batch.dist_label[keep]).abs()

            if training:
                optimizer.zero_grad()
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=0.5)
                optimizer.step()

            n_kept = int(keep.sum().item())
            total_loss_sum += loss.item() * n_kept
            total_loss_n   += n_kept
            total_mae_sum  += diff.sum().item()
            total_mae_n    += diff.numel()

    mean_loss = total_loss_sum / max(total_loss_n, 1)
    mean_mae  = total_mae_sum / max(total_mae_n, 1)
    return mean_loss, mean_mae

def train(args: argparse.Namespace) -> None:
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if device.type == "cuda":
        torch.set_float32_matmul_precision("high")
    print(f"[train_diff] device: {device}")

    data_mode = getattr(args, "data_mode", "cache")
    if data_mode == "lazy":
        if args.cache_graphs:
            raise SystemExit(
                "[train_diff] --data-mode lazy is incompatible with "
                "--cache-graphs (lazy mode never builds the item cache). "
                "Drop one of the two flags."
            )
        if getattr(args, "prewarm_only", False):
            raise SystemExit(
                "[train_diff] --data-mode lazy is incompatible with "
                "--prewarm-only."
            )
        if getattr(args, "num_workers", 0) <= 0:
            print("[train_diff] WARNING: lazy mode with num_workers=0 — "
                  "every pair composes in the main process (~8 pairs/s). "
                  "Set --num-workers (48 recommended).", flush=True)
        torch.multiprocessing.set_sharing_strategy("file_system")
        args.max_cache_size = 0

    print(f"[train_diff] data_mode = {data_mode}")
    print(f"[train_diff] Loading dataset from {args.run_root} ...")
    dataset = DiffPairDataset(
        run_root     = args.run_root,
        relation_csv = args.relation,
        l0_csv       = args.l0,
        features_csv = getattr(args, "features", None),
        max_delta    = args.max_delta,
        require_cell_counts  = args.require_cell_counts,
        p_to_np_csv          = getattr(args, "p_to_np", None),
        area_weighted_label  = getattr(args, "area_label", False),
        extra_features       = getattr(args, "extra_features", False),
        lib_features         = getattr(args, "lib_features", False),
        max_cache_size       = getattr(args, "max_cache_size", 100000),
        exclude_runs_file    = getattr(args, "exclude_runs_file", None),
    )
    print(f"[train_diff] max_cache_size = {getattr(args, 'max_cache_size', 100000)}")
    print(f"[train_diff] {len(dataset)} pair samples")

    _l0_csv_text = Path(args.l0).read_text()

    train_idx, val_idx = _run_level_split(dataset, args.val_frac, args.seed,
                                          val_design=getattr(args, "val_design", None))
    print(f"[train_diff] split (run-level): {len(train_idx)} train / {len(val_idx)} val")

    _train_run_indices = sorted({r for _pi in train_idx for r in dataset._pairs[_pi]})
    print(f"[train_diff] design norm stats over {len(_train_run_indices)} training runs "
          f"(of {len(dataset._samples)} total)")
    dataset.recompute_design_norm_stats_for_split(_train_run_indices)

    if data_mode == "cache" and args.cache_graphs:
        dataset.prewarm_cache(cache_path=args.cache_graphs or None,
                              n_workers=args.prewarm_workers)

    if getattr(args, 'prewarm_only', False):
        print("[train_diff] --prewarm-only: cache built, exiting.", flush=True)
        return

    print("[train_diff] building cell-type → val_pair index (lightweight) ...", flush=True)
    celltype_to_valpairs: dict[str, list[tuple[int, int]]] = {}
    for vi in val_idx:
        node_keys, in_b = dataset._get_pair_node_meta(vi)
        for k_idx, (key, b) in enumerate(zip(node_keys, in_b)):
            if b:
                celltype_to_valpairs.setdefault(key, []).append((vi, k_idx))
    print(f"[train_diff]   {len(celltype_to_valpairs)} cell types covered in val", flush=True)
    _celltype_order = sorted(celltype_to_valpairs.keys(),
                             key=lambda k: len(celltype_to_valpairs[k]))

    _probe_n = min(64, len(train_idx))
    _probe_sum = 0.0
    _probe_cnt = 0
    for _i in train_idx[:_probe_n]:
        _d, _y = dataset[_i]
        _mask = _d.loss_mask > 0.5
        if _mask.any():
            _probe_sum += float(_y[_mask].sum().item())
            _probe_cnt += int(_mask.sum().item())
    label_mean = _probe_sum / max(_probe_cnt, 1)
    print(f"[train_diff] training label mean (mask>0.5, {_probe_n} probe pairs) = {label_mean:.4f}")
    print(f"[train_diff] head.bias prior init = inv_softplus({label_mean:.4f})")

    from train.model_diff import DIFF_EDGE_FEAT_DIM
    _probe_data, _ = dataset[0]
    actual_in_channels = int(_probe_data.x.shape[1])
    print(f"[train_diff] node feature dim (auto-detected) = {actual_in_channels} "
          f"(baseline=46 / extra-features=48)")
    model = DiffDemandGNN(
        in_channels = actual_in_channels,
        edge_dim    = DIFF_EDGE_FEAT_DIM,
        hidden      = args.hidden,
        heads       = args.heads,
        dropout     = args.dropout,
        n_layers    = getattr(args, "layers", 3),
    ).to(device)
    print(f"[train_diff] n_layers={getattr(args,'layers',3)}")
    if hasattr(torch, "compile"):
        model = torch.compile(model, dynamic=True)
        print("[train_diff] torch.compile applied (dynamic=True)")

    if getattr(args, "init_from", None):
        ckpt_path = args.init_from
        if not os.path.isfile(ckpt_path):
            raise FileNotFoundError(f"--init-from checkpoint not found: {ckpt_path}")
        _raw = torch.load(ckpt_path, map_location=device, weights_only=False)
        sd = _raw["model"] if isinstance(_raw, dict) and "model" in _raw else _raw
        missing, unexpected = model.load_state_dict(sd, strict=False)
        print(f"[train_diff] warm-start from {ckpt_path}")
        if missing:    print(f"  missing keys   : {missing}")
        if unexpected: print(f"  unexpected keys: {unexpected}")

    optimizer = torch.optim.Adam(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )
    _warmup = max(0, int(args.warmup_epochs))
    _total  = max(1, int(args.epochs))
    _cosine_T = max(1, _total - _warmup)
    def _lr_lambda(epoch):
        if epoch < _warmup:
            return float(epoch + 1) / float(max(1, _warmup))
        import math as _m
        t = (epoch - _warmup) / float(_cosine_T)
        return 1e-2 + (1.0 - 1e-2) * 0.5 * (1.0 + _m.cos(_m.pi * min(1.0, t)))
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, _lr_lambda)

    out_dir = Path(args.out).parent
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path  = out_dir / "train_diff_log.csv"
    best_path = Path(args.out)
    best_val  = float("inf")

    with open(log_path, "w", newline="") as logf:
        writer = csv.writer(logf)
        writer.writerow(["epoch", "train_loss", "val_loss", "train_mae", "val_mae"])

        _rank_w = float(getattr(args, "rank_weight", 0.0))
        print(f"[train_diff] rank_weight={_rank_w}")
        for epoch in range(1, args.epochs + 1):
            train_loss, train_mae = _run_epoch(
                model, dataset, train_idx, device, optimizer,
                batch_size=args.batch_size,
                rank_weight=_rank_w,
                num_workers=getattr(args, "num_workers", 0),
            )
            val_loss, val_mae = _run_epoch(
                model, dataset, val_idx, device,
                batch_size=args.batch_size,
                rank_weight=_rank_w,
                num_workers=getattr(args, "num_workers", 0),
            )
            scheduler.step()

            writer.writerow([
                epoch,
                f"{train_loss:.6f}",
                f"{val_loss:.6f}",
                f"{train_mae:.6f}",
                f"{val_mae:.6f}",
            ])
            logf.flush()

            if val_loss < best_val:
                best_val = val_loss
                torch.save({
                    "model":      model.state_dict(),
                    "norm_stats": {k: list(v) for k, v in dataset._norm_stats.items()},
                    "features_l0_csv": _l0_csv_text,
                }, best_path)

            print(
                f"  epoch {epoch:4d}/{args.epochs}  "
                f"train_loss={train_loss:.4f}  val_loss={val_loss:.4f}  "
                f"train_mae={train_mae:.4f}  val_mae={val_mae:.4f}"
                + ("  *" if val_loss == best_val else ""),
                flush=True,
            )

            _probe_rng = random.Random(epoch)
            _n_types   = len(_celltype_order)
            _probe_n   = min(5, _n_types)
            _start     = ((epoch - 1) * _probe_n) % max(1, _n_types)
            _probe_types = [
                _celltype_order[(_start + i) % _n_types] for i in range(_probe_n)
            ]
            model.eval()
            with torch.no_grad():
                for _key in _probe_types:
                    _candidates = celltype_to_valpairs.get(_key, [])
                    if not _candidates:
                        continue
                    _pi, _node = _probe_rng.choice(_candidates)
                    _d, _y = dataset[_pi]
                    _d = _d.clone().to(device)
                    _yy = _y.clone().to(device)
                    _b = torch.zeros(_d.x.shape[0], dtype=torch.long, device=device)
                    _probe_model = getattr(model, '_orig_mod', model)
                    _probe_model.eval()
                    _dist = _probe_model(_d.x, _d.edge_index, _d.edge_attr,
                                         batch=_b, loss_mask=_d.loss_mask)
                    _label = float(_d.dist_label[_node])
                    _pred  = float(_dist[_node])
                    _ri, _rj = dataset._pairs[_pi]
                    _si = dataset._samples[_ri]
                    _sj = dataset._samples[_rj]
                    print(
                        f"     sample pair#{_pi:>6}  "
                        f"{_si['design']:<10} "
                        f"{_si['run_dir'].name}->{_sj['run_dir'].name:<10} "
                        f"cell={str(_key)[:20]:<20} "
                        f"label={_label:6.4f}  "
                        f"pred={_pred:6.4f}  "
                        f"diff={(_label - _pred):+7.4f}  "
                        f"|err|={abs(_pred-_label):6.4f}",
                        flush=True,
                    )
            model.train()

    print(f"\n[train_diff] Best val loss: {best_val:.4f}")
    print(f"[train_diff] Model saved:   {best_path}")
    print(f"[train_diff] Log:           {log_path}")

def main() -> None:
    p = argparse.ArgumentParser(
        description="Train DiffDemandGNN on ordered run-pair demand deltas."
    )
    p.add_argument("--exclude-runs-file", default=None,
                   help="file listing contaminated run dirs (one per line) to "
                        "exclude from the dataset scan")
    p.add_argument("--run-root",        required=True, nargs='+',
                   help="One or more design run-root dirs (multi-design supported).")
    p.add_argument("--relation",        required=True,
                   help="relation_clean.csv")
    p.add_argument("--l0",               required=True,
                   help="features.csv (L0 library cell features).")
    p.add_argument("--features",        default=None,
                   help="features.csv for mining candidates (optional).")
    p.add_argument("--out",             default="model_diff.pt",
                   help="Output model checkpoint path.")
    p.add_argument("--epochs",          type=int,   default=100)
    p.add_argument("--lr",              type=float, default=1e-4,
                   help="Peak learning rate (after warmup). Default 1e-4; "
                        "lower than 1e-3 to avoid first-epoch softplus blow-up.")
    p.add_argument("--warmup-epochs",   type=int,   default=5,
                   help="Linear warmup epochs from 0 to --lr (default 5).")
    p.add_argument("--init-from",       type=str, default=None,
                   help="Warm-start: load weights from this checkpoint before training.")
    p.add_argument("--weight-decay",    type=float, default=1e-4)
    p.add_argument("--hidden",          type=int,   default=128)
    p.add_argument("--heads",           type=int,   default=4)
    p.add_argument("--dropout",         type=float, default=0.1)
    p.add_argument("--val-frac",        type=float, default=0.2)
    p.add_argument("--val-design",      type=str, default=None,
                   help="If set, use this design's runs as val; all other designs as train (LODOCV).")
    p.add_argument("--batch-size",      type=int,   default=64)
    p.add_argument("--data-mode", choices=["cache", "lazy"], default="lazy",
                   help="lazy (default): compose each pair graph on the fly "
                        "in DataLoader workers (no cache file, <1GB RAM, "
                        "~2.2x slower epochs). cache: prewarm + on-disk item "
                        "cache (fastest epochs, ~165GB disk+RAM).")
    p.add_argument("--cache-graphs",    default=None,
                   help="Path to .pt cache for pre-built graphs. If file exists, "
                        "load it; otherwise build all graphs then save here.")
    p.add_argument("--prewarm-workers", type=int, default=-1,
                   help="Parallel processes for graph prewarm. -1 = cpu_count//2.")
    p.add_argument("--max-cache-size",     type=int,   default=100000,
                   help="Max graphs stored in _item_cache. 100000 caps RAM at ~313 GB "
                        "while caching most training pairs; -1 = unlimited (OOM risk).")
    p.add_argument("--prewarm-only",      action="store_true",
                   help="Build/save the graph cache then exit without training. "
                        "Run with CUDA_VISIBLE_DEVICES='' to avoid fork+CUDA deadlock.")
    p.add_argument("--max-delta",          type=int,   default=16)
    p.add_argument("--seed",               type=int,   default=42)
    p.add_argument("--require-cell-counts", action="store_true",
                   help="Skip runs that have only canonical_freq.csv (use only cell_counts.csv runs).")
    p.add_argument("--p-to-np",            default=None,
                   help="Global p→np canonical-class CSV "
                        "(mining_for_gat/global_p_to_np.csv). Enables "
                        "edge feature is_npn_class. Without it the model "
                        "falls back to is_npn_class == is_same_canonical.")
    p.add_argument("--area-label", action="store_true",
                   help="Weight labels by cpp_min (cell width). Default OFF "
                        "= count-fraction (count_B / Σ count_A). ON = "
                        "area-fraction (count_B * cpp_min / Σ count_A * cpp_min).")
    p.add_argument("--extra-features", action="store_true",
                   help="Append proxy_delay (= (max_stack_n+max_stack_p)*cpp_min) "
                        "and A-usage-rank-normalized to the design block. "
                        "Bumps node feature dim 42 -> 44. Requires a separate "
                        "cache file from baseline 42-d runs.")
    p.add_argument("--layers", type=int, default=3,
                   help="Number of GATv2Conv layers (default 3).")
    p.add_argument("--rank-weight", type=float, default=0.0,
                   help="Weight of pairwise rank loss (two-head only): "
                        "sampled (i,j) pairs per graph, hinge on sign(label_i-label_j) "
                        "vs sign(pred_i-pred_j). 0 disables.")
    p.add_argument("--lib-features", action="store_true",
                   help="Append pre-synthesis A/B library subset info: per "
                        "node is_in_A_lib + is_in_B_lib, plus 3 graph-level "
                        "broadcast counts (shared, A-only, B-only) / |L0|. "
                        "Bumps node feature dim by 5. Requires each run to "
                        "have PROVENANCE + juj_dir/data/dont_use.tcl.")
    p.add_argument("--num-workers", type=int, default=0,
                   help="DataLoader workers for parallel graph loading during training. "
                        "0 = single-process with main-thread cache (recommended). Workers disable"
                        "_item_cache to prevent per-fork RAM duplication.")
    args = p.parse_args()

    train(args)

if __name__ == "__main__":
    main()
