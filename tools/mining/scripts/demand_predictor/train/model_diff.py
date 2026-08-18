from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F

try:
    from torch_geometric.nn import GATv2Conv, global_mean_pool
    from torch_geometric.utils import softmax as pyg_softmax
except ImportError as e:
    raise ImportError(
        "torch_geometric is required.\n"
        "Install with: pip install torch_geometric"
    ) from e

DIFF_NODE_FEAT_DIM = 46
DIFF_EDGE_FEAT_DIM = 7

class DiffDemandGNN(nn.Module):

    def __init__(
        self,
        in_channels: int = DIFF_NODE_FEAT_DIM,
        edge_dim: int = DIFF_EDGE_FEAT_DIM,
        hidden: int = 128,
        heads: int = 4,
        dropout: float = 0.1,
        n_layers: int = 3,
        two_head: bool = False,
        scale_init_bias: float = 0.5413,
    ) -> None:
        super().__init__()

        self.n_layers = int(n_layers)
        self.two_head = bool(two_head)

        self.in_proj = nn.Linear(in_channels, hidden)
        nn.init.xavier_uniform_(self.in_proj.weight)
        nn.init.zeros_(self.in_proj.bias)

        self.convs = nn.ModuleList([
            GATv2Conv(
                hidden, hidden // heads,
                heads=heads, edge_dim=edge_dim,
                dropout=dropout, concat=True,
            )
            for _ in range(self.n_layers)
        ])

        if self.two_head:
            self.head_dist  = nn.Linear(hidden, 1)
            self.head_scale = nn.Linear(hidden, 1)
            nn.init.xavier_uniform_(self.head_dist.weight)
            nn.init.zeros_(self.head_dist.bias)
            nn.init.xavier_uniform_(self.head_scale.weight)
            nn.init.constant_(self.head_scale.bias, float(scale_init_bias))
        else:
            self.head = nn.Linear(hidden, 1)
            nn.init.xavier_uniform_(self.head.weight)
            nn.init.zeros_(self.head.bias)

    def forward(
        self,
        x: torch.Tensor,
        edge_index: torch.Tensor,
        edge_attr: torch.Tensor,
        batch: torch.Tensor | None = None,
        loss_mask: torch.Tensor | None = None,
    ):
        h = self.in_proj(x)
        for conv in self.convs:
            h = F.elu(conv(h, edge_index, edge_attr)) + h

        if not self.two_head:
            raw = self.head(h).squeeze(-1)
            if batch is None or loss_mask is None:
                return raw
            masked = raw.masked_fill(~(loss_mask > 0.5), float("-inf"))
            return pyg_softmax(masked, batch)

        assert batch is not None, "two_head requires batch indices"
        assert loss_mask is not None, "two_head requires loss_mask"

        raw_dist = self.head_dist(h).squeeze(-1)
        mask_kept = loss_mask > 0.5
        masked_logits = raw_dist.masked_fill(~mask_kept, float("-inf"))
        dist = pyg_softmax(masked_logits, batch)

        pooled = global_mean_pool(h, batch)
        raw_T  = self.head_scale(pooled).squeeze(-1)
        scale  = F.softplus(raw_T)

        scale_per_node = scale[batch]
        pred = dist * scale_per_node
        return dist, scale, pred

def diff_loss(logits: torch.Tensor, y: torch.Tensor,
              delta: float = 0.1,
              weights: torch.Tensor | None = None) -> torch.Tensor:
    if logits.numel() == 0:
        return logits.new_zeros(())
    per = F.huber_loss(logits, y, reduction="none", delta=delta)
    if weights is not None:
        per = per * weights
    return per.mean()

