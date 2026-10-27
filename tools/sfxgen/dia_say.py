#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
"""Voice lines with Dia2 on the GPU host (copied there by voicegen.py).

    dia_say.py REPO CFG TEMPERATURE JOBS.json

JOBS.json lists {"out": path, "seed": n, "text": line}; the model loads
once for all of them.
"""
import json
import sys

import torch
from dia2 import Dia2, GenerationConfig, SamplingConfig

repo, cfg, temp, jobs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), \
    json.load(open(sys.argv[4]))
dia = Dia2.from_repo(repo, device="cuda", dtype="bfloat16")
config = GenerationConfig(cfg_scale=cfg, audio=SamplingConfig(temperature=temp))
for j in jobs:
    torch.manual_seed(j["seed"])
    dia.generate(j["text"], config=config, output_wav=j["out"])
    print("wrote", j["out"], flush=True)
