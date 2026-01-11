#!/usr/bin/env python3
"""
Train an embedding-avg + linear classifier, then export quantized int8 weights to C headers.

Model:
  tokens -> embeddings (V x D)
  average embeddings over tokens -> D vector
  linear head -> 3 logits (low/medium/high)

Export:
  - vocab tokens (C string table) + token hashes
  - embedding matrix int8
  - classifier weights int8, bias int32
  - quant scales (float) for dequant at inference (or fixed-point scaling)

Requires: python3, numpy, pandas, torch
"""

import argparse
import math
import re
import json
from dataclasses import dataclass
from typing import List, Dict, Tuple

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.optim as optim


LABELS = ["low", "medium", "high"]
LABEL_TO_ID = {l: i for i, l in enumerate(LABELS)}

TOKEN_RE = re.compile(r"[a-z0-9']+")


def normalize_text(s: str) -> str:
    s = s.lower()
    # collapse URLs and emails to placeholders
    s = re.sub(r"(https?://\S+)|(\bwww\.\S+)", " <url> ", s)
    s = re.sub(r"\b[\w\.-]+@[\w\.-]+\.\w+\b", " <email> ", s)
    # collapse numbers
    s = re.sub(r"\b\d+\b", " <num> ", s)
    # normalize whitespace
    s = re.sub(r"\s+", " ", s).strip()
    return s


def tokenize(s: str) -> List[str]:
    return TOKEN_RE.findall(s)


def fnv1a_32(s: str) -> int:
    # FNV-1a 32-bit
    h = 0x811c9dc5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xffffffff
    return h


@dataclass
class Vocab:
    token_to_id: Dict[str, int]
    id_to_token: List[str]
    hashes: np.ndarray  # uint32

    @property
    def size(self) -> int:
        return len(self.id_to_token)


def build_vocab(texts: List[str], vocab_size: int) -> Vocab:
    from collections import Counter
    c = Counter()
    for t in texts:
        toks = tokenize(normalize_text(t))
        c.update(toks)

    # reserve 0 for <unk>
    id_to_token = ["<unk>"]
    # add some useful placeholders if present
    for special in ["<url>", "<email>", "<num>"]:
        if special in c:
            id_to_token.append(special)

    for tok, _ in c.most_common(vocab_size - len(id_to_token)):
        if tok not in id_to_token:
            id_to_token.append(tok)

    token_to_id = {t: i for i, t in enumerate(id_to_token)}
    hashes = np.array([fnv1a_32(t) for t in id_to_token], dtype=np.uint32)
    return Vocab(token_to_id, id_to_token, hashes)


def encode_text(vocab: Vocab, text: str, max_tokens: int) -> List[int]:
    toks = tokenize(normalize_text(text))
    ids = []
    for tok in toks[:max_tokens]:
        ids.append(vocab.token_to_id.get(tok, 0))
    if not ids:
        ids = [0]
    return ids


class EmbAvgClassifier(nn.Module):
    def __init__(self, vocab_size: int, dim: int, num_classes: int):
        super().__init__()
        self.emb = nn.Embedding(vocab_size, dim)
        self.fc = nn.Linear(dim, num_classes)

    def forward(self, x_ids, lengths):
        # x_ids: (B, T), lengths: (B,)
        e = self.emb(x_ids)  # (B, T, D)

        B, T = x_ids.shape
        device = x_ids.device

        # mask: True for real tokens, False for padding positions
        mask = (torch.arange(T, device=device).unsqueeze(0) < lengths.unsqueeze(1)).float().unsqueeze(-1)  # (B,T,1)

        e = e * mask
        denom = mask.sum(dim=1).clamp(min=1.0)  # (B,1)
        avg = e.sum(dim=1) / denom  # (B,D)
        return self.fc(avg)



def make_batch(seqs: List[List[int]], labels: List[int]) -> Tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    maxlen = max(len(s) for s in seqs)
    B = len(seqs)
    x = torch.zeros((B, maxlen), dtype=torch.long)  # PAD WITH 0
    lengths = torch.zeros((B,), dtype=torch.long)
    for i, s in enumerate(seqs):
        lengths[i] = len(s)
        x[i, :len(s)] = torch.tensor(s, dtype=torch.long)
    y = torch.tensor(labels, dtype=torch.long)
    return x, lengths, y


def quantize_int8_per_tensor(w: np.ndarray) -> Tuple[np.ndarray, float]:
    # symmetric per-tensor quant
    maxv = np.max(np.abs(w))
    if maxv < 1e-12:
        scale = 1.0
        q = np.zeros_like(w, dtype=np.int8)
        return q, scale
    scale = maxv / 127.0
    q = np.clip(np.round(w / scale), -127, 127).astype(np.int8)
    return q, float(scale)


def export_header(path: str, guard: str, body: str):
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(body)
        f.write(f"\n\n#endif // {guard}\n")


def c_array_int8(name: str, arr: np.ndarray) -> str:
    flat = arr.flatten()
    lines = []
    lines.append(f"static const int8_t {name}[{flat.size}] = {{")
    for i in range(0, flat.size, 16):
        chunk = ", ".join(str(int(x)) for x in flat[i:i+16])
        lines.append(f"  {chunk},")
    lines.append("};")
    return "\n".join(lines)


def c_array_int32(name: str, arr: np.ndarray) -> str:
    flat = arr.flatten()
    lines = []
    lines.append(f"static const int32_t {name}[{flat.size}] = {{")
    for i in range(0, flat.size, 8):
        chunk = ", ".join(str(int(x)) for x in flat[i:i+8])
        lines.append(f"  {chunk},")
    lines.append("};")
    return "\n".join(lines)


