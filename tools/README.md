# Generator hosts

`tools/artgen/` (the open pack's art) and `tools/sfxgen/` (its sound
effects) send their work to a ComfyUI server, and artgen runs its 3D
models (ships, fencers, the landing party) in Blender on a render host
over ssh.  Neither needs to be the machine running the scripts.

- `--server URL` (or `FLIBUSTIER_COMFYUI`): the ComfyUI server,
  `http://framework2:8188` by default.
- `--render-host HOST` (artgen only): the Blender host, `framework2` by
  default, reached with `ssh -o BatchMode=yes` (a key, no password).

## ComfyUI

A recent ComfyUI: artgen uses only core nodes (among them
`QwenImageDiffsynthControlnet`, `ModelPatchLoader` and
`LoadBackgroundRemovalModel`), no custom nodes.  On an AMD GPU with
ROCm, PyTorch from its own wheel index:

```sh
git clone https://github.com/Comfy-Org/ComfyUI.git
cd ComfyUI
python3 -m venv venv
. venv/bin/activate
pip install torch torchvision --index-url https://download.pytorch.org/whl/rocm7.14
pip install -r requirements.txt
python -c "import torch; print(torch.__version__, torch.cuda.is_available())"
python main.py --listen 0.0.0.0
```

Check that `torch.__version__` still names the ROCm build after
`requirements.txt` (a CUDA build from PyPI may replace it).  On a
shared-memory GPU (AMD Strix Halo) leave out `--highvram` and
`--disable-smart-memory`: artgen frees the models itself between passes
(`--keep-loaded` keeps them).

The models, each in its directory under `ComfyUI/models/` (the names are
those of `MODELS` in `artgen/artgen.py` and of `[render]` in
`sfxgen/sounds.toml`; about 57 GB in all):

| Directory | File | From (huggingface.co) | Used for |
|---|---|---|---|
| `diffusion_models` | `qwen_image_2512_fp8_e4m3fn.safetensors` | `Comfy-Org/Qwen-Image` | every picture |
| `text_encoders` | `qwen_2.5_vl_7b_fp8_scaled.safetensors` | `Comfy-Org/Qwen-Image` | every picture |
| `vae` | `qwen_image_vae.safetensors` | `Comfy-Org/Qwen-Image` | every picture |
| `loras` | `Qwen-Image-2512-Lightning-8steps-V1.0-bf16.safetensors` | `lightx2v/Qwen-Image-2512-Lightning` | `--draft` |
| `model_patches` | `qwen_image_canny_diffsynth_controlnet.safetensors` | `Comfy-Org/Qwen-Image-DiffSynth-ControlNets` | pictures drawn over a shape |
| `diffusion_models` | `qwen_image_edit_2511_int8_convrot.safetensors` | `Comfy-Org/Qwen-Image-Edit` | putting a portrait's character in a scene |
| `loras` | `Qwen-Image-Edit-2511-Lightning-8steps-V1.0-bf16.safetensors` | `lightx2v/Qwen-Image-Edit-2511-Lightning` | the same, `--draft` |
| `background_removal` | `birefnet.safetensors` | `Comfy-Org/BiRefNet` | cutting figures out |
| `checkpoints` | `stable_audio_3_small_sfx.safetensors` | `Comfy-Org/stable-audio-3` | sound effects |
| `text_encoders` | `t5gemma_b_b_ul2.safetensors` | `Comfy-Org/stable-audio-3` | sound effects |

Fetch them on the server itself with `/resolve/` URLs (a `/blob/` URL
returns the web page), for example:

```sh
cd ComfyUI/models/loras
wget https://huggingface.co/lightx2v/Qwen-Image-2512-Lightning/resolve/main/Qwen-Image-2512-Lightning-8steps-V1.0-bf16.safetensors
```

A repository may keep its files below `split_files/<directory>/` (the
Comfy-Org ones) or in a subdirectory named as the model directory
(`background_removal/`, `checkpoints/`, `text_encoders/`): the file
listing on its page gives the path.

## Blender

Blender 5.0 on the render host, as `blender` in the `PATH`.  artgen
copies its model scripts there and fetches the CC0 materials, Blender's
human base meshes and the sky once on the first render
(`artgen/materials.py`, `fetch_command()`) into `~/flibustier-cc0`
(`FLIBUSTIER_CC0`); renders use the CPU (Cycles).
