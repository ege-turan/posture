import re
import numpy as np

# paths relative to ai/train
VOCAB_H = "../generated/ai_vocab.h"
MODEL_H = "../generated/ai_model_data.h"

TOKEN_RE = re.compile(r"[a-z0-9']+")

LABELS = ["low", "medium", "high"]

def normalize_text(s: str) -> str:
    s = s.lower()
    s = re.sub(r"(https?://\S+)|(\bwww\.\S+)", " <url> ", s)
    s = re.sub(r"\b[\w\.-]+@[\w\.-]+\.\w+\b", " <email> ", s)
    s = re.sub(r"\b\d+\b", " <num> ", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s

def tokenize(s: str):
    return TOKEN_RE.findall(s)

def fnv1a_32(s: str) -> int:
    h = 0x811c9dc5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xffffffff
    return h

def parse_c_array_int8(text, name):
    m = re.search(rf"static const int8_t {name}\[\d+\] = \{{(.*?)\}};", text, re.S)
    if not m: raise RuntimeError(f"Couldn't find {name}")
    nums = re.findall(r"-?\d+", m.group(1))
    return np.array([int(x) for x in nums], dtype=np.int8)

def parse_c_array_int32(text, name):
    m = re.search(rf"static const int32_t {name}\[\d+\] = \{{(.*?)\}};", text, re.S)
    if not m: raise RuntimeError(f"Couldn't find {name}")
    nums = re.findall(r"-?\d+", m.group(1))
    return np.array([int(x) for x in nums], dtype=np.int32)

def parse_c_array_u32(text, name):
    m = re.search(rf"static const uint32_t {name}\[\d+\] = \{{(.*?)\}};", text, re.S)
    if not m: raise RuntimeError(f"Couldn't find {name}")
    nums = re.findall(r"0x[0-9a-fA-F]+", m.group(1))
    return np.array([int(x,16) for x in nums], dtype=np.uint32)

def parse_vocab_size(text):
    m = re.search(r"#define AI_VOCAB_SIZE (\d+)", text)
    return int(m.group(1))

def parse_dim(text):
    m = re.search(r"#define AI_EMB_DIM (\d+)", text)
    return int(m.group(1))

def load():
    with open(VOCAB_H, "r", encoding="utf-8") as f:
        vtxt = f.read()
    with open(MODEL_H, "r", encoding="utf-8") as f:
        mtxt = f.read()

    V = parse_vocab_size(vtxt)
    D = parse_dim(mtxt)

    hashes = parse_c_array_u32(vtxt, "ai_vocab_hashes")
    emb = parse_c_array_int8(mtxt, "ai_emb_q").reshape(V, D)
    fc  = parse_c_array_int8(mtxt, "ai_fc_q").reshape(3, D)
    b   = parse_c_array_int32(mtxt, "ai_fc_bias_q")

    return hashes, emb, fc, b

def vocab_lookup_id(hashes, tok):
    h = fnv1a_32(tok)
    # linear scan matches your C code
    for i in range(1, len(hashes)):
        if hashes[i] == h:
            return i
    return 0

def infer(text, hashes, emb, fc, b, max_tokens=40):
    toks = tokenize(normalize_text(text))[:max_tokens]
    if not toks: toks = ["<unk>"]

    acc = np.zeros((emb.shape[1],), dtype=np.int32)
    for t in toks:
        tid = vocab_lookup_id(hashes, t)
        acc += emb[tid].astype(np.int32)
    acc //= max(len(toks), 1)

    logits = b.astype(np.int32) + (fc.astype(np.int32) @ acc.astype(np.int32))
    pred = int(np.argmax(logits))
    return pred, logits

if __name__ == "__main__":
    hashes, emb, fc, b = load()

    tests = [
        "Target: 50% OFF everything today only!!!",
        "Calendar: Design Review moved from 3:00pm to 4:00pm on Wed.",
        "Meeting moved up: Demo Prep now at 2:00pm (in 10 min)!!",
        "can u hop on a call asap??",
        "FYI I sent the files over. No rush.",
    ]

    for t in tests:
        pred, logits = infer(t, hashes, emb, fc, b)
        print(f"[{LABELS[pred]}] {t}")
        print(f"  logits={logits.tolist()}")
