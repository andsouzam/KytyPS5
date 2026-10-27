# Integration test debt

## GPU-built buffer descriptors and first live Yōtei retry, 2026-10-25

`LoopBoundProof::Excludes` now clears the non-current and current predecessor
caches for each incoming edge. `resource_tracking_tests.exe` passes again,
including `bounded relative register writes` and the invalid loop-clobber
neighbors. Two new tests verify that a lane-dependent address-loaded buffer
base and a loop-carried buffer base remain GPU-side `IndirectBuffer` accesses.
The production condition is intentionally limited to supported indirect access
opcodes and non-host-evaluable descriptor address/size dwords; invalid stride or
format-only loop values still fail validation. The test executable and native
emulator target build successfully.

The first live retry exposed a broader set of descriptor expressions rejected as
`GetBufferResource dword 0 is not a valid runtime value`. They were either GPU
memory-read results (including `LoadAddressU32` and `ReadConstBuffer`) or
loop-carried address Phi values. These are now left in shader code for supported
BDA-backed indirect accesses instead of rejected by host-side descriptor planning.
On the latest installed build, the previous mass of resource-tracking shader
rejections and the fatal compute shader `d8959888aafd2552` translation error no
longer appeared; the process stayed alive through precompile and continued
submitting compute pipelines. Remaining runtime diagnostics include default
sampler substitution for heap-selected samplers and a bindless heap record that
is not a sampled image descriptor. No visual frame has been independently
captured, so this is a live-startup result, not a gameplay-quality claim.

`SanitizeBonePaletteStaging` remains a title-specific heuristic that rewrites
zero/degenerate 3x4 matrices using hard-coded row ranges and buffer size. The
launcher already sets `KYTY_GPU_PALETTE_SANITIZE=0`; production now honors it,
and only exact `KYTY_GPU_PALETTE_SANITIZE=1` enables the rewrite. This protects
guest-derived staging bytes by default but does not prove the zero-bone matrix
root cause is fixed. Required follow-up: capture the draw and relevant producer
dispatches for the same palette generation, identify the first producer that
should write the target rows, and compare its inputs/output with the draw. Do not
use a game ID, shader hash, guest address or fixed bone rows to select a correction.

The broad CTest run reported 189/207 passed (91%); 18 failed, including
unbuilt optional test executables and remaining shader, materialization, kernel
filesystem and GPU/cache failures. These have not all been isolated as pre-existing;
do not call the whole suite green. Keep `KYTY_GPU_PALETTE_SANITIZE=1` only for explicit A/B diagnostics;
it is not a supported fix and no visual gameplay improvement is claimed.

## Actual bound temporal image view, 2026-10-15

Game200706/5b82c5 reaches a visible world/cinematic with NGX initialized but
completed DLSS count0: Role rejects original descriptor view metadata. Saved
prep input0 requests R32_SFLOAT/COLOR; Image::FindView creates and caches actual
D32_SFLOAT_S8_UINT/DEPTH. Required synthetic CPU regression: resolve metadata
by the exact selected VkImageView in its owning cached records, independent of
the original request. Known normalized depth view must succeed; null, unknown,
foreign handles must fail, and another cached view must not substitute. Keep
all existing full-range/extent/channel/format/usage guards. Live diagnostic
201352/ce483a confirms exactly backingformat130 versus requestedformat100,
aspect1, mip/layer1, correct1920x1080; exits0. Intended CPU RED201620/aa29ae
passes null/color controls then fails native depth selection. Same unchanged
oracle (SHA1C53FC30) GREEN201654/8a43fb passes normalized depth, exact-handle,
foreign/empty cache, stencil and property-preservation neighbors. Production
now copies metadata from that exact bound view before the unchanged guards.
Game retry201809/81ad05 exits0: actual NGX evaluation+two-output RGB merge and
GPU retirement startframe313/serial1; counter200 byframe537. Root visually
confirms village cinematic/world with completedcounter46, not player control.
Native D32S8 is exercised with game validationOFF; prior offline fixtures used
R32F, so full native-depth validation/readback remains a coverage limit.
Preceding201257/ba6dfa failed before prep in loading memcpy,
so is not evidence about role admission. Offline PDB identifies the AMPR worker
as nearest calling function; its raw file-read destination only has null/overflow
checks, with mapping/permissions/lifetime validation still separate test debt.

## Native host ASLR and fixed guest mappings, 2026-10-15

Early-device startup let NGX initialize but claimed a foreign reserved region
covering a fixed guest heap. Game195159/05c419 and diagnostic195355/f790eb fail
writing0x1000000020 after BatchMap64GiB. Loader guard is0x1a08888000 and no
unresolved object is bound, so weak-import/guard attribution is rejected.
New actual-core no-ELF probe independently reproduces intendedRED195954/db3411:
NGXinit0, fixed16KiB BatchMap at canonical user base returns0x8002000c,count0;
native foreign MEM_PRIVATE region is preserved, physical allocation cleaned.
High host PEbase16TiB with no-ASLR200042/5e05f7 still fails device creation.
Changing only to ASLR/HIGHENTROPYVA with ordinary Memory-first startup passes
200127/64ea68, exit0: NGXinit0, exact fixed map/count1, guest/native ownership,
RW canaries, unmap/release and normal teardown. Actual PEbase is randomized.
ASLR is now the intended native default; retire the early-device workaround.
Final ordinary-startup/default-preferred-base coreFED1A83B also completes NGX
and the same map/canary/cleanup probe200606/709b17, then hits the previously
observed no-guest teardown timeout; this run is a scoped mapping PASS, not a
full-process PASS. Preserve that teardown debt separately from device/mapping.
Do not claim an NVIDIA internal allocation owner from VirtualQuery alone.
Build/wiring failures and stale-executable attempts are not regression REDs.

## Actual native constant-buffer snapshot, 2026-10-15

The temporal provider safely rejects ordinary cached GPU constant bindings;
historical d428/6a97 CBbuffer896713 differs from ActiveStream194. A synthetic
native fixture writes independent144B via GPU update at descriptor+adjust12,
while later CPU bytes differ. Extracted unchanged ActiveStream-only helper
produces intendedRED194552/18b44c: immutable-stream/short-range controls and
two completed native source/continuation readbacks pass, then ordinary actual
binding is Unsupported. Required GREEN: same fixture reads those exact GPU
bytes without current guest backing, mid-dispatch retirement callbacks or
unbounded waits; retain staging until its real GPU tick completes. An earlier
fixture build error and stale-executable missing-runtime exit are not RED.
Unchanged native GREEN194902/c136e7 passes the exact GPU bytes, continuation,
lease and final validation0. Extended neighbor195134/9f156a also passes with
the actual recording thread enabled and9 invalid requests rejected without a
submit or output write. The shared helper retains the stream fast path and
copies ordinary native bindings through a retained Download staging allocation,
finite5s timeline wait and fresh current command buffer without retirements.
Caller must supply a valid live native descriptor with TRANSFER_SRC/range
ownership; no new lookup or current guest-memory read occurs. Live retry pending.

## Internal unresolved-object guard versus foreign host reservations, 2026-10-15

Game193625/59f5aa on f5d5db0f/core26F95B reaches NGX initialization0 then
Relocate2041 fails allocating its fixed internal guard0x840000000. Before
changing allocation policy, require a native synthetic occupied-preferred-page
regression against the actual extracted loader allocator. Preserve a distinct
guest-owned NoAccess guard, the occupied page/canary and cleanup; no ELF bytes.
Native intendedRED194159/e67335 reproduces the occupied-hint failure. Allowing
the internal guard to relocate exposes separate intendedRED194236/f77be3:
kernel metadata is NoAccess but native protection is PAGE_READWRITE. Private
GuestAddressSpace::Commit now preserves explicit NoAccess. The same unchanged
test GREEN194401/eb0d81 verifies PAGE_NOACCESS, relocated ownership, two intact
neighbor canaries/permissions and cleanup. Full memory suite194401/0ff471
passes its available cases (two native SSE4a-host cases skipped). Game retry
on the combined fix remains pending; no game/DLSS/FPS claim follows.

## Copied temporal input channel mapping, 2026-10-15

Resolved a provider guard that rejected valid depth RRRR and motion RG01 views.
The private physical copy needs unchanged R for depth, RG for motion, and RGBA
for color; unused sampled outputs may be replicated or constant. The extracted
unchanged predicate reproduced RED192856/3be7ac, then the same test passed
GREEN192944/af4a05:7203 independently resolved mappings,898 accepted, invalid
enums and unknown roles rejected. Full-view/subresource/format/usage/extent
guards remain. This CPU result alone does not establish a live game evaluation.

## NGX device creation versus guest address reservation, 2026-10-15

First e5f3e1f9/B830 game retries190729/fd8f87 and190831/2e035d fail with
VK_ERROR_INITIALIZATION_FAILED in vkCreateDevice, before backend initialization.
No semantic game output or FPS sample exists. The second diagnostic enables
Console logging/validation and confirms the driver failure without a VUID.
Raw native NGX and earlier presenter fixtures passed. A primary NVIDIA forum
minimal reproduction reports this failure with NVX_binary_import after large
Windows placeholder reservations; this emulator reserves guest ranges first.
Full-presenter control191149/ad29b9 passes with real Memory first. Corrected
shared-prefix fixture192430/0e8972 initializes NGX but times out in no-guest
teardown; this is not the intended RED. Actual core diagnostic now stops after
host initialization, before loading any ELF. Owned empty directory + synthetic
156MiB param reproduces the driver failure192638/ace881, exit321; the same
native regression passes193149/7c9ad0, exit0 after a staged native-device
preparation before guest VA reservation. Memory and direct-placeholder
self-tests pass; renderer/input remain after Memory/Controller. Parsing early
or querying extensions early alone did not repair it. Required extensions and
guest ranges were retained. Ordinary metadata-OFF193227/0cbf46 initializes its
device but times out in no-guest teardown, also observed before this correction;
do not report that neighbor as a full PASS. Full presenter and actual game retry
on the completed source remain pending.

## Explicit temporal pass integration, 2026-10-15

User requests actual game DLSS use. Before wiring production behavior, reproduce
the missing explicit temporal association with independent synthetic tuples:
immutable private input copies, ordered same-frame producer/consumer, exact
color identity/generation, distinct resources and monotonic temporal serial.
Also require a native two-output RGB merge regression preserving each distinct
alpha and an untouched auxiliary R8 output, with downstream readback. Native
RED/GREEN must precede enabling the new pass. External pass metadata declares
roles; formats/counts alone do not infer them. Existing captured-input NGX
fixture is a separate acceptance experiment, not game activation.

These synthetic gates now pass with intended RED/GREEN and independent native
controls: association185348/b4756f→185423/8e50ce; merge185506/875a49→190059/a7fd38;
external descriptor retirement190541/23f9fa. CPU contracts190630/5b91bc pass3/3.
Saved real-input NGX185520/a018a4 passes two isolated resets/readbacks and final
validation0; temporal continuity/global exposure/full motion remain unproved.
First actual game provider retry is still pending. See
[the integration record](dlss-temporal-pass-integration.md).

## Captured temporal math narrows the provider debt, 2026-10-15

Diagnostic b4aa51cb produced four complete native dispatch snapshots550/551,
actual player/world/HUD confirmed. Prep actually1920×1080, accumulation actually
3840×2160; larger allocations do not imply larger prep execution. Captured
previous-output feedback and current/previous subpixel offsets are established.
Consumer math uses3×3MAX reverse-depth and normalized backward displacement
from negated rawRG16F.12 PS/VS and2compute offline variants pass CPU/IR/SPIRV;
that does not establish live PS/VS bindings, projection jitter or exposure.

Before introducing a provider or changing resource behavior, required synthetic
cases remain: camera-only matrix reprojection and independently moving object;
finite-motion preservation versus invalid-lane camera fill; UV orientation,
units/sign and whether jitter was removed; depth inversion and viewport subrect
versus backing extent; pre-exposed scene-linear color and changing exposure;
history reset/discontinuity, ping-pong resource reuse and ordered retirement.
Also prove command-buffer recording generation versus submission visibility:
descriptor-relative compact SRT offsets differ from original guest SRT offsets;
mapped constant uploads can follow draw recording but precede submission.
The actual camera module has no PushConstant; zero pushes are not its matrix.
The existing SDR presenter must still reject HDR; actual HDR insertion requires
later game tone mapping/HUD. No shader-hash/address/title-based core selection.

Selected-event replay now verifies the submitted pipeline and exact descriptor
tuple before744B of readonly constant reads:8/8 requests pass, including both
camera matrices, representative geometry transforms and temporal CBs for guest
frames432/433. Sparse actual-set lookup resolves the two PS array entries omitted
by access feedback; exact buffer/offset/range checks remain mandatory. Stable
baked-recording/work-ordinal correlation passes8 synthetic CPU
cases; raw chunk index/timestamp equality is invalid. These diagnostic reads
and numerical reprojection/re-jitter checks establish the selected matrix path,
not full motion coverage or a generic live resource provider.
Next use the saved real inputs in a separate bounded NGX fixture, retaining the
camera/object/jitter/exposure/reset and ordered-resource regressions above.

One-shot capture control tests and real Vulkan bootstrap capture pass, but the
first actual game capture expired during initial-state preparation and the pass
trace is partial. Do not treat partial spills or render metadata as a complete
guest frame. Retry173950/0d35c1 after user freed space completed5,016,579,112-byte
RDC over two guest flips, game exit0/no guards; replay coverage/producer analysis
is a separate pending stage. See
[capture evidence](yotei-temporal-input-capture-2026-10-15.md). Game DLSS/FPS
improvement remains unproved; no new production semantic fix in this round.

## Yōtei temporal provider and game activation remain unproved, 2026-10-15

Optional SDK build091119/61af2f reaches visually confirmed world,20s title mean
3.3389FPS/exit0, but ordinary production never requests/evaluates DLSS. Fresh
historical control091438/714d55 closed before the gate; no matchedFPS conclusion.
No production behavior changed in this game-verification round.

Before a provider/resource change, establish a complete guest-frame capture,
actual temporal producer order, pre-upscale/pre-HUD color, matching depth/full
camera-and-object motion, render-pixel jitter, exposure and history resets.
Synthetic RED/GREEN must prove the specific diagnosed resource/ordering contract;
generic image formats or saved shader identities are not expected semantics.
Cover camera-only/object-only motion, depth convention, dynamic extents,
reprojection reset, resource reuse/retirement and later postprocessing/HUD.
An external RenderDoc native-present capture can contain only repeated Frame
presentation; verify actual guest work before treating it as a full frame.
Real evaluated/completed/presented game output and genuinely lower render work
must precede any DLSS image-quality/FPS claim. See
[current game evidence](yotei-dlss-game-verification-2026-10-15.md).

## Native image recycler teardown discovered during DLSS validation

Actual full-presenter run084857/29b86e passed SR/copy/lease/guards but is NOT
a clean pass: real WindowContext teardown reported VUID-vkDestroyDevice-device-05137,
24 leaked images. The fixture's observer ended too early and process exit0 did
not establish success. BindlessTable releases24 placeholders through DeleteImage
into the global recycler; DestroyAllocator destroys pool/allocator without
draining those native images. No driver loss/reset occurred.

Before production correction, bound a minimal ordinary hidden-Window bootstrap
with NO DLSS init/evaluate or guest; keep a validation ERROR observer alive through
the actual WindowContext destructor and record intended cleanup failure. Same
fixture must pass after recycler drain before VMA pool/allocator destruction,
then rerun full presenter and affected allocation/reuse tests. Do not replace
cleanup with test-only drain, ignore teardown errors or claim FPS improvement.
Multiple simultaneous allocator ownership is a separate unproved case; no such
compatibility claim follows from the current one-renderer lifecycle regression.
Verified: ordinary bootstrap with missing runtime/noNGX, fixture SHA6C0B0483,
085350/7fca00 intended exit2/errors1, actual24 retired native images/19968bytes.
Production drain before VMA pool/allocator -> same unchanged085516/561efe exit0,
errors0. Full actual presenter085517/1c5915 also passes through final device
destruction: SDKoutput==ownedFrame GPU bytes, independent readback after source
destruction, duplicate ID/first reset/lease/guards/provenance all pass. Native
host-allocation/present neighbors085608/73aa85 pass2/2. No game or FPS claim.

## Optional host DLSS integration, 2026-10-15

User authorizes real Vulkan DLSS SR integration. This is new host functionality,
not a diagnosed guest emulation defect. Before SDK evaluation, prove a synthetic
moving scene's native uploads/readbacks and lifetime guards; capture explicit
Unsupported from the baseline adapter as the missing feature, then rerun the
same fixture against the real backend. Setup/driver errors are not that RED.
Contract cases require actual depth/full camera-and-object motion, known jitter,
valid extents/formats/layouts/usages, nonaliasing output, exposure convention,
finite frame timing, reset/recreation and bounded GPU retirement. No guessed
textures or title/shader/address selection in the core. Game activation remains
pending until a semantic provider exists; final presentation color alone is
insufficient. GPU smoke is not proof of Yotei support or FPS improvement.
Presenter HDR boundary: official NGX guide3.1.2 requires perceptually encoded
0..1 LDR; linear scene color uses HDR. Existing SDR swapchain does no tone map.
CPU guard initially accepted HDR and failed the new explicit unsupported-case
oracle (083023/3b5c64). Restrict this presentation bridge to display-ready LDR;
the standalone backend continues supporting real HDR SR before postprocessing.

## Cycle54: measured performance candidates, 2026-10-15

Public research + four native profiling runs completed on unchanged b1 core,
record ce620, EXEE46E. No production fix/new optimization algorithm. Function
report174rows650->686/10.185s all errors0; CPU counters and independent MAIN
PSS36/36 prove high Main CPU accompanies guest deadline polling, not an identified
hot HLE function. GPU109lateframes attribute buffer-copy32.59ms/frame and tiler
20.65, about385submits/131readbacks/frame; exact removable critical path remains
unproved. Full evidence: [research report](yotei-render-research-and-profile-2026-10-15.md).

Before resource behavior changes, required synthetic cases:
- Aliased views with different formats/extents over shared storage, unchanged
  repeated consumption, partial CPU/GPU writes at tile and subresource boundaries.
  Reuse must preserve latest native values, untouched bytes and read/write ordering.
- Eager old-token completion after new GPU writes, later demand reads and periodic
  retry. Measure stale-token admission separately; never suppress required current
  data publication or assume CPU consumption from an unprotected clean-page load.
- Actual hot shader resource kind/permutation: independently test8/16-bit stores,
  dynamic descriptor views and Workgroup/LDS atomics including neighboring-byte
  preservation and concurrency. Historical fc6f module CAS is LDS, not evidence of
  current SSBO byte-store defect. Native8/16-bit path already exists.

Diagnostic limits: noScopeProcessPm4/idleSpin, sampled same-thread-only exclusive,
rawCPUcycles notms, histogram saturation100ms, syncQueueLockWait includes drain,
GPUzones perturb overlap/have query coverage limits. Exact fresh SPIRV, hardware
queue critical path, image/view-specific upload reasons, used/superseded eager
token counts and VRAM residency remain pending. Scene must be inspected from
task-window captures; user says do not ask him to identify the phase.

## Cycle53: three new read algorithms, 2026-10-14

User requests old indirect experiment PLUS THREE NEW read-core variants and
then stop. New read production behavior must await each intended actual RED:
PM4 header/payload suspension must preserve cursor/snapshot and original queue
ordering while allowing an independent queue to progress; selected eager pages
across owners may share packed staging/barriers/publication without widening
admission or losing newer GPU writes; range/token publication must return A
while unrelated B publisher is held, with exact A/B bytes and generations.
Keep fixtures finite, retire all CPU/GPU/native resources before cost oracles,
and never count setup/timeout/driver-hang errors as RED. Agents own ignored
fixture proposals, root all native runs/production mutations. Preserve normal
metadata/unknown-case fallback. Actual packed203704 and targeted203705/A4C60E5B
cost REDs now pass all data/guard/cleanup controls first. PM4 initial203703 was
a fixture invalid-copy device loss, NOT RED; acquisition and bounds repaired,
resourceplan205059 PASS; corrected205131/2A82 now genuine header/payload wait
and progress RED with exact native/source/markers/cleanup. Packed f9fc0a9e
unchanged GREEN204725/CD6 passes all3cases; original eagerOFF and armedtypedON
neighbors PASS204919/204920. Ringblocked/generation-changed fallback and broader
cross-game live coverage remain limits. All four algorithm trials are now
finished; no further algorithm/game run belongs to this bounded task.

