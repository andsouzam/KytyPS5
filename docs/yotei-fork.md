# Ghost of Yōtei branch

This branch carries the work on running **Ghost of Yōtei (PPSA26344)** on a PC with an NVIDIA
GPU (tested on an RTX 4090 and a Ryzen 7 7800X3D). It builds on BryKytyPS5 (see the README),
which itself merges upstream [KytyPS5](https://github.com/KytyPS5/KytyPS5).

The order of work is: no GPU crashes or hangs first, correct graphics second, smoothness third.
Almost everything here is behind an emulator switch. Switches are environment variables
(`KYTY_...=1`) or check boxes in a game's profile in the launcher. Unless a switch says
"on by default", leaving it off gives upstream behaviour.

Status: the game boots, plays through the intro and reaches the open world. Known open issues
include unlit far terrain at some places, white or smeared interior walls (the stencil fix below
is under test), vertex explosions on some meshes and missing vegetation in some areas.

## Fixes that are on by default

| Switch | What it does |
| --- | --- |
| `KYTY_BINDLESS_KEY_BOUNDS` | Texture tables only load the entries a shader can reach; stale requests from an older table are dropped. Fixed a GPU crash (device lost) at the Torii. |
| `KYTY_BINDLESS_EAGER` | Texture tables of rare compute steps are loaded right away instead of one frame late. |
| `KYTY_COMPUTE_CHAIN` | Consecutive compute dispatches without a guest wait between them run without extra host barriers (on for Ghost of Yōtei, ASTRO BOT, ASTRO's PLAYROOM). |
| `KYTY_CONSOLE_COLORS` | Coloured console output. |

## Fixes under test (off by default)

| Switch | What it does |
| --- | --- |
| `KYTY_BINDLESS_TSHARP_CHECK` | Checks texture table entries (T#) before they are used; garbage entries show a placeholder instead of crashing. |
| `KYTY_BINDLESS_TYPED` | Reads each texture table entry through the image array of its own kind (2D, 3D, cube, integer), not the kind the instruction expects. |
| `KYTY_BINDLESS_REFRESH` | Table textures the game rewrites are uploaded again. |
| `KYTY_SAMPLER_COMPARE_SPLIT` | A sampler used for both a plain sample and a depth-compare sample gets two host samplers; before, the plain texture was compared too (undefined in Vulkan, black or white areas). Shaders are translated anew. |
| `KYTY_IMAGE_RESIZE_SYNC` | Dynamic resolution: images of different sizes at one guest address copy the shared rectangle from the most recently written one. |
| `KYTY_STENCIL_KEEP_FIX` | When a depth buffer is briefly used as a color image, its whole stencil plane is kept and given back; without a kept stencil the plane is cleared to 0 instead of left undefined (white interior walls). |
| `KYTY_STENCIL_FROM_R8` | A depth buffer coming back from color use takes its stencil from the same-size 8-bit stencil image shaders wrote in between. |
| `KYTY_GPU_PALETTE_SANITIZE=1` | Experimental, game-specific staging rewrite for degenerate bone matrices. Disabled unless explicitly set to exactly `1`; it changes guest-derived bytes and is not a correctness fix. Prefer draw snapshots and producer tracing to establish why the guest data is zero. |
| `KYTY_META_CLEAR_ZMASK` | A compute fill over HTile memory only counts as a depth clear when its words mean "cleared". |
| `KYTY_BC_BUFFER_WRITES` | Block-compressed textures take GPU buffer writes that do not start at their first byte. |
| `KYTY_COLOR_FEEDBACK_COPY` / `KYTY_COLOR_FEEDBACK_LAYOUT` | A draw that reads the picture it renders to reads a copy, or uses the attachment feedback loop layout. |
| `KYTY_WAVE_BRANCH_FULL` | Scalar branches decide over the whole guest wave (upstream a8beac19). |
| `KYTY_PIXEL_PROVOKING_VERTEX` | Flat pixel inputs come from the provoking vertex. |
| `KYTY_OCCLUSION_QUERIES` | Real occlusion queries (upstream PR #1259). |
| `KYTY_DYNAMIC_DEPTH_BOUNDS`, `KYTY_UNMAP_SKIP_DRAIN` | Fewer pipeline variants and fewer GPU waits. |

## Upstream changes taken behind switches (off by default)

| Switch | Upstream |
| --- | --- |
| `KYTY_BC_R64_TILING` | b7b335ac: block-compressed textures in render-target (R64) tiling. |
| `KYTY_NGG_VERTEX_COUNT` | cf77fb43: NGG vertex count from active lanes. Shaders are translated anew. |
| `KYTY_R8_SNORM_SAMPLING` | 7e4e142a: native R8 SNORM sampling. Shaders are translated anew. |
| `KYTY_AVPLAYER_RECYCLE` | a6f4e8e7: video output buffers go straight back to the decoder. |
| `KYTY_DRIVER_DMEM_EARLY` | 9a5dd9fa: driver DMEM reserved at startup (moves the game's memory layout). |

Taken without a switch: fbea51c6 (`sceAgcSetNop`) and 05dc968a (null SRT pointers read as zero
instead of crashing while translating a shader), plus the earlier AMPR and streaming merges.

## Diagnostics (off by default)

| Switch | What it does |
| --- | --- |
| `KYTY_PIXEL_HISTORY=x,y[,0xaddress]` | "Who wrote this pixel": after every draw, dispatch, clear, copy, upload and stencil keep/restore, the texel is read back and each change is printed with shader hashes and the value before and after. `KYTY_PIXEL_HISTORY_STENCIL=<address>` follows the stencil, `KYTY_PIXEL_HISTORY_FRAMES=first-last` limits the frames. With the launcher check box, `pixel_history.txt` next to the emulator holds the same values. |
| `KYTY_DISPATCH_SNAPSHOT` | Writes compute dispatches (SPIR-V, inputs, outputs) to disk; `kyty_emulator --dispatch-replay` runs them again without the game. |
| `KYTY_DRAW_SNAPSHOT` | Writes draws (index, vertex and stage buffers, descriptors, parameters) to disk; `tools/draw_snapshot_check.py` checks indices against vertex buffers and scans for NaN or huge values. |
| `KYTY_VTX_EXPLODE_CHECK` | Reports exploding vertex positions and indices past the end of vertex buffers. |
| `KYTY_DEBUG_VK_ERRORS_NONFATAL` | Vulkan validation errors are printed (deduplicated) instead of stopping the emulator. |
| `KYTY_DEBUG_SAMPLER_COMPARE`, `KYTY_DEBUG_STENCIL_SOURCE`, `KYTY_DEBUG_META_CLEAR`, `KYTY_DEBUG_FEEDBACK_LOOP`, `KYTY_DEBUG_DEPTHONLY`, `KYTY_DEBUG_IMAGE_CHURN`, `KYTY_DEBUG_DRAW_REPEAT` | Red console lines for the respective events. |
| `-DKYTY_AFTERMATH=ON` + `KYTY_AFTERMATH=1` | NVIDIA Nsight Aftermath GPU crash dumps (set `KYTY_AFTERMATH_SDK` to the SDK; the SDK is not part of this repository). |

## Notes

- Ghost of Yōtei 01.512 needs "Readback linear images" on in the game's profile, otherwise the
  character and buildings are missing.
- No game files, saves, shader caches or recordings are part of this repository. You need your
  own legally dumped copy of the game.