def two_head_loss(
    pred_dist: torch.Tensor,
    pred_scale: torch.Tensor,
    dist_label: torch.Tensor,
    scale_label: torch.Tensor,
    loss_mask: torch.Tensor,
    batch: torch.Tensor,
    scale_weight: float = 1.0,
    rank_weight: float = 0.0,
    dist_weight: float = 1.0,
    rank_margin: float = 0.02,
    n_rank_pairs: int = 16,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    mask_kept = loss_mask > 0.5
    EPS = 1e-9

    if mask_kept.any():
        lbl = dist_label[mask_kept].clamp(min=0.0)
        prd = pred_dist[mask_kept].clamp(min=EPS)
        L_dist = -(lbl * torch.log(prd)).sum() / float(pred_scale.numel())
    else:
        L_dist = pred_dist.new_zeros(())

    L_scale = F.mse_loss(pred_scale, scale_label)

    if rank_weight > 0.0 and mask_kept.any():
        idx_all = torch.arange(pred_dist.shape[0], device=pred_dist.device)
        n_pairs_total = 0
        loss_acc = pred_dist.new_zeros(())
        for g in range(pred_scale.shape[0]):
            in_g = (batch == g) & mask_kept
            cand = idx_all[in_g]
            if cand.numel() < 2:
                continue
            k = min(int(n_rank_pairs), cand.numel() * (cand.numel() - 1))
            ii = cand[torch.randint(cand.numel(), (k,), device=pred_dist.device)]
            jj = cand[torch.randint(cand.numel(), (k,), device=pred_dist.device)]
            sel = ii != jj
            if not sel.any():
                continue
            ii, jj = ii[sel], jj[sel]
            lbl_diff = dist_label[ii] - dist_label[jj]
            prd_diff = pred_dist[ii]  - pred_dist[jj]
            sgn = torch.sign(lbl_diff)
            loss_acc = loss_acc + torch.clamp(rank_margin - sgn * prd_diff, min=0.0).sum()
            n_pairs_total += int(sel.sum().item())
        L_rank = loss_acc / max(n_pairs_total, 1)
    else:
        L_rank = pred_dist.new_zeros(())

    total = float(dist_weight) * L_dist + float(scale_weight) * L_scale + float(rank_weight) * L_rank
    return total, L_dist, L_scale, L_rank

def demand_loss(
    pred_dist: torch.Tensor,
    dist_label: torch.Tensor,
    loss_mask: torch.Tensor,
    batch: torch.Tensor,
    rank_weight: float = 0.0,
    dist_weight: float = 1.0,
    rank_margin: float = 0.02,
    n_rank_pairs: int = 16,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    mask_kept = loss_mask > 0.5
    EPS = 1e-9
    n_graphs = int(batch.max().item()) + 1 if batch.numel() > 0 else 1

    if mask_kept.any():
        lbl = dist_label[mask_kept].clamp(min=0.0)
        prd = pred_dist[mask_kept].clamp(min=EPS)
        L_dist = -(lbl * torch.log(prd)).sum() / float(n_graphs)
    else:
        L_dist = pred_dist.new_zeros(())

    if rank_weight > 0.0 and mask_kept.any():
        idx_all = torch.arange(pred_dist.shape[0], device=pred_dist.device)
        n_pairs_total = 0
        loss_acc = pred_dist.new_zeros(())
        for g in range(n_graphs):
            in_g = (batch == g) & mask_kept
            cand = idx_all[in_g]
            if cand.numel() < 2:
                continue
            k = min(int(n_rank_pairs), cand.numel() * (cand.numel() - 1))
            ii = cand[torch.randint(cand.numel(), (k,), device=pred_dist.device)]
            jj = cand[torch.randint(cand.numel(), (k,), device=pred_dist.device)]
            sel = ii != jj
            if not sel.any():
                continue
            ii, jj = ii[sel], jj[sel]
            lbl_diff = dist_label[ii] - dist_label[jj]
            prd_diff = pred_dist[ii] - pred_dist[jj]
            sgn = torch.sign(lbl_diff)
            loss_acc = loss_acc + torch.clamp(rank_margin - sgn * prd_diff, min=0.0).sum()
            n_pairs_total += int(sel.sum().item())
        L_rank = loss_acc / max(n_pairs_total, 1)
    else:
        L_rank = pred_dist.new_zeros(())

    total = float(dist_weight) * L_dist + float(rank_weight) * L_rank
    return total, L_dist, L_rank

def direction_accuracy(
    logits: torch.Tensor,
    y: torch.Tensor,
    min_abs: float = 1e-4,
) -> float:
    mask = y.abs() > min_abs
    if mask.sum() == 0:
        return float("nan")
    return ((logits[mask] > 0) == (y[mask] > 0)).float().mean().item()