Packed game trial205520/beb2d6 rejected firstbelow title3.5<=3.58 within gated
window; no completedmean or visualgain. Native f9 core/installPASS, EXEE9BB9F99
archived, production reverted a1c414dc. Targeted revised proposal eaee (older
overlap/coherent guards) native205750 and unchanged GREEN210105/C20BDE31 PASS;
new-writer210637, dirty-image210638, pending-image210710 and armed-typed210711
neighbors PASS. Older-wider neighbor210941/50547530 has exact native/backing
A2 and all ordering/guard/cleanup checks PASS but invalid same-owner A+B+C
fixture assumption fails; this is NOT a production regression. Corrected
registered A+C owner setup211230/FD7CD216 PASS with unchanged numeric/order
checks, native/backing A2 current after all callbacks and required wait1.
Safety test b28793c5, production941fc65f; native core/install PASS211248/211348.
ON game211350/87a248 completed6s titlemean3.8333 (earlier control3.58); visible
scene/equivalence unproved. Same EXE OFF control211625/320d63 completed6s
mean3.60, about6.5% difference from ON; no scene proof or replicated gain.
Concurrent same-request once claim and live cross-game remain limits; no
verified2x gain. Targeted was temporarily reverted2349d757 to isolate final PM4,
then restored b1a9698e, exactly matching tested941 production, defaultOFF.
Native PM4 target211920 PASS; unchanged PM4 GREEN212021/
83AF97CB with fixture SHA D0D84C55 PASS: actual GPU header/payload, original
markers/canaries/current values, independent progress while A blocked/old,
dispatcher full/tick/priority waits0/0/0 and finite cleanup. Queued WAIT212038
and image alias212039 neighbors PASS. Fresh shader target212040, predication
212147 and command-lane212148 neighbors PASS. Native core212217/install212310
PASS; game212312/117d59 cut atframe569/title3.6<=latestcontrol3.60, before
completion, no mean/image; production reverted3dfd4664. Host/unmapped parser
tests do not cover GPU-owned
nested/predicated/legacy extended payload, kept as explicit coverage limits.
No correctness proof should be relabeled as gameplay/FPS success.

## Cycle52: copied indirect command experiment, 2026-10-14

Before production change, existing unchanged host-only fixture SHA35498FA3
has intended logged RED194639/EABD645F on production42c: all argument/order,
prior-work dependency and retirement controls pass; producerDrain1 fails0.
Reuse this recorded reproduction; no build/setup failure is the oracle.
Next shared copied vkCmdDrawIndirect hook remains opt-in until actual native
draw/argument-write/barrier/readback/resource-retirement proof and clean world
FPS improvement. Add finite zero/one/multiple-count and direct/foreign/consumer
routing neighbors. No GPU hang/repeated reset or game-title special path.
An independent bounded producer census must prove actual workload eligibility;
nonzero counts alone do not prove material removable wait time. New diagnostic
instrumentation is not a correctness or performance fix.

## Post-Dota measurement limits and next renderer witness, 2026-10-14

Native source42c unchanged; cycle51 clean6s titlemean3.85 versus drain-only10s
3.475. Recovery exits0 after first gated title3.3, before6s completion; no mean.
User confirms actual character/world during recovery, independently captured
images and equal scene proof for prior runs absent. No accepted source speedfix,
exact diagnostic penalty or2x original3.7FPS. See
[post-Dota evidence](yotei-post-dota-measurements-2026-10-14.md).

Next instrumentation is diagnostic, not production correction: actual producer
nonindexed API census or hardware-queue trace; optional upload counts-only/BDA
volume to rank candidates. Preserve genuine dependencies and unsupported cases.
Existing copied-hook CPU cost RED still requires unchanged GREEN plus finite
native indirect argument lifetime/draw/readback and neighboring routing tests.
Logical readback volume and overlapping waits cannot prove removable cost.
No full renderer rewrite or game-title branch has been tested or applied.

## Nonindexed indirect recording and typed tiler candidates, 2026-10-14

Cycle50 read-only source audit finds vkCmdDrawIndirect in fallback hooks:
the routed producer drains its recorder before forwarding five scalar arguments.
Indexed indirect already has a copied hook. Before any production change, add
the bounded host-only real-CommandStream fixture: forward all five arguments
(including64-bit offset), order around a withheld consumer gate, require that
forwarding follows prior recorded work, join/drain/restore ownership before the
final avoidable producer-drain count1 versus0 RED. No driver/GPU calls required.
Build/setup/timeout/order failure is not RED. Actual Yōtei nonindexed branch hit
is still unobserved; aggregate draw/indirect/meshargs counts cannot prove it.

Typed2-byte tiler proposal independently requires unchanged numeric/byte/padding
controls through actual TileManager::Detile, then specialize the captured module
with its actual BPE=2 VkSpecializationInfo and DCE before cost counting. Whole
module atomics include the preserved1-byte path and are not a valid RED/GREEN.
Current storageBuffer16BitAccess is already required/enabled. Preserve current
alignment/range/metadata/synchronization restrictions; no broad donor gate port.
Source conditional RMW counts are not observed native throughput or world FPS.

Current foreign GPU activity: Dota2 began18:46:30Z, after diagnostic575166 and
before cycle49 games. Parent saw37%GPU/4907MiB without an emulator. User chose
to close Dota2 personally; continue code/CPU research, no foreign process action.
No clean GPU benchmark while it remains active. This is a comparison confound,
not proof that all prior FPS loss came from Dota2 or from the emulator.

Actual host-only RED completed: build194126 PASS, original production RED194322
reaches intended final cost oracle. Logging-only stdout flush then build194503
PASS and logged RED194639/EABD645F: all five arguments/64-bit offset/order1,2,3,
forward-after-prior-work/worker/stream/environment cleanup pass, no timeouts or
GPU initialization; producerDrain1 fails intended0. Manual CLI before Vulkan
init, not registered in CTest. The fixture is an expected-RED research mode;
production fix, unchanged GREEN and actual Yōtei branch hits still pending.

## Renderer paths selected by actual semantics, 2026-10-14

Current source294a854d; see
[GTA/Yōtei renderer research](yotei-renderer-performance-research-2026-10-14.md).
Fresh575166 has only16-byte tiny tiled downloads; the prepared4B direct-download
cost fixture/candidate remains unapplied/uncompiled/unrun, with no applicability
witness. Do not call this a failed native test or an accepted optimization.

Next eligible witnesses: bounded q0 Process CPU cycles + timings of real native
counter query/semaphore wait/publication CV calls; bounded uploads/direct-route
admission refusal metadata. Diagnostic measurements are not fixes and require
no artificial speed RED. Do not add resource/driver queries or change scheduling
while observing. Account for censoring/overflow and overlapping time scopes.

If subword detile cost is confirmed, first prove byte/halfword tiling independently
with nonuniform finite data, odd dimensions/base offsets, padding/guard preservation,
tails/subresources and exact unsupported fallback. Only after byte/layout/cleanup
controls may excess native atomics/work fail as intended RED. A one-writer-per-word
path must retain correct partial-word ownership; no blind atomic removal.

If repeated backing lookup is confirmed, first exercise the real shared backing
store with independent mappings and generation changes. Exact bytes, owner changes,
remap/unmap/gaps and overflow must pass before excess locked lookup count RED.
Multi-entry cache is a candidate, not demonstrated current frame cause.
Existing last-writer transfer readback queue must be measured before proposing
another queue. CPU command assembly from GPU results cannot simply be discarded.

Fresh native bootstrap reproduction now confirmed on294: build target185126 PASS,
test185233 (`--readback-queue-ageoff-gpu`, EXE96E84A5F...) uses actual graphics
family0/transfer1. Warm unknown producer fallback and native A=abcdef09 pass;
completed A still rejected (`queued=0`, backingA=5a5a5a5a) while unrelated B
remains OPEN. All finalA/finalB numerical controls and owned cleanup pass before
the intended eligibility assertion fails. Exit-1073740791 is that explicit
Require/abort, not a driver hang or unrelated crash. Same fixture must remain
unchanged for GREEN. Root is investigating late/missing write-tick stamping;
do not enable queue1 in the game before this regression and neighbors pass.

Bootstrap RED185233→unchanged GREEN185634 after a post-command stamping draft
(native target185504 PASS, EXE8BA64F41...). Before accepting that draft, add a
finite pending-reservation/query/rollover neighbor: reserve A, query and reject
its OPEN producer, submit that preparation tick, record actual A on the next
OPEN tick, then a new plan must still reject. Finish and independently verify
native A/backing values/owned cleanup before checking the rejection oracle.
The query must never permanently assign an unrecorded reservation to an earlier
tick. Query-side pending0 must conservatively mean current/open; completed-prefix
pruning must stop at pending0. Also preserve same-tracker-page/new-A-writer denial.
No queue1 game trial until these actual ordering controls pass. Draft is not
a completed fix or demonstrated FPS increase.

Completed safe correction4e784308: pending/query/rollover RED190348/A3450E49
passes real producer values/guards/cleanup but openReject=0; unchanged
GREEN190538/AF3C72E4 has openReject=1 and samePageReject/newAReject=1. Queries
no longer permanently stamp pending writes and prefix pruning stops at pending0.
Final old bootstrap190600 and existing armed4/EOP3 neighbors190601/190603 pass.
Trust floor and overflow policy preserved. Native correction completed locally;
game queue activation/FPS comparison and broader cross-game coverage pending.

Runtime disposition: candidate4e FULLY reverted42c6c601 after no-gain queue1
trialcc7b05 firstframe400/title0.9<=control0.95 (df0078 completed6smean).
No ON averaged FPS/percentage or new visible-world proof. Both closed0 without
guards. Correctness artifacts remain archived; current implementation again
contains the reproduced debt. Initial41ad9a uses AGE='0' presence-enabled diagnostic
and is excluded. Next diagnostic-work test must distinguish a legitimate empty
timestamp tail from a real unmarked required command, including deferred recorder;
no actual missing mark has been found. Controlled same-executable drain setting
comparison and actual phase confirmation precede any new production correction.

After the actual defect/cost witness: unchanged GREEN, smallest affected neighbors,
native Windows build, clean warmed real-world title FPS A/B. New visual acceptance
is pending for575166: owned scene capture was withheld, not a world screenshot.
The minimum90s/frame400 gate alone is not scene proof. No2x increase claimed.

## Immutable shader format metadata storage, 2026-10-14

Actual captured compiler output has four readonly Private128-u32 tables,
2048 declaredbytes per invocation. This is a storage-cost witness, not measured
driver spills or proof of the world bottleneck. Before emitter changes, compile
the existing synthetic generic-buffer program with the requested optional mode
and independently check all128 metadata words/validity/width/type/prefix values
and seven-bit masking. Only after those preservation controls may the old2048
versus expected0 declared Private-byte budget fail as the intended cost RED.
Keep the same fixture/goldens unchanged for GREEN; build/validator failures
are not RED. Keep old invalid-format metadata and unsupported errors.

A real finite GPU probe must exercise the actual emitted old tables/new lookup,
not upload CPU expected values to GPU. Validate module layout before execution,
all128 values and wrapped/invalid boundaries, output guards and cleanup using
one bounded dispatch/fence. Neighbor actual generic formatted loads/EXEC/raw
and D16 cases before any original-game trial. A shared helper may be inlined by
the driver or increase pressure, so storage removal alone proves no speedup.
Fingerprint the experimental mode in cache/audit provenance or use isolated
matching build profiles without deleting/retagging old caches. Native module
statistics and valid phase-matched world FPS remain required for acceptance.
Production candidate9a8b61da is defaultOFF. Storage RED221328→GREEN221553,
option lifetime RED222703→GREEN223230, native388 metadata/60 canary probe,
52 actual channel/wave32/64/EXEC neighbors both modes PASS. Actual D16 coverage
remains pending. Generic4800 outputs equal exactly across modes, but both retain
144 preexisting UNorm8 endpoint errors/exit2; strict numeric pass is not claimed.
Six native GPU workload microbench medians differ below0.5%, no meaningful gain.
Exact full-record FC6 compile-only statistics OFF20s/ON30s both timed out before
driver pipeline creation returned; no spill/allocation statistics obtained.
Game OFF230053 mean3.75 (90s+400), ON230455 timed out compiling before game.
World benefit is UNPROVED. Warmed retry230912 also timed out240s atframe2,
second changed pipeline160.8s, third still compiling. FULL candidate9a8 reverted
ce0b3848, original patch/fixtures/caches retained outside tracked source.
This is no measured steady-world FPS rejection or cold-vs-cold compile comparison.

Experimental option lifetime must also preserve actual batch audit input:
fresh production compilations 0→1→0 in the same process must retain all128
metadata and produce2048→0→2048 Privatebytes. Run against the cached getter
first; only middle2048 instead of0 after guest/env cleanup is the intended RED.
Use one public per-compilation option reader and matching cache/audit policy;
do not fingerprint1 while actually recompiling with a cached0.

Cache mode admission must use the actual native loader and real empty Vulkan
cache bytes: OFF identity loads exactly, bad checksum/host are rejected and
source files/objects clean up before an existing ON filename with OFF header
must reject. Only wrongVariantLoaded>0 versus expected0 is the intended RED.
After namespace isolation, a missing ON file may seed from unchanged validated
OFF opaque bytes; malformed seed still rejects, inputs remain byte-exact and
the read-only loader creates no files. Never retag raw journals across keys.

CPU audit must preserve strict old manifest handling while adding the option:
an accepted legacy environment missing only the newly introduced switch must
clear inherited ON to OFF. Existing missing-other/type/extra-field denials and
complete parent-environment cleanup precede that final RED. Explicit saved
0→1→0 profiles must then match actual fresh compilation settings in one process.

Actual cache admission RED224353→GREEN225654 plus missing-ON warm225655 PASS;
legacy audit RED225525→GREEN225548 and explicit profiles PASS. Genuine checksum,
host, old missing/type/extra denials and byte-exact input preservation retained.

## World-phase WAIT and partial-flush diagnosis, 2026-10-14

Earlier frame400 slice trace started during loading and lost159763 rows; it is
not a world critical-path witness. Next sparse diagnostic starts after90s+400,
uses current SliceExit mask7 (no reverted quantum bit8), reports dropped/censored
rows and must associate a real world scene. Raw madeProgress can be stale; do
not interpret zero as proof of no work. Join blocked intervals to the actual
WAIT site, existing PollReadMemory outcome, single existing dereference/comparator
and real CV timeout/notification without adding memory queries or notifications.
100us requests round to at least1ms on Windows Clang; cumulative95ms/frame is
many waits, not one100ms wait. WriteFault is before the guest store retry and
cannot establish completed CPU-store→resume latency or justify an early notify.

CS partial flush07 currently uses ALL_COMMANDS and consumes compute-chain state.
Before narrowing it, establish actual native producer stage substitutions:
compute image clears may become Transfer or ColorAttachmentOutput loadOp clear,
and depth clears need early/late stages. Adding ColorAttachmentOutput everywhere
includes logically earlier graphics stages and may preserve serialization.
Require synthetic real RMW/transfer/dependency/guards/cleanup controls plus an
independent stage-cost RED, pending-chain/rendering and VS/PS/EOP neighbors. No
game-specific omission or unconditional Compute-only barrier is permitted.

Actual world run231842/692d96d8/EXEBF6F PASS native cleanup0/no guards; viewed
frame505 scene. Full94258-row label trace has0drops; CS07=9991 in35frames,
7930pendingcompute/34rendering. Actual413 nativeTransfer clears/0Color clears.
This is frequency, not measured barrier wall cost. WAIT cap suppressed49181
of65565 forwarded requests, so sparse complete episode attribution remains debt;
all-attempt counters and actual CV histogram are separately available. CS native
cost RED232311/EXE6E3EC6FD on unchanged692d96+fixture passes native numeric,
guards, cleanup, pending-chain, compute/transfer coverage and VS/PS/cache controls;
only final broad-source cost oracle fails (both src10000=ALL_COMMANDS). Native
target232152 build PASS. Unchanged GREEN232612/EXE2434AF70 passes src1800
Compute|Transfer versus prior10000, preserving all numeric/state/cleanup checks.
CPU planner neighbors232614 PASS, actual CS cost chain0 neighbor232700 PASS;
existing compute-chain default-scope0/1 neighbors232614/232616 PASS.
Native alias/rollover OFF/ON232954/232955 pass; their guest span initially omitted
padded linear rows, so correct fixture span0x800 (two0x400 slices) was used for
final233322/233323/EXED80348D2 PASS without footprint warnings. All32 native
texels perpass+readbackguards retain raw0.5/0.25 bits. Actual stage sequences
ON1800,1c00,1c00,1800 prove color history persists across native Flush/BeginNext
and resets only after recorded covering CS event; OFFall10000. Four new CTests
233411 PASS1.43s. Production/test commitb052877f was defaultOFF. Actual clean
sameEXE B7FA control233527 mean3.6 versusON233737 rejected3.4 at547/90.27s,
bothexit0/no guards. FULL trial reverted165a1530; exactcode/evidence preserved.
No FPS gain or whole-frame bottleneck proof. No other local game available;
cross-game runtime untested.

Existing broader compute-meta-clear group stops earlier at NativeIndirectDispatch's
old host-drain assertion with scopeOFF/ON and chain0/1, BEFORE later numerical
goldens. It asserts tick advance and CPU-clean args after dispatch. Verified also ORIGINAL
pre-CS native EXE6E3EC6FD via232847-with-dlls control: same host-drain assertion.
This is NOT proof of wrong numerical output. First232821 archive attempt failed
missingDLLs and is NOT reproduction.
Separate actual color-clear232756 scoped1 passes; do not claim the
whole meta-clear group passes or alter old numeric goldens. Diagnose old count
fixture/API contract separately: older57cf08688 assertion predates25194ba42's
GPU-built IndirectBuffer descriptor path. Suspected stale expectation still
requires a separate diagnostic that executes unchanged numerical goldens.
Diagnostic234420/EXE072B passes packet-consumption, logs count3/tick9→9/
argsGPUdirty1, then original CPU numerical golden failsword0. Independent
actual-native-owner234602/EXE89F1 probe copies64B from writable PeekPage owner,
proper shader→transfer→host dependencies and real Finish; native16 words EXACT
4,5,6 thenA5sentinels, both guardsPASS. Existing CPU golden still failsword0
and outputGpuDirty0. This confirms the first native count3 math and identifies
untracked BDA output publication as a separate boundary; not whole-suite GREEN,
not a minimal cleanup-safe semantic RED, not a proven performance cause.
Test-only diagnostics preserve originaldefault predicate/numeric asserts.
Correct old-binary provenance is RED232311
archive source692d+fixture, not launcher-recorded current3f1d HEAD.
WAIT forwarding cap raised131072 based on actual65565 requests+77874nonWAIT
rows (projected143439<262144 ring); future traffic can still overflow and must
report loss. No completed re-run with the new cap yet.

## PM4 queue service and bounded yielding, 2026-10-14

Actual causal diagnostic `aeb13a50` after frame 400 joins 73 real completion
labels: arm→finalize mean 0.730 ms, while queue 1 compute waits 72.804 ms before
first CP processing and queue 0 SuspendPoint waits 139.821 ms. This does not
prove starvation: same-queue FIFO, blocked producers and resumed slices can
explain part of the delay. First record uninterrupted Process slices and their
Complete/Blocked/progress status; do not classify all latency as removable.

