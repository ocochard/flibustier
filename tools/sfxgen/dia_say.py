#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026, Olivier Cochard-Labbe
#
"""Voice lines with Dia2 on the GPU host (copied there by voicegen.py).

    dia_say.py REPO CFG TEMPERATURE JOBS.json [VOICE.wav]

JOBS.json lists {"out": path, "seed": n, "text": line}; the model loads
once for all of them.  VOICE.wav, a few seconds of speech, gives every
line the same speaker (Dia2 transcribes it with Whisper).
"""
import functools
import json
import sys

import torch
import dia2.runtime.voice_clone as voice_clone
from dia2 import Dia2, GenerationConfig, SamplingConfig

# Dia2 transcribes the reference voice with whisper_timestamped on every
# line, which reloads Whisper and reseeds torch with 1234: every take
# would come out the same whatever its seed.  Transcribe once, without
# reseeding.
_transcribe = voice_clone.transcribe_words


@functools.lru_cache(maxsize=None)
def _once(audio_path, device, language=None):
    import whisper_timestamped as wts
    state = torch.random.get_rng_state()
    orig = wts.transcribe
    wts.transcribe = functools.partial(orig, seed=None)
    try:
        return _transcribe(audio_path, device, language)
    finally:
        wts.transcribe = orig
        torch.random.set_rng_state(state)


voice_clone.transcribe_words = lambda audio_path, device, language=None: \
    list(_once(audio_path, device, language))

repo, cfg, temp, jobs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), \
    json.load(open(sys.argv[4]))
voice = sys.argv[5] if len(sys.argv) > 5 else None
dia = Dia2.from_repo(repo, device="cuda", dtype="bfloat16")
config = GenerationConfig(cfg_scale=cfg, audio=SamplingConfig(temperature=temp))
for j in jobs:
    torch.manual_seed(j["seed"])
    if voice:
        dia.generate(j["text"], config=config, output_wav=j["out"],
                     prefix_speaker_1=voice, include_prefix=False)
    else:
        dia.generate(j["text"], config=config, output_wav=j["out"])
    print("wrote", j["out"], flush=True)
