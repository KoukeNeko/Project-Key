"""Prototype: rerank Project Key's sentence with a causal language model.

Usage: rerank.py <model> [--slip row|column] [--penalty P ...] [--top K]

The engine (adapters/projectkey.cpp with PK_CANDIDATES) gives each segment's
candidates of the same length. Starting from the engine's sentence, each round
tries every one-segment substitution and keeps the sentence scoring highest,
log P(sentence) under the model minus penalty per segment changed from the
engine's word, until nothing improves (at most three rounds). Prints, per
penalty, whole sentences right and character accuracy, as evaluate.py counts them.
"""
import argparse
import csv
import os
import random
import subprocess

import torch
from transformers import AutoModelForCausalLM, AutoTokenizer

from evaluate import CORPUS, char_errors, engines, slip, to_keys

ROUNDS = 3


def engine_segments(rows):
    command = [str(part) for part in engines()["projectkey"]]
    keys = "".join(row["keys"] + "\n" for row in rows)
    result = subprocess.run(command, input=keys, capture_output=True, text=True, check=True,
                            env={**os.environ, "PK_CANDIDATES": "1"})
    return [[segment.split("|") for segment in line.split("\t")[2].split("/")] for line in result.stdout.splitlines()]


class Scorer:
    def __init__(self, name):
        self.device = "mps" if torch.backends.mps.is_available() else "cpu"
        self.tokenizer = AutoTokenizer.from_pretrained(name)
        # Some tokenizers (feng-30m-zh-tw) declare no special tokens but have Qwen's.
        end = self.tokenizer.eos_token or "<|endoftext|>"
        if self.tokenizer.pad_token is None:
            self.tokenizer.pad_token = end
        # Models without a start token score from a leading end-of-text, so the first character counts too.
        starts = self.tokenizer("一")["input_ids"][0] != self.tokenizer("一", add_special_tokens=False)["input_ids"][0]
        self.prefix = "" if starts else end
        self.model = AutoModelForCausalLM.from_pretrained(name).to(self.device).eval()

    def known(self, text):
        return self.tokenizer.unk_token_id not in self.tokenizer(text, add_special_tokens=False)["input_ids"]

    @torch.no_grad()
    def log_probs(self, texts):
        """Natural-log P(text) for each text, its end included."""
        batch = self.tokenizer([self.prefix + t for t in texts], return_tensors="pt", padding=True).to(self.device)
        logits = self.model(**batch).logits[:, :-1]
        targets = batch["input_ids"][:, 1:]
        mask = batch["attention_mask"][:, 1:]
        picked = torch.log_softmax(logits.float(), dim=-1).gather(2, targets.unsqueeze(2)).squeeze(2)
        return (picked * mask).sum(1).tolist()


def rerank(scorer, segments, top, penalty):
    options = [[c for i, c in enumerate(s[:top]) if i == 0 or scorer.known(c)] for s in segments]
    choice = [0] * len(options)
    cache = {}

    def score(picks):
        key = tuple(picks)
        return cache[key] - penalty * sum(p > 0 for p in picks)

    def fill(variants):
        missing = [v for v in variants if tuple(v) not in cache]
        if missing:
            for v, lp in zip(missing, scorer.log_probs(["".join(options[i][p] for i, p in enumerate(v)) for v in missing])):
                cache[tuple(v)] = lp

    fill([choice])
    for _ in range(ROUNDS):
        variants = [choice[:i] + [p] + choice[i + 1:] for i in range(len(options)) for p in range(len(options[i])) if p != choice[i]]
        fill(variants)
        best = max(variants, key=score, default=choice)
        if score(best) <= score(choice):
            break
        choice = best
    return "".join(options[i][p] for i, p in enumerate(choice))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model")
    parser.add_argument("--slip", choices=["row", "column"])
    parser.add_argument("--penalty", type=float, nargs="+", default=[0, 2, 4, 6, 8])
    parser.add_argument("--top", type=int, default=8)
    args = parser.parse_args()
    with CORPUS.open() as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    rng = random.Random(0)
    for row in rows:
        keys = to_keys(row["zhuyin"])
        row["keys"] = slip(keys, args.slip, rng) if args.slip else keys
    segments = engine_segments(rows)
    scorer = Scorer(args.model)
    chars = sum(len(row["sentence"]) for row in rows)
    base = ["".join(s[0] for s in segs) for segs in segments]
    print(f"engine: {sum(r['sentence'] == t for r, t in zip(rows, base))}/{len(rows)}")
    for penalty in args.penalty:
        texts = [rerank(scorer, segs, args.top, penalty) for segs in segments]
        exact = sum(row["sentence"] == text for row, text in zip(rows, texts))
        accuracy = 1 - sum(char_errors(row["sentence"], text) for row, text in zip(rows, texts)) / chars
        fixed = sum(r["sentence"] == t != b for r, t, b in zip(rows, texts, base))
        broken = sum(r["sentence"] == b != t for r, t, b in zip(rows, texts, base))
        print(f"penalty {penalty}: {exact}/{len(rows)} ({accuracy:.2%}), fixed {fixed}, broken {broken}")


if __name__ == "__main__":
    main()