Before any opt-in packet quantum, reproduce cost using the actual public GPU
queue/parser APIs: enqueue a finite graphics progress stream and independent
compute capture while the real consumer is held, then release. Verify all
final payloads, checksums, queue FIFO, canaries and task cleanup before the
bounded-between-packets progress oracle. Baseline must fail only that cost
oracle; native bootstrap, shader/build failures or GPU hangs are not RED.
Budget is an explicit scheduling policy, not an invented guest memory-order
guarantee between independent queues. Distinguish Yielded from Blocked so a
runnable slice never enters the blocked poll (100 microseconds requested;
Windows Clang condition-variable backend rounds it to at least 1 ms). A graphics CE/DE round
must share the budget and actually return to the outer queue scheduler.
Preserve per-queue CP registers, buffer stack/tail chains, submit IDs, CE/DE
counters, speculator resume, every packet and real WAIT_REG_MEM predicates.
Cover nested/chain buffers, CE/DE synchronization, false blocked waits,
readback notifications, same-queue FIFO and real native completion before a
game retry. Do not force more recording-age GPU runs to reproduce device loss.
RED/GREEN and runtime benefit remain pending.

Policy test now has a genuine native RED `212750…real-RED`, EXE `115A34F5`:
all 4096 values, FIFO, guards and cleanup pass, short queues observe 4096.
Unchanged GREEN `212922`, EXE `E40BB0E4`: observes 32/64/32, all controls pass.
CPU IB/CE-DE ON/OFF and stronger same-queue tail FIFO neighbors pass.
Native queue-state fixture initially omitted a guest compute dependency:
OFF numerics failed although ON happened to serialize. This is an invalid
neighbor input, not another production defect. Added real ACQUIRE_MEM before
the second read-modify-write dispatch, retained goldens. Corrected fixture
`213632` OFF and `213633` ON, plus `213634/213635` nested IB/CE ON/OFF all
pass on EXE `87FB57A4`, including A+2/B+7, CPU fringe, guards and cleanup.
Actual world throughput and deferred-recorder neighbors remain pending;
do not infer that fair CPU slices are the measured world bottleneck.

## SRT materialization work: operation witness required, 2026-10-13

Completed46fe8e8b profile191442...730fa4 identifies shared graphics/compute
MaterializeResources245480calls/3926samples above probe floor, but no exact
redundant operation or critical frame path is proved. Before a production
cache/refresh/fallback change, count actual compiledsuccess/referencefallback,
flat-run/slot observations, memo eligibility/reuse and descriptor evaluation
within the current abstractions. Use sparse counters/one rotating sampled leaf,
not nested timers on every scalar read. Existing compiledplans/memo/64Bblock
caches must not be duplicated. Any new skip requires a genuine synthetic RED
with changed descriptor words/selector, same-page dirty siblings, pending/dirty
image aliases, user SGPR/shader base and failed-read fallback; preserve ordinary
versus strict readers and explicit unsupported errors. Actual witness/RED are
pending; 3.9us samplemean is not a promised FPS saving. Mutex/permutation timing
is near the probe floor and supplies no lock-removal/hotset justification.

## Repeated GPU-dirty state bookkeeping, 2026-10-13

Before skipping protection-mask construction, prove an actual native buffer
owner is CPU-clean, GPU-dirty, unarmed and not stale-readable across the full
requested range. Repeated writable acquisition must preserve every native/
backing word, page protection, token, canary and cleanup before the zero-mask
cost oracle. Baseline mask count1 versus expected0 is the intended RED;
bootstrap/build/guard failures are not. A harness-only observer must be absent
from game builds. Partial bit-word boundaries, initially clean pages, actual
armed publication, stale-read revocation and arms outside the written range
must retain their transitions. Keep CPU-conflict validation before any exit.
Independent AllInRange bit-by-bit boundaries and existing memory-tracker OS/
token/death tests are required. Native RED190549 masks1 and unchangedGREEN190651 masks0 prove redundant
maskwork.5actual GPU neighbors190652 pass; independent BitArray +complete
MemoryTracker OS/token/conflict190759 pass.12affectedCTests pass4.62s.
Actual clean30s world190931 mean3.725 vsprevious3.714286 (+0.29%) gives
no meaningfulFPSgain; same settings across two EXEs, notsameEXE A/B.

## Ready range after native wait versus unrelated publication, 2026-10-13

Function profiling shows foreign readback latency is mostly waiting. Before
changing its final global priority wait, reproduce actual A/B producers and
downloads at the same native tick. Hold A publication, then let it genuinely
finish while independent B publication remains held. Read A must preserve its
native/backing bytes, token state, guards and cleanup before the zero-unrelated-
wait oracle. A request covering A and B, an unpublished A, a new A writer and
pending/dirty image aliases must retain the necessary publication ordering.
No production shortcut before a genuine RED; image guards and tracker queries
must be authoritative and release their locks before any existing wait.

Native RED184937 and unchanged GREEN185119 prove the specific redundant wait;
policy0 retains RED and mixed/unpublished/queued/new-producer neighbors pass.
Post-wait new-writer and direct dirty/pending-image negatives pass185421/
185801–185802; pending case also dirty, isolatedpending-only remains debt.
Eight affected CTests pass. Actual FPS benefit still pending.
This is a dependency correction lead, not a proven frame-critical FPS saving.

## Repeated foreign CPU write-fault eager publication required reproduction

Three existing hot-page enrollment sites exclude every is_write request.
Cycle33 attributes most packed traffic to real foreign CPU write faults, so a
repeated CPU/GPU ownership exchange may always require a new on-demand download
and native submission. This is a profiling lead, not a proven correctness bug.
Before adding an optional actual-WriteFault policy, exercise two real cycles:
native GPU producer -> protected foreign CPU store -> BDA upload/native proof;
then a fresh GPU producer of the same page and unwritten siblings, normal
OnCommandRecorded/Flush/actual completion, and a second protected CPU store.
All native/backing bytes, fault-before-retry values, guards and cleanup precede
the second-fault copy/submission cost oracle. Required work must be relocated
to the existing normal producer submission, not omitted or made fake-ready.

Preserve ordinary explicit writes, GPU-thread writes, image alias guards,
unknown owners, newer producers, token rearming, pending publication and the
existing bounded hot-page policy. Any new enrollment must be opt-in; total
readbacks may rise for pages the CPU never revisits, so only clean world FPS
can justify a default. Native fixture/RED and production changes are pending.

## Recorder idle-spin policy required bounded reproduction, 2026-10-13

Native CPU RED `124109/686EF8EF`: all four real scheduling/payload/wake/Drain/
counter/cleanup controls passed, then the explicit 100-spin oracle failed with
2000. Unchanged GREEN `124438/45B5B22D` passes with 100. Shared optional
`KYTY_RECORD_SPIN_ITERATIONS` accepts 0..2000; default remains 2000. Only the
empty-ring bound changes; shipping has no test hooks/counters. Existing actual
3000-payload/wrap/order test passes in separate fresh `8F6AB348` processes with
unset/default, 0, 100 and 2000; 2001, nonnumeric, negative, trailing junk and
overflow reject explicitly. Eight affected CTests pass in 2.53 seconds.

Actual deferred WAIT_REG_MEM `125053/45B5B22D` confirms the routed recorder and
native result/markers/publication; fresh native SuspendPoint and command lanes
`125053–125054/8F6AB348` pass under record-thread 1/spin 100. Native core/install,
clean title FPS and actual recorder CPU benefit remain pending. Reduced CPU-test
cycles are not elapsed time or FPS evidence; default must remain 2000.

Matched cycle33 EXE/PDB and bounded native disassembly place 15 of 69 recorder
samples in its actual 2000-PAUSE empty-ring loop. Sample frequencies are wall
positions, not CPU attribution or removable frame time. Before changing its
policy, exercise the real public CommandStream with small known payloads,
alignment/FIFO callbacks, producers at three sleep-transition boundaries,
actual wake, a separately withheld Drain, bounded stop/join and route cleanup.
All functional controls precede the explicit 100-spin budget oracle. Test-only
observers must not enter shipping code or hot-loop timing measurements. Any
deadlock/setup failure is not the intended cost RED. Keep default 2000 and make
any smaller policy optional until clean native world FPS proves benefit.

Fixture, test-only hooks and optional policy have the native proof above.
No multi-thread scaling or FPS claim follows
from the recorder's approximately 30% of one core in a diagnostic run.

## Foreign CPU write-fault readback locality, 2026-10-13

Native RED `120847/B14270F4`: all three setup, actual protected foreign store,
GPU-before-CPU-retry, fresh producer fence, sibling preservation, full native BDA
consumer, backing, guards and cleanup controls passed before the cost failure.
Actual write faults copied 65548 bytes and reserved 65728; explicit invalidation
did the same. Unchanged GREEN `121131/F846079A`: write faults copy 12/reserve 192,
far pages stay GPU-dirty/protected/old, all controls pass; explicit invalidation
still copies 65548/reserves 65728. Initial `120725` rejected missing harness CLI
registration before running the fixture, so it is not a valid RED.

Real dirty/pending image on another page inside the selected window passed
`121256/DC810DAF`: whole-window guards retain wide 65540/65544-byte downloads,
actual native/CPU overwrite/image-order/fence/guards/cleanup all pass. Shared
experiment `KYTY_CPU_WRITE_FAULT_WINDOW=1` is default-off. Separate QueueReadback
geometry remains unchanged; no side-queue optimization or proof is claimed.
Cross-page/RMW/armed-generation native controls `121409/74065F48` pass; all
native/backing words, real fences, guards and cleanup precede footprint checks.
Twenty affected native GPU CTests pass in 9.66 seconds. Separate 64 KiB-window
boundary `124439/45B5B22D` passes all 192 KiB native/backing words, guards and
cleanup plus two actual write faults/native copies/ticks, 16 bytes/256 reserved.
Initial `123835` preserved every value and footprint but missed the exception
witness. Test-only noinline store plus compiler signal fences preserve its TLS
registration; unchanged native/data/cost oracles then pass. This was not a
production RED or weakened oracle. Clean world comparison is recorded in the
launch plan; useful net throughput and other-game performance remain unproved.

Actual cycle33 diagnostic attributes 66.89% of packed readback bytes to foreign
CPU write faults. This is traffic attribution, not removable critical-path time.
Before changing the current 512 KiB publication window, reproduce a real protected
foreign CPU store into a GPU-dirty page. Verify actual native producer bytes,
protected old backing, preservation of every unwritten dirty sibling on each
released page, final CPU upload/native values, fences, guards and cleanup before
the transfer-footprint oracle. Cover a cross-page store, a fresh GPU writer,
older armed publication superseded by a writer, ordinary explicit is_write and
foreign read locality. Dirty/pending image aliases, unknown and cross-owner
requests must preserve the conservative path. A validated fault-ahead window is
only a locality policy; GPU-dirty pages outside it must remain protected.

Production changes have native RED/GREEN as above. Use a semantic write-fault
origin, preserve InvalidateMemory re-query and real fence/priority publication,
and keep any new performance experiment opt-in until a clean 30-second world
title-FPS comparison demonstrates useful gain. No synthetic result proves
gameplay speed or effects correctness.

## Explicit GPU ReadMemory touched-page amplification required reproduction

Dedicated actual imagealias113045/BC337D50 native dirty/pending-only guard PASS,
wide65540/65544 with allimage/far ordering/prefence/fence/guards/cleanup. Initial
test112340 missed60B leadingalignment after4B realimage staging; expectedring
reservation corrected fromMap alignment contract, not a productionbug/RED.
Experimental0control112037/7F7A49AF preservesoldfull locality/all6controls;
optin1threechecksPASS1.39s. World4326mean3.488889 no usefulgain, mode nowdefault
OFFpendingmeasured workload need. Actualother-game CPU/GPU perf stillunproved.

Actual105225/585CD167 RED afterALL6casevalues/nativefences/armed-old/guards/
cleanup PASS: basic4B reads65540, otherexplicit cases widen similarly. Baseline
actualforeignfault105226 PASSwide. UnchangedGREEN105628/1B2D690F explicit
4/8/12/8B, whole touchedpage dirtysiblings/currentproducer and far-unarmedold
PASS; actualGPUfault/is_write65540 unchanged; foreign105629widePASS.
Eighteen affectedCTests PASS7.52s. Crossowner110341/40843F55 realnewmergedowner/
lowhigh/far/native/backing/guards/cleanupPASS;3registeredchecksPASS1.30s.
Native104857 initialtest compilefail(far macro/missingRelease) fixed beforevalid
RED; not reproduction. Dedicateddirty/pendingimage alias origin case and actual
core/game throughput remain pending; existingguard fallsback conservatively.

Current fresh read-only ReadMemory on GPU thread widens to512KiB, although its
explicit API has a known requested span. Before behavior changes, native6case
fixture must prove real requested A and far64KiB B in one owner, protectedold
CPU bytes/unarmed priorstate, real producer/canaries/prefence armed-old/full
native+CPU values/lifetime cleanup BEFORE footprint. Only four explicit reads
may narrow: basic/same-page sibling/crosspage/new current writer. Actual raw
protected GPU-thread read FAULT and is_write must retain wide65540bytes; real
foreign CPU fault also remains wide. Old armed-generation/EOP controls still
required. New semantic Explicit/Fault origin, not diagnostic-dependent TLS,
must distinguish central HandleFault from ordinary GPU API requests. Dirty or
pending image aliases on touched pages retain old path unless proved safe.
No production change before genuine RED; same unchanged GREEN, typed/foreign/
EOP/image/BC neighbors and native world30s needed. Runtime reason byte counts
are censored by old drainlog48-row limit, so its21MiB/frame is not eligible
traffic or removable time. No fake readiness/resources/page-access shortcuts.

## Retired native-image recycling byte budget required reproduction, 2026-10-13

Mixed native103850/FE975240 PASS actualsizes4096/16384/65536, genuine native
contents/canaries/ticks for all four images; insertion evicts BOTH smaller
retired entries, retained65536/count1, heldlive large image reread unchanged,
idle/envcleanup PASS. Registeredtwo memory CTests PASS1.06s; nativecore103425
onb5dd974c PASS. Runtime pool occupancy/physicalblocks/game FPS still untested.

Same unchanged GREEN102624/25DDEDFE retained32768/count2 vs old81920/count5;
allnative imagebits/canaries/ticks/live sixth/cleanup PASS. Neighbors102625
sameEXE oversize/reuse/format/zeroCap/count24/invalidParse/envcleanup PASS;
five affected2B/BC/mip CTests PASS2.36s. Mixed-size/multiple-victim control
and actual core/game needed. No physicalVRAM/FPS gain implied by allocationcap.

Genuine native RED102201/DA41B93E onb351 plus diagnosticobserver/fixture:
allreal imagebits/canaries/ticks/live sixth/cleanup PASS; nativeallocationA16384,
requestedcap2A32768, actualretired5A81920/count5; finalcostfails. Earlier bad
earlybootstrap draft was fixed before compilation, not counted as RED.
Same unchanged GREEN and oversized/reuse/format/zero/count24/invalid-env
neighbors required; policy still unapplied. Rootnative artifact savedchecks.

Current recycler retains up to24 native images without an allocation-byte cap.
JetU59 byte-budget policy is a concrete comparison lead; actual owned pool use/
paging remains unproved. Before production, use bounded tiny real images and
measured actual VMA allocation sizes, complete native GPU uploads/readbacks and
retirement. A caller-configured cap2A should reject retaining5A after real
retirement; old count-only pool is expected cost RED. Read the still-live image
again and preserve all guards/lifetimes before cost. Test-only pool statistics
may observe actual allocations, never change GPU readiness/ownership. Then
derive byte cap from real device budget and explicit host limit, preserve exact
reuse/layout reset/handle generations, count24 bound and pending/live owners.
Allocation bytes freed may remain in VMA blocks; don't claim physical residency
or game FPS from bookkeeping. Native game/cross-device budget behavior pending.

## Independent PrepareBda upload barrier batch required reproduction, 2026-10-13

Same unchanged native GREEN095142/095144 under Selective/Legacy/SelectiveChecked
1506A129: normal and temporary pre1post1/copy2/32768bytes; actual ring-wrap
pre2post2/tick+1 unchanged. ALL native8192words/guards/repeat/new CPU generation/
cleanup PASS. Thirteen affected CTests PASS6.03s; fresh shader-target chain0/1
and registered3BDA checks PASS2.24s; actual SuspendPoint095357/3EC8F33A PASS.
Native overlapping/disjoint-same-owner and actual inside-scope SSBO reader
095621/F13E8C49 keep2pairs plus first/final/backing/guards PASS. Deferred-recorder
095622 sameEXE normal/wrap/temp actual native bytes/guards/lifetimes PASS;
observer there counts recording hooks, not native submit-thread cost.
Tiny preceding compute-chain/untouched destination and core/game FPS pending.

Untouched actual chained C writer100259/477FBA47 PASS: CPU C staysold/GPUowned,
onlyA/B copied; following nativeSSBO seesAnew/Cmarker with no extra predependency,
all3nativeowners/output guards/Cpublication/actualcleanup PASS. Three registered
new overlap/deferred/untouched CTests PASS1.29s. Core/game benefit remains pending.

Genuine native RED094147/2C44EBA6 on14a8bac1: normal/actual24KB ring-wrap/
actual8KB temporary-source all native8192words/guards/CPU generations/clean
repeat/cleanup PASS before finalcost. Each copy2/regions2/32768bytes, pre2post2;
tick7->7/12->13/18->18. Expectednormal/temp1pair, actualwrap2pairs. Test-only
idle staging-ring recreation changes its capacity, not ownership/readiness;
finite real GPU fills/publicCPUpublication justify initialwritten cacheowners.
Same test unchanged GREEN required; no production applied. Source artifacts
`_Build/analysis/yotei-performance-cycle1-20261012/checks/20261013-094147-0312999-prepare-bda-upload-batch-baseline-red`.

JetU59 UploadBatch is a comparison lead: independent CPU-dirty native owners
currently each receive before/copy/after barriers. Reproduce public PrepareBda
on two precreated dirty owners with actual native bytes/guards/cleanup, copy2
and exact transferred bytes BEFORE native barrier4 exceeds expected2. Clean
repeat must copy0; CPU rewrite/new epoch must publish real changed bytes.
No production change until intended RED. Copy commands must remain immediate:
current staging Map/Commit and temporary source retirement belong to its tick.
Any deferred final visibility must close before native consumers/submits/tick
changes; ring-wrap, temporary staging, overlap and consumer controls required.
Diagnostic239.4 buffer-copy intervals/frame does not prove upload-only frequency.

## Two-byte full color tiled upload cost reproduction, 2026-10-13

Actual RED091807/FCAD4B4D on b8d1ceb3: complete128x128 R8G8UNorm native bits,
guards, legal raw view, source and cleanup PASS before copyBufferToImage1.
Same unchanged GREEN092043/FC1BB5F1: all controls unchanged, copies0. Shared
render-target swizzle and UInt16 half extraction into compatible R16Uint view;
Unknown-format writes require actual optimalStorageWriteWithoutFormat bit.
Existing4/8B cost/odd-offset/source-next-writer, BC ownership and mip-copy
four CTests PASS1.73s. Native2B odd-pitch/R16half/capability fallback and paired
1080 GPU timing pending before game. Revisions local-only, no game FPS claim.

Paired1080 GPU092513/41553354 PASS: seven warmed alternating pairs sameEXE,
device-local source/target, actual copies1/0, all1036800words and guards/source/
guest mapping cleanup. Legacy101184ns vs fused27520ns median (-72.80% operation).
No whole-game gain implied. New2B neighbors then native core/game still required.
Primary formatless contract: https://docs.vulkan.org/spec/latest/chapters/interfaces.html
(Unknown OpTypeImage writes with format-specific STORAGE_WRITE_WITHOUT_FORMAT).

Native2B neighbors092919/DAE6CB3D PASS R8G8UNorm/R16Sfloat127x65 all16510bytes,
literal half NaN/denormal/negative-zero bits, source+4, actual later source writer,
guards/cleanup, copies0. Capability092920 sameEXE forwards real R16 extended
query and removes only its formatless-write bit once: legacycopies1/allbits/
guards/cleanup PASS. Simulated missing host bit, not absent-capability hardware.
Three registered2B CTests PASS1.21s. Core/private install/world FPS still pending.

Jetsku U59 has a direct R16Uint storage-view upload route; ours admits only4/8B.
Actual late Yotei2B R8G8UNorm tiled1920x1080 site measured6.641ms/frame in
instrumented GPU zones. Before production changes, extend synthetic public
UploadImage fixture with independent16-bit packed pattern, native exact bits,
source/guards/cleanup and real formatless-write capability preconditions.
Expected cost RED is one full intermediate buffer-to-image copy versus zero;
failed setup/numeric correctness is not a valid cost RED. Keep unsupported
capability/metadata fallback and existing4/8B semantics. Native R16float bit
patterns, odd pitch/nonzero offset and capability fallback needed afterward.

