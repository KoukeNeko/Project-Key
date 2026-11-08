"""Writes a train.py model as sentence.bin for core/engine/SentenceModel.

Usage: export.py <model dir> <sentence.bin>

Layout, little endian: magic "PKSM0001", uint32 vocabulary size, layers, heads,
embedding size, context; the vocabulary as one uint32 code point per id (0 for
the special tokens); then float16 weights in the order SentenceModel reads them:
token and position embeddings, per layer ln_1 (weight, bias), attention
(c_attn weight [embd][3 embd], bias, c_proj weight [embd][embd], bias), ln_2,
MLP (c_fc weight [embd][4 embd], bias, c_proj weight [4 embd][embd], bias), and
ln_f. The output layer is the token embedding (tied), as in GPT-2.
"""
import struct
import sys

import numpy as np
from transformers import BertTokenizerFast, GPT2LMHeadModel

model_dir, out = sys.argv[1:]
model = GPT2LMHeadModel.from_pretrained(model_dir)
tokenizer = BertTokenizerFast.from_pretrained(model_dir)
config = model.config
assert config.activation_function == "gelu_new" and config.tie_word_embeddings
vocab = [0] * config.vocab_size
for token, i in tokenizer.get_vocab().items():
    if len(token) == 1:
        vocab[i] = ord(token)
state = model.state_dict()
names = ["transformer.wte.weight", "transformer.wpe.weight"]
for layer in range(config.n_layer):
    p = f"transformer.h.{layer}."
    names += [p + n for n in ("ln_1.weight", "ln_1.bias", "attn.c_attn.weight", "attn.c_attn.bias", "attn.c_proj.weight",
                              "attn.c_proj.bias", "ln_2.weight", "ln_2.bias", "mlp.c_fc.weight", "mlp.c_fc.bias",
                              "mlp.c_proj.weight", "mlp.c_proj.bias")]
names += ["transformer.ln_f.weight", "transformer.ln_f.bias"]
with open(out, "wb") as f:
    f.write(b"PKSM0001")
    f.write(struct.pack("<5I", config.vocab_size, config.n_layer, config.n_head, config.n_embd, config.n_positions))
    f.write(np.asarray(vocab, dtype="<u4").tobytes())
    for name in names:
        f.write(state[name].detach().float().numpy().astype("<f2").tobytes())
print(f"{out}: {sum(state[n].numel() for n in names) / 1e6:.1f}M weights")