def c_array_u32(name: str, arr: np.ndarray) -> str:
    flat = arr.flatten()
    lines = []
    lines.append(f"static const uint32_t {name}[{flat.size}] = {{")
    for i in range(0, flat.size, 8):
        chunk = ", ".join(f"0x{int(x):08x}" for x in flat[i:i+8])
        lines.append(f"  {chunk},")
    lines.append("};")
    return "\n".join(lines)


def c_string_table(name: str, strs: List[str]) -> str:
    lines = []
    lines.append(f"static const char* {name}[{len(strs)}] = {{")
    for s in strs:
        esc = s.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'  "{esc}",')
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="CSV with columns: text,label")
    ap.add_argument("--out_dir", required=True, help="output directory for generated headers")
    ap.add_argument("--vocab_size", type=int, default=2000)
    ap.add_argument("--dim", type=int, default=32)
    ap.add_argument("--max_tokens", type=int, default=40)
    ap.add_argument("--epochs", type=int, default=12)
    ap.add_argument("--lr", type=float, default=1e-2)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    df = pd.read_csv(args.data)
    assert "text" in df.columns and "label" in df.columns
    texts = df["text"].astype(str).tolist()
    labels = [LABEL_TO_ID[str(x).strip().lower()] for x in df["label"].tolist()]

    vocab = build_vocab(texts, args.vocab_size)

    # encode all
    seqs = [encode_text(vocab, t, args.max_tokens) for t in texts]

    # train/val split
    idx = np.arange(len(seqs))
    np.random.shuffle(idx)
    split = int(0.85 * len(idx))
    tr_idx, va_idx = idx[:split], idx[split:]

    tr_seqs = [seqs[i] for i in tr_idx]
    tr_y = [labels[i] for i in tr_idx]
    va_seqs = [seqs[i] for i in va_idx]
    va_y = [labels[i] for i in va_idx]

    model = EmbAvgClassifier(vocab.size, args.dim, len(LABELS))
    criterion = nn.CrossEntropyLoss()
    opt = optim.Adam(model.parameters(), lr=args.lr)


    def eval_acc():
        model.eval()
        correct = 0
        total = 0
        with torch.no_grad():
            for i in range(0, len(va_seqs), 64):
                batch_seqs = va_seqs[i:i+64]
                batch_y = va_y[i:i+64]
                xb, lengths, yb = make_batch(batch_seqs, batch_y)
                logits = model(xb, lengths)
                pred = torch.argmax(logits, dim=1)
                correct += int((pred == yb).sum().item())
                total += len(batch_y)
        return correct / max(total, 1)


    # training
    for ep in range(args.epochs):
        model.train()
        # mini-batches
        perm = np.random.permutation(len(tr_seqs))
        for i in range(0, len(perm), 64):
            bidx = perm[i:i+64]
            batch_seqs = [tr_seqs[j] for j in bidx]
            batch_y = [tr_y[j] for j in bidx]
            xb, lengths, yb = make_batch(batch_seqs, batch_y)
            opt.zero_grad()
            logits = model(xb, lengths)
            loss = criterion(logits, yb)
            loss.backward()
            opt.step()

        acc = eval_acc()
        print(f"epoch {ep+1}/{args.epochs} val_acc={acc:.3f}")

    # Extract float weights
    emb_w = model.emb.weight.detach().cpu().numpy().astype(np.float32)         # (V, D)
    fc_w = model.fc.weight.detach().cpu().numpy().astype(np.float32)          # (C, D)
    fc_b = model.fc.bias.detach().cpu().numpy().astype(np.float32)            # (C,)

    # Quantize
    emb_q, emb_scale = quantize_int8_per_tensor(emb_w)
    fc_q, fc_scale = quantize_int8_per_tensor(fc_w)

    # Bias: we want bias in int32 in the same "accumulator" scale.
    # Accumulator scale = emb_scale * fc_scale (since dot(int8,int8) accumulates into int32).
    acc_scale = emb_scale * fc_scale
    fc_b_q = np.round(fc_b / acc_scale).astype(np.int32)

    meta = {
        "labels": LABELS,
        "vocab_size": int(vocab.size),
        "dim": int(args.dim),
        "max_tokens": int(args.max_tokens),
        "emb_scale": emb_scale,
        "fc_scale": fc_scale,
        "acc_scale": acc_scale,
        "token_hash": "fnv1a_32",
    }

    import os
    os.makedirs(args.out_dir, exist_ok=True)

    # Export vocab header
    vocab_body = "\n".join([
        "#include <stdint.h>",
        "",
        f"#define AI_VOCAB_SIZE {vocab.size}",
        "",
        c_array_u32("ai_vocab_hashes", vocab.hashes),
    ])
    export_header(os.path.join(args.out_dir, "ai_vocab.h"), "AI_VOCAB_H", vocab_body)

    # Export model header
    model_body = "\n".join([
        "#include <stdint.h>",
        "",
        f"#define AI_NUM_CLASSES {len(LABELS)}",
        f"#define AI_EMB_DIM {args.dim}",
        "",
        f"static const float ai_emb_scale = {emb_scale:.10g}f;",
        f"static const float ai_fc_scale  = {fc_scale:.10g}f;",
        f"static const float ai_acc_scale = {acc_scale:.10g}f;",
        "",
        c_array_int8("ai_emb_q", emb_q),
        "",
        c_array_int8("ai_fc_q", fc_q),
        "",
        c_array_int32("ai_fc_bias_q", fc_b_q),
    ])
    export_header(os.path.join(args.out_dir, "ai_model_data.h"), "AI_MODEL_DATA_H", model_body)

    # Export metadata for debugging
    with open(os.path.join(args.out_dir, "ai_model_meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print("Exported to:", args.out_dir)
    print("Meta:", meta)


if __name__ == "__main__":
    main()