## Redundant empty PM4 tail submission required reproduction, 2026-10-13

Native RED `072755-8939373-empty-pm4-tail-baseline-red` on eeebdd96/225D7037:
all finite producer, protected old bytes, publication gate, actual native/CPU
values, marker/canaries and cleanup pass before submits2/tick2->4 exceed1.
Five required-work negatives baseline072757 pass; automatic native Begin-query
negative baseline074305/2CA780E4 passes actual reset/top/bottom/retirement.
Same unchanged GREEN074403/F8A1C852: submits1/tick2->3, every value/control
unchanged. Required transfer/fill/callback/eager/literal GREEN074404 and native
timestamp GREEN074405 all pass; eight affected CTests pass4.22s. Scheduler keeps
all explicit waits/submits/finish/flip/suspend dependencies. CP optional boundary
uses conservative handle exposure, pending dependency and current-tick callback
work. GPU zones/checkpoints are marked; capability-specific execution remains
uncovered. Native deferred WAIT passed in affected set; actual GpuSuspendPoint
074742/F2648188 PASS, new3 registered CTests PASS1.22s. Real-world FPS next.
Artifacts: `_Build/analysis/yotei-performance-cycle1-20261012/checks/20261013-*`.

Real GuestGpu NOP -> GPU-backed WAIT submits the required readback in PollReadMemory,
then progressed parser slice may BufferFlush a newly empty command buffer. Native
regression must check actual queue submissions and tick progression only AFTER
finite producer, protected old bytes, real pending publication, marker/canary and
cleanup controls. Expected required submit1 rather than redundant2. Production
must remain unchanged until genuine RED and copy/fill/priority+normal callbacks/
eager/literal/timestamp negatives establish a conservative work oracle. Existing
draw/dispatch-only m_recording_has_work is insufficient. Public explicit Flush,
Wait, Finish and SuspendPoint dependencies must stay unconditional.

## Consecutive selected eager pages native cost regression, 2026-10-13

Public BufferFlush native061744/70239F69 genuine RED after all three cases'
finite GPU/native/CPU/same-page sibling/unselected gap/owner/canary/cleanup
controls PASS. Adjacent selected same-owner pages incur two source barriers,
two host barriers, two copy commands and two publication tokens; expected one.
Gap and different-owner negatives correctly require two. All cases copy exactly
12 bytes/3 sparse ranges. Initial061057 build failure was missing test API name;
corrected to existing TryReadGpuCleanBacking before valid RED, no oracle changes.

Candidate coalesces only sorted consecutive SELECTED pages bounded by SAME
live complete owner, with nonmutating page-table ID check; retain downloader,
dirty-page union, staging/tick/token/native fence/publication and all gaps.
Same unchanged GREEN and native WAIT/EOP/typed/fused-image/BC/mip neighbors
required. Coverage of adjacent eager pages in this game and FPS gain unproved.

Same unchanged GREEN062003/204BA457: adjacent source/host/copy/token1, gap and
otherowner2; native/CPU/sibling/unhot gap/exact12bytes3regions/guards/cleanup PASS.
Fifteen affected CTests PASS6.12s (GPU WAIT, async/foreign/literals, tiled uploads,
BC and mip copies). Larger ring-wrap/group-pressure case remains pending.

## Image read visibility and covered-scope barrier regressions, 2026-10-13

Existing public GetBarriers ignores destination stage when layout/access match.
Independent planner fixture on a real tiny two-mip/two-layer image establishes
TransferWrite -> FragmentRead -> ComputeRead -> Write. Native055809/042570B7
intended RED: second reader has no required barrier. Separate055808 same EXE
cost RED: all writer/layout/new-stage-with-different-access/disjoint resource
controls pass before coveredCost1 and heterogeneousCost1 exceed zero budget.
No submitted GPU command/shader hang or fabricated value is used as reproduction.

Candidate: only reuse nonzero known read-access subset with EXACT same stage
and layout; retain original published coverage, preserve heterogeneous per-mip
states instead of claiming global union. Any new stage requires its existing
barrier, which chains visibility of already available writes. Unsupported,
write, layout and bounds behavior retained. Same tests unchanged GREEN and
existing image transitions/native affected resource neighbors required before
world retry. Actual game incidence/FPS/graphics outcome unproved.

Same unchanged native covered/new-stage GREEN060021/060022 (8A1E3DE6), existing
image-transition same EXE PASS. Partial mip/layer reconciliation GREEN060548/
93E2E211, no oracle changes. Additional actual upload-to-sampler stage/access
subset fixture native061003/AE6734D3 intended cost RED1 after all new-stage,
expanded access/stage, aggregate-mask and malformed-stage negatives pass.
Follow-up permits only nonzero literal stage-bit subsets; no aggregate expansion.
Same stage-subset test unchanged GREEN and all previous neighbors required.

Same unchanged upload stage-subset GREEN061349/B7593F32; previous partial
neighbor rerun062102 PASS without golden changes. Literal masks only, nonzero
stages; the original broad scope is retained for the following writer. Cross-game
rendered correctness and actual GPU/world cost improvement remain pending.

Compute-chain existing configuration native061004/061005 SAME AE6734D3 PASS
ordinary direct-buffer finite producer/consumer/disjoint dispatches with real
guest ACQUIRE/CS_PARTIAL_FLUSH boundaries under flag0 and1. This is scoped
configuration proof, not a defect RED, BDA/image alias proof or FPS improvement.

## Scalar ORN2 SAVEEXEC B32 wave32 baseline native failure, 2026-10-13

ORACLE AUDIT: unchanged isolated32 native052707/BFB52561 fails raw EXEC_HI,
native052708 Wave64 passes and partial32 fails. Translate.cpp masks all wave32
EXEC_HI writes by live_lanes[1]=0 before SAVEEXEC. RDNA2 says high EXEC/VCC
ignored; it does not explicitly promise raw nonzero high storage for SALU reads.
Do not patch production to preserve A5 on this evidence or label the failure a
proved SAVEEXEC defect. Old numeric fixtures remain unchanged pending stronger
wave32 raw-high contract/native ISA hardware. Supported-wave64 scoped controls
can independently validate HALF_UNIFORM; they are not a full-suite pass.

Fresh current native shader test EXE53230EFE, --wave64-only, fails existing
ScalarOrn2SaveexecB32Wave32 readback with HALF_UNIFORM1 (051903) AND ordinary
HALF_UNIFORM0 (051933). Adjacent scalar/strided controls pass. Do not classify
this as an option regression or change goldens to permit it. Add isolated selector
for the SAME existing fixture, verify its independent expected old EXEC/destination,
new low32 EXEC/SCC and unaffected high mask against primary RDNA ISA, then fix
shared decoder/IR/emitter only after valid isolated native RED. Shader opcode,
state lifetime and fixture setup all remain hypotheses. Neighbor masks, partial
waves, both source forms and wave64 controls required; game relevance unknown.

## PM4 polling touched-page download footprint required reproduction, 2026-10-13

Unarmed GPU PollReadMemory currently widens a scalar request through ordinary
CPU ReadMemory's 512KiB locality policy. Native scalar and cross-page fixtures
must verify all dirty words in each touched tracker page, real native/CPU data,
same owner, protected publication, guards and normal later far-page CPU read
before testing redundant far-page arm and download-ring footprint. Genuine RED
is required before narrowing only this GPU poll path. Preserve the existing
page-wide dirty union, owner discovery, copy barriers, tokens, max actual tick,
priority publication, ring retirement and ordinary CPU batching. Game hit and
FPS gain unknown; aggregate 20MiB/frame readback does not prove PM4 attribution.

Native genuine RED044730/E66C9D6D: scalar/cross-page native words, same-page
sibling, real publication, later CPU far read, canaries and cleanup all PASS;
download192 rather than128 and far page unnecessarily armed/published. Earlier
044420 build failed due Windows near/far macros; 044617 was setup failure from
deferred recording default and is NOT reproduction. Fixture setup corrected
before valid RED (names, early direct0/queue0 config and native push descriptor).
Same unchanged GREEN044825/04C7E627 after shared touched-page downloader:128bytes,
far remains dirty/unarmed/old, all original controls PASS. Nine affected CTests
PASS3.63s include mixed arms, new writers, true queues, async and literal EOP.
Retained actual notification token native044617 also PASS before/after actual
GuestGpu destruction; no sanitizer claim. Core/private install/world pending.

37b94dbe core044859/install044949 PASS; full neutral world30s mean3.038462,
min2.8/max3.2,651..743/26titles/exit0/no guards. Previous3.028 effectively unchanged
(+0.35% single-pair); useful game FPS gain NOT proved. Next bottleneck attribution
uses owned-PID GPU Engine utilization and independently validated Main sampling,
with diagnostic overhead explicitly separated from clean title measurement.

## Publication-to-queue notification required reproduction, 2026-10-13

After GPU-backed PM4 polling, actual world throughput fell from 3.033333 to
2.358333 FPS. A bounded native fixture will release a finite GPU producer at
the real all-blocked queue boundary and verify authoritative publication,
native words, independent queue progress, canaries and cleanup before checking
whether the real condition variable times out without publication notification.
The explicit 1ms scheduling stimulus is not natural game latency. A spurious
wake or baseline cost zero is not a reproduction. Apply a lifetime-safe shared
notification only after genuine RED; measure one 30s world retry afterwards.

Native 043400/107885A5 intended RED: all actual GPU words/11/22/33,
independent queue progress, publication, guards, cleanup and zero dispatcher
waits pass; real CV times out after publication (readyTimeouts=1). Same unchanged
043454/4AE64E5A GREEN after shared notification: readyTimeouts=0, CV signalled.
The generation closes publication-before-requeue loss; retained token is
detached after worker join; debug backing reads cannot fault under queue lock.
Six affected CTests and real asynchronous recording pass. Game recovery pending.

dca5722b native core/private install PASS; full neutral world30s mean3.028,
min2.9/max3.2,frames650..738,exit0/no guards. Prior2.358333 regression recovered,
old3.033333 baseline essentially unchanged. No useful net gain or images.

## Foreign readback queued-state race required reproduction, 2026-10-13

Actual foreign CPU ReadMemory sees A unarmed then queues its existing GPU
callback. An earlier GPU callback publishes A through real readback before
making sibling B GPU-dirty/open. The stale callback should read actual A while
leaving unrelated B pending and reserving no further download ring bytes; all
native/CPU values, queue ordering, canaries, cleanup and a new-A-writer negative
control precede this cost. Existing API only, real PendingCommands test getter,
no delay as a substitute for ordering. Native RED pending.

Native 042544/6A15CF7E intended RED: native/CPU values, new-A writer fence,
guards and cleanup pass; stale callback allocates ring64->128 and arms B.
Same unchanged 043231/E4AF6BA0 GREEN: ring64->64 and B remains unarmed/pending;
new real A writer still waits and publishes abcdef09. Shared correction is local
e55aded4. Fully-armed callback neighbor and game hit/FPS remain pending.

Fully armed queued callback native044134/9C3DA782 PASS: actual old backing and
armed/free tick proved at foreign reader wait, real priority callback held then
released, A12345678/B76543210/guards/cleanup correct; ring64->64, B remains
pending/unarmed and open tick3 unchanged. Actual async/foreign/ready-continuation
registered CTests3/3 PASS1.29s. Ring-pressure and image-only neighbors pending.

Candidate after genuine RED: authoritative requery inside the queued callback,
clean ->tick0, fully armed ->owned tick/flush if current, unarmed ->existing
record/discovery/publication. Preserve actual outer fence/priority waits,
CPU overwrite/new generation and unknown range errors. Game hit/FPS unknown.

## TLS MOV replacement arithmetic FLAGS required reproduction, 2026-10-13

Actual SafeCall and TlsRegStub in loader/jit.h adjust RSP with SUB before saved
FLAGS and ADD after restoring FLAGS. Replaced x86 MOV FS/TLS must preserve
arithmetic flags. Minimal standalone native CPU regression uses real byte
templates/final-address Call9 relocations/compiled MS-ABI callback, all64
CF/PF/AF/ZF/SF/OF patterns, two stack reservations and seven supported output
paths. Native MOV controls, real JZ/JC/JS/JO outputs, callback once, RSP/GP/R8–R11
neighbors must pass before intended flag/branch RED. Stop Call9 with RET before
the independent MOV control so it cannot mask actual stub output.

Baseline production stayed unchanged until genuine native RED. Flag-neutral
LEA adjustments cover BOTH shared stubs and preserve SetFunc/GetOffset/code
layout expectations. No title/hash/address branch, raw FS fault or game input
or performance claim. Caller SIMD/MXCSR and unsupported RSP/high-register forms
remain separate existing ABI debt; do not claim those from arithmetic controls.
Native RED032105/F0868860 has896controls PASS and882flag/branch failures.
SAME unchanged GREEN032207/69026323 after LEA in BOTH stubs:896cases/zero
failures, CTest1/1 PASS0.03s. Fixed pointer/rel32/output field offsets and packed
capacities remain. Local a980b942 core032554/install032732 PASS; original-game
FPS retry active, impact pending.

## Nonblocking GPU WAIT_REG_MEM readback required reproduction, 2026-10-13

Fresh1ea27e21 diagnostic records about100 GPU-thread read faults/full drains per
frame at WAIT_REG_MEM. Example5s/15frames:1559full drains2071.64ms (~138ms/frame),
277tick waits815.74ms; total readbacks274.5MiB. These timers overlap and are not
additive. Six frequent AGC body timings sit near their empty-query measurement
floor (~1.13% gross sampled Main-cycle estimate), so optimizing their tiny bodies
is not justified by frequency. GPU busy/gap are real timestamp-query intervals,
not host wait timers or GPU-unit utilization; historical17.95ms totalGPU claim
must not drive decisions.

Before production changes, bounded actual PM4 WAIT_REG_MEM fixture must record
a real GPU producer of protected32-bit data, execute existing processor API,
prove complete native value/comparison/publication/neighbor/cleanup controls,
then fail an avoidable synchronous GPU-dispatcher wait/full-drain cost budget.
No nonexistent new PollReadback API in the baseline, fake value/ready label,
skipped comparison, unsupported range masking or deliberate driver hang.

A candidate may submit required bytes once, suspend that guest queue while actual
publication is pending and process other queues; read real memory only when
CPU-ready. Both/all queues blocked must still drive completion and priority
publication. Cover producer-before/after/wakeup,32/64-bit comparisons/masks,
unmap/reuse/generation and newly GPU-dirtied pages. Exact page downloads must
include every GPU-dirty byte before CPU page protection is removed.
Native034342/203C9D2E is genuine costRED after actual values/markers/guards/
publication/cleanup; SAME034629/BD73E808 GREEN waits1/0/1 ->0/0/0. Real
graphics32/compute64/third-queue progress035123 PASS. Poll040047 old-arm/new
writer/CPUoverwrite/crosspage/host controls and old typed/span/EOP/BC/mip
neighbors pass. Focused CTest4/4 PASS1.77s. Buffer-backed mapped path now polls
authoritative publication; image-only fallback and sustained ring-pressure
latency remain pending. Wake-token architecture is deferred; existing wait100
is microseconds/native Windows alternative~1ms. Native core/game impact pending.

## Native occlusion and tiled upload cost reproductions required, 2026-10-13

Three read-only proposals screened before production changes. Existing
`KYTY_OCCLUSION_QUERIES=1` needs actual8x8 depth-hidden/visible raster proof,
tagged PM4 counter publication/predication and completed native IA vertex counts.
Flag0 conservative-visible controls must pass before avoidable hidden IA3 exceeds
budget0; this is a cost regression, not a claim Flag0 violates its fallback.
Same unchanged fixture Flag1 must retain visible/mixed pixels and skip only
proved hidden work. Unready/no-wait, logical unmap, lag/capacity and actual device
feature/validation controls remain required. Ignored cycle9 fixture is uncompiled.

Fused tiled upload proposal needs actual public FindImage/FindTexture path,
independent128x128 RenderTarget64KB4/8-byte tiled source, exact native raw bits,
input/output guards, real legal raw integer views and completed resource cleanup
before real copyBufferToImage count1 fails expected0 cost. Production unapplied.
Only complete supported2D/color/single-sample metadata may use a future route;
transforms/BC/depth/partial/unsupported format features preserve original route.
Bounded native GPU A/B/A on genuine dirty uploads must show benefit before game
FPS attribution; source opcode/copy counts alone are insufficient.

Actual private saved-mode preference is a diagnostic artifact operation, not a
core fix: typed guest suffix/key/enum/checksum proved, exact new output hash and
six corrupt neighbors checked. Live owned read-only36bytes at01:47:12UTC/PID16352
confirm requested/effective2Performance, default2, override-1. Complete30s world
FPS and actual render dimensions remain pending. Never infer60 from mode target.

## Configured user game preset required reproduction, 2026-10-13

Actual shared UserService NID export -sD02mFDBh4 invoked through SymbolDatabase,
after setting new config value0/1/2 without changing HLE. Existing error/null/user,
40/48/16/8/0 capacity negotiation, other-field zero and canaries first; old priority0
should fail configured1/2 last. Preserve supported40 and unknown48 tail. Name
mapping independently proved from actual consumer enum strings:Performance1
preference selects mode2, Resolution2 mode0 Quality on non-Trinity. Game-specific
capture is evidence only; source/API setting is general and default0. Production
HLE intendedRED011830/284F617C after all configuration/error/capacity/canaries:
configured1/2 stillreturned0. SAME GREEN011910/45F0E092 returns correct0/1/2 only
where negotiated capacity>=16, other bytes unchanged. Dynamic users0/1001 neighbor
012045/1ACA857F PASS; focused CPUCTest2/2 PASS0.06s. Known40/header40 and48tail
preserved; request1..7 malformed header capacity remains pre-existing debt, not
changed. NativeCLI/build/game pending; saved graphics option can override default.

## Opaque Vulkan cache source-only rejection required reproduction, 2026-10-13

Extract current exact signature factory into shared CPU policy before behavior
change. CPU test preserves literal legacy bytes and source-strict journal,
vendor/device/driver/UUID/config/diagnostic rejection, envelope sizes/checksum and
Vulkan VERSION_ONE little-endian32byte controls, then expects opaque host cache
compatibility after synthetic compiler source changes. Existing source gate
intendedRED005245/B1EABF29 fails last. Same CPU GREEN005655/51C3C357 passes full
old journal tests after correction; actual loader compiled005811. Legacy offsets
are parsed without editing cache bytes or pinning source keys. Real Vk native
roundtrip010845/21E43738 PASS:15132byteactualdriver payload, legacy139bytewrapper,
actual application cache feedback valid/hit1, full/asymmetric/nativecanaries.
Actual loader returns empty forwronghost/checksum/rehashednativeUUID. Focused
CTest CPUjournal/native2/2 PASS0.53s. Full existing182MB gamecache reuse and any
warm-up/runtimeFPS gain remain pending; no FPS claim from these controls.

## Runtime native buffer length reuse required reproduction, 2026-10-13

Real GenericCompilation1/8/32 reads for waves32/64, all loads/stores live and
SPIR-V validation; count runtime SSBO length queries by actual variable/constant
descriptor slot/member, each length still feeds native bounds. Six modules must
pass setup before repeated query/view fails one-query budget. Production pending
fix: intendedRED005054/C80DCEE4 allsix setups/bounds pass then repeated query
budget fails. Any candidate must keep exact4800 lanes, native range rebinding,
all guards and compare fixed-batch GPU A/B/A before a full cold game retry.

## Next cycle required reproductions, 2026-10-13

Generic channel selection:52 literal native cases before actual invocation-entry
variable-division cost. Counts1/2/3/4, reversed/constants, exact end/OOB/prefix,
guest wave32/64 and asymmetric EXEC. Intended native RED001021/ECA63286 passes
all52 controls first, then four divisions per resource fail expected0 for both
waves. SAME GREEN002637/598C217D has0 divisions. Native100requests/4800lanes
002637-9134037 byte-identical to3b235505 (0 changed/missing/extra;144 known UNorm
golden errors both). Category/codegen neighbors PASS002639/002704; CTests2/2
PASS0.93s. Host selected source0..3/count>=1 exact mask/count3 proof applies also
to future positive counts. Core/build and useful world FPS still pending.

