"""Trains a character-level GPT-2 on prepare.py's output.

Usage: train.py <data dir> <out dir> --size small|medium [--epochs N]

Saves a model and tokenizer that transformers' Auto classes load, so
tools/corpus-evaluator/rerank.py can score with it. The tokenizer is
BertTokenizerFast on prepare.py's vocabulary: it splits Han text into single
characters and wraps each text in [CLS] ... [SEP], as the training data is.
"""
import argparse
import math
import time

import numpy as np
import torch
from transformers import BertTokenizerFast, GPT2Config, GPT2LMHeadModel

SIZES = {"small": dict(n_embd=384, n_layer=6, n_head=6), "medium": dict(n_embd=512, n_layer=8, n_head=8)}
CONTEXT = 64  # evaluation sentences are 5 to 16 characters
BATCH = 256  # 512 needs 15 GiB of logits and gradients, past a 12 GB card
PEAK_LR, WARMUP = 1e-3, 1000
VALID_BATCHES = 50


def batches(data, generator):
    starts = torch.randint(len(data) - CONTEXT, (BATCH,), generator=generator).numpy()
    return torch.from_numpy(np.stack([data[s:s + CONTEXT] for s in starts]).astype(np.int64)).cuda()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("data")
    parser.add_argument("out")
    parser.add_argument("--size", choices=SIZES, required=True)
    parser.add_argument("--epochs", type=float, default=2)
    args = parser.parse_args()

    train = np.memmap(f"{args.data}/train.bin", dtype=np.uint16, mode="r")
    valid = np.memmap(f"{args.data}/valid.bin", dtype=np.uint16, mode="r")
    tokenizer = BertTokenizerFast(f"{args.data}/vocab.txt", do_lower_case=False)
    config = GPT2Config(vocab_size=len(tokenizer), n_positions=CONTEXT, bos_token_id=tokenizer.cls_token_id,
                        eos_token_id=tokenizer.sep_token_id, pad_token_id=tokenizer.pad_token_id, **SIZES[args.size])
    model = GPT2LMHeadModel(config).cuda()
    print(f"{sum(p.numel() for p in model.parameters()) / 1e6:.1f}M parameters")

    steps = int(args.epochs * len(train) / (BATCH * CONTEXT))
    decay = [p for p in model.parameters() if p.dim() >= 2]
    rest = [p for p in model.parameters() if p.dim() < 2]
    optimizer = torch.optim.AdamW([{"params": decay, "weight_decay": 0.1}, {"params": rest, "weight_decay": 0}],
                                  lr=PEAK_LR, betas=(0.9, 0.95), fused=True)
    schedule = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda step: min(1, (step + 1) / WARMUP) * (
        0.1 + 0.45 * (1 + math.cos(math.pi * min(1, step / steps)))))
    generator, fixed = torch.Generator().manual_seed(0), torch.Generator()

    def validate():
        model.eval()
        fixed.manual_seed(1)
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            losses = [model(x, labels=x).loss.item() for x in (batches(valid, fixed) for _ in range(VALID_BATCHES))]
        model.train()
        return sum(losses) / len(losses)

    started = time.time()
    for step in range(steps):
        x = batches(train, generator)
        with torch.autocast("cuda", dtype=torch.bfloat16):
            loss = model(x, labels=x).loss
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optimizer.step()
        schedule.step()
        optimizer.zero_grad(set_to_none=True)
        if step % 1000 == 0 or step == steps - 1:
            print(f"step {step}/{steps} train {loss.item():.3f} valid {validate():.3f} "
                  f"{(time.time() - started) / 60:.1f} min", flush=True)
    model.save_pretrained(args.out)
    tokenizer.save_pretrained(args.out)


if __name__ == "__main__":
    main()