Public GPU ReadMemory ready-A/open unrelatedB on distinct4KiB pages of same16KiB
owner: real EOP store, old completed native tick, bounded priority publication gate,
clean/published and real newer GPU/CPU generations. All native/backing/cleanup
controls first, then intended native RED001020/ECA63286: oldA ring0->64,
cleanA64->128, CPU-knownA256->320 and unrelated B armed. All four native/backing/
cleanup controls passed first; newA truly waited its new tick8->9. Queue1 disabled;
no shader changes needed. Same GREEN001450/9941F873 and final001641/EBB5F04C
avoid each unrelated allocation; newA still waits10->11 and readsabcdef09.
Typed neighbors002004/C8A3296E PASS: mixed armed/unarmed span, same-page newer
sibling, armed/clean is_write CPU-dirty+BDA epoch BEFORE pointer write, then exact
native CPU upload and A/B guards. CTests2/2 PASS0.81s. EOP/fault/BC partial/mip
ownership neighbors001451..001453 PASS. Game FPS remains pending native build.

Boundary not directly covered: request crossing an existing owner's end into
mapped unowned memory; production conservatively retains original discovery.
Invalid/null/unmapped cases also retain that path. Separate readback queue stays
off. No all-game correctness or stableFPS proof from these synthetic checks.

## Generic format control cost validated; game comparison pending, 2026-10-13

Actual emitted shared conversion helper, nine legal format inputs, bounded taken
CFG walk counts evaluated selection decisions after unchanged lazy-conversion
goldens. Mutually exclusive integer/normalized/scaled format should require one
selection; Float can require one further width decision. Current sequential six
checks fail only this work budget: native235420/D38503A9. The same fixture handles
both10-flag and5-parameter ABI before RED. Unchanged GREEN235504/0F796FBD has one
decision for integer/scaled/normalized, two for Float. Native235421/235505 each
100requests/4800lanes are byte-identical (0 changed/missing/extra);144 existing
UNorm precision errors remain in both. Static mode2 neighbor235544 has0 errors;
generic codegen neighbor235507 and focused CTests2/2 PASS0.79s. Game gain pending.

## Ordinary native buffer fill required reproduction, 2026-10-13

Public DispatchDirect synthetic full literal UInt fill after independently verified
nonuniform GPU primer; retain CPU-old before fence, native dirty owner, exact64
words and neighboring sentinels after real completion. Only then the baseline
GameDispatch1 should fail expected0; candidate must keep actual transfer work.
Native intended RED235325/A23D7856 verified the real GPU primer, CPU-old and all
native/golden/sentinel controls, then GameDispatch1/BufferCopy0 fails expected0.
No production fill change. Saved hot kernels representing95.967ms/65.5% of
146.43ms compute profile are excluded by current pure-fill proof; remaining runtime
coverage unknown. Generic conversion hotspot is the next implementation priority.

## Recording budget validated; gameplay measurement pending, 2026-10-13

Next independent performance candidate: two real load/add/store dispatches with
an8ms bounded host stimulus between them, warmed pipeline, exact modulo32-bit
initial+2 output and untouched guards. Intended native RED233238/408E8B32 retained
the aged tick; unchanged GREEN233751/86C5F063 and final234008/F083CE9D advance it.
All numeric/sentinel controls passed before the cost assertion. Five CTests pass
in2.00s: real dispatch cost, direct/async policy thresholds, disabled policy and
native indirect producer2,1,1 -> eight exact output words without argument download.
Active1000us scheduler/PM4 WAIT/resume/command-stream and EOP publication neighbors
pass. Budget remains opt-in/default0; native game FPS improvement still pending.

Existing default --native-indirect-only fails the GPU descriptor-count dependency
on candidateD228C047 and archived prepatch408E8B32 identically (234010/234039).
Existing --eop-write-ordering-gpu crashes at unmapped/unregistered label observation
on candidateAA596EA7 both active/default and archived prepatch5D01A13D (234128,
234141,234237). These are retained baseline debts, not passing affected controls.
The separate registered EOP IRQ/publication/generation neighbors pass234129.
Archived runner's initial missing-DLL failure234030 is not semantic evidence;
234039 uses the normal native DLL directory. No GPU reset/hang occurred.

## Performance cycle 3 diagnostics, 2026-10-13

Real transfer-queue AGE-off bootstrap RED223050/5D01A13D now reproduced with
distinct native families0/1. Unknown warm producer safely falls back; A is truly
completed/CPU-old, B remains unsubmitted on another4KiB page of the same16KiB
owner; all value/setup/cleanup controls pass before expected queue eligibility
fails. Earlier duplicate test-accessor compile failure and old-executable CLI
rejection are explicitly not RED. Production fix remains pending, fixture opt-in.
Required safety neighbors: binding preparation followed by flush before native
write, owner JoinOverlap copies, image-to-buffer copies, new writer/CPU overwrite,
history reset and actual submitted-but-unfinished wait. Keep unsupported cases
on the existing ordered graphics path; do not bypass the incomplete-log guard.

Static-format mode2 collector223120/5D01A13D:4800 lanes/100 requests, all original
goldens PASS. Generic0 baseline215050 has144 known endpoint errors; all144 profile
differences improve those goldens, no new lane errors. Generic codegen neighbor
223122 PASS. Dirty GPU metadata/zero-stride/swizzled cases remain outside this
matrix. Two private300s warm attempts never reached a full world sample; no FPS
or broad compatibility claim. Existing native-name regression remains proved.

CPU diagnostic now has intended RED214049/58794710 at exact OS-name oracle;
unchanged GREEN214122/836A6F68 passes ASCII/UTF8/null/no-Tracy-startup. Production
change publishes native descriptions without starting Tracy; no guest/GPU ordering
or shader emitter change. Native core/name-to-role runtime mapping remain pending.

Before profiling new performance changes, establish native OS thread names while
Tracy is disabled. CPU-only regression must restore the original thread description
and then fail the exact ASCII-name oracle on the existing implementation. Same
fixture after the diagnostic correction must prove UTF8/null handling and no Tracy
startup; this is measurement support, not an FPS improvement. Current tests and
native runtime title baselines remain separate. Three agents research current PRs,
GPU work and CPU waits; root retains serialized native operations and30s title FPS.

## Performance cycle 2 required regressions, 2026-10-13

LiteralEopPublicationCost now has a valid native RED: build211441 PASS,
test211508/BA99ECCF, eight setup/native value/ownership/CPU overwrite/WAIT/IRQ
controls pass before the intended zero-download cost assertion fails (ordinary
literal reserves64 bytes, repeated literals128). All native work cleaned up
before abort. Earlier fixture ACCESS_VIOLATION was missing test fault-handler
bootstrap and is explicitly not the reproduction. Shared correction now has
unchanged eight-case GREEN211809/DD73A957: known-only reservations0, mixed unknown
reservation64, all semantic controls retained. Existing EOP literal and native
consumer/generation/IRQ neighbors211818–211819 PASS. Final native eight-case
GREEN212334/4BA4B219 also PASS. Three span neighbors212333 PASS: 64-byte caller
payload mutation, an allowed eight-byte store spanning tracker pages, and a merged
dirty range with real unknown GPU prefix/suffix. PM4 wait/resume and command-stream
neighbors212232/F1763B39 PASS. Native build/install PASS; game212552-5ee908
completed30-second title FPS sample at2.257/min2.1/max2.4, no gain, exited0.
Possible slowdown requires a fresh matched control; no visual check performed
under the user's FPS-first rule. Tiny mixed
split ranges can reserve128 bytes instead of baseline64; no universal byte-saving
or FPS claim follows from the pure-literal cost proof.

- Known ordered EOP literals: actual GPU producer/store/consumer must agree for
  32/64-bit labels; CPU label and following WAIT-dependent work stay old before
  the native fence. After completion, exact literal bytes and interrupt ordering
  match baseline without a redundant download-ring allocation/copy for that span.
  Keep pending ownership even when no unknown-byte copies remain. Cover mixed
  dirty pages, newer GPU writers, CPU overwrites, repeated labels, partial masks
  and false WAIT conditions. An early predicted WAIT success is not a fix.
- Repeated subgroup votes: compile a real bounded scalar-loop module and prove
  duplicate constant-true subgroup votes on the supported wave64/native32 path.
  Preserve low/high EXEC halves, partial waves, helper masking and native numerical
  loop/guard cases. Driver optimization may eliminate the cost; FPS is unproved.
- Resource-cache invalidation: call the actual IsRegionGpuModified predicate;
  show that unrelated image registrations cause redundant scans while returning
  correct answers. Relevant image writers, range collisions, GPU/CPU dirty state
  and lifecycle changes must still invalidate the cache. Instrumentation alone
  does not prove an optimization or runtime gain.

Root owns all native operations. User requests recurring three-agent proposals,
sequential correctness checks and 30-second in-game window-title FPS measurements,
without menu navigation; inspect images only after a measured FPS increase.

## Generic conversion reduced work; precision debt preserved, 2026-10-12

Actual-helper9-path cost RED195324/E74AEA7B -> unchanged GREEN195549/0ADF5A1B.
UInt/Float32 skip unrelatedFDiv/casts/half/sign work; normalized formats retain
one required division. Existing codegen-size neighbor PASS. Native4800-lane
baseline195325/candidate195550 are bit-identical with no missing/changed lanes;
both measurementexit2 with144knownUNorm8-one errors. Independent goldens unchanged.
ExactGPU speed/worldFPS, othergameplay titles and strictUNorm precision remain
pending. Source-generated cache key must refresh naturally after emitter change;
do not force old journal or blob signatures to avoid new compilation.

## Performance cycle: current proved/remaining scope, 2026-10-12

PM4 protected-page false sharing has intended controlled RED192851/F1A166EF and
unchanged4-case GREEN194021/002DECF3, including GPU-modified header/payload and
cross-page controls. WAIT/resume, rewind and scheduler neighbors PASS. Legacy AA
centroid bundle/PushMarker retain original payload; their nonblocking prediction
is conservatively deferred, not a new bounded-read correctness claim.

ConservativeImagePressure fails safe on-demand recovery in aggregate
performance-memory suite193611 and again in scoped unfixed-source control193905.
Separate existing debt; the PM4 correction is absent in the latter build.
Native data is finished/unmapped before assertion; no GPU-reset reproduction.

Compute prefetch controlled RED192852 proves true distinct zero-stride program
identity before the suppressed queue request. Production fix pending. Indirect
metadata/cached memo must be tested with GPU-old/raw-new/once/tick and CPU mutation
controls before allowing speculative clean-backing reads or inline policy changes.

Required generic-conversion cost regression BEFORE production: compile public
synthetic generic-format BUFFER input and walk actual helper CFG under legal
format flags. UInt32/Float32 path must avoid unrelated normalization/half/signed
conversions; native4800-case results must match baseline byte-for-byte under
unchanged denominators/rounding, including a separately recorded strictUNorm1ULP
baseline debt. Path-cost proposals are not yet executed or a measured GPU gain.

## Performance cycle: PM4 page sharing and exact pipeline prefetch, 2026-10-12

Latest user now authorizes WSL shutdown and recurring three-agent proposals with
root-owned sequential native fixes/tests/game comparisons. This supersedes older
leave-WSL-running records. Begin from clean published a73b9592/c0 source; archived
sampler drafts are not active production changes.

Required PM4 reproduction BEFORE production: a CPU-owned, byte-exact clean
WRITE_DATA packet shares a protected page with unrelated pending GPU-written bytes.
Run the actual GuestGpu parser/lookahead; correct packet/output and dirty sibling
canaries must survive without advancing submission/full-drain solely for fetching
the clean packet. GPU-written packet/header/payload controls must instead observe
the producer's new bytes with normal coherence. Preserve original guest packet
identity for speculation and read fresh bounded packets across wait/resume; do
not snapshot a whole mutable command buffer or read unbacked/dirty ranges directly.
Current direct packet dereference is a page-false-sharing lead, not yet proven.
Saved PM4 fault attribution may describe the previous opcode because header access
precedes SetPm4Op; do not assume every R_RELEASE_MEM-labelled fault is in its handler.

Required pipeline reproduction BEFORE production: after compiling a genuine
compute source, prefetch the same source with a different supported buffer/static
state permutation. Its actual pipeline must be queued/prepared rather than
discarded solely because source address/wave was already seen. Repeated exact
requests deduplicate; unsupported/dirty specialization metadata does not trigger
speculative host faults or admit stale input. Use real cache/program keys and
observable dispatch/once/canaries, plus bounded queue completion. Current coarse
m_compute_sources gate is a lead for known-source late synchronous misses.

Third candidate: subword detile currently uses two atomics per element. Before any
word-owned store optimization, prove complete aligned-word destination ownership
and retain atomic tail/padding paths; run numerical CPU/GPU parity for 8/16-bit
formats, odd rows, arrays/mips, tiling families and untouched padding. Loading-only
tiler timing does not prove steady world cost; collect an actual world profile first.

## Dynamic material sampler identity and mirror coherence, 2026-10-12

Required regression BEFORE production: scalar-buffer-loaded four-word genuine S#
with independently GPU-computed image/sampler selectors and separate bounded roots.
The current default-sampler substitution must be distinguished from valid image
heap/control planning. CPU planner/materializer must preserve exact sampler root,
16-byte record stride/prefix and raw byte bound48/32/31/49, original selectors and
compiled/reference equality. A baseline image/setup failure is not sampler RED.

Bounded real GPU proof then uses genuine nearest wrap versus clamp on two2x2
images at UV(-0.25,0.25), independent imagekey/samplerkey, four static S# controls,
real producer atomiconce1, native image/output readbacks and all guards. Dynamic
wrap image controls must pass before clamp mismatch is counted as intended RED.
Before enabling the dormant host mirror broadly, require same-frame CPU S# change,
bound shrink2->1, GPU-written S# after earlier successful mirror, and unreadable/
incomplete record rejection. Current mirror identity omits bounds and can retain
old region/count on dirty/unreadable input; do not silently use those stale records.
Logged fallback CS sites prove substitution but no water/fog/vegetation attribution.
Ignored CPU/GPU proposals: _Build/analysis/yotei-effects-20261012/.

## Effects review: statistics lock and alpha coverage, 2026-10-12

Required statistics reproduction BEFORE correcting lock ownership: enable real
DrainStats accounting around existing BC partial-publication and GPU DCC-clear
fixtures; preserve native texels/metadata/guards. Current explicit publication
acquires BufferCache while holding the texture lock, and its DCC accounting query
reacquires the same TrackingSpinLock. Host recursive-lock rejection is a bounded
valid RED; disabling metrics or loosening lock checks is not a fix. Supply exact
known-DCC classification inside an already-held lock while ordinary queries keep
locking. BC/BDA write ownership is a separate pre-existing unsupported gap.

Required alpha-to-mask reproduction BEFORE production: actual DB_ALPHA_TO_MASK
and DB_SHADER_CONTROL disable override, real pixel COL0 alpha0/1, depth-only
no-color render at1x/4x. Alpha0 must preserve old depth when enabled, alpha1 must
write; disabled/override controls write both. Required pixel-stage retention must
be proved separately from Vulkan coverage state. AMD quad dithering at fractional
alpha has no proved portable Vulkan mapping yet; do not claim exact equivalence.

Current BC and actual121-layer HTile regressions have intended RED/unchanged
GREEN with ownership/registration neighbors; live effect correctness pending.
Ordinary counted indirect draw10 cases already pass baseline after fixture controls
were corrected. BDA20-mode stops at explicit FLAT writable-ownership guard before
execution; not a COUNT failure. NativeIndirectDispatch's old descriptor-sync
assertion also fails with both new production patches absent; numerical diagnosis
pending, no full-suite PASS claim. Details: first launch checkpoint.

## Effect cycle: GPU-produced counted indirect draw reaches vegetation, 2026-10-12

Required reproduction: actual guest command processor reads GPU-produced COUNT
with CPU shadow0 and real ownership/protection intact. Independent RGB regions
and vertex-index coverage must execute min(N,max3), N0/1/2/3/5, ordinary SRD and
BDA producers, indexed/nonindexed packets. Producer atomic-once1, GPU count
readback, argument/count/draw guards and true-zero no-work are mandatory controls.
Current direct count dereference may already fault coherently: if this fixture
passes, close the lead without modifying production. An actual grass/particle
packet/count witness is required before claiming visible causality.

## Effect cycle: HTile clear over actual registered array extent, 2026-10-12

Required pre-fix regression: register121-layer D32+HTile through real depth
target discovery. Clear/expand all actual layers, update31/32/120 individually,
refresh memo and reject121/UINT32_MAX/unregistered addresses. GPU seed0.625,
acquire selected slices with pending clear0.375, read back31/32/120 and untouched
neighbors. A later actual GPU0.75 on consumed32 must survive second acquisition.
Current uint32 clear state loses registered layers>=32. Use registered geometry
and metadata footprint for bounds; do not replace the limit with another magic
capacity, alter clear-value oracle or suppress depth-register validation.
Real effect/shadow use remains a live witness requirement after synthetic proof.

## Effect cycle: cached BC array after partial buffer writes, 2026-10-12

Required regression BEFORE production: real cached BC7 array,3layers/8mips,
CPU A remains stale; one GPU buffer writes B at a nonbase packed-tail block.
Full24-subresource BC readback must see B and preserve every other A byte.
Independent native buffer control and guards must pass before wrong consumer
is an intended RED. Neighbor starts with genuine GPU image C, then partial
buffer B; C must survive even if the write owner initially lacks the array base.
Extend bounded BC4/5/6H/7 cases, repeated writes and ownership replacement.
Current experimental BC_BUFFER_WRITES flag is a comparison, not a safe fix.
An actual reflection/water pass witness is still required for a visual claim.

## Active effects debt after user inspection, 2026-10-12

User now visually confirms prior geometry/texture artifacts gone. Current request
is shadows, grass/vegetation, water, particles, wind, volumetric fog, fire and
reflections, with vegetation/shadows first. Old192 matrix rows are diagnostic
debt, not an independently proved current visible defect. Preserve prior proposals.

For each effect: identify a current pass/indirect work count, input/output resource,
native rendering state and visible result; record its required reusable synthetic
regression before changing production. Three alternatives per cycle must be
supported by these witnesses. Keep conservative unsupported errors and capacities;
warnings (121depth slices/first32HTile, partly unmapped image, placeholder records)
are leads, not specifications or permission to suppress work/validation.

Manual user game133526/B28453B2 ended13:38:50UTC/exit0. Native runs stay serialized.
User removed old3-attempt cap; bounded lifetimes and one native slot still apply.

## New launch authorization and recurring cycles, 2026-10-12

User explicitly removed the original-game attempt limit. Native fixes/tests/game
launches are now authorized in recurring batches of three sequential candidates.
Historical stopping statements below no longer govern current work. Bounded
lifetimes, serialized native execution and regression-first proof still apply.
Start from current B28453B2 game baseline; choose regressions from actual new
evidence, rather than assuming the old192 matrix holes or246 journal mismatches
still describe the current visible blocker. Stable1920x1080/~60FPS remains the goal.

## Vertex journal shape and original-game debt, 2026-10-12 12:38 UTC

Source5b231fdf native build/install/help PASS; packageB28453B2. Fresh full corpus
2770 distinct inputs:2524 PASS/246 FAIL, all246vertex resource-shape mismatch,
before emit. AllCS542/PS1331 pass. Evidence fullbatch122604 (FDF74819).
Native single-record238 without today's Phi code123522 (C9118E81) still fails;
do not attribute the246group to this port, or claim all246 baseline-proved.

Required next diagnosis: compare current translated buffers/images/completion
against recorded specialization for preserved vertex inputs; identify the earlier
shared semantic change or missing replay metadata. Keep the cardinality check,
original journal/code and all captures. Do not fill new specialization entries
with defaults, remap indices by guess, or relabel this failure as a shader PASS.
Any production correction needs its own independent regression first. Current
native replay rejects these mismatches before specialization; cold live paths
use actual resources, but a successful game launch remains unproved.

Record238 pre-Phi IR lead:7 distinct live buffer operand groups versus recorded6;
15-block CFG prevents embedded-fetch lifting under older f2593c61. Required
synthetic regression before modifying it: recognized embedded vertex fetch within
multi-block control flow, preserved conditional/instance/vertex indexing and SSA
dominance, accurate native-attribute versus ordinary-buffer resource cardinality,
and unsupported CFG rejection. First prove actual ShaderInfo counts; the IR's
operand-group count alone is not that invariant or proof for all246 failures.

Original192 missing matrix rows, body/world stability and1920x1080/~60FPS remain
pending. Historical3/3 original-run limit was not reset; no new game run occurred.
Further game capture must use new explicit launch authorization. Sources restored
byte-exact and no owned native processes remain; details in the first launch checkpoint.

## Upstream ports proved; remaining coverage, 2026-10-12 12:22 UTC

Required Phi/DMA/BC/raw/partial-copy reproductions below now have independent
native RED->GREEN and12 focused CTest PASS. Local commits3f98bd1d/30db5f71/
2b5504f3 and [evidence](yotei-upstream-ports-validation-2026-10-12.md) preserve scope.
Pending final native core/auditor build and fresh full corpus. No game retry.

- Original192 first64/512 matrix holes and stable objects/~60FPS unproved.
- Full default ResourceTracking still fails pre-existing mixed numeric class
  policy; full Materialization UniformVectorDescriptorRead debt remains.
- Existing relative-register test hits ConditionRef argument-count validation;
  guarded scalar descriptor disabled/null-BVH test also fails without the Phi
  behavior (saved absence executable58669B72). No core suppression or broad
  suite success claim. Six actual Phi neighbors pass; typed unsupported store
  still rejects transactionally and supported raw GPU fallback is tested.
- Broad image-overlap GPU test initially had stale two-export MRT fixture state;
  native absence121557 reproduces it, test-only reset restores coverage/UV.
  Follow-on121952 reaches unaligned writable buffer adjustment2/stencil
  binding failure. Need independent regression and baseline attribution before
  fixing that shared capability; broad suite remains failing.
- Phi GPU proof uses independent harness bindings and exact descriptor-word
  checks, not production guest-address resolution. DMA IndirectBuffer high16
  and40-bit bounds have planning/runtime checks, not separate mapped boundary GPU
  or decoded indirect-buffer shader proofs. Other-game runtime unavailable.

Historical3/3 original-game attempts remain exhausted; current authorization
covers these ports/tests and records, not an automatically reset launch budget.

## Authorized upstream-port regressions, 2026-10-12 11:20 UTC

The new "делай" instruction authorizes implementation and bounded native tests.
Historical original-game attempts remain 3/3; no additional game run is implied.
Before changing production behavior, reproduce each candidate independently:

- BC/raw alias ownership: actual GPU-written raw integer image with a compatible
  BC7 alias, multiple layers/mips and packed tails. Read back the BC consumer;
  require correct written bytes and unchanged untouched layers/padding. Extend
  to BC4/5/7 and partial destination copies preserving GPU-owned bytes. Use
  current local upload-layout contracts, not newer upstream layout fields.
- Writable descriptor-word Phi: CPU planning for a pure user-data scalar diamond,
  both polarities/input orders, nested OR plus ADD/ADDC carry, detached interpreted
  and compiled snapshots, independent descriptor preservation, unchanged GPU
  SSA edges. Reject memory-loaded and lane-derived predicates. GPU proof requires
  both valid mapped destinations, bounded writes/atomic-once and neighbor canaries;
  CPU planning alone does not prove GPU execution or game behavior.
- First-use direct DMA base: decoded FLAT address from an adjacent user-data
  low/high pair, without CPU SMEM/SRT reads. A 16KiB window crossing cache pages
  must be resident on first dispatch; check endpoint, unused user-data decoy,
  unmapped/null boundaries, atomic-once and GPU-dirty owner preservation. Trace
  supported expressions conservatively; do not scan arbitrary pointer-looking
  values or infer roots through memory-loaded operands.

Game causality, stable visible scene and approximately60FPS remain unproved.

## STOP after3 attempts: residual matrix and runtime debt, 2026-10-12 10:10 UTC

User's attempt budget3/3 is exhausted and work is stopped. Entries below are
unproved cases for a possible future explicitly authorized task, not instructions
to continue now. Stable picture/~60FPS remains unachieved.

- Attempt2 true DA4 BAD bank now fills943/1135 rows after known metadata
  residency; holes1050..1113/1562..1625/2074..2137 remainzero. Target24DWORDs
  match zero producer AFTER and VS readback, so no later positive-loss proof.
  Actual root pages now present, code unchanged E386. Executed input/EXEC/LDS
  and store witnesses for first guest wave are still missing; simple1135 loop passes.
- Attempt3 full-wave flag sanity passed BEFORE original run, but300s original
  produced no DA4/VS module capture and no visible scene. No equivalence,
  matrix-effect or gameplay-FPS conclusion is justified from loading/title30.
- Old UniformVectorDescriptorRead compiled/reference result mismatch reproduced
  WITHOUT this metadata observer in092111; focused5 cases pass093817. Broader
  materialization suite is not green. No unrelated repair attempted under budget.
- GPU shader/image regression proofs do not prove stable nearbyobjects/body,
  cross-game compatibility, internal1920x1080 or60FPS. No further native
  test/game/implementation/research permitted until new user instruction.

## Virtual null-reader values are not backed memory, 2026-10-12 09:38 UTC

Attempt1 introduced an original early crash: successful provider reads at0x30
were synthesized zeros for guarded null paths, then erroneously preacquired as
backing. Independent CPU test first reproduced current metadata error093738
before observer filtering; same5 cases passed093817 after provider contract.
The new synthetic_zero_read_end defaults0, is declared only for GPU zero-provider,
and excludes whole successful reads starting below its end, exactly as the
provider defines them. Real exact-boundary ranges remain, values stayzero,
default ordinary/strict cookies, failure/memo semantics and Memory errors remain.
Original attempt2 and refreshed GPU pointer/dirty-owner neighbors pending.

## First-use SRT metadata pages read through BDA, 2026-10-12 08:46 UTC

Attempt1 preparation under user's new maximum3 attempts. Before any production
preacquisition change, synthetic mapped root/child/control/output in distinct
16KiB pages: actual SMEM resource materialization reads root/child metadata;
only ordinary control/output SRDs acquire cache buffers. The real dynamic FLAT
path must read root+24 pointer and child+152 positive value on first dispatch.
Record PTEs before/after, ordinary positive control, atomic once1 and canaries;
cleanup before assertions. No loops, blind replay, fabricated resources, title
selection or arbitrary userdata pointer scans. Expected RED is wrong first-load
value with valid materialization/other controls, not a driver error.

Possible shared correction only after RED: retain successful, bounds-validated
SRT materialization read ranges in ResourceSnapshot, acquire those known metadata
pages for uses_dma before PrepareBda. Cover memo refresh, failed reads, range
overflow, duplicate/coalesced reads and GPU-written data preservation. Original
actual root-page PeekPage was absent in all4 captures; actual GPU PTE/load remains
unobserved, so synthetic correction is not yet proof of original causality.

## Plain persistent loop passed baseline, 2026-10-12 08:30 UTC

TESTONLY ceeb543d uses WG512, guest64/native32/groups1/count1135/step512,
all1135 positive row markers, all512 per-lane store counts, scalar bailout=0
and complete canaries. Four independent scalar checks bound a broken vector
induction variable; there is no LDS/readlane/barrier inside the divergent loop.
Native target082842PASS; baseline083006 E007BF8C child0, same shader production
7cb371e7. The tested fixture was A32012C8 (later comments only cleaned).
No intended RED, so no loop production correction. Nested phase/gate/input
ancestry in actual DA4 remains unproved.

## Persistent matrix producer loop and surrounding meshes, 2026-10-12 08:23 UTC

Required independent bounded guest regression before any loop/EXEC correction:
logical WG512 on native32 with guest64, j=localID, count1135, step512;
restored EXEC and positive indexed store witness on all valid j over3iterations.
Check first64 of each512block and tail boundaries, counts512/1024/1135 and
sentinels; no workgroup barrier/readlane inside divergent loop. Failure must be
wrong expected readback, not unrelated driver fault. Basic lane grouping already
passes but is loop-free and does not prove the large producer's nested phases.

Actual original da4 producer covers1050..2184 and leaves1135zero rows for BAD
bank versus fillsall1135 for FINITE. Inputs differ, so actual executed masks,
values and first relevant source/PTE reads require proof. Captures75refs/41blobs
complete; no subsequent loss of target24positiveDWORDs. Existing snapshots
contain only heuristic BDA pages; missing captured page does not prove unmappedGPU.

Targeted original081727 ended321 at semaphore counter/wait with unknownVkResult;
keep heavy retries stopped until bounded diagnosis. Instrument stability does
not prove all static/world meshes correct: user explicitly points to nearbyobjects.
The diagnostic job-capacity clamp doesn't describe internal strided loops; disable
JOB_RANGE for actualproducer capture until a semantics-aware admission hint exists.


## Late image-backed BDA fringe: regression resolved, 2026-10-12 07:50 UTC

The actual production graphics preparation phase reproduced the defect before
changing production: `checks/20261012-074808-0238556-late-bda-production-phase-red`,
C07E7549 executable, child -1073740791. The new owner/PTE, image pixel,
ordinary SRD, preserved GPU marker and atomic counter=1 passed; the first
FLAT fringe read returned zero instead of 0x71335599.

The identical fixture (SHA256 7B870F50792AABA38291127E3651FD890447DEABE7A50C23D18950C13156CB86)
passed after final BDA synchronization following resource acquisition:
`checks/20261012-074940-4760654-late-bda-production-phase-green`, D3CC0DE5,
child 0. Graphics and both direct/indirect compute use this ordering before
snapshot/command handles/chains. Existing image-binding, first-fault deferred,
rendering/chain protocol and ordered EOP neighbors passed at 075016/075025/075026/075027.

Still pending: original game retry; direct/indirect compute integration variants;
late vertex/index owners created after graphics preparation; forced upload-ring wrap.
This image-backed reproduction does not establish the zero-matrix producer cause:
the previously captured 549 dispatches had no image bindings.


## Late image-backed BDA owner fringe (2026-10-12 07:24 UTC)

RequiredTESTONLYrealmaterialization reproduction beforeanysharedsyncfix:
earlierlegitimateGPUwrite inregisteredpageA; freshsampledlinearimage8KiB
crossesA/B; FindBuffers registersA beforePrepareBda, RebindImages mustactually
merge32KiBownerAB AFTERprepare. KnownmappedCPUfringeB+0x2000 isoutsideall
requestedimage/SRDspans. PublicCompiledguestFLAT/image/ordinaryloads andatomic
oncecounter: image/ordinaryvalues+GPUmarker/PTE/sentinels positive, Babsent
afterprepare thenPTEactualnewowner nonzero beforedispatch. Knownnonzerofringe
mustreadcorrectly onfirstdispatch, counter1 forbidsblindproducerreplay.
Noactual lateowner =>invalidsetup, notRED. FrozenB445DB3A .inc, defaultcore
unchanged. Thisgeneralimagecase doesnotprove549cause (itscapturesimages0).

## First64 matrix rows: wave grouping baseline and unresolved inputs (2026-10-12 06:16 UTC)

Newreusable fa7ee9fb TESTONLYgroupingfixture64866D5B: nativebuild060754PASS,
run061225 C41A4AE1 allsixWG64/128/512 full/sparseEXECcasesPASS, independent
logicalIDs/READLANE31/63/first/fullsentinels. No groupingRED, no productionfix.
Existingtwo-wave47/LDS tests retained. Count349 is a workquota, notWG349;
native64/generalallowedVulkan subgroup permutations remaincoverage limits.

Capturedfirst64-zero vsremaining285finite shape remains a lead. Elevenfixed
assetinputs match/nonzero first64XYZ, but42SRTwords/dynamicpalette/written
scratch differ; fullinputequivalence notproved. MissingBDAcapturepage is not
actual host-unmapped/fault proof. Requirednextcapture filtersjobcoverage2097/
2098 beforequota, keepsactual page/fault/EXEC/inputmetadata. First-useBDA
zero-until-next-frame behavior is a sourcelead, notdemonstrated549causality.
Anysharedresidency/ordering fix needs knownfinite boundedfirst-useGPU RED,
sameGREEN andneighbor atomic/side-effect safety; never blindlyreplay work.

## NGG partial native vertex-wave entry count (2026-10-12 05:30 UTC)

Update05:50: original054138 (B048335F) endedexit0/300s, severegeometry persists
in210sownedimage,FPS~3.3diagnostic/internalabove1080. CorrectNGGentry proof
stands; doesnotresolvepalettezeros orprove~60FPS. Newactualreaderrows match
before/aftercorrespondingCS, zeros predatethosewriters; missingproducerjob
coverage requiresnewdiagnosticselection, notspeculativestore/mathfix.

Update05:42: production1c9e3dfa committed; focusedCLI b026e202 separately.
Unchangedfixture053446GREEN,422records/12cases,71C43725; embedded053448PASS.
Existingmetadata/export/cacheguards053823PASS viafocusedmode(FD31DBBE).
FullCFG053704 abortmatchesolder215846/6ba0computeempty-declarationsbaseline;
fullsuite remainsred, oracleunchanged. Core053904/install054009PASS, original
054138 nowACTIVE; actualgamecausality/host64/sparseinitialmasks notproved.

Update05:32: nativebuild053046PASS,fresh96DFB5CC. SAMEfixture053122 intended
REDafterall12nativecases andcleanup: wave32S3low8=32vsinitialactive1/17/31;
wave64low8=64vs1/17/31/32. AllvertexIDs/records/sentinels/upperS3controls valid.
Productioncommoninitialpopcount correction nowauthorized for BOTH ordinary
andWaveHalves paths; preserveSGPR2capacity andupperS3group/waveID packing.
SameunchangedGREEN plusCFGmetadata/exportguards andembeddedfetchneighbors
pending. No actualgamecausality proven from this synthetic count regression.

Upstream cf77fb43 active initial ESVertCount already exists behind an opt-in.
Required loop-free native RED before any default change: public guest vertex
compiler, point-list/discard pipeline; capture initial EXEC as FIRST guest
instruction, before S3-derived mask changes, then rawS3 and VertexIndex into
bounded independently ticketed records. CPU bitloop popcount of initial mask
checks S3 low8, all requested indices/header/padding/sentinels/upperS3 fields.
Draw1/17/31/33/64/65, guestwave32/64 on actualnative32; do not equate draw count
with number of Vulkan invocations or use compute virtual-half semantics here.
Fixture05B8A4A3 no loops and8192record guard; GPU cleanup before aggregate RED.
Unrelated setup/compiler errors are not RED. Preserve merged-wave packing and
neighbor metadata/cache guards. ActualVS2a staticS3 use is observed, but neither
wrong live count nor zero-palette causality has been proved in the game.

## BC R64 texture block layout (2026-10-12 05:08 UTC)

Update05:17: committedf55ffc00. SAMElayout/GPU051227 bothGREEN (F6088272),
BC1/BC7eightlayers/twodirections/padding verified. Broadtiler358cases/264pairs
051559 PASS (0202862E) afterfixtureidentityupdate to current scheduler-lived
scratchcontract (68a5d42f/59cb8b16). Original14numericDWORDoracle unchanged,
no productiontilerchange; previous051233 obsoletegrown==reused fail explicit.
Originalgamewithnewcore stillpending.

Update05:11: build050920 PASS; fresh9FB012D0. Unchanged publiclayout
RED051015 rejects compressed R64 admission beforeVulkan. Previous050742
compile failure was only Windows small macro name collision, not RED; fixed
fixture name without oracle changes. Shared correction now authorized; same
layout/GPU roundtrip and existing tiler neighbors pending.

Upstream b7b335ac already partly present behind an OFF opt-in gate. Before
changing default public admission, run TEST-ONLY --bc-r64-texture-layout-only
against current core: BC1/BC7 block elements must be admitted for render64KB,
depth must reject compressed/multitexel and unsupported wide elements.
Independent BC1 2048x2048x8 footprint: total0x1000000/slice0x200000, block128x64.
Then bounded --bc-r64-texture-gpu-only must tile/detile BC1/BC7 67x51x8 through
actual public layouts/copies, checking every layer and padding byte. Fixtures
53CB9C63/giant858B8D61 frozen before RED; no production BC change yet. Preserve
existing format bounds; do not transplant upstream's broader layout rewrite.

## Packed buffer prefix loads (2026-10-12 05:04 UTC)

Update05:10: committed3ebf42f8. SAMEcostGREEN050649 0/1/1/2 push+storage;
GPU050651 all20fullbyte/sentinelchecksPASS after fixtureBegin setup, original
independent oracle unchanged. Strictgenericneighbor050700 knownoneULP fails
identicalold000321; fullmatrix050825/4800lanes/100requests bit-identical to
baseline001759 (SHAe167dc0e). Precision debt NOT resolved; cost/GPU port verified,
originalgame/FPS gain pending. Originalfrozen fixtures preserved under analysis.

Update 05:05: build050330 PASS, executable77E02AF3. Intended RED050428
validated all eight public modules and live stores: actual0/1/4/5 reads for
push and storage, vs budget0/1/1/2. Aborted at explicit cost oracle. GPU
baseline050433 stopped at scheduler inactive-current guard before dispatch;
this is harness plumbing, not a reproduced emulator numeric defect. Preserve
independent bytes/sentinels while establishing actual recording scope.

Upstream 13e3ca31 reuses each packed DWORD for four host-buffer prefixes.
Before adapting shared EmitMemoryOffsets, test public compilation with 0, 1,
4 and 5 real descriptors in both push-data and storage fallback placement.
Validate SPIR-V and count actual shader-data loads in the reflected offset
interval: budget ceil(memory_offset_count/4), with positive load controls and
all observable guest stores retained. Run intended cost RED before production
changes. Independent bounded GPU readback must retain prefix/suffix sentinels
and store values for 1/4/5 descriptors in both placements. This proves module
cost and correctness; actual FPS benefit remains unmeasured.

## Upstream R8 SNORM sampled texture admission (2026-10-12 04:53 UTC)

Update 05:03: committed c38f1981. Native build 045841 PASS; fresh executable
CFDC2E53, same numeric-class oracle plus seven GPU cases 050238 all PASS:
signed sample/gather, swizzle, linear, 128-thread wave32/wave64, R8/RG8 scaled
neighbors. Swizzle neighbor initially required sampled-view plumbing; original
source B37B7404 preserved, basic admission/numeric oracle unchanged, final test
5E20456B. CTest texture_r8_snorm registered; original game retry pending.

Fresh peepostarexx main730e1a3c contains7e4e142aaebd2a5169ac73cb1a91b6c8d6fe2a7f.
Local code has native R8 SNORM path behind opt-in gate but excludes sampled
format by default. Ported synthetic sampler/gather/swizzle/linear/wave32+64
fixtures plus public numeric-class oracle are frozen B37B7404. Native build
045038 PASS, unchanged gate-unset RED045239 on774906dd: classification
Unsupported0 vs expectedFloat1, before texture GPU allocation. Production
sampling admission change is now authorized; SAME GREEN and actual GPU
signed-normalized results/neighbors pending. Do not infer zero-bone causality
or working textures/FPS from this format-level proof. Other formats/attachment
eligibility and host capability errors remain intact.

## First fault binding inside deferred recording reservation (2026-10-12 03:25 UTC)

Update03:43: actual publicPrepareBindings→plannedCommit deferred DF052D1B
oracle RED033923 exactguard321 withrouted/probe/planned controls1 andno GPU
dispatch ->sameGREEN034220 exit0. Five-line earlyPrepareFaultBuffer acquisition
is WIP. Graphics active-rendering/compute pending-chain neighbors after
preparation, originalinitchecks and originalCSsnapshotretry pending.

Init683172a8 original032417 with2CSsnapshot request ended after6.38s/exit321,
before captures: commandHooks.cpp647 rejects a nested command between
ReserveRecordedCall andCommitRecordedCall. New lazy first GetFaultBuffer
records Buffer::Fill; deferred binding materializer calls this getter after
reservation (descriptors.cpp3542..3595). Do not suppress transaction guard or
omit initialization. Required TEST-ONLY regression: real first fault-resource
CommitBindings with deferred command stream active, same native scheduling/
resource preparation path; reservation mustcommit and actual producer results
remain correct. First-use clear must precede reservation, observation stayspure,
and repeat bindings preservependingfaults. Preserve existing init a27c and
transport/queue goldens. CPU command-hook invariant fixture is acceptable if
it exercises real binding path with bounded native cleanup; unrelated setup
crashes or unenabled extensions are not intendedRED.

## Initial BDA fault bitmap and readback visibility (2026-10-12 02:24 UTC)

Update03:12: unchangeda27c oracle GREEN030802 onfreshd10b98cf afterfirstGPU
binding init, poisonRegistered0/currentRegistered1. Same exactreuse/seed/pin
setup asRED025856; testcleanupcomplete. Observation/repeatbinding/laterbatch
neighbors and original retry stillpending; initproductionWIPuncommitted.

Update03:00: native controlled-reuse oracle025856 intendedRED, actual same
memory/offset/12582912bytes checked, predecessor zero+known positive poison,
new actual atomicOr positive page, first binding plus actualparser. Aftercleanup
poisonRegistered1/currentRegistered1; expectedpoisonabsent fails. Bootstrap
KHRpush_descriptor explicitlyenabled/max32; oldbuild025236 notexecuted without
it. Frozena27c1e28 oracle; production init correction now authorized, sameGREEN
pending. Never first-parser clear that would erase genuine guest work.

Independent source audit: DeviceLocal Buffer allocation (streamBuffer.cpp65–147)
does not initialize contents; FaultManager constructor has no bitmap clear.
Diagnostic explode clearing covers only the tail; first parser pass clears
words after earlier guest GPU writes may already have occurred. Initial bitmap
state is separate from proved quota RED and from unproved original page0 origin.
Require a bounded actual initialization/lifecycle regression with a controlled
nonzero allocation predecessor before changing this behavior. Clearing at first
ProcessFaultBuffer would discard genuine guest faults and is not a correction.

Parser host-visible output also needs explicit COMPUTE_SHADER/SHADER_WRITE to
HOST/HOST_READ dependency before fence signal/wait/invalidation; existing
bitmap-only barriers do not express this. Include output visibility and bitmap
read/write atomic accesses in the pending proved quota/readback protocol fix,
retain normal nonblocking scheduling and existing host-memory cache operations.

## BDA fault batch capacity and pending-bit preservation (2026-10-12 02:16 UTC)

Update02:37: unchanged726232c5 oracle GREEN022836 processes2049 bits in1023/
1023/3 lossless bounded batches. Nine quota/bit-boundary neighbors023505 and
existing EOP neighbors023506 PASS. Shared fix committed69b620a9; current native
original retry and affected queue transport refresh pending. Original page0
causality remains unproved; bitmap initialization and source4 are separate debts.

Update02:22: actual unchanged parser GPU RED022201 gives count1087/exported1023/
pending0 from2049 bits, all guards preserved.1026 requests lost; assertions
follow native cleanup. This proves quota/count/conservation defect independently
of original page0, whose actual overflowing pass was not captured. Native
shared correction now authorized; same frozen726232c5 oracle GREEN pending.

Original candidate020757 ended after29.66s/exit321 near frame292 before palette
trace/screenshots. Exact core link map and matching PE/PDB identity show
FaultManager::ProcessFaultBuffer deferred callback -> FindBuffer -> CreateBuffer;
fault page0 becomes invalid cached range0+8000. No direct EOP write in this
chain. Actual fault count/input bitmap is not captured; root cause of page0
remains unproved. Do not bypass range validation or substitute zero resources.

Separate concrete protocol lead: fault_buffer_process.comp increments a32-bit
counter before quota checking, stores only slots1..1023, clears each inputword
before returning on exhaustion. CPU callback trusts count and reads all slots
through count. Require TEST-ONLY bounded actual parser SPV regression first:
input >1023 known bits, output1024x8 bytes plus preserved sentinel guards;
dispatch only allocated inputword count; independently inspect bounded count,
exact exported addresses and remaining bits. Exported and pending sets must
equal the original unique set; repeated bounded batches must eventually drain
all bits without duplicates/loss/guard damage. No synthetic GPU stalls/loops.

Only after intended RED correct reservation/publication protocol, retain all
unexported bits for later processing and validate CPU count before reading.
Saturating monotonic reservation is a candidate; clamping away work, enlarging
capacity blindly or accepting page0 is not a correction. Runtime causality
needs actual count/bitmap evidence and a fresh original retry separately.

## End-of-pipe publication and timestamp ordering (2026-10-12 01:00 UTC)

Update 02:04 UTC: core015853/private install020135 and writer-history CPU015700
PASS. Existing giant queue target cannot yet build: stale18 image copy capacity
arguments/missing sampled expectation fixed, then missing vector fixture defs/
old DescriptorSource benchmark member found. Restore real tests, never fake
them; affected queue checks remain pending. Standalone calibrated Vulkan probe
supports required native domains/period/int64, but guest epoch and source4
bridge remain unimplemented. No original retry or picture/FPS proof yet.

Update 01:52 UTC: literal focused014742 and three registered/native-consumer/
IRQ/repeated-write/real-compute32 neighbors015005 PASS with exact data goldens.
Fixture teardown now performs normal renderer mapping/unmapping; original
two-case oracle preserved. Existing affected queue tests and accurate source4
calibrated native timestamp are still pending. Actual graphics causality unknown.

Update 01:45 UTC: actual public64 BOTTOM literal/source4 early-publication
RED is recorded in checks/20261012-012308-3986084-eop64-publication-nofault-native-red.
No gate was needed: an unsubmitted real native fill supplies bounded pending
work; TryReadBacking avoids a guest fault/readback during observation. Candidate
014416 produces correct literal observations but fails fixture teardown with
PageManager live state; GREEN and neighbors remain pending. Source4 native
clock-stage/calibration remains uncorrected. These tests do not establish EOP
causality for the original zero palettes or 4–5 FPS.

Read-only source lead: graphicsRun.cpp WriteAtEndOfPipeCommon breaks compute
chaining but writes32/64-bit values directly with memcpy at CPU packet parsing.
Source4 samples the100MHz reference clock there. Sync::RecordEndOfPipeWrite
records debug metadata/interrupt, rather than publishing the value at actual
pipe completion. A registered/protected label page might incidentally force
readback synchronization; verify that path before claiming actual early reuse.
Current game palette capture observes valid active indices2098/2097 returning
zero rows in one draw and finite rows in the next. EOP involvement is NOT proved.

Before any production correction, require a bounded public guest EOP regression:
native queue waits on a task-owned host-signalled timeline gate, then EOP32/64
and source4 timestamp. Snapshot an ordinary unregistered guest label before
releasing the gate: it must retain sentinel bytes. Signal the gate, drain and
dispose ALL task resources before asserting an intended failure; never leave
a GPU waiting after abort. After completion verify exact width/value and
timestamp units/ordering independently. Compare interrupt delivery ordering,
registered native buffer visibility and subsequent GPU reads; preserve cache
actions and queue distinctions. Missing dependencies/setup crashes are not RED.

Vulkan profiler nanosecond conversion and existing100MHz clock-scaling tests
already agree with their contracts; do not alter frequency or guest resolution
to hide the measured4–5FPS. Source4 capture must represent the required pipe
stage, not parser or delayed callback time merely because both are monotonic.

Primary stage clarification: PAL BOTTOM_OF_PIPE_TS=0x28; CS_DONE=0x2f.
Use a legal64 bottom event for a transfer producer; internal normalized
index0 differs from raw RELEASE_MEM EOP index5. The32 parser currently
rewrites toCS_DONE/6, so a transfer-only32 test is not a completion oracle.
An unsubmitted real native producer is already deterministic pending work
and avoids a semaphore gate; assert observations only after Finish/cleanup.
Existing GpuCommandLane immediate-label expectations encode the current
approximation and need contract-based reconciliation after validRED, while
their independent nonblocking-parser assertions must be preserved.

## Repeated generic buffer conversion code cost (2026-10-11 23:54 UTC)

Optimization candidate only: generic formatted loads inline the same
normalization ladder for every returned component. Require a synthetic
public-compiler fixture with1/8/32 distinct live XYZW reads from one runtime
generic slot, all outputs retained. Before executing the baseline, freeze
two independent cost budgets: additional reads grow the module by at most
1536words/read, and repeated normalization for that shared format requires
at most16 static FDiv instructions (no unrelated guest division). Any
equivalent scalar/vector/shared conversion or reciprocal lowering is allowed;
the fixture must not require a helper name or one exact opcode sequence.
If a budget already passes, it is not RED. Distinguish code-cost RED from
an emulated ISA correctness defect or a measured game speed improvement.

Before a production optimization, preserve unchanged actual GPU numerical
goldens for runtime format/stride/offset, signed/normalized/floating values,
dst_sel constants, bounds/overflow/alignment and both wave64 halves with
sparse EXEC. Never remove loads or required conversions to meet a budget.
Inspect the actual host optimization path for helper re-inlining. Record
native build and actual post-optimization size, then measure the original
warm-cache game separately; smaller SPIR-V alone is not60FPS evidence.

Cost RED000048 -> unchanged GREEN001747 is proved. Shared bodyde9a89cf
reduces FDiv8/64/256 ->2/2/2 and32-load words48632 ->37015. Actual100-request/
4800-lane measurements001124 and001759 are fully byte-identical, including
the existing strict UNorm8 endpoint discrepancy (3f7fffff versus3f800000 on
144 active lanes). All24 other cases/inactive lanes satisfy their independent
goldens. This is numerical equivalence plus cost proof, NOT a full strict
numerical pass. Guest normalized-conversion precision remains unproved;
do not silently add tolerance or infer its connection to the game defect.

NFv1 actual2a CPU002120 validates but grows to1942548 words; original retry
is withheld for that diagnostic version. Compact shared writer NFv2 must
retain the fixed6272-byte bounds and pass actual GPU/map/output controls.
The inactive-load fixture correction derives from exact emitted IR sinking
to load0/MUL0/SQRT0, not an emulator correction or proof of skipped arithmetic.

## Independent near/far clipping and bad-vertex attribution (2026-10-11 23:45 UTC)

Diagnostic lead only: ClipControl retains distinct min_z_clip_disable and
max_z_clip_disable, whereas IsZClipEnabled returns the AND of both enabled
planes. Native Vulkan chains depthClipEnable separately from depthClampEnable.
Disabling one guest Z plane therefore currently disables both host planes.
This has not been linked to the actual stretched scene; capture copied flags
on exact admitted draws first. Preserve raw-word unavailable labels rather
than reconstructing discarded PA_CL_CLIP_CNTL bits.

Before any correction, require independent synthetic plane truth-table and
bounded rasterized pixel cases: both planes, neither plane, near-only,
far-only, finite points on/inside/outside each plane, DX/non-DX clip range,
negative/zero W and existing guest clip/cull distance capacity. Guest API/AMD
and Vulkan contracts must define the oracle; a screenshot is not that oracle.
Do not apply a blanket invalid-position guard or alter arithmetic semantics.

Actual trace14 and0 exports are finite. Nonfinite reporter's heuristic lastdraw
is not exact attribution; the next metadata-only change must expose the
already-written immutable token and verify selected shader/stage/CPU token
before joining. If a selected vertex changes between runs, capture the actual
failing invocation with a bounded diagnostic protocol instead of assuming its
previous vertex ID remains stable. No new semantic RED or fix is claimed here.

## DX10 instruction CLAMP portability (2026-10-11 22:49 UTC)

Primary AMD ISA defines NaN-to-zero for the decoded instruction CLAMP under
the accepted DX10_CLAMP graphics mode; ordinary IEEE RCP/RSQ/MUL must not be
globally sanitized. Existing FPSaturate32 emits GLSL FClamp, whose NaN choice
does not guarantee that guest result on all conforming hosts. RTX may already
choose zero, so a passing hardware case is not an intended failure.

Required independent regression: wholly synthetic decoded clamped MUL/ADD
with NaN and finite/unclamped controls, actual public CompileProgram output,
SPIR-V validation and observable export evaluation under an explicitly
Khronos-permitted NaN-propagating host choice. Check numerical exported value,
not a preferred opcode pattern; accept any equivalent correct backend. A
fixture/evaluator unsupported operation is not semantic RED. Only after the
intended failure may the shared saturate mechanism change. Actual2a cause
remains unproved; its three CLAMP PCs are not all known executed for vertex14.

Intended portable numerical RED231105 is now proved after ten passing finite/
unclamped controls: clamped qNaN exported7fc12345 under the allowed host model
instead of+0. SAME frozen oracle617b09fa passes all14 cases231751 after the
shared accepted-Vertex/Pixel saturate correctionCEFDE8E5. No GPU execution
or original2a causality is claimed by this proof. Other stage DX10 metadata
and GPU runtime-value saturation remain separate unproved cases.

## Stable picture and frame-time investigation (2026-10-11 22:43 UTC)

User target is stable image and approximately60FPS after warmup. Current
clean warm-cache observation is~5FPS, not achieved. Keep resolution/internal
render size and instrumentation explicit. Profiled frame histogram saturates
at100ms; use true intervals for60FPS acceptance, not its clipped quantiles.

Next diagnostic only: exact drawtoken, actual selected vertex/instance/EXEC,
sample coordinates and returned bits, live original buffer indices/results,
preclip Position. Bound records and memory, preserve outputs, do not use the
existing PS/CS taps that replace MRT/IMAGE_STORE payloads. Probe missing PCs,
dead loads or unobserved branches must be explicit rather than invented values.

Before a codegen optimization, use independent varied format/stride/alignment/
bounds/half-EXEC numeric cases for the actual generic buffer mechanism; prove
the intended failure or cost invariant on the existing implementation. TopCS
215/214/213 emitted large older reconstructed modules but current live-module
equivalence is unproved. Do not specialize a per-lane varying format by hash.

Before reducing coherence waits/readbacks, exercise actual tracked GPU writes
and host publication: repeated read with no new write needs no redundant copy;
newer overlapping write must invalidate it; unrelated pending ticks must not
drain all work. Preserve ordered bytes/lifetimes, never return stale data to
meet a frame-rate target. Existing required synthetic ownership tests apply.

## Discarded embedded-fetch offset (2026-10-11 21:56 UTC)

Independent review found a possible stale offset candidate: add s18 to v0
before the canonical CNDMASK overwrites v0, then fetch with the raw vertex
index. The discarded addition must not publish vertex_offset_sgpr=18.
Required synthetic public-translation RED checks the actual final fetch and
exact offset metadata (-1), with ordinary once-only offset controls retained.
Intended RED215803 now confirms actual raw index with native4 attributes but
incorrect offset18; exact metadata assertion fails, no timeout. Test EXE
A2EDDF4B20DE6F9FBED2BE1BF7BED92F10067D94AE2067A6CDC6C6FF8A2BE336.
Selected-offset/reselection and multiple-fetch variations are being added
before correcting the shared lifetime. No production offset-lifetime fix yet.

The neighbor branch encodings were corrected from SOPC bf05 to SOPP bf85;
the earlier join result did not exercise a CFG join. A public decoder gate
now checks the actual conditional branch/target/end instruction before the
eligibility oracle. The former boundary error was a fixture error, not a
production native-substitution failure. Corrected actual join and finite
backedge both pass215806, retaining guest fetches.

Affected shader_cfg215732 fails the empty-shader unused-types budget. A
scoped byte-preserving frontend reversal to6ba and native rebuild215829/run
215846 reproduce the SAME failure; restored WIP SHA d1a604 byte-exact.
This is an existing independent baseline failure, not a full-suite pass.
Current corrected target must be rebuilt before any future cfg execution.

Additional intended RED220004: canonical selection, valid once-only add,
then reselection discards it but publishes offset18. RED220005: two live
fetches need raw+7 and raw respectively, yet both become native attributes
under offset18 (attributes8/no shader loads). Exact metadata/second-consumer
assertions fail with EXEAB2BFC4AD7AD9F91F6D9626D47E747FF3D8A2D6BBA8FA3488015D3A2EFEE025C,
no timeout. Shared per-register offset-lifetime patch is frozen4f0c1f9d,
and unchanged all-neighbor GREEN220533 now passes: both discarded offsets
publish -1; the incompatible two-fetch case retains both actual buffer loads
and their raw+7/raw expressions. All nine original and twelve neighboring
cases pass on EXE97f31b90. No original-game causality established.

Required GPU follow-up: actual graphics pipeline with distinct synthetic
vertex records, transformed guest fetch and independent raw/high-ID exports.
Include host ResolveDrawOffsets/native baseVertex plus guest prolog offset
and mixed native/manual reads; CPU expression evaluation supplies raw input
independently and cannot prove Vulkan builtin/native-offset equivalence.
Derive guest register initialization and host addressing separately before
any new ABI correction. Masked-fetch numerical proof is now established by
unchanged real GPU oracle221933 RED (frontend6ba only) ->222119 GREEN4f0c1f9d:
wrong55667788 native raw-index sentinel becomes expected11223344 shader
index1, with full001d0001/high29 preserved. Canonical control passes both.
Host offset/builtin equivalence and a working original world remain pending.

Position tracing must include vertex-image sample results, coordinates and
sampler/T# when the selected branch depends on them. Actual2a SSA union
contains3 ImageSample operations; vertex-image CONTENTS are absent from draw
snapshots. Matching finite packed buffers alone do not prove all shader inputs
finite. No arithmetic/format/sampling correction is justified yet.

## Exported-position / transformed-index investigation (2026-10-11 21:05 UTC)

The original retry after6ba0a504 still has dispersed objects/covering surfaces.
VTX diagnostic reports actual exported NaN/Inf in68 VS at finalrun; screenshots
1006/1109 confirm visualfailure. A few finite huge/cull/zeroW events can be
legitimate clipping, and attribution to the last draw is approximate.

Before any geometry fix, capture actual buffers/index type/native fetch layout
and live shader for db7a7e115b3004a2 and2a6a418c0787e9b9. Draw snapshot MAX1
matches first occurrence and forces readback synchronization; retain this
coverage limit. Suspect32-bit index words001d0000/1/2 alone do not prove wrong
width. Valid32-bit examples also exist. Packed game metadata is not ISA proof.

Conditional required synthetic regression if transformed-fetch eligibility
is wrong: real decoded guest fetch explicitly uses index=(raw_index&ffff),
with raw001d0001. Independent memory oracle selects vertex1 while raw index
and its upper29 remain available for other guest expressions. Native fixed
attribute fetch must represent that computation or preserve a shader fetch;
never globally strip indices/change widths. Cover ordinary16/32-bit indices,
mask/BFE and supported offsets/instance cases. Prove intended RED first.
Source gap has not been established yet; no production fix is authorized by
these logs alone. Other transform/buffer/ISA causes remain open.

Read-only frontend review now establishes an independent eligibility gap:
ordinary VGPR writes do not clear vgpr_is_index, and canonical CNDMASK trusts
ABI register numbers after prior transformations. Native GetAttribute then
omits the guest index expression. Per-PC replacement exists; do not describe
it as destination-only matching. A standalone public TranslateProgram CPU
test is being constructed with canonical/offset/Local controls and explicit
masked/overwritten/ABI-transformed index variants. No intended RED or production
fix yet. Captured current-game cause remains unproved; first masked db7a loads
precede tagging and may correctly remain buffer pulls.

Native public TranslateProgram build212514 and canonical/supported-offset/
Local controls212814 passed. Intended RED212841 mask-dst and ABI-instance-shift
now prove incorrect GetAttribute substitution for transformed indices while
raw001d0001/high29 remain intact. Original immutable fixture127fa0cc is the
unchanged GREEN oracle. Shared provenance correction is in progress; actual
real-game cause and GPU-readback equivalence remain unproved. Neighbor widths,
CFG joins/branches, identity and repeated-offset boundaries remain needed.

Same original fixture213941 is GREEN for all nine cases after shared index
provenance/native-eligibility correction. CPU public translation and index
expression evaluation only; actual GPU vertex/readback remains unproved.
Extra widths/repeated-offset/control-flow neighbors are in construction.
Fresh v9 verification resolves code identity:2a primary8016/db7a2240 match
registered code. Legacy rows451/1416 are fe3, not2a; discard that earlier
misattribution. The snapshot's refound-stage/native-VB bytes can differ before
final storage synchronization; do not treat this as a proved binding bug.

Neighbor build214551 passed. Single and Local offsets214614/214615, relative
write/identity/join214617–214618 pass. Two additional intended CPU REDs:
repeated same offset214615-8640752 emits native4 for two guest additions that
one native base offset cannot represent; wide ABI214616-6579370 emits native4
after V_ADD_F64 overwrites both v4/v5 but provenance clears only v4. Eligibility
failure occurs before F64 numerical evaluation; no new FP64 semantics claim.
Shared once-only offset and actual destination-width corrections are now
authorized by these REDs. Preserve unchanged eligibility oracles.
Backedge214619 emits no native attributes and no retained fetch at expectedPC;
that failure is not proof of incorrect native substitution. Diagnose fixture/
CFG reachability independently before making any production claim or change.

## Active descriptor-retirement pixel regression (2026-10-11 20:32 UTC)

User explicitly reauthorizes game execution and tests. The fresh d7895a56
retry202640 has severe covering stretched surfaces at frame414 and missing
world/body with floating items at frame1180. This is not fixed by the earlier
table-retention and access-age corrections.

Required minimal RED: wholly synthetic loop-free native compute shader reads
a real registered red texture through both the actual translation-selected
slot and the original fixed sampled-array slot. Positive control must read
red. Record the old consumer without submitting, tag its actual ownership,
invoke actual cache invalidation/FreeImage, then submit and read back both
pixels. Expected earlier guest use retains red; host Vulkan UAB behavior
alone does not preserve the old descriptor. No GPU semaphore gate or unsafe
hang is needed. Bound the harness and retain native image/view lifetime.
Test-only construction is in progress; no production fix or intended RED
has been obtained yet. Deferred slot clearing alone is insufficient if the
translation word or physical image ownership changes first.

Construction and native build203417 completed. Intended GPU RED203642 now
passes actual opaque-red positive control, then reads RGBA0 from both old
translation-selected and direct slots after actual cache invalidation. Exact
recorded tick2 ownership assertion, no timeout/GPU gate/reset. The immutable
test is the GREEN oracle. Shared recorded-consumer completion ordering is in
progress; preparation-before-recording, batching and replacement neighbors
remain pending. Real game geometry causality is still unproved.

Unchanged native sampled oracle GREEN204246 now returns original opaque red
from both paths. Eleven-line shared whole-set use-tick/scheduler.Wait ordering
correction protects recorded consumers before descriptor AND translation
mutation. General callbacks are not drained by Wait. Batching/replacement,
no-region-use and completed/no-use boundaries are next; preparation-only and
actual original-world rendering remain unproved.

Native neighboring sampled cases205604 passed: red/green batch ownership with
only first incomplete-use wait, no-ref/complete fast paths, callback sentinel,
fresh green replacement after old physical deletes, and fixed-only blue shader
without translation/region leases. Existing affected cases205215–205220 pass.
Preparation-only and original geometry causality remain unproved. Whole-set
wait is conservative; original retry must measure behavior/performance before
claiming that current flicker is resolved.

## Periodic texture disappearance / bindless region lifetime (2026-10-11)

Native actual-table RED181957 reproduced active mapping loss at temporary30
and983168 requested arena entries onRTX5060Ti after realGPU/image setup.
Exact expected assertion, no shader dispatch/timeout/hang; standalone target
bindless_lifecycle_tests and unchanged fixture are the GREEN oracle. Shared
safe-reclaim/lifetime correction passed the same unchanged fixture at183446,
exit0 after native build183359. Stable offsets/mappings survive the same pressure.
Preparation/growth/abort neighbors passed184423/184627 (bounds on/off). Native
feedback copies/owner reuse passed184623/184624 (bounds off/on). Visual runtime
retry184655 is active; stable textures remain unverified.
Retry ended gracefully18:49:40 exit0 on user input concern; window1848 still
shows severe stretched surfaces. Stable visual rendering remains unresolved.

Independent epoch-policy case: with KYTY_BINDLESS_KEY_BOUNDS=0, an old completed
feedback snapshot must remain rejected after numeric-region reuse. A native
test now uses real feedback copies, same-owner controls and actual arena reuse.
Scoped restoration of only the legacy conditional guard produced intended
native RED184536. Byte-exact restored unconditional guard then passed the same
unchanged test184623; old/new owner controls passed, no shader issuer claim.
Review limits: first growth pins its old region even without an earlier patch;
old growth feedback is not forwarded to the current incarnation. Pending-tick
transfer completion and actual SRT-upload coverage remain unproved.

Independent next required regression: actual CommitBindings with a registered
bindless image and no ordinary image bindings must refresh its access tick/LRU,
with a direct-binding positive control. Ordinary GC is already blocked by the
successful-resolution pin; ResolveOverlap uses access age and can retire a
pinned resource. Prove intended access-age RED before any shared correction;
held-GPU descriptor-slot retirement needs separate bounded coverage.
Manual comparison185229 with autoswipe0/chain0 still shows covering stretched
surfaces at frames512/704; ended18:54:28 gracefully exit0. Controller log is
neutral only. An access-age oracle must inspect raw image metadata; GetImage
refreshes access and cannot be used as the observation that proves this RED.
First native attempt185759 failed fixture preparation, not a semantic RED.
Tracked direct-memory backing and cold-insertion generation setup were fixed
in the test alone. Native build190102 passed; intended RED190144 now observes
actual bindless use initial1/current34/observed1 and GC33/LRU0, while direct
RefindImage control observes34. Actual CommitBindings/cache/views and raw
PeekImage observations; no shader dispatch/timeout. Same unchanged oracle is
required for GREEN before further neighboring or visual claims.
The same unchanged age fixture passed190356 and191046 after one shared
commit-path MarkImagesAccessed call: observed34/LRU33. Actual compute/graphics
duplicate/repeated/already-read-only/target/null/unregistered/stale-ID cases
passed191010/191011 under binding-plan1/0, including zero-write early returns.
User permits tests only; no further game launches. This does not prove sampled
pixels, preparation overlap or pending old-consumer descriptor lifetime.

Fork-derived required regression: a recorded or pending native shader sampling
consumer must retain its original image/descriptor slot through actual image
invalidation until its owning GPU tick completes. Sharp926 queues null-write
and slot reuse at completion; our OnImageUnregistered rewrites shared slots
immediately despite deferred native-image deletion. Use real sampled readback,
positive controls and bounded ownership; do not claim a metadata tick test
proves old pixels or defer arbitrary callbacks without generation checks.

User reports all textures disappear and redraw. Run d8addc visibly alternates
normal world view (`window-now-1758.png`) and severely distorted surfaces
(`window-now-1756.png`, `window-now-1800.png`). This establishes unstable
rendering; controlled character movement is not yet proved.

Region exhaustion in FindOrCreateHeap calls ResetHeaps, erasing all settled
heap mappings/image references and restarting region1. No individual reclaim
or scheduler retirement protects live mappings; delayed feedback must rebuild
them. Recorded reset epochs30/57/85/111 confirm repeated resets. Earlier
prepared.bindless_patches and older recorded GPU references may retain/reuse
those regions. Mapping retention now has intended native RED/GREEN proof;
pending GPU use and late feedback require separate neighboring cases.

Required regression against actual host state: stable active heap A's resolved
mapping must survive bounded unrelated temporary-heap churn, with old temporary
users completed/inactive. No rejection of required allocations or larger limits
may substitute for reclaim. Also test allocation pressure within one prepared
stage, preserving earlier patches, and rejection of epoch-old feedback after
region reuse. Prefer a behavior-preserving CPU metadata seam; if existing
Vulkan-backed table setup is used, label incidental device setup and bound it
without shader dispatch. Preserve real capacity and fence/ownership boundaries.

## Visible texture artifacts and pointer-record provenance (2026-10-11)

Focused native RED171311 -> unchanged GREEN172445/172446 proves scalar
pointer-mask preservation and distinct pointer-member source identity. Vector
mask transformations, host/GPU readback and actual845 artifact causality remain
pending. Final mask/source neighbors and original visual retry are next.

Existing full ResourceTracking default suite172901 failed the invariant
mixed-numeric-class check. Scoped baseline173620 on0c201484 with the same v2
fixture failed identically; all five saved files were restored byte-exact.
This is a pre-existing permissive finite-candidate-to-null policy, controlled by
KYTY_STRICT_INDIRECT_CANDIDATES; strict-suite validation is still pending.
Do not suppress this failure, change its oracle or claim the default suite passes.

User requests clear/correct textures. Original retry5a4933 shows calligraphy
and later hand/cloth animation, with many requested invalid T# records for a
pointer-record image shader (labels identify artifacts, not behavior guards).
Repeated81818181/7e7e7e7e values alone do not prove root/address/dirty-memory
causality. Capture original pointer record/T#, stride/offset/target/bounds,
dirty state and feedback allocation epoch before attributing the image defect.

Independent source defect lead: TryMakePointerRecordImage strips immediate
low/high AND and zero-offset BFE without retaining their masks/width, while
host resolution follows a raw48-bit pointer. Required synthetic CPU regression:
P differs from Q=P&mask, descriptors at P+976 and Q+976 have different numeric
classes; require the guest-selected Q read/class. Run intended RED before any
production metadata/resolver correction, then unchanged GREEN and low/high
masks, extraction widths, offsets/overflow and supported pointer flavors.
Signed extraction and later GPU/feedback ownership remain explicit boundaries.
This regression is a general binding contract, not proof that the current
visual artifact uses this transformation. Native slot is occupied by5a4933.

## Pixel permutation journal drops live compiler inputs (2026-10-11)

Native RED162451 -> unchanged GREEN162914 proved the correction; final
neighbor suite164451 passed. Codec9 preserves all three fields, explicit8
reconstruction rejects known nondefault values, and versioned filenames retain
older journals. Optimizer/driver/runtime/memory effects remain pending.

Read-only v8 journal review found omitted `ps_perspective_sample_vgpr`,
`target_shader_mask` and `parameter_mode` in Pixel serialization, despite
current frontend/IR/backend consuming them. Required CPU regression: a
synthetic nondefault pixel permutation must preserve each field through the
unchanged Encode/Decode contract. Record intended mismatch RED before any
codec/version correction; validate defaults, supported modes, other stages,
malformed/truncated input and journal compatibility. Existing v8 inputs remain
reconstructed-default scope. Different replay/live keys and memory retention
are investigation leads; no memory-causality or performance claim is proved.

## Oversized volume encountered during eager heap sweep (2026-10-11)

CPU admission RED163408 reproduced the exact4GiB/U32 fatal without GPU work;
unchanged GREEN164349 after checked unsupported-footprint rejection passed.
Neighbors164350 passed8MiB/2GiB geometry plus4/8GiB soft rejection. Ordinary
requested4GiB and strict requested4/8GiB still exit321 with the explicit reason.
This proves planner rejection only: actual original T#, later-demand/slot
settlement, guest backing/alias ownership and GPU behavior remain unproved.
The inherited fork fallback remains diagnostic and does not implement these
oversized resources. Native artifacts are under the current analysis checks.

Pinned engine6d041035/private EXEb3ed warm run154145 with existing autoswipe4
reached interactive writing-names image, then naturally exited321 at15:44:16UTC.
No timeout/memory guard/reset; minfree16.28GiB. Fatal tile.cpp1348:
layout.total_size > UINT32_MAX in TileGetTextureTotalSize for a tiled3D volume.
Symbolized chain: ResolveTextureInto -> ResolveBindlessKey ->
ResolveBindlessHeapEagerly -> PrepareBindlessHeaps -> DispatchDirect. This is
not proof that a GPU-selected resource or shader emission failed.

First collect actual raw8-DWORD T#, extent/depth/mips/format/tile/address,
heap/key/shader and eager-versus-feedback selection context before correcting
behavior. Temporary bounded diagnostics may preserve the same failure. Required
CPU regression must exercise descriptor admission before allocation: oversized
volume during speculative/eager inspection, actual requested unsupported volume,
supported neighboring volumes and arrays, arithmetic bounds, no truncated U32
sizes or invented resources. Preserve strict selected-resource rejection and
avoid counting unrequested heap words as actual required shader work.

Artifacts: _Build/runs/game-20261011-154145-integration-warm-intro-9a03b0;
last owned image window-1543.png confirms input progress. Character movement
and world entry remain pending.

Diagnostic-only native retry161201 (EXEd0185f62, exact diff archived under
_Build/analysis/yotei-integration-20261011/runtime-built) passed the earlier
fatal point without producing an oversized T# record. Finished16:18:25UTC,
graceful exit0 at the8GiB freeRAM guard (7.76GiB observed), no forced cleanup,
timeout or GPU reset. Latest image1617 is black after calligraphy images1613/1615.
This is not RED/GREEN evidence or a semantic fix. CPU descriptor admission
regression is pending; native execution slot is idle after stream drain.

## Resource/cache memory pressure after introduction (2026-10-11)

Run161201 reached998VS/2031PS/549CS before the freeRAM guard. These counters
alone do not locate retained memory or prove a leak. First identify owners and
entry/byte accounting (program/IR/source memo, image/buffer residency, driver
pipeline cache) with bounded diagnostics. Any later eviction/variant identity
change requires independent lifecycle/aliasing/ownership regression; do not
raise limits, discard required resources or classify all variants by hash only.

Read the current checkpoint in ghost-of-yotei-windows-plan.md and the historical
G:\repos\KytyPS5\_Build\worktrees\yotei-windows-bringup-5664f3ff\docs\emulator-test-debt.md. Verify older results against current
source and executable hashes.

Pending: warm introduction input and actual character movement/world entry;
fallback collection preserving original inputs and taint/coverage; safe bounded
GPU variants; source/capture admission follow-up on the previous branch; rerun
old corpus groups against this fork before selecting ports.

The fork currently uses sampler/resource substitutions in experimental paths.
Their visual success is not independent proof of ISA/API correctness. New semantic
corrections require shared synthetic intended RED/GREEN and affected neighbors.

## Windows Ghost of Yotei follow-up (2026-10-25 23:45 -03:00)

The current local build was rebuilt and deployed to `F:\KytyPS5-Windows-x64\kyty_emulator.exe` after the SrtWalker DWORD-end overflow correction. Focused tests pass: `resource_tracking_tests`, `scalar_provenance_tests`, and `bone_palette_staging_policy_tests` (6 checks). This proves only those local invariants, not a visual/gameplay fix.

Warm/performance runs remain unacceptable and inconsistent: approximately 2.5-16 FPS over frames 185-296, with process working set reaching about 20 GB. A run with `KYTY_OCCLUSION_QUERIES=1` did not prevent the collapse; the host query pool initialized without the hostQueryReset warning, but no 300-frame occlusion summary was captured before stopping. Keep the flag as a diagnostic A/B option, not a proven fix. Avoid leaving the emulator running while it consumes this much RAM.

VTX logs contain genuine exported non-finite positions in several shaders, including `58e7b88265eeaa9b`, `aade60096468d233`, `9f1f009365afc874`, `a1974f29e1101813`, `50e56e7b6de6fdaf`, and `176e4a5fd1fd7205`. The `58e7` report was a GPU-argument draw at RT 3840x2160; one trace at PC `0x16e4` showed the exported position composed from loop-carried values initialized to `0xffffffff` on active lanes and replaced only on a condition (`%589`). This is a lead, not proof that the initialization or branch is wrong; the trace used instance 0 while a reported failing invocation used instance 11, and the attempted auto/instance-11 trace did not produce a joined record. Do not patch arithmetic or install a blanket non-finite-position guard without exact draw-token, stage, vertex/instance/EXEC, buffer/index, and sample-result attribution.

`176e4a5fd1fd7205` also reports huge finite positions on 1024x1024 depth-only draws, which may be ordinary off-frustum shadow geometry. Treat those huge/cull reports as heuristic until correlated with visible artifacts. The palette staging rewrite remains opt-in only; the A/B run did not log any restored matrices, so it is not established as the cause or remedy. Next investigation should capture the actual `58e7` instance-11 invocation with a verified immutable token and full selected shader inputs, then isolate why its position remains non-finite. Do not claim the render or performance issue is fixed.

Follow-up to the 58e7 instance-11 trace: wildcard vertex plus KYTY_VS_VALUE_TRACE_FRAME=auto reported 58e7 at frame 283, vertex 1232 / instance 11, exporting four NaNs on the GPU-argument draw (one index, RT 3840x2160). Auto trace slots initialized on frame 298 for that shader/instance but had no events; the failing invocation was not joined. This is a trace-tooling gap, not evidence the shader is valid. The trace IR shows position outputs may remain bit-pattern 0xffffffff through loop-carried values when %589 is false; this is only a lead because sampled inputs and the condition were not captured. Next attempt: select vertex 1232 / instance 11 explicitly, verify the immutable draw token and actual invocation, and capture image-sample coordinates/results before changing semantics.


### 2026-10-25 — narrowed diagnostic instrumentation

Changed `src/graphics/shader/recompiler/ir/passes/BindingLayout.cpp`: when `KYTY_VS_VALUE_TRACE` is enabled, `UsesVertexExplodeLog` now instruments only the selected shader hash, rather than every vertex/mesh shader. With no value-trace selection, the existing general `KYTY_VTX_EXPLODE_CHECK` behavior remains unchanged. This is diagnostic-only and does not alter guest vertex arithmetic or clamp output positions.

Build: `_Build/windows-custom` Ninja target `kyty_emulator` succeeded. Deployed executable SHA-256: `94800cde19131d0291635064b41f09b1af8ed5b2298a1c775f3133ae517de23d`. Backup: `F:\KytyPS5-Windows-x64\kyty_emulator.pre-trace-narrow-20261025.exe`. Tests passed: `shader_vertex_metadata_tests`, `resource_tracking_tests`, `scalar_provenance_tests`, `bone_palette_staging_policy_tests` (6 checks).

A new explicit trace run still failed to produce the selected trace directory/event before memory reached ~19.5 GB; emulator was stopped. The narrowed instrumentation compiles and is deployed, but the exact `58e7` instance-11 invocation remains uncaptured and there is no visual or performance fix yet. Further work should investigate why the selected shader's trace binding is not admitted/observed in the current boot/session and avoid another unrestricted long run.


## 2026-10-26 — instance-wildcard value trace follow-up

- Added `*` support for the instance component in `KYTY_VS_VALUE_TRACE`, matching existing wildcard vertex behavior. `hash:*:*` now filters only to invalid F32 expressions or non-finite position exports while allowing any vertex and instance. This is diagnostic-only; it does not alter normal rendering when tracing is disabled.
- Build: `kyty_emulator` and `resource_tracking_tests` rebuilt; deployed executable SHA-256 `8be1a236f5c73203e45dec7998edf978566805a28c89b623f7818860e8650b54`.
- Tests passed: `shader_vertex_metadata_tests`, `resource_tracking_tests`, `scalar_provenance_tests`, `bone_palette_staging_policy_tests`. `resource_tracking_tests` emits expected negative-test diagnostics for invalid resource cases but exits successfully.
- Bounded wildcard trace run used `KYTY_VS_VALUE_TRACE=58e7b88265eeaa9b:*:*`, `KYTY_VS_NF_TRACE=1`; process was stopped by a 17 GB memory guard at ~17.6 GB after ~61 seconds, during shader/pipeline warm-up (~70% of 1476 shaders).
- It recorded three target-shader draw slots around frame 317, but all were indirect draws with `n=1`, `inst=1`, and no trace events. These are not the known failing invocation (vertex 1232 / instance 11 at frame 283). This supports that the current launch is spending its memory budget in shader/pipeline warm-up and has not reproduced the failing draw before the safety stop; it does not prove the shader is correct.
- Current deployed build includes diagnostic wildcard-instance support and the earlier selected-shader-only instrumentation gate. No vertex-explosion root cause or FPS improvement has been established. Do not treat this as a graphics fix.
