# Ghost of Yōtei: active integration and session resume

## Current checkpoint: GPU-built descriptor reads and real-game startup, 2026-10-25

Branch `yotei-bringup`, source base `cba6167b` with local changes. The native
`kyty_emulator` target builds. The bounded-loop proof now clears predecessor-
dependent evaluation caches per incoming edge; `resource_tracking_tests.exe`
passes, including the invalid loop-clobber cases. New regressions cover a
lane-dependent address-loaded buffer descriptor and a loop-carried descriptor base.
GPU-built buffer accesses now stay on the GPU when a descriptor dword is not
host-evaluable and depends on a GPU memory read, or when its base/size contains a
loop-carried Phi. The guard is limited to supported indirect accesses and the
address/size words; it does not turn invalid stride/format-only values into a
valid descriptor. Indirect image tables still reject mixed numeric classes,
same-shape refreshes preserve output-vector storage, and captured specialization
reads are exported for renderer overlap checks. `KYTY_GPU_PALETTE_SANITIZE` is
now exact opt-in (`=1`); the normal launcher setting `=0` is honored. This removes
an unconditional matrix heuristic but does not prove the underlying zero-bone
matrix issue is fixed.

Installed the newly built executable to `F:\KytyPS5-Windows-x64\kyty_emulator.exe`
after preserving the previous runtime executable in `kyty_emulator.pre-bringup-20261025.exe`
(and intermediate test builds as separate backups). A real Ghost of Yōtei run
stayed alive for more than two minutes through shader precompile and continued
submitting compute pipelines. The latest log reports 3,202 shader permutations
replayed, `skipped 0`, and 765 compute pipelines queued; the earlier mass of
`GetBufferResource dword 0 is not a valid runtime value` shader rejections and
the fatal `d8959888aafd2552` translation failure no longer appear. Remaining diagnostics include heap-selected samplers
using the default sampler and a bindless heap record that is not an image T#; no
visual frame has been independently captured in this session. Full CTest was
91% passing in the first broad run (18 failures, including unbuilt optional tests
and remaining shader/materialization/kernel/GPU/cache failures); do not call the
whole suite green or assume all failures are pre-existing. Next: visually
inspect the live game window, capture the first stable scene/draw snapshots, and
trace the zero bone matrices from producer to consumer before changing renderer
semantics. Keep the executable backup, logs and caches; no push or public milestone.

## Previous checkpoint: DLSS actually evaluates and merges into the visible game world, 2026-10-15

Source0a02979a retires early-device startup, enables native host ASLR and keeps
ordinary Memory-first initialization. CoreFED1A83B run200706/5b82c5 initializes
NGX, reads actual GPU-bound temporal constants and reaches a visually confirmed
night village cinematic/world (world-phase-diagnostic.png), then exits0. Its
20s title sample after frame1107 averages2.5375FPS; DLSS GPU remains0. This is
NOT active DLSS or a demonstrated performance improvement. Loader guard and
guest-memory corrections and their independent RED/GREEN evidence are in the
test debt. ASLR no-ELF ownership/canary probe200127/64ea68 exits0; final default
probe200606/709b17 passes mapping but hits existing no-guest teardown timeout.

Current sourceccd7b8a7 fixes actual bound-view selection. Diagnostic201352/ce483a
confirmed original depth requestformat100/aspect1 versus backingformat130,
with correct full1920x1080/mip1/layer1. CPU intendedRED201620/aa29ae then
unchangedGREEN201654/8a43fb prove selecting exact native cached metadata;
neighbors201709/96f067 pass5/5. All existing role guards remain in place.
Installed core SHA256
`FC3467A8FED58B7C738368F7B0907B90C133DB5BF536A9F821E8801329872426`
is `_Build/analysis/yotei-dlss-activation-20261015/runtime/kyty_emulator.exe`.
It is built from committed production contents (embeddedccd7b8a-dirty comes
from pending documentation, not a different production patch).

Run `_Build/runs/game-20261015-201809-dlss-bound-native-view-live-81ad05`
exits0 with no memory/timeout guard. Actual native NGX evaluation+two-output
RGB merge begins guestframe313/serial1/reset1; serial2..8 continue reset0 and
GPU completion logs confirm retirement. Title reaches DLSS GPU200 atframe537.
Root viewed `dlss-live-first-world.png`: nighttime village cinematic, buildings,
people and horses are visible with counter46. This proves game-integrated
processing and visible downstream world, not controllable gameplay, complete
temporal quality, or a new publication milestone. Game validation was OFF;
earlier independent merge/readback fixtures have final Vulkan validation0.

`verified-world-title-sample.json` records a20s title-only window afterframe400:
18observations, frames401..452, mean2.7667FPS, min2.6/max2.9; completedDLSS
grows64→115. This is NOT a matched performance gain: original temporal CS
still runs for alpha/auxiliary output, then NGX+merge adds work. Next performance
work must remove duplicated work only after proving dependent outputs, or lower
the actual upstream render workload; window1080p alone does not change the
1920x1080 input→3840x2160 temporal output. Exposure1/full motion/history quality
remain experimental. Keep ASLR, bound-GPU CB snapshots and exact native views.

External metadata SHA256CEDB4ECF remains
`_Build/analysis/yotei-dlss-capture-20261015/temporal-dlss-live-metadata.json`.
Reusable manual launcher (explicit DLSS environment, no automatic buttons):
`_Build/analysis/yotei-dlss-activation-20261015/start-yotei-dlss.ps1`.
The normal launcher without this metadata does not implicitly enable DLSS.
All measured game workers are closed. WSL shutdown requested again; preserve
original game/save/cache/captures and user untracked prefetch fixture. No push
or issue comment. Separate intermittent AMPR loading memcpy crash201257/ba6dfa
remains debt; an identical-artifact retry passed, so do not attribute it to DLSS.

## Previous checkpoint: live temporal provider wired; NGX starts, loader guard blocks game, 2026-10-15

Source f5d5db0f includes e5f3e1f9 live explicit temporal provider and85b5a0d5
role-channel correction. Native installed core26F95B7679610F7D60D75072E73A9EBADEB23E82301FF2C7144A04E3B6CD2D81
is in `_Build/analysis/yotei-dlss-activation-20261015/runtime` (built on85b5a0d5
WIP, committed unchanged). External recipe is
`_Build/analysis/yotei-dlss-capture-20261015/temporal-dlss-live-metadata.json`.
Do not claim active game DLSS yet: run193625/59f5aa initializes NGX successfully,
then exits321 before gameplay at RuntimeLinker::Relocate2041: fixed internal
unresolved-object guard allocation0x840000000 returns0. Next: independent
occupied-preferred-address guard regression, preserve NoAccess semantics and
allocate a relocatable internal guard if the preferred address is unavailable.

The first live provider retry190729/fd8f87 failed in vkCreateDevice. A native
actual-core no-ELF synthetic156MiB directory reproduces192638/ace881 exit321;
staged device preparation before guest VA reservations passes193149/7c9ad0
exit0 with NGXinit0, memory/placeholder self-tests and normal teardown. Renderer,
input and memory callbacks still initialize after Memory/Controller. Ordinary
metadata-OFF and full game-config host-only controls initialize their devices,
but no-guest teardown sometimes hangs until the bound (also before the fix);
do not call those full passes. Full presenter193510/ece776 passes real NGX,
ownership/readback and final validation0; CPU contracts193510/3f32dd pass4/4.
Channel guard intendedRED192856/3be7ac→GREEN192944/af4a05 covers7203 mappings.

Provider now copies actual prep color/depth/motion, associates immutable
bound-host constants and writes NGX RGB into two actual accumulation outputs
while preserving each alpha and the auxiliary R8. Own merge/association native
RED/GREEN, descriptor-retirement neighbor and real saved-input NGX validation
are recorded in [the integration record](dlss-temporal-pass-integration.md).
Exposure1, complete motion coverage and continuous temporal quality remain
experimental. Current bound constants only support actual ActiveStream; a
cached-buffer binding is safely refused and may need a native immutable
snapshot. Do not read current guest backing to bypass that restriction.

Title now reports `DLSS GPU:N (+delta)` from completed NGX+merge GPU ticks;
zero is not active DLSS and this counter is not a presented/world marker.
Reusable local launcher is
`_Build/analysis/yotei-dlss-activation-20261015/start-yotei-dlss.ps1`.
WSL remains stopped. Keep original game/save/cache/captures/user untracked
prefetch fixture. Root serializes native execution, three agents inspect/test
offline. No FPS sample or new game-DLSS world proof exists yet. No push/post.

## Previous checkpoint: real temporal snapshots and shader math; game DLSS inactive, 2026-10-15

Continue from diagnostic source b4aa51cb, coreBB06268F83F9F0F45351BAF07268C74F3D81826689FD974BCCD99D7C6F4D4AF4
(built on a78a6dbd WIP later committed unchanged; embedded a78a6db-dirty).
See [capture evidence and remaining gates](yotei-temporal-input-capture-2026-10-15.md).
User says continue/stopWSL and subsequently freed disk; WSL stopped. Three
agents analyze metadata, shader math/constants and captured pixel statistics;
root owns native build/game/GPU execution. No game NGX provider/evaluation yet.

New one-shot RenderDoc guest-flip file gate and CPU state/filesystem tests pass;
actual Vulkan layer bootstrap produced267686-byte RDC with final validation0,
exit0. API injection alone was insufficient. First real guest capture timed out
during initial-state spilling;34 partials/15.72GiB preserved onF, not a readyRDC.
After approximately92GiB became free onG, retry173950/0d35c1 completed a
5,016,579,112-byte RDC (SHA98F8570B80A5E2E577EE6D099F96EFE40D65551F94EC2E5B4079653964DBCE45),
146.55s/two guest flips, game exit0/no guards. Player/world confirmed552;
20s pre-capture title mean3.4333FPS. FirstGPUreplay loads but misses cooperative
deadline; bothShutdownreturn/hostexit0. PureCPU structured decode12.61s+scan6.687s
exports1115shaders2356pipelines157538chunks and174rawsamples. LinkedGPL is
resolved. V5 corrects the BeginCommandBuffer field case and traces only actual
selected descriptor sets, preserving128/4096 bounds:174/174prior associations
pass, all8selected buffer bindings resolved. Subsequent selected-event GPU
replay reads744B from8/8 requests with exact pipeline/buffer/offset/range checks;
camera matrices, representative geometry transforms and temporal CBs are real
captured values. Two PS array elements omitted by access feedback are resolved
through bounded sparse actual-set lookup,7 metadata probes+1 descriptor each.
Clean replay shutdown/host0/no guards; no game NGX evaluation.

No-RenderDoc targeted run172948/61b90f exited0/no guards, player/world/HUD
visually confirmed at551; four complete snapshots550/551 preserve liveSPV,
CBs and before/after native image bytes. Actual prep dispatch is1920×1080
despite4K backing allocations; actual next temporal accumulation is3840×2160.
Byte-identical previous-output feedback and changing current/previous subpixel
offsets established. Consumer math establishes reverse-depth3×3 selection and
normalized backward reprojection of negated rawtwo-channel input. Full motion
producer/projection-jitter/exposure/reset conventions remain pending.

Frozen v9 journal1983records checksums verified; all12 selected PS/VS variants
and2compute variants pass native CPU decode/emission/Vulkan1.3 SPIRV validation.
Explicit reconstructed profiles are not original PS/VS userdata/live-module
proof. Metadata trace172259/a39773 is partial (noEND):4208rows/7279calls;
do not call it a complete frame. Diagnostic title samples around3.4FPS are
not matched optimization results. No FPS gain, active gameDLSS or verified
complete guest-work coverage claimed. No new issue milestone, post or push.

Both CS live modules byte-match CPU/regenerated outputs;2349sampledpoints/frame
confirm preparedMV/MAX-depth exactly andlinearDepth/luma withinFP16. Actual
camera PS hasNO PushConstant:12matrixfloats are set1/binding109 SSBO bytes64..112,
not its128Bzero pushes. Actual48B matrices from both submitted camera draws
and representative geometry matrices are now retained. CPU/replay chunk
indices and timing bases differ; the repaired diagnostic mapper uses unique
baked recording plus direct-work ordinal/arguments, with8 synthetic CPU cases
passing. Same-capture temporal CBs identify guest frames432/433; do not conflate
them with the independent earlier550/551 snapshots.
Camera mappings numerically match geometry other*inverse(current) in both
selected frames, coefficient errors<8e-8. Previous geometry transforms use the
current jitter, with independently checked re-jitter residual<3e-8; this proves
the selected path, not complete motion coverage. PS UV normalizers are actual
(1/1920,1/1080). Next: evaluate saved real inputs in an offline NGX GPU fixture
before designing the live provider. Full UV/motion coverage and exposure/reset remain pending;
synthetic provider/ordering regressions still precede a production change.
HDR insertion belongs before actual game tonemap/HUD; do not feed sceneHDR into
the existing SDR presenter. Preserve user prefetch fixture/save/cache/captures.

## Previous checkpoint: optional build reaches world; game DLSS inactive, 2026-10-15

User requested actual game/FPS verification and then explicitly asked whether
the displayed3.7 meant DLSS was active. Answer: NO. Production startup still
does not request NGX; ordinary flips call PrepareFrame, and no game semantic
provider or temporal callsite exists. Host fixture PASS is separate evidence.
See [the game verification record](yotei-dlss-game-verification-2026-10-15.md).

Record f3dcf785, optional EXE45F8FDE3C20086900BDAAA615CDF86930D53D4C72FFC3002537E1DDCFB6C6460,
production content34e725f4+84a9c1ec built on360fc774. Native game run
091119/61af2f reached visually confirmed character/world at frame653;20s title
mean3.3389FPS, exit0/no guards. Copied byte-matching save/cache into independent
optional runtime; shader key matches; neutral input/full1080 output. Scene
PrintWindow at sample start is diagnostic, not capture-free benchmark. No
improvement claimed. Fresh historical E46 control091438/714d55 exited0 before
frame650/sample; closure reason unknown, no complete comparison or regression
proof. Current save already Performance; no new production fix/build this round.

Three agents audited call graph/resource captures/internal resolution. No
verified pre-upscale color+depth+full motion+jitter provider yet. Existing
end-of-frame image dumps skip depth. Portable RenderDoc dependency prepared
outside tracked source, no capture/execution yet; native-present captures may
omit a full guest frame. Next: bounded complete guest-frame capture, semantic
producer proof and genuinely lower internal workload; then real NGX evaluations,
presented output and matched20–30s FPS. No zero inputs/guessed roles/placebo toggle.
Preserve saves/caches/game/artifacts/user prefetch fixture. No push/issue post.

## Previous checkpoint: native DLSS SR and presenter validated; game provider pending, 2026-10-15

User authorized integration. Backend360fc774, explicit host negotiation/native
Presenter fixture34e725f4, allocator teardown fix84a9c1ec, all local commits.
Three agents researched/audited; root
performed native execution serially. Default OFF, external SDK/runtime only.
See [DLSS implementation and evidence](dlss-vulkan-integration.md).

Actual RTX5060Ti directNGX8frames960x540->1920x1080 requestedM, both GENERAL
and shader-read-only inputs, exact loaded runtime310.9.1.0, validation errors0.
Original unsupported-adapter RED082323/c047d0 -> unchanged GREEN082520/d44151.
CPU input contract unchanged GREEN082545/c24232; HDR->SDR guardRED3b5c64 ->
GREENf16d84; SDKOFF3contractsPASS083744/0b3ca6. Full hiddenSDL actualrenderer/
NGX/PrepareTemporalFrame tested085517/1c5915: GPUcopybytes/owninglease/firstreset/
dupid/independentFrame after source destruction/guards/finaldevicevalidation0.

First full attempt29b86e is NOTcleanPASS despiteexit0: observermissedfinal24-image
leak. Corrected ordinarybootstrap(noNGX,runtimeabsent)fixtureSHA6C0B0483 has
intendedRED085350/7fca00 errors1/exit2 -> unchangedGREEN085516/561efe errors0/exit0.
DestroyAllocator now drains retired VkImages BEFORE VMApool/allocator teardown.
Existing hostallocation/presentdownscale2/2PASS085608/73aa85. No driverreset/hang.
Final84a9 coreSDKOFF build085856/bbf444 PASS, finalSDKOFF CPUcontracts3/3
085958/516e65 PASS; optionalON core085610/64db36 and install085824/0667ed PASS.
ON installed _Build/analysis/dlss-integration-20261015/runtime-emulator-optional,
EXE45F8FDE3C20086900BDAAA615CDF86930D53D4C72FFC3002537E1DDCFB6C6460;
preserved presenter GPU EXEAF8ED3171609574C66D7308723427233EBAE1E49D31033E2A270B050588019FC.
Those ON artifacts were built on360fc774 working tree containing exactly the
later34e725f4+84a9c1ec production changes; finalOFF rebuild embeds84a9c1ec.
The optional installed runtime was build/test checked, not game-tested.

No current Yotei semantic provider exists for correct pre-upscale/HUD color,
depth, full motion and jitter; ordinary game startup never requests DLSS. Do not
fabricate inputs or expose a placebo enabled setting. Presenter supports explicit
display-ready LDR only; earlier HDR insertion plus tonemap still pending. No game
activation, FG/MFG or FPS improvement claimed. Next: verified game resource-role
provider and actual lower internal workload, then image/FPS comparison. Preserve
user prefetch fixture, saves/caches/artifacts. No push/issue comment/game retry.

## Previous checkpoint: public research and four profiling runs completed, 2026-10-15

New user-authorized research/timing task is complete; no production fix was
applied. Tested record ce620a1f, core b1a9698e, same EXE E46E5CC99DAFA8B4E67177C854FA7A7DE8E8CA1391DE6AB43169F562212E8ABE.
See [research and measurements](yotei-render-research-and-profile-2026-10-15.md).
Three agents researched GitHub, primary articles and timers; root ran native
Windows serially. Clean073924/b2e5c9 mean3.80; functions074305/ee1163 diagnostic
mean3.59; GPU074724/0053ef3.67; dedicated MAIN-IP075115/b77dcb3.72. These are
20/20/20/25s title windows, gated>=90s and frame650/650/650/550. All exit0,
no timeout/memory guard; no build needed, targeted option OFF, neutral full1080p.

Function collector650->686/10.185s:174rows/27threads, all query/exclusive/
truncated/outstanding/overflow errors0. Separate20s CPU counter: Main86.8%
onecore, GPU-command46.9%, drawspec32.4%, recorder25.7%; process3.32cores.
Fresh PSS selftest39/39 PASS and separate MAIN36/36 PASS5.015s/allcleanup0;
Main88.18%CPU but29/36positions in guestRDTSC/deadlinepoll,3GPUwait,3alertwait,
1otherguest. Position fractions are NOT CPU-cycle fractions or removable time.

Late GPU109frames: busy135.23ms/frame, inter-buffer gap140.00, zones buffer-copy
32.59/tiler20.65/gamecompute52.57. About385submits/131readbacks/21.06packedMiB
perframe; zones/waits overlap, not additive. Whole-subresource upload-side
tiling of two aliased surfaces is a measured candidate; exact view/extent and
unused-prefetch generations remain unproved. Main load alone is not the culprit.
Fresh liveSPIRV/permutation and full causal critical path still pending.

Scene verified independently through PrintWindow without input/focus changes:
function-run frame557 and both sides of MAIN-IP sample show character/world.
User explicitly says determine phase ourselves, DO NOT ASK him. Clean/GPU-only
run scene was not captured directly; matching save/input is supporting inference.
Source reports/raw JSON under _Build/analysis/yotei-performance-cycle54-20261015.
AnyPS5c92ae6e narrow-store and SharpEmuf9fe5cb adaptive-readback are useful code
references; ordinary8/16stores/hot epochs/poll throttling already exist locally.
Peepostarexx public main/YOTEI API404. No blind port or claimed2x/60FPS gain.

WSL helper exceeded20s but vmmem stopped before first game; WSL/Dota/native
task processes absent at final check. Preserve untracked user prefetch fixture,
game/saves/caches/artifacts. No push/issue comment. Next required synthetic
tests: shared alias/dirty-subresource reuse, eager token stale/demand semantics,
and actual hot-shader storage paths; see test debt. Existing four-algorithm
series remains historical; this profiling round did not add a fifth algorithm.

## Historical checkpoint: four algorithm trials finished; stop, 2026-10-15

Branch yotei-integration-native. Production b1a9698e is byte-equivalent to the
separately tested targeted candidate941fc65f (src/CMake/3rdparty diff empty).
Only targeted publication remains, strict KYTY_READBACK_TARGETED_PUBLICATION=1,
defaultOFF. Indirect, packed and PM4 production are reverted; their regression
fixtures, commits and artifacts are preserved. User's OLD+THREE NEW scope is
complete: STOP, no fifth algorithm or new game retry. Preserve untracked
tests/ComputePrefetchPermutationTests.inc. No push or issue comment this round.

See [four-trial evidence](yotei-four-read-algorithms-2026-10-15.md) and
[public developer references](yotei-public-developer-references.md).
Old indirect202651/6a668b controlmean3.58; ON203006/2cfcd6 cut at3.4, reverted
e3f22a9c. Packed205520/beb2d6 cut at3.5<=3.58, reverted a1c414dc. PM4 candidate
532a54db core212217/install212310 PASS, EXE D9F916EBBDA5698CC71704EE93F0CADC5573FC186AD98BA11728B0C6ABC8B12D;
ON212312/117d59 gated557/90.102s, cut at569/title3.6<=latestcontrol3.60,
no completedmean/image, exit0/no guards. Reverted3dfd4664.

Targeted native RED203705 -> unchanged GREEN210105/C20BDE31 PASS; four existing
new-writer/image/typed neighbors PASS210637/210638/210710/210711. Older wider
A1+C1/newer exact A2 neighbor211230/FD7CD216 PASS after fixture owner setup
repair; original210941 failure is NOT a production RED. Distinct tokens/ticks,
required older-overlap wait1 and exact native/backing A2 after ALL callbacks.
Native core211248/install211348 PASS, tested EXE
0A95CCA6A82FA9AA458B66DCE0E6A614CE5A11FD2D3105253B35B31A4D97D275
at cycle53/runtime-targeted-publication. Same EXE ON211350/87a248 completed6s
mean3.8333 versus OFF211625/320d63 mean3.60, about6.5% difference. One window
each and no image/equal-scene proof; stable throughput gain remains unproved.
No verified2x from original3.7 (target7.4) or60FPS. Internal resolution pending.

PM4 corrected resource-plan205059 and intendedRED205131 -> unchanged
GREEN212021/83AF97CB PASS: native header/payload/current markers/guards/cleanup,
independent queue progress with A blocked/old and dispatcher waits0/0/0.
Queued WAIT212038, image aliases212039, fresh parser predication212147 and
command lane/nested-resume212148 PASS. GPU-owned nested/predicated/legacy
extended payload and concurrent targeted once-claim remain explicit test debt.

Final native core212529/install212808 for selected b1a9698e PASS. Installed
cycle53/runtime-selected-targeted-publication EXE
E46E5CC99DAFA8B4E67177C854FA7A7DE8E8CA1391DE6AB43169F562212E8ABE
is build-checked, not game-tested. Archived tested runtime0A95 is preserved.
No task-owned native process or WSL/Dota remains at final check. Next unresolved work is proving
actual workload eligibility and dominant removable frame cost, not another
speculative core rewrite. WSL/Dota absent at last native run; task-owned games
closed normally. No new menu/gameplay/visual correctness milestone is claimed.

## Historical checkpoint: three read cost REDs; packed readback GREEN/core build, 2026-10-14

HEADf9fc0a9e, fixturesdb979491; active native core build for opt-in packed
readback. FREEZE source/test/CMake until completion; root owns native execution.
Old indirect trial is rejected/revertede3. User cap remains OLD+THREE NEW,
then STOP; no fifth algorithm. Preserve user untracked prefetch fixture.

All read fixtures compiled203551/A4C60E5B. Packed203704 and targeted203705
are intended cost REDs AFTER native/numerical/guard/cleanup controls pass.
Packed unselected-gap/different-owner has2tokens/barrierCalls vs1; targeted
A is native-complete/armed/old behind B, actualPriorityWait1 vs0.
Initial PM4 run203703 exited321/deviceLost from a TEST invalid copy: CPU
ObtainBuffer returned ActiveStream; discarded offset caused8GiB source offset
on64MiB stream. NOT RED or emulator mechanism defect. Test-only registered
owner acquisition, pre-copy capacity checks and shared helper bounds repaired.
Build204921/2A82 PASS; safe resourceplan205059 PASS (invalid guest-derived
offset rejected, actual owner4092/16384). One corrected bounded GPU retry
205131 now intended RED: both header/payload current native values/markers,
guards/cleanup pass; actual waits prevent independent queue before release.
No timeout/driver loss/reset in that corrected run. New PM4 core pending.

Packed correctionf9 actual unchanged GREEN204725/CD6E1093: all3cases preserve
12bytes/3regions/exact native/backing/gap guards; one token/hostbarrier and
one copy per real owner. Original eager OFF204919 and armed typed ON204920
neighbors PASS. Strict defaultOFF KYTY_READBACK_PACKED_BATCH=1; original
downloader fallback retained, source identity regather after stagingMap.
Game native core build active; FPS pending. Targeted safe updated proposal
has older-overlap/coherent-allocation guards; remains unapplied/uncompiled.

## Historical checkpoint: indirect experiment rejected; three read algorithms pending, 2026-10-14

Branch yotei-integration-native, correctness candidatec7c199fd, production
reverted by e3f22a9c to42c byte-equivalent. Native GPU fixture/tests retained.
Latest user scope supersedes earlier three-check
wording: TRY this experiment PLUS THREE NEW read algorithms, then stop.
Three agents prepare distinct PM4 continuation, selected eager multi-owner
batching and targeted publication fixtures; no new read production changes yet.
User test WIP preserved.

Genuine CPU cost RED194639/EABD on42c remains the reproduction. Fixture
SHA35498FA3 unchanged. Native target201108 PASS; unchanged GREEN201249/02CEA345:
fivearguments/64-bit offset/order/dependency/worker cleanup pass and actual
producerDrains0 instead of1. Existing idle-spin routing neighbors201249 PASS.
These are host-only proofs, no native GPU commands or game outcome.
Native test target201903 PASS, initial GPU201947/C9F2 rejected as SETUP error:
fixture redundantly enabled an already-enabled scheduler. This was not RED,
driver hang or production defect. Removed only redundant enable; build202048
PASS. Actual GPU202320/F01C5AF6 ON and202352 sameEXE OFF PASS: IA vertices0/3/6,
all64 pixels, deferred GPU args copy, argument/pixel guards, finite2s waits,
worker joins and native resource retirement. ON producerDrains0/OFF1 percase.
Native candidate core202524/install202639 PASS, runtimecycle52/
runtime-indirect-recording-experiment, EXE9806136746FA2D90788D0CD27AD5B9648A06A5015BCFBDC3302B7C7CC51AFA63.
Same EXE/runtime/neutral1080 after400/min90: control202651/6a668b completed6s
mean3.58; ON203006/2cfcd6 gated531/90.180s but rejected at563/title3.4<=3.58
before10s completion. ON has NO mean; both exit0/no guards, phase/image pending.
Production correction removed per user; source/CMake diff42c empty. Archived
candidate runtime is c7, NOT the reverted source. THREE NEW read algorithms
remain unapplied/uncompiled/unrun; batch test-only harness merge in progress.
Do not run a fifth algorithm. [Public SDK/engine references](yotei-public-developer-references.md)
were checked during the user's question; no private SDK access or game source.

## Historical checkpoint: post-Dota native measurements, 2026-10-14

Record HEAD5dc3f3c3, production42c6c601, installed EXE8E3D9CD6 unchanged.
User closed Dota; before native launch it/WSL/task jobs were absent, GPU2%.
Same runtime, neutral full-render1080, after400/min90:
clean `195656…ad75c5` completed6s mean3.85FPS;
drain-only `195953…487850` completed10s mean3.475FPS;
clean recovery `200245…1e8184` first gated title3.3, exited0 before completing
6s. Recovery has NO mean; cause of early normal exit unknown. All runs exit0,
no guards/timeouts; no native job remains. User confirmed character/world
during recovery; no independent scene image or prior-run scene equivalence.
First-run post-FPS screen captures were withheld because foreground acquisition
failed. See [exact results and renderer strategy](yotei-post-dota-measurements-2026-10-14.md).

Sub1FPS is not reproduced here. Historical0.95 also differs in executable/source;
do not attribute its entire drop to Dota or call recovery a code optimization.
Last diagnostic reports intervalbusy129–144/gap143–166ms/frame and logical
readback20–22MiB/frame. Scopes overlap; GPU intervalbusy is not hardware GPU%.
No exact debug penalty, full renderer rewrite,2x original3.7,60FPS or new
publication milestone. Cycle50's host-only indirect cost RED remains unfixed.
New census/counts-only observer proposals are parked/uncompiled/unrun.
Next actual eligibility census or bounded hardware queue evidence, then a shared
mechanism regression/correction; preserve real compute-to-CPU dependencies.
Preserve user untracked test. No production change/push/issue comment.

## Historical checkpoint: three new research results and host-only RED, 2026-10-14

Branch `yotei-integration-native`; production remains42c6c601 (documentation
HEAD52fef513 before this round). Installed native EXE unchanged8E3D9CD6...,
runtime `cycle49/runtime-current-42c6c601`; no game/GPU test in this round.
Preserve untracked user `tests/ComputePrefetchPermutationTests.inc`.

Three agents completed [cycle50 research](yotei-performance-research-cycle50-2026-10-14.md):
nonindexed indirect recorder Drain; typed16-bit tiler instead of two RMWs/element;
proven readonly BDA footprint without losing dirty pages/epoch hints. Production
proposals remain unapplied. Actual Yōtei branch eligibility/cost still pending.
No2x/FPS/cross-game correctness or new menu/gameplay publication proof.

New manual host-only realCommandStream regression (early CLI before Vulkan init)
build194126 PASS; original production intended RED194322. Added only stdout flush
for evidence; build194503 PASS, logged RED194639/EABD645F. All five arguments,
64-bit offset, order1/2/3, prior-work dependency, worker/stream cleanup, environment
restore pass; actualProducerDrains1 versus intended0 fails after those controls.
No native GPU calls. This manual expected-RED mode is not registered in CTest.
GPU/game correctness and unchanged GREEN remain unproved; no production fix yet.

Important new confound: Dota2 PID35348 started18:46:30Z, after diag575 at18:32
and before clean49 games19:08+. Without emulator GPU37%/4907MiB; short later
snapshot33–36%/5455MiB. These are aggregate background GPU values, not Dota-only
or whole3x causality. Prior3.1diag versus0.95clean cannot prove emulator or
DrainStats regression. User chose to close Dota personally; no foreign process
was stopped. GPU benchmarks deferred while it remains active; CPU/code research
continued. No task-owned process or WSL remains after bounded CPU runs.

Next: clean same-world baseline after foreign GPU activity resolves, then sparse
actual nonindexed API hit/upload refusal/BDA upload counters and one selected
shared correction with regression-first proof. No blanket pool Drain removal,
metadata bypass, raw donor clipping, unsafe age retry or work skipping.

## Historical checkpoint: readback trial rejected; renderer research saved, 2026-10-14

Active branch `yotei-integration-native`, native Windows `42c6c601`.
Correctness candidate4e784308 is FULLY reverted here; commit/proposal/native
RED/GREEN artifacts remain preserved. No source/test WIP except user-owned
untracked `tests/ComputePrefetchPermutationTests.inc`; preserve it. Documentation
commits after42c do not change executable behavior.

Actual finite proof: original bootstrap RED185233→unchanged final GREEN190600;
pending/query/rollover RED190348→unchanged GREEN190538. All native values/guards
and owned cleanup precede intended failed assertions. Final armed4/EOP3 neighbors
190601/190603 PASS. Native candidate core190636/install190755 PASS, EXE
`035F6EF30FE33163EA94C2D1D38EBC6A587F20A62F0E1DB1FE262E3223897D21`.
That executable contains REVERTED4e; never relabel it as42c. Fresh42c native
core192003/install192310 PASS. Current runtime
`_Build/analysis/yotei-performance-cycle49-20261014/runtime-current-42c6c601`,
EXE SHA256 `8E3D9CD6546A63307CF8559D302C6DB67B2A40E7B93035B89C421751A8633F7C`.
No game retry on the freshly rebuilt revert; no native job remains active.

Same candidate EXE, isolated copies of the same saved runtime, neutral/fullrenderer
1920x1080 output/full replay, min90s+frame400:
`game-20261014-191111-perf-cycle49-clean-world-readback-queue0-df0078`
control title6smean0.95, gate400/136.575s, closed0/no guards;
`game-20261014-191535-perf-cycle49-clean-world-readback-queue1-cc7b05`
real transfer family1 created, first evaluation at400 title0.9<=0.95 immediately
rejected peruser, closed0/no guards. ON has no completed6smean: do not invent a
percentage regression. No FPS gain/no post-gain screenshot. Actual visible phase
for BOTH new clean runs remains pending; gate time/frame alone is not world proof.
No new original3.7→2x acceptance. No scene equivalence to older3.1 diagnostics.

Initial candidate control `190847…41ad9a` EXCLUDED: `KYTY_DEBUG_READBACK_AGE='0'`
still enables this presence-tested diagnostic. It is not a clean control. Corrected
pair omits the variable; no readback-age output. Other omitted experimental flags
defaultOFF confirmed. No unrelated task-owned process or vmmemWSL remained after
ON ended. Do not stop unowned containers merely because their name includes vmmem.

Three-agent source audit confirms --drain-stats injects real timestamp commands
and NoteSubmissionWork on every command buffer, changing otherwise-empty optional
flush admission. No actual missing native-command mark found in audited paths.
Existing empty-tail tests deliberately cover direct recording; shipping deferred
recording coverage remains a lead. Next one controlled discriminator: SAME fresh
EXE/runtime/actual scene, AGE absent/queue0, change ONLY drain-stats setting, then
bounded independent actual-command-versus-recorded-work witness if justified.
Do not promote debug/clean differences to a proven scheduler bug or enable empty
flushing blindly. No unsafe recording-age retry.

[Renderer research and options](yotei-renderer-performance-research-2026-10-14.md)
records primary GTA sources, official Yōtei compute→CPU command assembly, game
shader-package inventory limits and shared semantic fast paths. Parked proposals:
upload admission observer cycle48/upload-route-research (unapplied/uncompiled);
4B direct download does not match observed tiny16B downloads; bounded DMA footprint
and multi-mapping cache require current cost witnesses. Source/workflow evidence
and three agents' ignored notes remain preserved. No push or issue publication.
At least2x original3.7FPS and60FPS/fullFX goals remain unmet.

## Historical checkpoint: GTA comparison and actual path admission, 2026-10-14

Native Windows, `yotei-integration-native`; current correction `4e784308`.
Fresh readback bootstrap RED185233/96E84A5F→unchanged GREEN185634/8BA64F41,
then independent pending-query/rollover RED190348/A3450E49→unchanged
GREEN190538/AF3C72E4. All native values, canaries and cleanup pass before each
intended failure. Final unchanged bootstrap190600 plus armed4/EOP3 neighboring
cases190601/190603 PASS. Actual same-page and new-open-writer denial pass in
rollover fixture. Original bootstrap file remains byte-identical. Queued mode
remains defaultOFF. No FPS improvement proved yet. Current native core build
at4e is active; source/tests/CMake FROZEN until completion.

The last completed diagnostic executable below is source
`294a854d97a52d860d91f569a3d064f555ed94fc`, before the4e correction. Format
candidate9a8 and CS candidateb052 remain
fully reverted byce0/165a. Preserved untracked `tests/ComputePrefetchPermutationTests.inc`
is unrelated WIP; do not delete or stage it automatically.

Native core `checks/20261014-165607-8915943-build-core` and install
`checks/20261014-183015-7204444-install` PASS. Exact installed EXE SHA256
`FD5A4CE0D7699F93C579331007E5B0A793578528838BF5DCD621C8839B015E78`, runtime
`_Build/analysis/yotei-performance-cycle48-20261014/runtime-tiled-download-cost`.
Completed run `_Build/runs/game-20261014-183219-diag-cycle48-world-tiled-downloads-575166`:
exit0/no timeout/no memory guards, full pipeline replay, neutral1080 output,
title gate frame562/91.018s. Instrumented25s mean3.0952 is NOT a clean FPS trial.
Owned scene capture withheld because task window was not foreground; no new
image exists, visible outcome remains pending. Last independently viewed world
is `game-20261013-231842-diag-cycle46-world-wait-partial-slices-8c6611/world-phase-diagnostic.png`.
No new menu/gameplay publication milestone or issue comment.

Latest full label epoch565–597:141640/141640 rows,drop0,pointererrors0;
60917 WAIT attempts forwarded,0suppressed. TileImage observer:1152 recordings,
six192-call buckets, all16B RGBA32_FLOAT/2..64px; bucket overflow0. This rejects
the current applicability of the parked4B direct-download proposal, not the
general feature. Capacity arguments are not actual transferred byte counts;
format-read caps/per-download GPU completion unobserved. NativeFormat107 is
RGBA32_FLOAT, not a16B raw-format incompatibility proof. Tiler zones21–24ms/frame
mostly have different large-resource keys and include possible unmarked work.

Previous complete `164640…d1d523` identifies upper flag GPU publishers and
pre-producer q0 delay. Post-finalization8060=.074ms,8340=.183ms; no generic
CPU-notify/timer fix is justified. ThreadGpu PSS72/72 valid with matching PDB
GUID/age shows native waits and scattered CPU work, no single dominant function.
Its preferred-base-only historical symbolization is not valid for ASLR.
Actual native indirect observationb250 proves GPU output4,5,6/guards but not
CPU publication; the original host/numeric assertions remain failing. No full
group pass or proven frame bottleneck from that observation.

Three-agent GTA/code audit and official Sucker Punch rendering explanation are
saved in [renderer performance research](yotei-renderer-performance-research-2026-10-14.md).
GTA external FPS does not prove intrinsically better optimization. Many general
donor optimizations already exist. Last-writer transfer readback is implemented
but defaultsOFF (`KYTY_READBACK_QUEUE` unset in current diagnostics). Preserved
bootstrap test has now reproduced RED and unchanged GREEN above, plus a second
query/rollover RED/GREEN. Root is building the4e emulator before a clean queue0/1
game trial. Verify build completion before source edits or another native job.
Yōtei explicitly uses compute→CPU readback→command assembly. Next: bounded q0
CPU/native-call attribution and upload admission counts; then select one shared
semantic correction, real RED/GREEN/neighbors, clean warmed same-world title A/B.
Do not replace required CPU reads with native indirect without a semantic proof,
skip required effects, or switch core behavior by title/hash/address.

Before new native work verify processes and exact build inputs. No task-owned
emulator/Ninja/MSBuild or vmmemWSL remained at the start of this research round.
WSL stopped in the prior round; continue stopping it if it restarts as authorized.
No native game was launched for this read-only comparison. No push/issue comment.
At least2x original3.7FPS and60FPS/fullFX goals remain unmet.

## Historical checkpoint: actual world confirmed; CS stage trial rejected and reverted

Native Windows local `165a1530`, branch `yotei-integration-native`.
The optional format lookup `9a8b61da` is FULLY reverted by `ce0b3848`; exact
feature patch/caches/test artifacts remain archived under cycle45 outside
tracked source. Genuine storage RED/GREEN
`221328/221553` preserves all128 metadata and removes2048 declared Privatebytes;
same-process0/1/0 RED/GREEN `222703/223230` preserves per-compilation profiles.
Native388-format probe/60 canaries and52 channel/wave32/64/EXEC neighbors pass
both modes. All4800 generic numeric outputs match exactly; BOTH retain144 old
UNorm8 endpoint failures/exit2, so strict numeric suite is not passing. Six
native microbench cases differ below0.5%; no meaningful throughput benefit.
Cache admission RED224353→GREEN225654 and warm missing-ON neighbor225655 pass;
legacy audit RED225525→GREEN225548 plus explicit0/1/0 profiles pass. OFF cache
identity remains unchanged, ON files isolated; no cache headers retagged.

Native core225814/install230026 pass. Current EXE SHA256
`516BCF5B0F2A708990040DE8F24B4C23C799BEA5C190DE8D829E7F2087913C4D` in
`_Build/analysis/yotei-performance-cycle45-20261014/runtime-format-world`.
OFF control `game-20261013-230053-perf-cycle45-format-control0-world-37dc99`
closed0,90.136s gate,frames447–467,title6s mean3.75,no guards. No new visual
claim for that control. ON `230455…90a07a` did NOT reach the game: one actual
driver compile151.6s,next still compiling; timeout180/forced cleanup/exit-1.
No sampled world FPS and no performance rejection/acceptance from loading.
Warmed ON retry `230912…6e3456` also ended timeout240/forced cleanup/exit-1,
onlyframe2, no world sample. Second changed pipeline compiled160.8s, third
still compiling. This is a startup-budget failure, not a steady-world FPS
comparison. Both wrappers drained/native processes absent before the revert.
Candidate acceptance declined without a throughput witness; no cold-vs-warm
compile regression claim. Installed6a77 EXE still contains reverted code;
do not relabel it as currentsource. Fresh native build required.

User correction: frame400 alone may be loading. Use minimum90s PLUS400 for
short6s title samples; positive acceptance additionally requires actual world
pixels. A time guard alone also cannot prove scene readiness. Do not stop a
startup merely for high loading FPS or compare a cold load with warmed world.
Retain neutral input/no menus/1080p/full render. Exact FC6 compile-only stats
OFF20s/ON30s both timed out before native pipeline creation returned: no
statistics/spill values obtained and no dispatch occurred. Do not extend/retry
that diagnostic repeatedly. Stats-only tooling6a77 remains independent/defaultOFF.

Observer0b9133f6 initially hit ordinary ADL compile error, corrected692d96d8;
native core231739/install231808 PASS. EXE
`BF6F44211BB6D1E2EAD4A657C41C4E755D0565F5746A3ED5F10E8E15A1C8B5A7`.
Actual world diagnostic `game-20261013-231842-diag-cycle46-world-wait-partial-slices-8c6611`
closed0/no guards. Screenshot `world-phase-diagnostic.png` viewed: character,
village, vegetation and particles in real3D world; no claim of interactive
controls or stable fullFX from this single still. Frame505 capture metadata,
20s title mean3.4588 is instrumented, NOT clean FPS rejection/acceptance.
Trace frames480–515:94258requested/retained,dropped0,pointererrors0; actual
CS07=9991,pendingcompute7930,rendering34; PS10=5574,rendering2458. Native
Transfer clears413,actualColor clears0. Counts alone are not GPU elapsed cost.
WAIT all-attempt totals captured, BUT requested65565/forwarded16384/suppressed49181
means sparse episode joins are incomplete; do not claim whole-window causes.
CV6286returns/2029timeouts/4257signalled,3.247s inclusive instrumented total;
overlaps queue/episode gaps and is not wholly removable. No CPU-store timestamp.

Native CS cost RED232311/6E3→unchangedGREEN232612/2434 proves broad stage cost
after actual numeric/guards/cleanup controls. Actual alias/color/rollover/reset
neighbors OFF/ON final233322/233323/D803 PASS with padded guestspan0x800 and
unchanged native float bits; four CTests233411 PASS1.43s. Newproduction b052877f
remained defaultOFF. Native core233424/install233525 PASS; EXE
`B7FAEB3D02CEE8DA35DD352C43F59DCFFD841E6EBD25D4AC7A9F273894EDE82C`.
Clean SAME EXE/runtimes/args/neutral1080 fullrenderer AFTER90s+400:
control `game-20261013-233527-perf-cycle46-world-cs-scope0-aacac7` title6smean3.6,
start509/90.348s,exit0. ON `233737…264cee` began543/90.271s, then547/title3.4
<=3.6 was immediately rejected peruser,exit0/no guards. No post-gain screenshot
or FPS gain. FULL b052 production/tests/CTest reverted165a1530, exactpatch/test
artifacts kept cycle46/rejected-cs-scopes. Earlier native behavior/storage cost
proof was not a frame bottleneck proof. Do not blindly carry this into PS10.
InstalledB7FA still contains archived feature; do not relabel it as currentsource.

Next: expanded complete WAIT diagnostic (cap131072 already3f1d, pointer span10000
or bounded allwriter capture) for upper counterbank CPU/GPU producer causality.
Existing100us CV floor and pre-store faults do not prove an earlier-readyCPUflag.
Separate TEST-only native-indirect observation WIP preserves the old original
host-drain assertion bydefault and unchanged later numerical goldens; it examines
a likely stale test expectation, not a production fix. Current test target build
active; sourceFROZEN. Fullcodecoverage/group PASS not claimed; original group
stops before numerics even on pre-CS6E3 archive. Three agents audit exact cause,
clean A/B parity and remaining shared barriers. No pre-store notify, no blind
barrier deletion. WSL
stopped again23:09UTC; user data/old caches/Prefetch WIP preserved. No push or
issue comment. At least2x original3.7FPS and60FPS/fullFX goals remain unmet.

## Current checkpoint: warmed control disproves quantum gain; candidate reverted

Native Windows `406395a4` on `yotei-integration-native`.
Completed actual world/HUD comparisons on SAME EXE `4035C2BC…`:
control0 `214216…11d1b4` mean2.516667; quantum64 rejected2.4; quantum512
`214917…3a92f2` mean3.56; quantum2048 `215517…876677` rejected3.5.
Necessary repeated warmed control0 `220203…715c61` mean3.54, frames536–560;
same village/player/HUD visually matches quantum512 at537–561. Bothcontrol
images and one512 image inspected; second512 capture withheld by focus guard.
All these runs closed0/no memory guards, full render/neutral input/90s+400 gate.
512 versus warmedcontrol is only+0.56%, not meaningful. Earlier+41.5% pair
was not a confirmed policy gain and is superseded by this repeat. No2x result.

User non-gain rollback carried out: FULL `028b5994` production/tests/CTest
reverted by `406395a4`, with exact patch, fixtures and all runtime/test evidence
preserved under cycle44-20261014/rejected-PM4-quantum and normal check folders.
No native process active now; verify before edits. Installed quantum runtime
stillcontains oldEXE and must not be relabelled as revertedsource. Diagnostics
remain defaultOFF; nextnativebuild must use currentsource and isolated runtime.

Next largest shader experiment is immutable dynamic-format metadata: actual
FC6 has four Private128u32 arrays, 2048 declaredbytes/thread/no stores.
DefaultINDEXED_SWITCH does not affect this emitter-generated path. This is
not measured driver spill cost and FC6-alone9.2ms cannot establish2x. Shared
constant-returning lookup proposal/independent128metadata CPU regression and
finiteactualGPUprobe are being prepared in ignored cycle44/gpu-next-candidate.
Production shader patch UNAPPLIED; genuinecostRED/GREEN/cacheidentity/native
numeric/driverorworkload proof required. BDA/globalrecording-query ports lack
a large current cost witness; avoidduplicate caches/unsupported range skipping.

Common::CondVar::WaitFor100 requests100MICROSECONDS (Windows Clang roundsat
least1ms), not100ms; one100msblocked-gap hypothesis was wrong. Old cumulative
blocked-poll timing aggregatesmanywaits. Testdebtcorrected; no schedulerchange.
Userdata/oldruntime/PrefetchWIP preserved, WSLunused/stopped, no push/issues.
Actual currentworld≈3.54FPS, ≥2×/60FPS/fullFX goals remain unmet.

## Current checkpoint: native quantum regression proved; world FPS still pending

Native Windows local `028b5994`, branch `yotei-integration-native`.
Default-OFF CPU packet-budget candidate has genuine RED/GREEN and independent
native GPU/IB/CE/FIFO proof. Source policy has not been accepted as a world FPS
fix. Complete CPU fairness RED `212750`/EXE `115A34F5` → unchanged GREEN
`212922`/`E40BB0E4`; all values/guards/cleanup pass. Corrected native dependency
fixture (real ACQUIRE_MEM, unchanged numerics) OFF/64 and IB+CE OFF/64 pass
`213632`–`213635`/`87FB57A4`. CPU IB/CE/FIFO both modes pass. Fresh existing
WAIT queued/ready/async, gpu_command_lane and WAIT32/64 ABI pass `213918`–`213923`.
Native core `214009` and install `214147` PASS; private runtime
cycle44-20261014/runtime-quantum-world is cloned from preserved cycle42 runtime.
EXE `4035C2BC2DE6F02F487289145F649E1BC1C3F13A9F08AABF5ACD59496A6B282B`.

Latest user confirms too-early run had not reached the game. A warmed startup
can pass frame400 while loading. Do not count 15–40 loading/black FPS as gains.
Current comparisons retain frame400 but wait90s before the short6s title gate;
positive acceptance still requires a visibly rendered world. This later scene
is not comparable to the prior 4.16 short early-world sample.
Control `game-20261013-214216-perf-cycle44-world-control-quantum0-11d1b4` closed0,
90.55s gate, frames1382–1395, mean2.516667; no guards, no screenshots/no visual
claim from this control alone. SameEXE quantum64 `214515…c81ca2` startedsample
at495/90.57s, then title2.4<=control rejected/closed0; restore/remove64 setting.
Quantum512 `game-20261013-214917-perf-cycle44-world-quantum512-3a92f2` rootPID53436
currently ACTIVE, source inputs FROZEN; outcome pending. Wait/drain it before
any build/test/source edit. Try only authorized bounded variations; if entire
candidate family lacks gain, preserve evidence then revert the policy commit.
WSL remains unused/stopped; no push/issue; keep old runtimes/data/PrefetchWIP.
Goal ≥2× original3.7FPS and60FPS/fullFX remains unmet. Three agents independently
audit next generic BDA read ranges, recording synchronization and readonly shader
format tables; no further production port is applied or promised to double FPS.

## Current checkpoint: actual queue latency isolated; Demon’s Souls comparison

Native Windows, branch `yotei-integration-native`, local `48e71fdc`.
User confirmed the 30+ FPS Demon’s Souls result belongs to another person.
Saved primary-source comparison and candidate ranking:
[Yōtei versus Demon’s Souls](yotei-vs-demons-souls-performance-2026-10-14.md).
No local Demon’s Souls dataset/game is available. Do not attribute the difference
solely to hardware or assert Demon’s Souls uses a simpler rendering model.

Best tentative normal setting remains GPU core 1: short full-render title sample
`201848…91f0b6`, 402–421, 4.16 versus 3.7 FPS, actual world in two post-gain
screenshots. Combined affinity/priority change, not sustained or doubled FPS.
Label interval 0 rejected at 3.9; pure clock-spin PAUSE rejected at 3.8 and
fully reverted `731acca9`. Age 1000 failed before frame 400 at frame 333;
restore age 0 and do not repeat risky age runs without bounded regression.
Exact wait-result logging `fe0ef118` is now included in the built diagnostic;
it diagnoses a later failure, does not fix the earlier unknown Vulkan result.

Actual causal trace `game-20261013-210900-diag-cycle44-main-table-readable-4a6531`:
source `aeb13a50`, EXE `9431EA6FE88935D8ABCE0546BB3879620FA9E533C3D7667E70954DF3E3A74914`,
native core/install PASS, exit 0, no guards, stderr empty. Frames 400–437;
20869 retained rows, dropped 0, pointer errors 0. Loader global is private
memory outside Kernel backing; bounded readable-only RPM diagnostic resolves
the actual GPU sequence table to `0x2000000000` on every sampled frame.
All 73 label arm/publication/finalize chains join by owner/tick/token.
Arm→finalize mean 0.730 ms, p95 1.574, max 2.132: backing publication is not
the measured 140 ms delay. Guest enqueue→first CP start: queue 1 Compute mean
72.804 ms/p95 120.043; queue 0 SuspendPoint mean 139.821 ms/p95 161.973.
Queue 52 CP start→label mean 75.151 ms includes resumed slices and other work,
not pure CPU cost. Analyze uninterrupted processing versus dependency blocking
before declaring unfair scheduling or changing the shared queue mechanism.
No faults at selected PCs were recorded; unfaulted polls are invisible.
Artifacts: cycle44/trace-analysis-4a6531/{causal-analysis,submission-to-label-analysis}.json.

Existing `--async-submit false` quick comparison `211518…3af95d` showed a
nominal 15.22 FPS after frame 679, but BOTH post-gain captures were fully black,
only 159 replay compute pipelines were consumed, and close required forced
cleanup/exit -1. User independently confirmed it did not reach the game.
REJECTED, not an FPS improvement; restore async submit true. Frame 400 starts
evaluation, but a positive reading must still be followed by visible world.

Next temporary diagnostic `48e71fdc` adds SliceEnter/Exit around actual
GuestGpu::Process; native build currently finishing, execution pending.
Three agents audit finite packet-budget yield regression, CPU resume/binding
semantics, and actual slice/label joins. Fair-yield code not applied; requires
genuine RED, unchanged GREEN, FIFO/CE-DE/indirect/blocked/native neighbors.
GC donor 19.7→45 FPS does not reproduce here: existing frame ages and 500 ms
budget cache already present; late cycle33 registrations 35–39/s, collected
35–40/s, buffer allocation 0.23–0.26 ms/s. No blind GC rewrite.

Default diagnostic OFF; preserve all guest work, clocks, completion, bounds
and original captures. Goal ≥2× original world FPS remains unmet.
No push/issue publication; keep WSL stopped and preserve historical untracked
ComputePrefetchPermutationTests.inc, user games/saves/caches and prior artifacts.

## Current checkpoint: after400 reject loop; DMA trial removed; Main polling confirmed

User nowrequires immediate FPS evaluation afterframe400; equal/lower candidate
mustbe removed and nexttried. This supersedes650/90s warmgate fornew trials.
Overall goal remains actualculprit and >=2x cleanworldFPS (~7.4+ from3.7).
Do not stopat another smallcostproof orclaimgoalcomplete. Currentlocal8b9d9118,
8b9 nativecore200814/install200943 passed, EXE023918DBCAEF49FED47DF31AB4CBCC3EBF4B7DF029EE86EA0F48CC3D8993E828.

Native DMAfill e615 had genuineRED193418/F9B40B8B→GREEN194610/104E8B0B
and3realGPUunwritten/CPUold-new/pendingoutsidebytes-neighborPASS194611–194612.
Actualworldeligibility only~16/frame, allGPUdirty/noCPUfastpath. Fast400trial
200249...c5d6aa reached401; at414 title3.7==baseline3.7, rejected/closed0.
Userrequestedrollbackexecuted65727fc1 (wholee615consumer+newfixtures/CTest
removed); exactsources/patch archivedcycle42/rejected-DMA-fill andallrunskept.
Independent sampledmaterialization diagnostic b829 retained; knownCPUfixture
UniformVector compiled/reference discrepancy reproducedWITHOUTIRdiaghooks
194135/35AE5A50, observer-only194037/6777766E PASS, notinstrumentationfault.
MissingcompiledLoadBufferU32 remainsseparatecorrectnessdebt; CPgamefallback0.

Actualdiag194922...210633 e615/EXE73FEB ended195223UTC0/noguards: CP4060/4060
success/referencefallback0; Flatleaf1.731us abovefloor butnotdouble-FPSlead.
NamedCPU20.032s/query0: Main13548 86.345%, GPU32100 53.823%, Spec46496 33.15%,
recorder46620 30.26%;NVML19samplesavg48.42%GPU (30–61). MainCPUinitiallylooked
busy; newRIP confirms WAIT. Newafter400 diagnostic201017...874d08 ended0:
ownedMain37884/31420,10.015s73/73valid/allcleanup0, CPU81.75%;58/73 positions
inpureRDTSCdeadlinepoll loops,10/73 nativewin32uGPUwait. Matchesexactoriginal
SELF hash andvalidhistorical224/224 dataset; no costlyHLE/TLSbodydominance.
PSS samples scheduling/wall positions, notCPUcyclefractions orcleanFPS.
Rootselfcontrolcurrent5sPASS. Data cycle42/owned-main-31420-after400/pss-main.json.

Next3independentagentspreparing (read-only/ignored): generaldecoded PUREclockspin
PAUSE preservingflags/register/actualdeadline; topology-aware dedicatedCPUcore
policy usinghostCPUsets; exactrealproducer→native→publication→guestconsumer
latency. No guestaddress/hash/title hacks, fakecompletion, clockshortening or
barrierdeletion. Actualcountertablewriterunknown; sourceSubmit/Suspend global
barrier+priorcreditnativecompletion semantics must be kept. ETWprofiling
privilegesabsent; validatedboundedPSS helper supportsStartFrame400, no explicit
SuspendThread butcapture mayperturb scheduling. Latestzero-fileReadfail markers
+existingWindowsbounce/APRstaging ruleoutblindfileReadport asFPSclaim.

No nativegame/build/testactive now; verifybeforeedits. WSLshutdownauthorized;
no push/issues; preservehistuntrackedComputePrefetchPermutationTests.inc,
usergame/saves/caches/oldruntime/artifacts. FullFX/60FPSand >=2xgoal unmet.

## Current checkpoint: completed SRT drill narrows next target; clean FPS still3.7

NativeWindows localHEAD46fe8e8b. Both cost corrections have genuine native
RED/GREEN and requiredneighborproof; actual clean30s FPS gains are negligible:
readbackready3.714286→3.746429 (+0.87%, SAMEEXE), GPUdirtymask3.714286→3.725
(+0.29%, isolatedproduction acrossEXEs). No meaningfulgain/images; readyflag
remainsdefault0. Full60FPS/FX unachieved. HistoricalPrefetchWIP/userdata kept.

SRT diagnostic run game-20261013-191442-diag-cycle41-srt-function-profile-730fa4
completed19:17:31.958UTC/0/no guards/stderr empty/noimages. Source46fe8e8b,
EXEA015DE1F34575D52DB6CE2990771691728D3BB004C09BC46B22B2D685A7B5113;
650→686,10.1873071s,27threads/176rows, allquery/exclusive/truncated/outstanding/
depth/overflow errors0. Three independent agents audited actualcompletedrows.
ParsedJSON/MD: cycle41/srt-drill/completed-cycle41; independentgpu-state JSON.
No task-ownednativegame/build/test remains active; verify onnextresume.

MeasuredCP11404 MaterializeResources245480calls/3926samples: incl4.570us/
excl3.907us, raw14232cycles vsrootempty~1630–1753. This sharedgraphics+compute
body is the clearest nextCPUcandidate, notprovedframe-critical orremovable.
Mutexwait0.031us/permutationmatch0.151us nearprobe floor: do NOTremove lock/
addworkers/hotset basedonthis. ParentGetGraphics14.464us versusold4.42 has
addednestedprobes; notproductionregression evidence. RoleMain1 ishostentry;
loggedworkerTID56256 recordsrole0, noactualDrawSpecrole4 assignment.

Next: sparse operation counts and rotateONE sampledleaf within actual
MaterializeCompiled/refresh-flat-SRT/memo-key/descriptor-eval/referencefallback.
Existing compiledplans/memo/64Bblock caches alreadypresent. Neither duplicate
reads norfallbackfrequency proven. Obtain actualwitness then recordsynthetic
RED coveringdescriptor/selector/ownership/imagealias changes beforefix. Foreign
readback stillmostlyrequirednative/publication wait; noexactguestMainCall/frame
request correlation exists, so do notsum acrossTIDs orclaimcritical-chaincost.
WSLoff, no push/issues. Research reconciled in docs/yotei-performance-research-2026-10-13.md.

## Current checkpoint: two proved cost fixes have no meaningful world FPS gain; SRT drill active

LocalHEAD46fe8e8b. RepeatedGPUdirty53bfa9a1 nativecore190816 andprivateinstall
190912 passed. Actual clean full30s worldtitle run190931...1aca64 closed
19:12:03UTC/0/no guards: mean3.725,28titles,frames650–758, range3.5–4.0;
previous isolated-change baseline3.714286 (+0.29%) givesNOmeaningfulgain/noimages.
Different EXEs, samefull1080p/render/neutral/settings; do not call sameEXE A/B.
Optional ready-after-wait staysdefaultOFF (+0.87% prior sameEXE pair).

New diagnostic46fe8e8b splits real GetGraphicsPrograms into stageprepare/adopt,
inputchecks, mutexwait/lockedbody, exactProgramCacheGet/permutation/adoption,
SRTmaterialization/readvalidation. Existing sampler/collector remainsdefaultOFF;
no new GPUtimestamps/submits/fences/forcedFinish or shippingtestcounter. Native
CPU smoke191317/CD971B87 passes, core191318 andprivateinstall191429 pass.
EXEA015DE1F34575D52DB6CE2990771691728D3BB004C09BC46B22B2D685A7B5113.
ACTIVE bounded diagnosticgame46464 / execsession35688:
game-20261013-191442-diag-cycle41-srt-function-profile-730fa4. All other heavy
instrumentationOFF, neutralfullrender; profiler650→10seconds, closeafter15s
sample. SOURCEFREEZE until completion; verify no remaining ownedprocesses.
This is diagnostic-only; do not use its FPS as clean comparison.

Root/3agents will inspect actual CP vsworker rows, calibratedquery overhead and
errors/outstanding/depth, identify stage/cache/SRT body or requiredwait before
anotherproductionfix. Avoid duplicate already-existing preparedinputs/last-
permutation/compiledplan/memo/64Bblock-cache optimizations. Preserveallcache/
save/artifacts/historyPrefetchWIP. WSLoff; no push/issues; fullFX60FPSunmet.

## Current checkpoint: redundant GPU-dirty protection mask proved; world check pending

Native Windows dd8ff4de + WIP. Genuine nativeRED190549/B8B01E70: all setup/
all-pageGPUdirtyCPUcleanUnarmed/PAGE_NOACCESS/native4MiB generations/fullbacking/
guards/cleanup controls pass, then redundant protectionMaskEvaluations1 fails.
Unchanged GREEN190651/625EE5D8 gives0;5native neighbors190652 pass expected
mask counts0/1/1/1/0: partial63/64boundary, initiallyclean, realEOParmed,
actualstaleread+reprotection, armoutsidewrittenrange. Shared locked state guard
is after range and CPU-conflict validation; AllInRange touches only requested
words. The observer compiles only into the existing lifecycle-test target.

Native independent BitArray64/128/1024 bit-by-bit190759/DD6EF117 and entire
MemoryTracker190759/837D57D4 pass, including OS protection/tokens/hotstale/
conflict death paths. Fixed a preexisting dangling BenchmarkCleanUploads call
which prevented rebuilding that CPU test target; no benchmark implementation
existed in source. Twelve exact affected CTests pass4.62s. Native core build
190817 currently active; game/privateinstall/actual FPS improvement pending.
Baseline world policy0 mean3.714286 on previousEXE0FC1FA9A, samefullrender/
neutral1080p/settings; optional ready-after-wait staysOFF after its negligible
+0.87% pair. Preserve warmcache/save/privatepreviousruntime for new comparison.

No push/issue update, WSLstopped, historicalPrefetchWIP untouched. Full60FPS/
FX remain unmet. Finish nativecore, privateinstall, clean30s worldtitleFPS;
images only if meaningfulgain, then checkpoint/handoff/local separatecommit.

## Current checkpoint: first measured wait optimization gives no meaningful FPS gain

Local327065f6 + result recorddd8ff4de. Actual completed clean native world
comparison, SAME EXE0FC1FA9A, full1080p/fullrender/neutralcontrols: policyOFF
3.714286 vs ON3.746429 (+0.87%),28titles/full30s each, frames650–759/652–761.
Both exit0, no memory guards/screenshots; opt-in remains default0. Exact runs:
185854...dc2fe8 and190137...d7121f, comparison cycle41/clean-ready-wait-comparison.json.
This does not prove the redundant wait was a frame-critical bottleneck.

Next shared candidate: repeated writable buffer acquisition recalculates a
whole4MiB-region protection mask even when exact range is alreadyGPUdirty,
CPUclean, unarmed and unstale. Three-agent proposals prepared; root integrated
only the harness diagnostic counter/native fixtures and recorded required
regression in debt. Native RED target build190448 is active; production state
transition correction is NOT applied. Verify completion before edits/execution.
Only after genuine all-controls-pass/mask1 cost RED: apply exact locked/range-
validated idempotence, independent bit-by-bit AllInRange and CPUconflict/OS/token
neighbors; unchanged nativeGREEN then core/privateinstall/worldFPS. Existing
work/proprietarydata/pipelines/historicalPrefetchWIP preserved, WSL stopped,
no push/issue update. Full effects/60FPS remain unmet.

## Current checkpoint: ready-after-native-wait optimization proved; world comparison pending

Cycle41, native Windows, c8dde68a plus recorded WIP. Genuine RED `184937`,
EXE0C3747C1: two finite GPU producers/downloads on one native tick, A genuinely
published while independent B remained gated. All native/backing/token/guard/
cleanup controls passed; only redundant PriorityWait1 failed the zero-wait
oracle. Unchanged GREEN `185119`, EXE33D64895, passes with PriorityWait0.
Same executable policy0 `185132` retains the intended cost failure, all controls
passing. Optional KYTY_READBACK_READY_AFTER_WAIT=1 rechecks full requested
buffer state after native wait, full rounded-page pending/dirty image guards,
mapped range, then current buffer state again. Default remains off; existing
CPU retry/write invalidation/native completion/token paths are preserved.

Old-armed and queued/new-producer controls `185130–185131` pass. Fresh mixed
A+B and unpublished-A controls `185307–185308`, EXEA1F1252A, pass with the
necessary PriorityWait1, real bytes, guards and no extra submission. Strong post-wait new writer185421 passes newest native/backing/faultload.
Actual dirty/pending images outside requested bytes on the samepage185801–
185802 EXE18E346D6 pass, retaining PriorityWait1 and complete imagepublication.
Pending image also staysdirty; pending-only directpostwait isolation is still
testdebt. Eight affected native CTests pass in3.89s; existing read/write image
controls185440–185441 pass. Core185448 and privateinstall185820 pass.
Clean matched world policy0/1 comparison completed on327065f6/EXE0FC1FA9A:
OFF mean3.714286 vs ON3.746429 (+0.87%),28titles each/full30s, frames650–759
and652–761, both exit0/no memory guards. Runs185854...dc2fe8/190137...d7121f.
No meaningful gain or screenshots; default stays0. Next measured lead:
repeated GPU-dirty/protection mask bookkeeping, native regression first. This is not yet an FPS improvement.

Discard cycle41 control `game-20261013-184050-...-b5edb2` from world FPS:
frame650 arrived while still loading; the two post-title captures show loading
and transition, not gameplay. Its ~20.6FPS is invalid as a world baseline.
New ignored helper cycle41/world-title-gate requires elapsed>=90seconds AND
frame>=650 before the full30-second title sample; syntax checked. Use identical
full-render neutral-control executable/settings for policy0/1, then inspect
images only after meaningful clean title gain. WSL shutdown185232 exits0.
No push/issue publication; full effects and60FPS remain unmet.

## Current checkpoint: WSL stopped; performance research and completed profile reconciled

Native Windows, branch `yotei-integration-native`, investigated source `fd11eba2`.
The latest user requests research into the low FPS. WSL shutdown completed with
exit0; subsequent running-distro list was empty/exit0. No native game, build or
test is currently active. No game/build was launched during this research.

The function-profile core build `135630` and install `135920` passed. Actual
run `game-20261013-135940-diag-cycle40-sampled-function-profile-bb6889`, EXE
`AE228D2366B103DB09C495BE50BEC84E4297A675D6F1E690EB946758E40D8C9C`, closed
14:02:28 UTC/0/no guards. Profile: frames650–689,10.1963867s,26threads,165rows,
no query errors/exclusive-invalid/truncated/outstanding/overflow. This resolves
the pending core/install/profile entries below. Diagnostic FPS is not a clean
performance result; no screenshots were taken.

Self-contained Russian research:
`docs/yotei-performance-research-2026-10-13.md`.
Three independent notes and WSL state are preserved under
`_Build/analysis/yotei-performance-research-20261013/`.
Best-supported diagnosis is mixed translated GPU work, CPU/GPU publication
dependencies and repeated resource preparation; the exact frame critical path
remains unproved. Logging/label/spin changes gave no substantial clean gain.
Do not treat1.2M Sync/Upload calls as1.2M actual copies or rank probe-floor CPU
measurements as expensive code. GetGraphicsPrograms is above calibration;
split its stage/cache/SRT work next. Correlate readback requests to actual
producer/publication/consumer before removing waits; isolate main GPU groups
with sparse native timestamps. No new optimization was applied in this research.

Defaults/guest correctness preserved, historical Prefetch WIP untouched, no
push/issue publication. Full effects and60FPS remain unmet. Continue serial
native execution and30-second clean title-FPS verification after a proved fix;
images only after meaningful clean gain. User authorizes keeping WSL stopped.

## Current checkpoint: requested function timing installed; native CPU accounting verified

Latest user steering is to time functions, identify the real bottleneck and
optimize that measured mechanism. Diagnostic `KYTY_DEBUG_FUNCTION_PROFILE=1`
now instruments finite draw/dispatch, binding, buffer/image, native submission,
readback/publication, recorder payload and wait scopes. It defaults off. Actual
presented frame650 starts one 10-second window; randomized roots sample1/64 and
synchronous nested scopes inherit that decision. Per OS thread/role: calls,
sample counts, inclusive/exclusive steady wall and raw QueryThreadCycleTime,
matched empty probe, query overhead/errors, truncation/outstanding/depth limits.
CPU cycles are never converted to milliseconds; nested/thread totals overlap.
No GPU timestamp queries, forced Finish, extra native submits or memory dumps.

Native CPU smoke `134728/861341F4` passes real bounded work, two actual threads,
exact selected child subtraction for wall/raw cycles, empty-query counts,
disabled collector and zero outstanding samples. Build `134532` passes. There
are no allocations/global mutexes at first fault-scope registration; fixed
diagnostic slot overflow is reported. Activation acquires the epoch; default
inactive scopes initialize only their inactive handle before returning.

Core build/private install and one game profile remain pending. This is
diagnostic instrumentation, not an emulator correctness fix or FPS improvement.
Heavy drain/GPU-zone/other timing switches stay off for its run. After profiling,
choose the largest evidenced CPU body/wait relation, establish its regression,
then perform a clean 30-second world-title FPS comparison without profiling.
Existing two opt-in experiments/defaults and prior game artifacts are preserved;
no new push/issue publication, full effects/60 FPS still unmet.

## Current checkpoint: debug/settings trials no substantial gain; eager write lead next

All owned native runs/builds/tests are closed. Local code `a533ee3d`, EXE27567CFF
same-build full 30-second comparison: spin2000 mean3.862069 vs spin100
mean3.820690, 29 titles each; spin100 frames650–763, range3.5–4.0, closed
12:58:53 UTC/0/no guards. No meaningful gain/screenshots; default remains2000.
Buffered stdout, labelinterval2000, write-window1, chain1, BDA/exactreads off.
Earlier logging and label-interval trials likewise showed no substantial gain.
Heavy diagnostics were already disabled; required rendering/coherence remains.

Next three-agent round found a narrower cause to test: actual foreign write
faults never enroll their page into existing eager publication, so repeated
CPU/GPU ownership exchanges can keep forcing on-demand downloads/submissions.
Prepare a two-cycle native regression first; required producer work must execute
at its normal submission, all GPU/CPU bytes must remain current. Only an opt-in
policy is contemplated; neither production change nor RED exists yet. See the
new regression entry in `docs/emulator-test-debt.md`.

Separate-queue enabling remains deferred: current writer reservation is not
an actual writer ticket, and ownership-copy/image producers plus reverse
dependencies need independent proof. Latest upstream df5b9b92 and Jet f9e19582
provide no blindly compatible performance patch. All previous commits/tests/
captures/user data are preserved; no push/issue update, 60 FPS/full effects unmet.

## Current checkpoint: recorder control closed; same-build 100-spin trial active

Local source `a533ee3d5c9827126632eafa0774e50d084fa14d`, native core `125151`
and install `125238` pass. EXE
`27567CFFCDF7702E08084506EB5CA883FE0B7031433D890C33C5D710069421F9` in private
`_Build/analysis/yotei-performance-cycle38-20261013/runtime-a533ee3d-idle-spin`.
Default behavior remains 2000 spins; the optional input accepts 0..2000 only.

Same-build control `game-20261013-125251-perf-cycle38-idle2000-buffered-world-title-30s-abba11`
completed clean 30 seconds, 29 titles, frames 653–770, mean 3.862069 FPS,
range 3.5–4.1. Closed 12:55:20 UTC/0/no guards, no screenshots. Buffered output,
write-window1, compute-chain1, BDA/exact reads off, label interval2000us,
full rendering and neutral input. No heavy diagnostics enabled.

Only spin input changes to100 in
`game-20261013-125625-perf-cycle39-idle100-buffered-world-title-30s-4894f9`,
PID39912 ACTIVE. Source/tests/CMake frozen until owned run closes. Measure full
30 seconds after frame650; images only after meaningful clean gain. Three agents
now independently prepare next shader/backend, readback writer-provenance and
compatible fork candidates using saved evidence; none may build/edit native
inputs or enable an unproved side queue. All current synthetic/native validation
is complete; this workload result and actual CPU benefit remain pending.

## Current checkpoint: settings trials closed; optional recorder policy native proof

All game runs are closed. Same `ac0ac772`/EXE6205371B clean 30-second title trials:
write-window-on 3.839286 FPS; removing unbuffered stdout 3.75; buffered stdout
and label interval 4000us 3.865517 (29 values, frames 652–769, range 3.6–4.2).
The last run `123427-311c4b` closed 12:36:54 UTC/0/no guards. None demonstrates
a substantial stable FPS gain. Keep ordinary label interval 2000us; no images,
new push or issue publication. Heavy debug work was already off.

Actual 64 KiB-boundary test `124439/45B5B22D` now passes every native/backing
word and guard, two real write faults/ticks/copies, 16 bytes/256 reserved.
Earlier `123835` missed only the test exception witness; a test-only noinline
store and compiler signal fences preserve its TLS registration. The production
write fix and data/footprint oracles were unchanged.

Next experiment is `KYTY_RECORD_SPIN_ITERATIONS=100`, optional 0..2000 with
unchanged default 2000, changing only the recorder's empty-ring spin bound.
Native CPU RED `124109/686EF8EF` passes all four payload/order/sleep-race/wake/
Drain/counter/cleanup controls before the 2000>100 cost failure; unchanged
GREEN `124438/45B5B22D` passes with 100. Existing real 3000-payload/wrap/order
case passes under unset/default, 0, 100 and 2000; invalid inputs reject clearly.
Eight affected CTests pass in 2.53s. Actual deferred recorder WAIT_REG_MEM,
native SuspendPoint and native command lanes pass under record-thread1/spin100
(`125053–125054`, exact executables 45B5B22D and 8F6AB348).

Native core/install and clean world comparison for the recorder policy are
pending. A smaller spin can sleep before a tiny next batch; wake/submit/Drain
behavior remains mandatory, so CPU savings alone do not justify the default.
No shipping test observers or counters. Full effects/60 FPS remain unmet.

## Current checkpoint: debug overhead trial gives no gain; settings comparison active

Local fix `ac0ac772`, native core `121810` and install `121920` pass. Installed
EXE `6205371BB86A63FE018AEFB250300EE687C7BD986367C313DDE7951F7D44471A` in
`_Build/analysis/yotei-performance-cycle34-20261013/runtime-ac0ac772-write-fault-window`.
Clean write-window-on run `game-20261013-121951-perf-cycle35-write-window-on-world-title-30s-29addc`
completed 28 titles, frames 653–766, mean 3.839286 FPS, range 3.6–4.2; closed
12:22:19 UTC, exit 0, no guards. Versus the off baseline 3.611111 this is a single
pair +6.32%, not a confirmed useful gain; no screenshots were taken.

Actual clean-run audit confirms heavy diagnostics, Vulkan/shader/GPU-assisted
validation, dumps and async pipeline skipping are off. Eager descriptor setup,
linear-image readback, raw-image buffer coherence and hardware bounds are
correctness paths and remain enabled. PAD_RECORD writes one 29-byte neutral
event. Residual `vmmem` presence alone does not establish running WSL: bounded
`wsl --list --running --quiet` returned empty/exit 0 and its working set was 0 GiB.
Continue shutdown if WSL really restarts, as authorized by the user.

Same EXE/settings, only `KYTY_DEBUG_UNBUFFERED` removed (presence, so 0 is not
off): `game-20261013-123038-perf-cycle36-unbuffered-unset-write-window-on-world-title-30s-13c8a9`
completed full 30 seconds, 28 titles, frames 650–759, mean 3.75 FPS, range 3.3–4.2;
closed 12:33:25 UTC/0, no guards. No demonstrated logging FPS gain. Helper
`_Build/analysis/yotei-performance-cycle36-20261013/run-buffered-title-fps.ps1`
changes only that override and preserves neutral controls and measurement gates.

Run `game-20261013-123427-perf-cycle37-label-flush-4000-buffered-world-title-30s-311c4b`
PID53092 is ACTIVE, source/tests/CMake frozen: same EXE/buffered/window1/chain1,
only opportunistic label flush interval 2000→4000 microseconds. Mandatory
dependency/readback/submission waits remain. Drain this owned run before edits,
builds or native tests. Settings evidence is in
`_Build/analysis/yotei-performance-cycle35-20261013/settings-debug-impact.md`.
Clean 64 KiB-boundary fixture is ready outside source and remains unexecuted;
optional recorder idle-spin fixture is also prepared but not integrated or RED.
No new visual validation, push or issue publication. Full effects/60 FPS remain unmet.

## Current checkpoint: write-fault locality native proof complete; world comparison next

Shared opt-in `KYTY_CPU_WRITE_FAULT_WINDOW=1` carries actual `WriteFault` origin
through invalidation/retry and async GPU callback. A previously live complete
buffer owner may use the validated fault-ahead window (default 64 KiB, capped
at the original 512 KiB). Full selected pages must be mapped and have no dirty
or pending image aliases. Normal discovery, explicit/kernel writes, reads,
generation tokens, fences and priority publication are retained. Separate
QueueReadback geometry is unchanged. The performance experiment defaults off.

Genuine RED `120847/B14270F4` passed all native/value/fault/fence/guard/cleanup
controls before failing only the transfer cost. Unchanged GREEN
`121131/F846079A` copies 12 instead of 65548 bytes for the two real foreign
write-fault cases; explicit invalidation remains 65548. Real other-page dirty
and pending images `121256/DC810DAF` preserve wide downloads and image order.
Actual atomic RMW, cross-page store, and old-armed/new-GPU-writer cases
`121409/74065F48` pass every native/backing word and guard. Twenty affected
native GPU CTests pass in 9.66 seconds; log
`_Build/analysis/yotei-performance-cycle34-20261013/affected-ctest.txt`.

Initial `120725` was a missing fixture CLI registration, not an intended RED.
Independent production and fixture reviews found no blocking defect. A separate
64 KiB boundary control is being prepared; core build/install and clean world
30-second title-FPS comparison are pending. Current same-build off baseline is
3.611111 FPS, frames 653–757. No new FPS gain, image proof, push or issue update.

## Current checkpoint: clean baseline before CPU write-fault locality experiment

Native Windows, branch `yotei-integration-native`, source `3a8a74e6` (local only).
Run `_Build/runs/game-20261013-120151-perf-cycle34-write-window-off-baseline-world-title-30s-c62c92`
used EXE `06E533BF62A0DF06DDF93B9A6BF591631F9410D1121809E19AE1F730BE2B4E39`.
Full clean 30-second world-title sample: 27 values, frames 653–757, mean 3.611111
FPS, minimum 3.2, maximum 3.9. Closed at 12:04:23 UTC, exit 0, no memory guards.
Chain 1, BDA batching off, exact GPU reads off, full render, neutral controls;
no diagnostic timers, menus or screenshots. WSL was stopped again after automatic
restart; the user explicitly authorizes continued shutdown. No useful gain yet.

Next candidate is actual protected foreign CPU write-fault readback locality,
motivated by 66.89% of packed bytes in the separate cycle33 diagnostic. Three
agents independently prepare the native reproduction, review owner/image/page
preservation, and audit recorder scheduling. Production behavior is still unchanged.
Regression requirements are recorded in `docs/emulator-test-debt.md`.

Matching EXE/PDB bounded disassembly confirms the recorder's empty-ring 2000-PAUSE
loop. Its true CPU share and FPS benefit remain unproved; reducing that loop is a
separate fallback candidate, not evidence for more workers. Offline evidence:
`_Build/analysis/yotei-performance-cycle33-20261013/pss-cp-recorder/owned-52968-1141/offline/conclusions.md`.
Keep experiments opt-in until useful clean throughput is demonstrated. Actual
60 FPS and complete effects remain unmet. No new push or issue publication.

## Current checkpoint: actual CP/readback diagnostic; foreign CPU writes priority

3a8a74e6/EXE06E533BF diagnostic114100-ca85fc exited11:43:36UTC/0,no guards;
both game/PSShelper fullyclosed. Nevera cleanFPS orimageproof. ActualownedPSS
10.014s,69rounds/69validsamples perrole, allsnapshot/handlecleanup0,errornull:
Thread_Gpu32068 CPU5375ms/~53.67%onecore; GpuQueueSubmit18452 CPU2984.375ms/
~29.78%. Rawcycles retained; wallRIP frequencies are NOT CPU percentages.
MatchedPDB GUID/age +exactEXE validatedbyagents. NTDLL waitreturn24/69CP and
46/69recorder; win32u GPU-sync return6/69CP. Recorderowncode15/69 in
CommandStream::Consume, boundedassembly checkpending. CPnative25/69 spread
acrossSRT/buffer/prep/recordingwithnomajor>2-sampleaddress; no caseformoreworkers
fromthat alone. Existingphasewall/TSC includeswaits, no CPUcostsumclaim.

New reporter30blocks allrow-count/packedbyte sums validated. Last6completed5s
intervals108frames/13870readbacks:21.10097packedMiB/frame. ForeignCPU WRITE
faults14.11387 (66.89%),GPU READ faults3.39818(16.10%),foreignREAD2.91672
(13.82%),unattributedcandidate0.54288(2.57%),eager0.12932(0.61%volume but45.83
requests/frame). This explains smallcoverage ofexplicit-page experiment; packed
bytes are not nativecopied/requested/removable time. Next establishforeignwrite
FullDrain timing and actualwrite-fault-ahead/RMW preservation policy BEFORE
changingits window; priorexactGPU experiment explicitlykeptthispathunchanged.

Actuallate retiredpool24/29,038,592B (~27.7MiB) farbelow1GiBcap: hugeidlepool
is NOTthisscene's demonstratedcause. OverallVMA/driverresidency interpretation
stillpendingagentanalysis. Forkbindingprep broaderthanneeded; existingTLS/SRT/
memos/certificatesalreadyhere. Read-only3agents now auditwritefaultRMW, matching
wait/pause code and measured CPUphase costs. No nextproductionfixapplied.
BothBDA/exactreads OFFdefault; no screenshots/newpush/issues,WSLkeepstopped.

## Current checkpoint:3a8a74e6 CP/recorder diagnostic ACTIVE, 2026-10-13

Nativecore113937/install114058 PASS; privatecycle33/runtime-3a8a74e6-diag
EXE06E533BF62A0DF06DDF93B9A6BF591631F9410D1121809E19AE1F730BE2B4E39.
`game-20261013-114100-diag-cycle33-cp-recorder-bytes-pool-ca85fc` PID52968/
root45965 ACTIVE/sourceFROZEN. Both experiments0/fullrender/neutral/chain1/
HALFoff/scratch1024. Dedicateddiag addsdrain5, existingdrawphasesall/specstats1,
newreadbackbytes1/memorybudget1; automaticDrain timestamps alterwork. This is
NOTcleanFPS or screenshot proof. NoGPUzones/full-buffer dumping enabled.

Owned-onlyPSS wrapperroot71546 ACTIVE waitsframe600(max240s) then10s/lowrate
sequential Thread_Gpu/GpuQueueSubmit snapshots; outerownedhelper270s bound,
bothstreams drained/outputignoredcycle33/pss-cp-recorder/owned-52968-1141.
No rawstack/guestmemory/otherprocess contexts, exactPID/hash/start/TIDlifetimes.
SelfTest39/39 actualspin/cycles/cleanupPASS. Helperstdoutpresentation fixed
only([pscustomobject]dictionary); capture/validation codeunchanged.
Verify/drain BOTH game andhelper beforeediting/building/GPUtests. Diagnostic
sampling mayfinish first, gamekeeperstillbounded650/30s/300warm. Outcomepending.
UserWSLNOTneeded/keep stopping,no menu/input/push/issues,oldartifactsprotected.

## Current checkpoint: defaults restored; dedicated CP/recorder diagnosis next

0fe5f40f preserves original default readlocality; exactpages1 andBDA batch1 remain
explicit experiments after wholeworld3.49/3.22 showedno usefulgain. Existing
positive paths/nativeguard tests retained, new4GPU-read registeredPASS1.82s.
Source39sample PSSSelfTest validated, ownedCP/recorder workload capture pending.

New diagnostic-onlyreadback reporter+memorypool snapshot compile/nativebindless
build113434 PASS, defaultOFF. ExistingEOP113809/5E5411C1 withdrain+byteflag PASS
(doesn't callfullReport, not formatting proof). Prior113705wrongCLI used1
instead of a case selector and failedargumentguard; corrected, not a regression.
Reporter prints existingnonzero(reason,op) packed-byte rows beforetop48time
cutoff; eligibilityunproved. Budget snapshot separates retiredallocbytes from
VMAallocationBytes/blockBytes/usage/budget, gcTick frompresentedFrame. Actual
runtime diagnosticoutput still pending. No extraGPUsubmit/Finish/timestamps
added bythose reporters; DrainStats itself createsqueries/affects scheduling.
Next nativecore/privateinstall and dedicated10s CP/recorder PSS +phase/byte/
pool attribution. Nevercompare diagnosticFPS againstclean title samples.
UserWSLNOTneeded/continue stopping; no menu/input/push/issues,oldartifactskept.

## Current checkpoint: exact-read world no gain; default restored; CP profile next

4326 world111028-7e7f26 full30s650..754/27titles mean3.488889,min3.1/max3.8,
exit0/no guards11:13:00UTC; against priorimagebudget3.671429 -4.97% singlepair,
no usefulgain/images. ExactGPU pages are now experimental optin
KYTY_GPU_READ_EXACT_PAGES1; default0 keeps old locality. SAME native7F7A49AF
threeoptin checks PASS1.39s; default0control112037 all6semantic/native/fence/
guard/cleanup controls PASS with originalwide65540/44/48 then intentionally
fails optin costoracle. MainvalidRED fixture/oracle unchanged.

Actual explicit dirtyimage/pending-only aliases113045/BC337D50 PASS: realnative
clear/realpendingstaging+actualbufferwriter, all A/far/imageordering/native/
guards/realcompletion/cleanup, oldwide65540/65544 retained. Initial112340 all
semanticcontrols passed but test omitted60B leading64-alignment gap after real
4B image staging; corrected from StreamBuffer::Map allocation contract (not
production change/not a RED). Windows far_words rename fixed beforevalidcase.
Safeexperimentalpath retained for later matched sameEXE0/1 diagnostics.

PSS owned CP/recorder helper SelfTest113047:39/39 validnative-loop samples,
actual cycles/user/kernel deltas and allcleanup PASS, native5.0068s; newgame
profile pending. Only Thread_Gpu/GpuQueueSubmit onexactownedPID, dedicated5-10s
diagnostic, nevercleanFPS. Forkaudit foundexistingSRT/TLS/memo/spec certificates
alreadyhere; candidatepurebindingprep requires actual CP/PSS/phasecostwitness.

## Current checkpoint:4326d348 explicit GPU reads world retry active, 2026-10-13

Completed shared explicit/touchedpage correction localcommit
4326d348e4974e60c20e6f45565e8367aafe1554; nativecore110626/install111026 PASS.
Privatecycle32/runtime-4326d348-exact-gpu-read EXE
75482D665EE47DF9DC7FBFFEBC5F75F5EC3DF76871105A69109334B04542B630.
`game-20261013-111028-perf-cycle32-exact-gpu-read-world-title-30s-7e7f26`
PID55612/root24865 ACTIVE/sourceFROZEN, same neutralfullrender chain1/HALFoff/
scale100/scratch1024/BDA0,650gated30s300warmbound; actualFPS result pending.
Onlycomplete sample+10%against3.882143 permits post-sample captures. WSLnot
running beforelaunch; latestuser confirmedNOTneeded/keep stopping, no askagain.
Sourcecleanexceptdocs/preservedprefetchWIP. No menu/buttons/newpush/issues.
Additional explicitdirty/pendingimage alias test pending; existingguard keeps
oldpath and18affectedtests passed. Frameahead CLI is dormant; don'toffer it as
speedup. Larger Suspend credits/CP-prefix forkport require separate proof, not
a dependency timeout bypass. Next diagnostics may measure actual pool and named
readback reasons/credit wait costs separately with flagsOFF in cleanFPS runs.

## Current checkpoint: explicit GPU read pages native GREEN; core/world next

6be6 plus shared explicit-read origin WIP: genuineRED105225/585CD167 all6case
controls PASS before small requestcopies65540 instead4. SAME unchanged
GREEN105628/1B2D690F explicit4/8/12/8 bytes, farGPUdirty/unarmed/CPUold kept;
actualGPU-thread fault and is_write both retain65540/65600reserve. Foreign
rawCPUfault105226baseline ->105629GREEN stayswide. Allrealproducer/current
generation/sibling/armed-old beforeSubmit/nativefence/priority/guards/cleanup
unchanged PASS. Eighteen affectedWAIT/EOP/foreign/2B/BC/mip CTests PASS7.52s.
Crossowner110341/40843F55 PASS realtwo-owner8B discovery/newnative merge and
wholefar/canaries/backing;new3registeredCTests PASS1.30s. Dirty/pendingimage
aliases retain old path via existingpage-wide queries; directalias-specific
neighbor still a pending additionalcase. No explicit-image capability claim.

Initial104857 compilefailed dueWindows far macro and nonexistenttestgate.Release;
both corrected before VALID105225, not calledRED. Production narrows only
trackedcompleteowner/read-only/Explicit GPU requests, keeps centralHostFault
originFault plusforeign/is_write/invalid/discovery and realpublication waits.
Separate fixcommit/core/privateinstall/world30s next; no gameFPS claimyet.
Lastclean imagebudget/defaultBDA0 world104422-3ca394 completed30s653..761/
28titles mean3.671429,min3.1/max3.9,exit0/no guards10:46:49UTC,no usefulgain/images.
WSL needed? latestuserNOTneeded,continue stopping; no reapproval needed.

## Current checkpoint:6be6dde0 image budget world retry active, 2026-10-13

Nativecore104257/install104420 PASS, privatecycle31/runtime-6be6dde0-image-budget
EXE7C174573186B6E07B310E2A6CE0D83B4D1D6F7E9476DBBD5E138D97165B2F640.
`game-20261013-104422-perf-cycle31-image-byte-budget-world-title-30s-3ca394`
PID53256/root51412 ACTIVE/sourceFROZEN; neutralfullrender chain1/HALFoff/scale100/
scratch1024/BDA batching explicitly0;650gated30s300boundwarm, result pending.
Capture only complete+10%mean gain vs3.882143, excludedfromsample.
Latestuser clarified WSL isNOT needed: continue stopping it. Boundedshutdown0
afterprevious18GiB restart, physicalfree33.67GiB/virtual54.27GiB; verify whether
it autorestarts before interpreting host isolation. No other app stopped.
Next GPU explicitReadMemory six-case +foreignfault fixtures savedignored
cycle30/gpu-read-memory, NOTcompiled/RED/policyapplied. CentralFaultorigin must
staywide; typed Explicit/Fault plus dirty/pendingimage aliases before narrowing.
No menu/input/newpush/issueposts; preserve historical prefetchWIP and allartifacts.

## Current checkpoint:b5dd974c byte-limit core; mixed native proof complete

Separate memoryfix b5dd974ca19df56269a7a89def75e47501cc6c7c, nativecore103425
PASS. Real mixed allocation neighbor103850/FE975240 PASS: native sizes4096/
16384/65536, large insertion evicts BOTH older small entries, retained65536/
count1; allimagebits/guards/completion/live heldowner/cleanup PASS. Registered
two memory CTests PASS1.06s. Test-onlymixed follow-up/CMake registration pending
commit; no new memory policy edit. Actual memory-pool world benefit untested.

WSL restarted again (PID55344/18.01GiB); optional user question asks whether
another current task needs it. Continue native small tests/research; no more
WSL shutdown while clarifying conflicting actualusage. Game retry needs enough
RAM/cleanhost conditions; no claims of60/paging/VRAM savings from these tests.
Source clean except follow-up test/docs and preserved prefetchWIP (verify).

## Current checkpoint: retired-image byte-limit native GREEN; game pending

ad5f5bbf plus byte-limit WIP: unchanged realRED102201/DA41B93E retained81920B/
count5 vs cap32768 ->GREEN102624/25DDEDFE retained32768/count2. All real image
bits/guards/native ticks/live sixth/cleanup unchanged PASS. Neighbors102625
sameEXE oversized/reuse/formatMismatch/cap0/count24/invalid+overflow/envcleanup
PASS. Five affected2B/BC/mip CTests PASS2.36s. Mixed-size/multiple-victim native
control pending, then commit/core/private install and neutralworld30s.
Byte limit clamps existing count24 by min(explicit/default1GiB,planningbudget/8),
evicts only already-retired allocations, preserves live owners/fences/reuse.
Retainedallocationbytes do not equal physicalVRAM blocks/FPS; actualruntime
pool occupancy and usefulgame speed remain unproved.
Default BDA batching staysOFF; supportedoptin1 sixnativechecksPASS2.52s;
off-control retainsoldpairs/allsemanticcontrols before intentionallyfailed
optin costoracle. Core/default-off world nottested yet. WSLautorestarted4.3GiB
aftershutdown; don'tclaimall recenthostloadisolated. Preserve historical WIP.

## Current checkpoint:BDA experiment opt-in; retired-image byte-limit genuine RED

b351 worldrepeat101111-191cfb complete30s650..745/27titles mean3.222222,
min2.6/max3.6,exit0/no guards10:14:02UTC. No usefulgain/images. WSL restarted
again (4.30GiB sampled), so label wsl-off describes launch intent only; no claim
of isolated background load or measured causal regression. Useful60 unresolved.

Keep BDA batching experimental: KYTY_BDA_UPLOAD_BATCH1 explicitly enables it;
default0 retains original per-owner pairs. SameEXEDA41B93E six opt-in native
checks PASS2.52s; disabled-control102315 allnativebytes/guards/repeat/generation/
ring/temp lifetimes PASS with originalpre2post2, then intentionallyfails the
opt-in costoracle. The regression fixture/oracle was unchanged; only config
differs. Future sameEXE0/1 world comparisons avoid rebuilding/rolling back.

Retired-image bound nativeRED102201/DA41B93E: sixreal64x64 image data/canaries/
realcompletion/live sixth/cleanup PASS. Each actualallocation16384B; requested
idlecap32768B; count-only pool retains81920B/5. Finalcostoracle alonefails.
Diagnosticobserver/fixture only, no byte-policy change yet. Production cap and
native neighbors next; allocationbytes are not physicalVRAM or FPS proof.

## Current checkpoint:BDA world retry after WSL memory guard, 2026-10-13

b3513063 native proof/core/install remains verified. Firstworld100608-9c4740
closed10:06:56UTC/0 with systemMemoryGuard=true/free7.96GiB beforeframe650;
last486/title1.7 is NOT an in-world30s FPS comparison. WSL had restarted and
vmmemWSL used10.79GiB. Authorized native wsl--shutdown completed0; freephysical
30.98GiB/virtual49.69GiB afterward, no other application process stopped.

Repeat sameEXE47C3F888/sourceb351/privatecycle30/runtime-b3513063-wsl-off
`game-20261013-101111-perf-cycle30-bda-batch-wsl-off-world-title-30s-191cfb`
PID48248/root24792 ACTIVE/sourceFROZEN. Same neutralfullrender chain1/HALFoff/
scale100/scratch1024/650gated30s/300bound warm, outcomepending. Capture ONLY
complete sample+10%gain vs3.882143. Verify/drainbeforebuild/tests.
Next retired-image budget fixture E8F555F6 and productionCEF34F37 savedignored
cycle26/retired-image-budget; nativeREDNOTRUN/policyNOTAPPLIED. Early CLI
initialization draft was caught/fixed before compile, not a regression proof.

## Current checkpoint:b3513063 BDA batch world retry active, 2026-10-13

Completed shared immediate-copy BDA batch localcommit b351306302e67b8cdaa5be40fbfe245adba90bee.
Native core100416/install100605 PASS; private cycle29/runtime-b3513063-bda-batch
EXE47C3F88826E4429DF92EA079426CB2709956EE0019E29EF7F90E217287B418A5.
`game-20261013-100608-perf-cycle29-bda-batch-world-title-30s-9c4740` PID53436,
root64822 ACTIVE/sourceFROZEN. Neutralfullrender chain1/HALFoff/scale100/default
scratch1024,650gated30s afterbounded300warm. FPS result pending. Capture only
after complete sample and>=10% gain against3.882143, excludedfromsample.
Verify/drainownedrun beforebuild/tests. No menu/input/newpush/issueposts.

## Current checkpoint:BDA batch complete native proof; core/world next, 2026-10-13

Untouched compute neighbor100259/477FBA47 PASS: real previous chained shader
writesC; publicPrepareBda uploadsA/B only; old CPU C/GPU ownership retained;
following nativeSSBO reads newA/C without extra prebarrier, all3owners/guards/
publication/cleanup PASS. Three new overlap/deferred/untouched registered CTests
PASS1.29s. Combined with unchanged3profileRED->GREEN, 13affected, freshchain0/1
and Suspend proof above, shared immediate-copy batch fix is ready for separate
commit/nativecore/privateinstall/world30s. Actual FPS benefit remains pending.
Default scratch1024 restored; no menu/input/newpush/posts. No nativeprocess now.

## Current checkpoint:BDA batch native GREEN; scratch trial no useful gain

14a8bac1 plus immediate-copy batch WIP: genuineRED094147/2C44EBA6 ->unchanged
GREEN095142/44/1506A129 for Selective/Legacy/Checked. Normal/temp barriers4->2;
actual ring-wrap stays4/tick+1; each copy2/32768B and native8192words/guards/
repeat/new generation/cleanup PASS.13affectedCTests PASS6.03s; fresh chain0/1+
new3BDA registered PASS2.24s; Suspend095357/3EC8F33A PASS. Overlap/disjoint same
owner/publicSSBO consumer095621/F13E8C49 keepall requiredpairs+first/final/backing
PASS; actual deferred-recorder095622 sameEXE nativewords/ring/temp lifetimes PASS
(counts in that case are recording hooks, not driver-cost proof).
Untouched preceding compute-chain neighbor and nativecore/world next; no game
FPS or other-game compatibility claimed from native batch tests.

Scratch128 full30s652..760/28titles mean3.628571,min2.8/max4.0,exit0/no guards
completed09:46:38UTC. Against candidate3.469231 this is +4.59% singlepair, still
below historical reverse3.640741/prior3.882143: NO useful verified gain/images.
Keep original1024 benchmark setting. No active native process now (verify).
Retired image-pool bytes/VMA budget is another concrete source lead (24 count
limit lacksbytecap); actualpool occupancy/paging and correctness proof pending.

## Current checkpoint: scratch128 trial active; BDA barrier cost genuine RED

Historical reverse b8 run093530-74b8cf complete30s650..754/27titles mean3.640741,
min3.4/max3.8,exit0/no guards09:38:05UTC. Candidate2B mean3.469231 is -4.71%
vs reverse control; no useful world gain, drift/repeatability still limited.
Neither whole-game2B gain nor a fully isolated regression is established.

Current14a8bac1 exact6F607CDE EXE, new private cycle28/runtime-14a8bac1-scratch128,
`game-20261013-094338-perf-cycle28-scratch128-world-title-30s-a271ff` PID38236,
root80039 ACTIVE. Only existing KYTY_SCRATCH_IDLE_MB128 (default1024) changed;
same neutral fullrender chain1/HALFoff/650gated30s/300bound warmup, pending.
Native source FROZEN. No menu/buttons/push/posts; capture only fullsample+10%
gain over3.882143. Whole-device pre-sample reverse observation15106/16311MiB,
P0/50C/2880MHz/43% GPU is memory-pressure lead, not emulator-only/paging proof.

New independent PrepareBda native RED094147/2C44EBA6 on14a8bac1 test-onlyWIP:
normal4MB staging, actual24KB ring wrap/tick+1, actual8KB temporary sources ALL
initial/native8192words/guards/dirty/clean/repeated/new CPU generation/cleanup
PASS; copies2/regions2/32768bytes each. Actualpre2/post2; normal/temp expected1/1
cost fails last, wrap must retain2/2. Shipping behavior unchanged. Immediate-copy
batch proposal and independent review saved cycle26/buffer-copy-review; exact
production patch/GREEN/neighbor profiles/runtime benefit still pending.

## Current checkpoint:2B world3.47; verified historical reverse control active

14a8bac1/EXE6F607CDE full neutral chain1 run093125-141ea6 completed09:34:06UTC,
30s653..753/26titles mean3.469231,min3.2/max3.7,exit0/no guards. This is -8.01%
vs immediate preceding b8 mean3.771429 and -10.636% vs older3.882143. Operation
gain -72.8% did NOT translate to useful world FPS. No images captured.

Reverse control `game-20261013-093530-perf-cycle27-reverse-b8-world-title-30s-74b8cf`
PID16108/root41942 ACTIVE. Separate private clone of completed candidate runtime
retains its saves/caches, only original previously verified b8 executable restored
with exact EF33708B SHA. Diagnostic helper accepts historical binary ONLY when
its saved complete/exit0/no-guards world run revision/hash match; current14a8bac1
source is preserved/frozen. No checkout/source reversal/rebuild of old code.
Same neutral full-render chain1/HALFoff/scale100/650gated30s; pending. Artifact
and invocation profile `_Build/analysis/yotei-performance-cycle27-20261013`.
Do not interpret a candidate-regression claim until reverse control completes.

## Current checkpoint:14a8bac1 direct2B world retry active, 2026-10-13

Native core092953/install093110 PASS; private cycle26/runtime-14a8bac1-2byte
EXE6F607CDEB335CF7401D1B9686A17E31222BF3D0E6454FC44151AA9EEA6065420.
Neutral chain1/fullrender/HALFoff/scale100 run
`game-20261013-093125-perf-cycle26-direct-2byte-world-title-30s-141ea6` PID49988,
root23184 active, source inputs FROZEN. Bounded warmup300 then650gated30s title
sample; result pending. Capture helper permits two images only AFTER a full
sample and >=10% mean gain against recorded3.882143; capture excluded from FPS.
Verify current process/artifacts before continuation. No menu/buttons/push/posts.

## Current checkpoint: direct2B completed native proof; core/world next, 2026-10-13

Shared2B direct upload unchanged RED091807 ->GREEN092043, copies1->0 with exact
native data/guards/source/cleanup. Actual1080 operation paired092513/41553354
median101184->27520ns (-72.80%), NOT game FPS. New neighbors092919/DAE6CB3D
R8G8UNorm and R16Sfloat127x65: all16510bytes including final halfword, interior
source4, native subsequent writer, source/output guards and cleanup PASS;
copies0. Missing actual formatless feature bit simulated by one forwarded
native format query092920 sameEXE: legacycopies1, exact bits/guards/cleanup PASS.
New3CTests PASS1.21s; old4 affected cases PASS1.73s. Independent reviewer found
no blocker. Capability-specific native2B device with absent bit is unavailable;
simulated query tests fallback, not a second physical GPU.

Completed fix will be committed separately, native core/new private install and
neutral chain1 window-title30s compared against b8d1ceb3 mean3.771429 and prior
repeat3.882143. No active native process now (verify). Stable60/full effects
remain unresolved; no screenshots until useful clean title FPS gain.

## Current checkpoint: direct2B native GREEN and1080 operation -72.8%; world pending

On b8d1ceb3 plus shared2B upload WIP, real public UploadImage RED091807/FCAD4B4D
exact bits/guards/source/cleanup PASS before intermediatecopies1 -> unchanged
GREEN092043/FC1BB5F1 copies0 with all controls unchanged. Existing4/8B odd
pitch/interior source/next GPU writer, BC owner and mip-copy4CTests PASS1.73s.
Native shader uses shared render-target swizzle and integer half extraction;
compatible R16Uint Unknown/writeonly view requires actual Vulkan1.3 format
StorageWriteWithoutFormat bit. Unsupported metadata/feature retains old route.

Actual SAME EXE41553354/1920x1080 native paired GPU092513: seven alternately
ordered warmed pairs, same device-local source/target, fresh zero clear before
each measured upload, all1036800words and output/source/guest guards plus
mapping cleanup PASS. Legacy median101184ns, fused27520ns (-72.80%). One GPU
upload operation only: NOT whole-game FPS or attribution of all diagnostic6.6ms.
Artifacts in `_Build/analysis/yotei-performance-cycle1-20261012/checks/20261013-*`.
Two-byte odd/R16half/capability fallback still pending, then commit/core/private
install and neutral chain1 title30s. No active native process now (verify).
Other actual games untested; independent synthetic shared-path coverage only.

## Current checkpoint: empty tail world3.77; direct2B upload next, 2026-10-13

Local b8d1ceb392afa10922b0811ea211d7c3891d5bc3 native core074806/install074908
PASS; private cycle25/runtime-b8d1ceb3-empty-tail EXEEF33708B439BD3F7DA99A5091A1291AC5DF7830A016BC90D24E360400E1D6FFC.
Neutral full-render chain1/HALFoff/scale100 world run
`game-20261013-074923-perf-cycle25-empty-tail-world-title-30s-50ae28` complete
30s653..761/28titles mean3.771429,min3.5/max4.0,exit0/no guards. Against previous
chain1 repeat3.882143 this is -2.85% single-pair: NO meaningful gain, no images.
Actual native submit reduction remains proven; useful game throughput unproved.
No active native process after cleanup (verify before next build).

Next: reproduce direct2B tiled-upload intermediate copy cost, then narrow shared
R16Uint view/shader capability-gated correction and timing. Three-agent cycle
delivered independent proposals/review; later child retries hit model capacity,
root continues saved proposals sequentially. No game menu/input/new push/posts.

## Current checkpoint: redundant tail native GREEN; world comparison pending, 2026-10-13

eeebdd96 plus shared optional CP flush correction: genuine native RED072755
225D7037 submits2/tick2->4 -> unchanged GREEN074403 F8A1C852 submits1/tick2->3.
Required transfer/fill/callback/eager/ordered literal negatives and actual
automatic Begin timestamp reset/query/retirement all pass before/after. Eight
affected scheduler/PM4/async WAIT/foreign/EOP CTests pass4.22s. Explicit flush,
wait, finish, flip and SuspendPoint remain unconditional. Native core/install
and clean chain1 neutral world30s against recorded3.882143 remain pending.
Native deferred WAIT neighbor and actual GpuSuspendPoint074742/F2648188 PASS;
three new registered CTests PASS1.22s. Core build and new private install next.

Interrupted diagnostic `game-20261013-065805-diag-hot-fc6f8c56eb7e168f-dfda43`
exited07:00:20UTC/0 before full30s: NO FPS comparison. Late actual GPU zones
attribute buffer-copy37.647ms/frame, tiler21.56 (two hot sites13.387/6.641),
FC6 compute9.208. Diagnostics can serialize/mark otherwise empty tails;
their~381 native submits/frame do not prove removable clean-run submissions.
Actual viewport/RT evidence: last30 top-five records lead with1920x1080;
shadow/downsample targets differ, top-five telemetry is incomplete. Previous
internal1080 uncertainty is narrowed, not an all-pass1080 claim.

Other-game research: Jetsku U59 pinned f9e19582f0deac0d1226a7ce3faae30a9ab408ab
supports direct2-byte image upload; our shared fused path currently rejects it.
Actual Yotei late hot2B tiled1920x1080 upload is a concrete next regression and
timing candidate. Its Astro results are not Yotei validation. SharpEmu PR926 is
a comparison source, not a backend merge. Timestamp accuracy/complete BDA
capture remain separate candidates, no FPS promise. WSL stopped; no menu/input,
no new push/issue comments. Preserve all historical artifacts/prefetch WIP.

## Current checkpoint: repeated chain1 +27%, two world frames reviewed, 2026-10-13

Same184fdb2b/EXE982918F5 repeated full neutral chain1 run
`game-20261013-063738-perf-cycle23-chain1-confirm-and-capture-82ec95` completed
06:40:12UTC/exit0/no guards. Full30s651..761/28title values mean3.882143,
min3.7/max4.1 versus matched chain0 mean3.0625 (+26.76% single control).
Prior chain1 repeat3.8 (+24.08%) agrees directionally. FPS comes from window
titles in world, not screenshots. Useful observed gain; stable60 still absent.

After complete sample, two1936x1119 window captures (1920x1080 client) at766/771
were reviewed: character/instrument/nearby buildings show no obvious stretching
or disappearance; visible grass, fire and sparks. The second title2.0 reflects
post-sample capture/focus perturbation and is excluded from the benchmark.
Evidence: `confirmed-gain-world-1.png`, `confirmed-gain-world-2.png` in that run.
Two nearby frames are NOT full motion stability/wind/water/volumetric/reflection/
shadow correctness or actual internal1080 proof. No new issue milestone post.

Use explicit KYTY_COMPUTE_CHAIN1 for subsequent performance candidates; retain
chain0 as the recorded control. No new global environment/core default change.
HALF_UNIFORM stays off (cold driver compile prevented world sample); raw-wave32
EXEC_HI oracle remains pending, no production SAVEEXEC patch.
Next strongest research: bounded native redundant tail-submit reproduction with
copy/fill/callback/timestamp/eager negatives before any conditional flush; actual
GPU hot-work capture/complete inputs; generic video-output contract (no static
always4K bug established, Astro resolution patch does not apply to Yotei).
No native process active now (verify); source clean except docs/preexisting
prefetch WIP. WSL stopped/no menus/input/new push/posts; all artifacts preserved.

## Current checkpoint: chain1 actual world +24%, repeat/image pending, 2026-10-13

SAME184fdb2b/EXE982918F5 full neutral chain0 mean3.0625 vs chain1 candidate
`game-20261013-063118-perf-cycle22-chain1-world-title-30s-a384e1` complete30s
650..762/29title values mean3.8, min3.5/max3.9,exit0/no guards. Single-pair
gain24.08%, a useful observed candidate, NOT stable60 or full rendered proof.
No images from the timed run. Existing native ordinary-buffer chain boundaries
pass under0/1; BDA/image alias coverage remains partial.

SAME runtime/source/shader policy repeat+post-sample capture
`game-20261013-063738-perf-cycle23-chain1-confirm-and-capture-82ec95` active
PID50700/root78455,started06:37:38UTC. Helper captures two frames ONLY after
complete30s and >=10% mean gain against3.0625, then normal owned cleanup; capture
does not enter measured interval. SOURCEFROZEN, verify actual run/process on resume.
No default flag change yet. Native rendering/effects/internal1080/image stability
still pending. Three next researches: generic output resolution contract;
actual hot GPU work/capture with complete inputs; submission flush boundaries
with required publication/order retained. No menu/buttons/push/posts, WSL stopped.

## Current checkpoint: image/eager world3.06, existing chain trial active, 2026-10-13

Local184fdb2b core062315/install062510 PASS, EXE
982918F5613F7387033B0E29767A8904812139CDC19D829D1F3E169F6BC66A1A.
New registered7CTests PASS2.72s. Full neutral chain0/HALFoff world
`game-20261013-062604-perf-cycle21-image-scopes-eager-world-title-30s-455f07`
complete30s650..739/24title values mean3.0625, min2.9/max3.2,exit0/no guards.
Previous clean37b3.038462 differs by0.79% single-pair: NO useful FPS gain or image
proof. Shared native correctness/cost fixes retained; actual60 unresolved.

SAME EXE, new private clone, only existing KYTY_COMPUTE_CHAIN1 candidate
`game-20261013-063118-perf-cycle22-chain1-world-title-30s-a384e1` started06:31:18UTC
PID29508/root66029. Full neutral/original shader policy/no diagnostics or input,
650gated30s after bounded warmup. SOURCEFROZEN; verify process/run before resume.
Ordinary-buffer native boundaries proven0/1, but BDA/image alias and rendered
game effect still pending. No default policy change, no images until useful
title FPS gain. WSL stopped; no new push/issue comments. Preserve prefetch WIP.

## Current checkpoint: image scopes and eager page batches native GREEN, 2026-10-13

Image planner genuine RED055808/055809(042570B7) ->same GREEN060021/060022
(8A1E3DE6), original image-transition PASS. Partial mip/layer GREEN060548
(93E2E211); actual upload-to-sampler literal stage/access-subset RED061003
(AE6734D3) ->same GREEN061349(B7593F32); partial neighbor repeated062102 PASS.
Shared GetBarriers retains published read scopes, emits needed new-stage
dependencies and preserves per-subresource heterogeneity. Conservative stage
bit subsets only, no aggregate expansion. Planner proof is NOT rendered pixels.

Real eager pages genuine native RED061744(70239F69) ->same GREEN062003
(204BA457): adjacent SAMEowner source/host barriers/copy commands/tokens2->1,
gap and different owner remain2. Exact12bytes/3sparse regions/native/CPU/sibling/
unselected dirty gap/canaries/cleanup unchanged. Fifteen affected CTests PASS6.12s,
including actual WAIT/async/foreign/EOP/fused upload/BC/mip GPU cases. Larger
ring-pressure/adjacent-run game incidence/FPS remain unproved.

Existing compute-chain configuration native061004/061005(AE6734D3) PASS under0/1:
real ordinary-buffer finite producer/consumer/disjoint work plus guest ACQUIRE,
CS_PARTIAL_FLUSH and explicit Break boundaries. BDA/image alias and game images
are not proved by that fixture. Historical chain0 began as a diagnostic; no
evidence it was required by the final artifact correction. Default benchmark0
retained until separate in-world candidate comparison.

HALF0/1 supported21native fixtures053422/053424(A6530691) PASS, actual structured
wave64 SPIRV1655->1645words proves option active. HALF1 game053539-558a0b never
reached650/30s sample; last161, driver pipeline403953...compile51.5s, deadline300
then forced cleanup/exit-1 at05:40:57. GPUidle/CPUcompilation observed, no reset
or proved GPU hang. FPS unknown; keep option off. Old raw-wave32 high mask oracle
remains unresolved/unchanged; full-suite pass not claimed.

Next: commit completed fixes separately, native core/new private install, ONE
clean neutral30s world comparison with original shader policy; then existing
chain candidate if safe/needed. No process active now (verify). Source WIP is
image+eager+test/registration records; preserve historical prefetch. WSL stopped,
no menus/input/images/new push/posts; actual1080/full effects/stable60 unresolved.

## Current checkpoint: wave32 oracle unresolved; no saveexec production patch, 2026-10-13

Unchanged isolated32 fixture native052707/BFB52561 reproduces earlier failure;
native052708 Wave64 fixture passes, partial32 fails. First mismatch is raw
EXEC_HI snapshot (A5 expected,0 actual), not SAVEEXEC destination corruption.
Translate.cpp initializes wave32 live_lanes[1]=0 and masks every raw EXEC_HI
write by it BEFORE SAVEEXEC. Primary RDNA2 states upper EXEC/VCC ignored in
wave32; its SOP1 low32/SCC contract alone does not specify arbitrary raw high
storage. A writable A5 high-half oracle is unproved; no production change or
golden relaxation applied. Old full-suite32 failure remains explicit test debt.
Three agents now separate supported-wave64 half-uniform correctness/config/cache
proof from that unresolved mode. New scoped selector pending; full-suite pass
must not be claimed from it. Native ISA32 hardware/stronger specification needed.

Measured wait attribution is stronger than earlier CPU guess: all dictionary
offsets give152/224 PSS samples (67.857%) in four decoded RDTSC GPU/job polling
loops;40/224 (17.857%) in real driver CPU GPU-sync wait. Wall-context occupancy,
not CPU-cycle share or FPS proof. No clock/readiness/guest address patch.
Latest clean FPS remains3.038462; actual1080/full effects/stable60 unresolved.
Native process idle (verify); test-only isolated selectors WIP, historical
prefetch preserved. No menus/input/images/push/posts, WSL stopped.

## Current checkpoint: Main waits on GPU; native shader baseline defect found, 2026-10-13

Diagnostic-only local3abecd72 core050211/install050306 PASS, private EXE
CCA19F98FD628014311E78068F7D26BADF0EE77D3819939D59A3A3BB338D92A4.
Full neutral diag18 run `game-20261013-050928-diag-cycle18-owned-pdh-pss-main-30s-dedb3d`
exit0/no guards,30s title3.034615 (instrumented, NOT clean speedup proof).
Owned helpers completed: `_Build/analysis/yotei-performance-cycle18-20261013/owned-24252-20261013-050943-0568634`.
PSS Main31652:224/224 valid,30.010s,all marker/snapshot cleanup0,max capture2.589ms.
Own synthetic RX-spin proof4BF helper:16/16 known-loop RIPs/real exit counter/cleanup.
PSS captures scheduling-sensitive contexts; separate diagnostic, no VA clone/dump.
GPU PDH589rows/31sets/30valid intervals/no errors:3Dmean59.8827/max63.2432%,
Copy2mean0.5967%; sample span33.845s,frames569..669. Engine utilization is NOT SM
occupancy; don't sum overlapping engines. Exact owned PID/path/start/hash verified.

Offline SELF hash5178 and bounded disassembly prove four hot guest regions are
RDTSC deadline spins polling GPU labels/queue sequence.119/224 samples hit two
exact spin RIPs;40/224 native samples are win32u!NtGdiDdDDIWaitForSynchronizationObjectFromCpu.
Thus historical83–92% Main CPU mostly represents GPU/job waiting, NOT proven CPU
work bottleneck. Do not shorten time artificially, fabricate readiness or skip
synchronization. Uncensored Main fault BODY diagnostic17, five fully contained
intervals/76frames:36067faults/36066valid/1straddle/no query errors;
fault cycles/Main cycles1.820997%, wall66.949ms/frame. Prefix VEH/IsMapped and
post-timer logging/history are excluded. Details in ignored cycle17/main-fault-cpu
and cycle18/hot-main-offline. GPU shader/transfer/synchronization alternatives next.

Fresh native shader target051628 PASS, EXE53230EFE8751A919FA9B05CC8C23E2F46E73C17E198B8824BD686F44A0447C24.
Existing HALF_UNIFORM1 strided/scalar-branch GPU controls051904/051905 PASS.
Full wave64 suite051903 fails actual ScalarOrn2SaveexecB32Wave32 readback;
same HALF_UNIFORM0 baseline051933 fails SAME case. This is an existing baseline
defect, NOT a proved half-uniform regression or optimization. No game with this
option started. Isolate unchanged fixture/verify AMD ISA first; shared fix needs
same native GREEN before further performance claims. No active native process
now (verify); historical prefetch WIP preserved. No menus/input/images/push/posts,
WSL stopped, actual1080/full effects/stable60 remain unresolved.

## Current checkpoint: exact-page Poll world3.04, attribution next, 2026-10-13

37b94dbe core044859/install044949 PASS; private EXE
E8AF53C8B6B0DCA27D28C8EB336613BAAA7C645FDB5D47C00F58D1710E9512D5.
Full neutral run `game-20261013-044959-perf-cycle16-poll-pages-world-title-30s-35af5a`
closed04:52:59UTC/exit0/no guards. Complete30s651..743/26title values:
mean3.038462, min2.8/max3.2 vs dca3.028 (+0.35% single-pair): NO useful gain.
Native redundant-copy proof remains valid, but the world bottleneck is not
resolved by this correction. No images inspected. Actual1080/full effects/60 pending.

Change next iteration to bounded bottleneck attribution: owned-PID Windows GPU
Engine counters (read-only, English paths available); actual Main instruction
pointer sampling through PSS if independently validated and accessible without
admin, in a separate explicitly perturbed diagnostic run; uncensored Main fault
CPU totals. Windows ETW alternative prepared but current token lacks profiling
privilege/admin, so no trace/elevation/UAC started. Do not apply speculative empty
mutex notification or per-zone GPU rewrites without evidence. Existing per-zone
BottomOfPipe timestamps may perturb GPU overlap; old200ms interval is a lead,
not an uninstrumented shader bottleneck proof. Three agents preparing helpers.
No active native process now (verify); source clean except preserved historical
prefetch WIP. No menus/input/new pushes/posts; WSL stopped; routine handoffs authorized.

## Current checkpoint: exact-page Poll native GREEN, world pending, 2026-10-13

Independent scalar/cross-page genuine RED044730/E66C9D6D after test setup fixes:
all native/same-page dirty siblings/backing/publication/CPU far read/canary/cleanup
controls pass, but redundant far page arm and192byte download exceeds128budget.
Same unchanged GREEN044825/04C7E627 after only shared PollReadMemory change:
all touched-page dirty spans published,128bytes, far remains dirty/unarmed/old.
Ordinary CPU512KiB locality route, owner discovery/barriers/tokens/max tick and
ring retirement are unchanged. Nine affected CTests PASS3.63s. Retained actual
notification token native044617 PASS after actual GuestGpu destruction; no
sanitizer or performance claim. Core/new private install/30s world next.
Last actual world dca5722b mean3.028 recovered regression, no net gain; stable60,
actual1080/full effects remain pending. No images/menu/input/push/posts, WSL stopped.

## Current checkpoint: world regression recovered, no net FPS gain, 2026-10-13

dca5722b core043609/install043708 PASS. Private Performance runtime EXE
0E1378A5940BF3E051161CEE1E7CDD46B710F6E411196F04BEBB2F5E76129782.
Full-render neutral world run `game-20261013-043719-perf-cycle15-wake-requery-world-title-30s-e71467`
closed04:40:19UTC/exit0/no guards. Complete30s frames650..738/25title values:
mean3.028, min2.9/max3.2. Recovering from Poll2.358333 is28.4% single-pair,
but old TLS baseline3.033333 is effectively unchanged; NOT useful net gain.
No images inspected. Native fixes are retained; next proof is exact-page Poll
download, preserving all dirty bytes of touched tracker pages and ordinary
CPU locality batching. Independent native far-page/same-page/cross-page RED
pending. Fully armed callback neighbor integration/native test pending.
WSL stopped/no menus/input/push/posts; actual1080/full effects/stable60 pending.

## Current checkpoint: foreign requery and publication wake native GREEN, 2026-10-13

Foreign CPU callback correction local e55aded4: intended cost RED042544/6A15CF7E
with all numeric/fence/canary/cleanup controls passing; same GREEN043231/E4AF6BA0
removes redundant ring64->128 allocation and leaves unrelated B dirty/unarmed.
Real new A writer still waits and publishes abcdef09. Fully armed neighbor pending.

Real queue continuation RED043400/107885A5: actual publication/native5145a7c3,
graphics11/compute22/independent33, both blocked, guards/cleanup/zero waits pass,
but CV times out despite publication. Same GREEN043454/4AE64E5A after retained
notification token/generation: signalled CV/readyTimeouts0. The bounded 1ms test
stimulus and CV18.1us are NOT a natural game performance measurement. Debug queue
backing snapshot avoids protected fault under queue mutex; lifetime detached after
worker join. Six affected CTests pass2.46s; foreign and real async recording with
wake043534/043535 also pass. Core/private install/30s world recovery pending.

Prior actual world regression2.358333 vs3.033333 remains unaccepted. Recover or
disable Poll candidate; no images until useful title FPS gain. WSL stopped,
no menus/input/new pushes/issue comments. Preserve historical prefetch WIP.
Actual1080/full effects/stable60 remain unresolved.

## Current checkpoint: Poll correct but world regresses2.36FPS, 2026-10-13

7d93 core040627/install040914 PASS; EXE95B5ECD05DB613ECD57FA4816A0287C73B870F80A8C00FAD67D7607B584B3A85.
Full neutral/no-diagnostics world run `game-20261013-040924-perf-cycle14-poll-world-title-30s-488282`
closed04:13:22UTC/exit0/no guards. Complete30s651..720/24titles:mean2.358333,
min2.2/max2.4 versus TLSa980mean3.033333. This is a22.25% single-pair regression,
NOT an accepted optimization. No images inspected. Restore/disable the candidate
if the next measured correction does not recover prior throughput.

Read-only OS sample30.030124s/frames552..624/zero query errors: Main47016
83.769% one core, process~2.705cores; hostGPU31.275%, DrawSpec22.268%, submit18.991%.
Separate CPU window precedes650FPS and does not establish a latency cause.
Native async-recording041940/A69D259C PASS: actual record-thread1 routed callback
consumed, original native scalar/11/22/canaries/publication/two-pending/0wait
controls unchanged. Earlier tests pinned record-thread0; this closes that
ordinary async route proof, not all ring-pressure cases.

Three next alternatives: bounded publication-to-queue latency proof and safe
wake if justified; foreign CPU ReadMemoryAsync callback authoritative requery
before widening512KiB window; targeted image/ring fallback characterization.
Wake proposal must fix debug_queues raw protected dereference under queue lock:
it can wait priority while publication notifier needs that lock. Existing100
wait is microseconds/Windows alternative1ms, not100ms. No wake behavior change
or stale-ready shortcut applied yet. Async test-only WIP twofiles; no native
process active now. Preserve historical prefetch WIP. No new push/issue posts,
WSL stopped, no menus/buttons. Actual1080/full effects/stable60 unresolved.

## Current checkpoint: GPU WAIT poll native GREEN, world pending, 2026-10-13

Three-file shared PollReadMemory/PM4 suspension correction WIP. Actual existing
PM4 producer fixture owner setup fixed BEFORE genuine baseline034342/203C9D2E:
native word/11/22 markers/guards/publication/cleanup PASS ->costRED1/0/1 dispatcher
FullDrain/Tick/Priority waits. SAME unchangedGREEN034629/BD73E808 allPASS, both
pending initially, budgets0/0/0. Real GuestGpu graphicsWAIT32/computeWAIT64 plus
independent compute035123/153EF80B PASS bothblocked/third-before-publication and
all native values/guards/cleanup, no waits. No wake-token framework: existing
100-unit wait is microseconds (Windows alternative~1ms), not100ms.

Native Poll040047/80BCB8D0 PASS old-arm/new-GPU-writer, CPUoverwrite/no-submit,
cross-page mixed-arm8-byte data, host false/true fallback, guards/cleanup/0waits.
Existing typed/span035232, resumed eight EOP literal035606, BC4/5/7+mipcopy040212/
213 PASS. EOP driver now retries legitimate Blocked execution until real
publication; byte/fence/false-mask/ownership/zero literal download oracles stay.
Image-only protection keeps legacy fault route and ring-pressure retirement may
still wait; those scoped limitations remain pending. [Details](yotei-wait-reg-mem-readback-2026-10-13.md).

TLSa980 world retry closed03:30:44UTC: complete30s651..739,24titles,mean3.033333/
min3/max3.1/exit0/no guards: no useful gain, no images. Best priorworld3.045833.
Native1080 upload comparison committed separately1b83e3f6; operation~81–82%
improvement does not imply game FPS. Poll commit/core/new private install/
uninstrumented30s world retry next; no native process active, verify on resume.
Source WIP is Poll/core test observer/fixtures/EOP driver/records; preserve
prefetch WIP. No push/issue posts/menu/input, WSL stopped, actual1080/full effects/
stable60 remain unresolved. Three agents propose, root serializes native work.
## Current checkpoint: TLS correction native retry active, 2026-10-13

Local a980b942 core032554/install032732 PASS. NEW private cycle13 actualPerformance
runtime EXE4BF4B7040D2B8C9BEB62ABFECACE435E0C5DA91113A7EA53AAE3B2B605F9289B.
Neutral/full-render/no diagnostics/no menus or input retry started03:27:44UTC,
PID13560, run `game-20261013-032744-perf-cycle13-tls-flags-world-title-30s-3698c0`,
300s warming bound followed by650-gated30s title sample. Root session49292;
check actual process/run before continuing, source inputs frozen while active.
Arithmetic FLAGS/GP/RSP/branch regression already RED ->GREEN896cases; gameFPS
and images remain pending. Prior uninstrumentedmean3.045833; no60 claim.

Nonblocking PM4 WAIT native cost proposal B64D2921 staged cycle12/wait-reg-mem,
CLI `--wait-reg-mem-nonblocking-cost-gpu`: existing finite real compute producer,
protected32-bit guest word, two actual PM4 processors, native timeline gate with
1s failsafe, all actual values/masks/suffix/guards/publication/cleanup before
GPU-dispatcher WaitTimer0 cost. No new Poll API in baseline; uncompiled/noRED
yet. Actual GuestGpu all-queues round-robin neighbor and production lifetime/
page/epoch proof pending. Do not edit/apply/build it alongside current game.

Tracked WIP is records and test-only1080fixture/hooks; preserve historical
prefetch WIP. No issue posts/push; WSL stopped. Source/diagnostic correctness,
native128/1080GPU operation gains and world FPS remain distinct evidence.

## Current checkpoint: TLS FLAGS shared defect proved/fixed, 2026-10-13

Standalone native CPU actual SafeCall/TlsRegStub/Call9 regression032105/F0868860
passes all896nativeMOV/callback/GP/RSP controls, then intendedRED882arithmetic
FLAGS/conditional-branch failures. LEA replaces stackSUB/ADD in BOTH shared stubs;
entryNOP padding retains callback/relative-call/output offsets and32/0x90byte
capacities. SAME test unchanged032207/69026323 passes all896 and actualJZ/JC/JS/
JO neighbors; CTest1/1 PASS0.03s. Native core/install/game result pending; no FPS
claim from this semantic proof. [Contract and limitations](yotei-tls-flags-regression-2026-10-13.md).

1080 upload SAME-EXE setup repaired with real bounded guest mapping, preserving
native device-local source/target. Build031121/native031229/CB311C9D PASS all36
full1920x1080image trials and complete native+guest source/guard checks.4B legacy/
fused median154400/27616ns (~82.11% operation reduction);8B304864/57984ns (~80.98%).
Device-source preparation is outside both GPU intervals. This is throughput
proof for that operation, NOT game improvement; world stillce9mean3.045833.

Next: commit TLS fix separately, native core/new private install, ONE neutral
uninstrumented650-gated30s world retry. Nonblocking WAIT required native cost
fixture staged ignored-only; no PollReadback production fix applied. Extra SIMD/
unwind/red-zone/RSP/high-register ABI forms remain explicit debt. No native task
currently active; verify on resume. Current WIP includes only new1080tests/hooks,
records and TLS correction; preserve prefetch WIP. No images/menus/buttons/push/
issue posts; WSL stopped, actual1080/full effects/stable60 remain pending.

## Current checkpoint: AGC bodies cheap; GPU WAIT readback target, 2026-10-13

Default-off diagnostic1ea27e21 core024704/install024837 PASS. Private EXE
C63A7BA6BCD641937A8BB9E4BC94C746E8C0FF45C57DD3EF63543700EFD2346F;
run `game-20261013-024852-perf-cycle12-main-body-fault-cost-30s-fe66ca` completed
02:51:51UTC/exit0/no guards, full650-gated30s instrumented window. FPS is not
an optimization result. Owned collector30.02662s/frames602..692 confirms physical
Main48032/internal3 at91.013% core, zero query errors. Five contained nominal
130..155s reports: six AGC bodies gross sampled estimate1.132% of Main cycles;
empty controls1048–1071cycles/sample versus bodies1009–1493. All query/censor/
underflow errors0. No subtraction or MHz/time conversion: bodies are near the
measurement floor, not the important optimization target.

Fresh drain5s/15frames: WAIT_REG_MEM GPU-thread read-fault1559full drains2071.64ms
(~138ms/frame),277tick waits815.74ms; all readbacks274.5MiB/5s. GPUbusy/gap use
real device timestamp queries (roughly200/130ms per frame), NOT host wait timers
or GPU-unit utilization; do not add overlapping waits. Earlier17.95ms totalGPU
assumption is discarded. Full-drain itself does not hold TrackingSpinLock:
QueryReadback releases region lock before waiting; Main fault-site timer includes
later region-lock acquisition but omits IsMapped/early VEH work.

Next verified lead: nonblocking required readback for PM4 WAIT_REG_MEM so pending
publication suspends one guest queue and other queues progress. Synthetic native
RED/all-queues completion/real-value/page/epoch safety pending in test debt; NO
poll behavior fix applied. Three agents investigate independent mechanisms.

Uninstrumented best world remains ce9 mean3.045833 (~3% single-pair change,
no useful gain), no images/actual1080/full effects/60 proof. A new SAME-EXE1080
upload benchmark build025910 compiled, attempt030032 exits321 on INVALID FIXTURE
guest image range (host allocation mistaken for guest address). This is setup
failure, NOT regression or performance proof; repair with actual bounded guest
mapping, preserve image validation. Native process currently closed; source WIP
is test-only1080 fixture/hooks plus these records. Preserve prefetch WIP. WSL
stopped, no menus/buttons/push/issue posts.

## Current checkpoint: fused world3.05FPS; Main92% core, 2026-10-13

Correction committed locally ce9ead1a; core022559/install022747 PASS. New private
actualPerformance runtime EXE51D56207EC96E07A7F7A23B22A1594FDBBC3DBD2EECA96FA63DCDFFF0A8DC1F2.
Neutral/no counters/full-render world run `game-20261013-022757-perf-cycle11-fused-upload-world-title-30s-2b1025`
completed650-gated30s at02:30:23UTC:24values, mean3.045833,min3.0/max3.1,
frames650..741; exit0/no guards, maxWS16.42GiB. About+3.1% versus baseline2.953571
in one pair is not useful game improvement. Images remain uninspected. The
19–24% native128x128 operation improvement remains separately proved.

Owned read-only OS CPU sample SUCCESS at frames553..646, actual30.021694s,
no counter/Suspend/input and zero query errors. Unique Thread_Main/OSTID16160
used27703CPUms =92.277% of one core; process89375CPUms~2.98cores. Thread_Gpu
host CPU38.309%, DrawSpeculator27.430%, GpuQueueSubmit21.025%; these are CPU
threads, NOT GPU-engine utilization. CPU window precedes650-title window; do not
claim exactly matched timings or attribute Main cycles from call frequency.
Artifact cycle3 `cpu-threads-validated-37960-20261013T0229519928929Z.json`.

Next separate bounded diagnostic: sampled inclusive AGC-body CPU cycles/wall with
empty-probe overhead control, plus explicit Main-only fault-site wall scope.
No fake clocks, skipped GPU work, shader-hash hacks or speculative spin fix.
Three agents/root serialized native work; no active game/build now, verify again.
Source tracked clean before this docs update; preserve historical prefetch WIP.
No push/issue posts/menus/buttons; WSL remains stopped. Actual1080/full effects/
stable60 unresolved; current world~3FPS.

## Current checkpoint: fused upload GREEN and19–24% native gain, 2026-10-13

Shared production fusion WIP at4fbf4ea9+saved patch. Native build020946 PASS;
SAME cost fixture021044/F5602E25 GREEN after015650/2AB4DBB4 intendedRED: BOTH
4/8-byte full native bits/guards/raw views/cleanup remain exact; copy1 ->0.
GPU A/B/A015658/021054/archived021055:4B21952/16576/21760ns,8B32800/26560/
32928ns median. Bracketing operation time reduction24.1581%/19.1821%; all54
real fresh uploads pass. This is128x128 operation proof, not game FPS or4K proof.
New shader sources archived beside native-a-b-a-comparison.json in cycle9.

Fresh shader-compute build021201 and shared native tiler021320 PASS358cases/
264format-mode pairs. BC4/5/7 cache aliases and mip-copy neighbors021408/409 PASS.
Odd127x65/pitch128/interior sourceoffset4 controls021857 PASS. Final022337 build
and CTest3/3 PASS1.36s cover actual later native source overwrite: image retains
original bits, source changes, guards exact. New-route unsupported capability/
partial/stream/deferred controls remain pending; original unsupported paths have
shared tiler/cache coverage. Native core/install/uninstrumented30s world retry
pending. No native process currently active.
Details: [fused upload](yotei-fused-tiled-upload-2026-10-13.md).

Main-only closed diagnostic extractor validates exactscope/internal3/OSTID25668:
five NOMINAL counter periods145..170s record1,228,568calls; six graphics builders
62.04% of frequency. No selected clock/sleep traces or counted kernel rows there.
Coverage is incomplete and frequency is not CPU time; missed CPU sampler is not
invented. Agents prepare bounded sampled HLE duration and Main fault-site scope.
Three-agent alternatives/root serialized work/fullrender/neutral/no menus or
buttons/WSLstopped remain. World~3FPS; actual1080/full effects/stable60 unresolved.

## Current checkpoint: Main-only diagnostic completed, 2026-10-13

Local native instrumentation4fbf4ea9 core015819/install015903 PASS. Private
EXE76A4C80891AA20118E3271643B5AE3F8BE55F6121B16905F93B836C75F941EDD,
cloned actualPerformance profile/warm cache; no guest behavior optimization.
Neutral full-render run `game-20261013-015914-perf-cycle10-main-hle-attribution-world-30s-ca4726`
PID40916 closed02:02:12UTC/exit0/no guards after complete650-gated30s diagnostic.
Counters explicitly tag scope=main; exact identity internal3/WindowsTID25668.
Top Main calls are AGC packet builders; time APIs do not dominate frequency.
Frequency/callsites do not prove CPU duration. Read-only OS CPU sampler missed
its window, so NO new CPU occupancy numbers may be claimed. Corrected ignored
extractor/timed attribution proposal pending. Instrumented FPS is not a gain.

Completed native upload A015658:4-byte median21952ns/min21280/max22880;8-byte
32800ns/min32416/max33280; both exact full native bits/guards/liveupload PASS.
Production fusion remains unapplied pending narrow shared patch/native unchanged
costGREEN and GPU A/B/A/unsupported-neighbor controls. Actual world remains
~3FPS; native query option's2.1% is not useful. Tests/instrumentation commits
eade141b/4fbf4ea9 local only; no push/issue posts/images. No active native process
at this checkpoint; check again on resume. WSL stopped, source work preserved.

## Current checkpoint: query option no useful gain; upload RED; Main diagnostic, 2026-10-13

Actual Performance world baseline2.953571 versus existing real occlusion option
3.016 (+2.1% single pair): no useful gain, no images, default unchanged. Both
complete650-gated30s title samples/exit0/no guards. Native8x8 same-fixture
Flag0costRED ->Flag1GREEN retains required visible pixels. CTest new native
occlusion1/1 PASS0.42s after test-only build015611. No game currently running.

Actual public tiled upload cost fixture015650/EXE2AB4DBB4 passes BOTH128x128
4/8-byte full native-bit/input-output-canary/legal raw view/cleanup controls,
then intendedRED: one avoidable copyBufferToImage versus budget0. No production
fusion applied. Native timing A015658 passes18 genuine fresh dirty uploads,
two warm/seven measured per format;8-byte median32800ns/min32416/max33280.
4-byte result remains in the same stdout. This is a small route screen, not
world/4K throughput proof. Test-only fixture observes v1/core2/KHR2 without
dropping work. Production proposal and native A/B/A/neighbor proof pending.

Main CPU attribution instrumentation now WIP/defaultoff: explicit actual Main
TLS role, existing counters filtered before bucket update, one internalID/OSTID
identity line even in Silent mode. It changes no guest scheduler/clock/render
behavior. Native linked build015611 PASS, core/private install and separate
30s diagnostic pending. Count frequency/callsite is not CPU duration; instrumented
FPS will not count as a performance gain. Three agents remain read-only/ignored
proposal workers; root serializes native execution. WSL stays stopped.

## Current checkpoint: actual Performance2.95FPS; real occlusion trial, 2026-10-13

Owned-process read-only proof at01:47:12UTC confirmed saved requested/effective2,
system default2, override-1: actual Performance accepted. Same088 EXE completed
650-gated30s world title sample01:48:19..49UTC:28 values, mean2.953571,
min2.8/max3.0, frames650..738, exit0/no guards, min available28.37GiB. No images
or achieved60 claim. This combines newer source/settings and is not an isolated
backend speedup comparison with historical329Quality mean2.6704.

Existing opt-in actual occlusion route tested before game: test-only build014915
PASS; EXECA27C318EF7F3A0AA39BFFAC2C5C744113C40801FD608C336A3AF4EBFB6C9FA2.
Flag0 intended costRED014945 AFTER real depth/raster/counter/PM4/native work
controls: hiddenIA3/visible3/mixed3. SAME unchanged fixture Flag1GREEN014952:
hiddenIA0/visible3/mixed3, identical required visible pixels/canaries, ready
publication, conservative unready/no-wait and logical-unmap suppression. Layer
environment015018 exit0/stderr empty; active validation-layer proof pending.
Existing CPU dump/predication neighbors015019 PASS; older CPU EXE A23D7856,
unchanged affected mechanisms. Actual unmap/lag/capacity native neighbors pending.

Actual option run started01:50:42UTC/PID28720, same088 EXE/privatePerformance/cache,
`game-20261013-015042-perf-cycle9-occlusion1-title-30s-world-1080-fc774f`.
Loaded193838368byte cache; at300frames actual query counters report120dumps/
60segments/33empty/no flip waits.
Closed01:53:42UTC: complete30s650-gated mean3.016 (25values,min2.9/max3.1,
frames650..736), exit0/no guards. Only+2.1% versus sameEXE actualPerformance
baseline2.953571; no useful gain established, option stays experimental/defaultoff.
Details: [native occlusion screen](yotei-native-occlusion-screen-2026-10-13.md).
No new production change, menus/buttons/images/push/issue posts. Three agents
continue native upload cost/timing and Main CPU attribution; serialized root
execution. Full effects/actual1920x1080/stable60 remain unresolved.

## Current checkpoint: saved Quality overrides system Performance, 2026-10-13

Native core012153 and private install012304 PASS at0889459a; installed EXE
SHA5D681682E646363494B54B6D4B5514509F717209B71C5711F352B18E3625FCEC.
CLI invalid preset rejects with exit1; Performance/help exit0. Neutral/full-render
run `_Build/runs/game-20261013-012430-perf-cycle9-system-performance-title-30s-world-1080-4f55b4`
accepted184549517 bytes of intact old driver cache; saved193704388 bytes. New
shader compilation still took36–59s for five heavy kernels. At300s warming bound,
exit0/frame410,650-frame gate not reached: NO completed30s worldFPS measurement.
Physical RAM stayed above29.66GiB; no resource guard or driver reset.

Read-only owned-process36-byte proof found system default2Performance but saved
renderer requested/effective0Quality, override-1. The HLE preference works;
saved game choice wins. Actual source deserializer/enum/checksum were then proved
on an immutable PRIVATE runtime save: graphics dictionary entry38 reaches its
typed terminal enum; mode at430864, exact checksum over supported0x150 content.
Offline self-test accepts original/output and rejects six corrupt neighbors.
Only eight preference bytes and checksum span change; all other bytes/zero tail
and585728-byte length match. Original user data and older runtime remain intact.

NEW ignored runtime `runtime-0889459a-private-performance` under cycle9 holds
the proved Performance copy (SHAFD06CA575FDF288BA5299AC69E1263735FC3CCBCB12B15AF4FF42445A7C061EC),
same EXE and warm driver cache. Bounded neutral game retry started01:45:47UTC,
PID16352, run `game-20261013-014547-perf-cycle9-private-performance-title-30s-world-1080-eaf650`.
Actual effective mode2, full30s titleFPS and rendered dimensions remain pending.
No menus/buttons/images/push/issue publication; WSL stopped. Latest complete
worldFPS remains2.6704. Three parallel read-only proposals cover fused tiled
uploads, actual occlusion/predication, and saved-mode verification. Native work
stays serialized; no production edit while this game is active.

## Current checkpoint: system Performance preset exported correctly, 2026-10-13

New general CLI `--game-preset-priority Default|Performance|Resolution` sets
shared user preference0/1/2; default0 preserves previous behavior. Real UserService
export -sD02mFDBh4 now writes configured priority only when negotiated capacity
covers offset12+4. Known40byte/version header and caller48tail remain; no guessed
new48schema, game title/address branch, menu or save edits. Actual source eboot
consumer/name/target tables independently establish priority1 -> mode2Performance
target60, priority2 ->Quality30 on current non-Trinity path. A saved game choice
can override this default, so actual runtime mode/size/FPS remain to be checked.

CPU intendedRED011830/284F617C -> SAME GREEN011910/45F0E092: null/error/user and
40/48/16/8/0-capacity/field/canary controls first. Dynamic users0/1001 neighbor
012045/1ACA857F PASS; focused CTest2/2 PASS0.06s. Source fix pending core/private
install/nativeCLI validation and a neutral30s title run with Performance1080window/
scale100/full rendering. No interpretation of target60 as achieved60.

Opaque-cache correction4b6b0326 is completed with CPU+realGPU proof; next native
game should retain intact old182MB driver data despite source edits. Journals
still require exact current source. Shader modulo proxy showed no GPU gain;
ArrayLength proposal remains unapplied. Latest complete worldFPS2.6704 (329).
No push/issue publication, WSLstopped. Actual1080/full effects/stable60 pending.

## Current checkpoint: compatible driver cache survives compiler edits, 2026-10-13

Shared opaque-cache policy completed locally after exact existing factory
extraction: CPU intendedRED005245/B1EABF29 -> SAME GREEN005655/51C3C357, all existing
strict journal/version/repair tests pass. Opaque KytyPC4 wrapper omits compiler
source; intact oldKytyPC3 host/config/diagnostic tail and actual139bytepayload
offset are accepted with length/checksum/native32bytevendor/device/UUID validation.
Journal remains exact current compiler/source/schema/application key. No cache
header edits, key forcing, changed shader semantics or missing-integrity bypass.

Actual shared LoadDriverCacheShard native010845/21E43738 restored15132 real
vkGetPipelineCacheData bytes from a private legacy wrapper, native outputs and
canaries/full/asymmetricEXEC PASS, valid actual driver feedbackcacheHit1. Wrong
host/checksum/rehashednativeUUID yield emptycache. CPUjournal/nativeCTest2/2
PASS0.53s. Native bindless build010720 and final010943 PASS. Core/private install
and original182MB cache in-game validation pending; this improves iteration
policy rather than claiming higher FPS. Last completeworldFPS remains2.6704.

New legitimate user setting candidate: actual eboot GetGamePresets caller reads
priorityoffset12 after requesting48bytes; name table proves internal2=Performance,
internal0=Quality. Current KernelIsTrinityMode(false) branch maps priority1->2,
priority2->0; default0 currentlyalwaysreturned. Shared config/API CPU regression
pending; no title/address branch and no guessed enum. Keep supported40byte ABI
negotiation/caller48tail intact. Outputstatus already1080; clock100MHz correct.
Source-only/static consumption does not prove performance or rendered dimensions.
No menus/buttons/images/push/issuepost; user30s titleFPS/fullrender/WSLstopped
constraints and stableactual1080/60/full effect goals remain.

## Current checkpoint: GPU timing screens zero-gain changes, 2026-10-13

Channel-modulo sourceba3ed1b1 core002906/install003108 PASS; privateEXE
97EEB17B27F31156184DBB22A89D7912E69E026DC5868C118BC7540979AA9BB3.
Bounded native GPU A/B/A with32 live XYZW reads,512Binput, waves32/64 and channels
1/3/4: original-modulo003515/915786, no-modulo003614/668312, archived original003705/
915786. Same six batch counts37,39,41,27,28,28; all input/output/tail goldens PASS.
Seven GPU timestamp samples/case; medians~29.7us/wave32 and58.49us/wave64;
candidate changes-0.66%..+0.44% relative to bracketing baseline. No useful proxy
GPU gain despite a passing source opcode cost. Worldba3 FPS pending; avoid another
blind cold600s attempt. Production modulo was restored exactly after baseline.
The one-slot/uniform/single-group proxy is not the twelve-slot fc6f workload.

Opaque cache policy has CPU intendedRED005245/B1EABF29 -> sameGREEN005655/51C3C357
with full old journal/host/diagnostic/size/checksum/native-header controls.
Source-strict journal unchanged; host-compatible opaque driver blob survives
compiler changes and reads intact legacy139byte wrappers without key pinning or
cache-byte editing. Actual native loader compiled005811; real Vulkan loader/output
regression pending. Keep this policy WIP until native proof. No game currently
running at this checkpoint; check processes before resume.

Independent ArrayLength cost intendedRED005054/C80DCEE4: all six live/validator/
native-bounds controls pass;1/8/32loads query2/16/64(w32),4/32/128(w64). Production
length hoist remains unapplied pending a GPU timing comparison and descriptor
range controls. Main CPU attribution pending; all-thread counters are diagnostic.

Read-only preset research: actualgame imports/calls GetGamePresets and reads its
priority offset12; caller requests48bytes, current stub40bytes returnsdefault0.
Numeric Performance mapping and actual mode override remain unproved. Output1080
is already advertised under actual attribute3 0x480044; no confirmed display bug.
Do not change GPU clock (actual~100MHz) or guess enum values. Fullrender/neutral,
no menu/buttons/images, WSLstopped,30s titleFPS goal unchanged. Latest full world
mean2.6704; useful speedup/actual1080/full effects/60FPS still pending. No push.

## Current checkpoint: channel division removed; no useful FPS gain yet, 2026-10-13

Shared generic format channel selection replaces source0..3 modulo positive count
with bounded masks and the exact count3 repeat. Metadata validity, conversions,
stride/index/offset/bounds, EXEC and format keys unchanged. Native intendedRED
001021/ECA63286 -> SAME GREEN002637/598C217D after52 literal native cases covering
three-channel/both waves/EXEC/end/OOB/prefix. Four general entry divisions become0.
Independent100requests/4800lanes match3b exactly (0 changes;144 known UNorm errors
both). Category/codegen neighbors PASS; focused CTests2/2 PASS0.93s. Completed
source fix pending native core/private install and world FPS validation.

Latest completed native world run uses329492cf ready-readback+3b converter:
core002053/install002145 PASS, privateEXE
1308AC8757CBC80712D22179C372D06F4E7524087A13002EE15513F3C38B2D47.
Game002157-bdc248 loaded182MB cache/1921records without invalidation. Full30s
neutral title sample fromframe650 mean2.6704/min2.5/max2.8, exit0/no guards.
No useful improvement vs previous completebc age0 control2.6148. Do not assign
this small difference independently to readback or converter without a matched
3b-only full sample. No images/menu/buttons, all render work remains enabled.

Next loop also tests shorter native GPU measurement before another whole-game
cold compilation and audits replay identity. Main-only HLE diagnostic proposal
uninstalled; existing all-thread counts can guide an optional separate run.
Actual1080/60FPS/full effect correctness remain pending; no push/issuecomment.

## Current checkpoint: ready buffer reads avoid unrelated downloads, 2026-10-13

ReadMemory shared GPU-thread route queries a valid fully mapped range inside one
live existing owner before widening. CPU-clean or already armed requested bytes
avoid copying/arming unrelated dirty pages. New/unarmed/mixed requests keep the
original download; true native tick AND priority completion waits and is_write
CPU/BDA markers remain. Null/invalid/unmapped/unowned/partial spans keep discovery.
Intended native RED001020/ECA63286 -> same GREEN001450/9941F873; final001641/
EBB5F04C preserves four exact producer/CPU/native cases and removes ring growth.
Four typed neighbors002004/C8A3296E PASS; CTests2/2 PASS0.81s. Existing EOP/fault/
BC-partial/mip-owner cases001451..001453 PASS. Next native build/private install,
then full-render neutral30second titleFPS with preserved3b shader cache.

Generic-kind source3b016bfc core235714/install235757 PASS; nativeEXE
C9F1458F1C9B82A08BEC51C94DD32DDC533EB9ADA9D6C18BFFF3C53A968C721C.
Two bounded300s attempts game235805-653813 and000322-aea57f exited0 with no guard;
first cache-key refresh required cold pipelines/frame294, warm retry loaded147MB
and finishedframe649, cache182240066 bytes. Neither reached a full30s frame650
sample; FPS result remains unverified. No more identical warming retries; the
next memory fix preserves shader identity/cache. Do not claim loading13FPS asgain.

Next shader channel candidate has validRED001021/ECA63286 after52 exact native
literal cases including three-channel, both waves, EXEC/bounds/prefix guards.
Four general modulo/division operations per entry resource; production pending.
ArrayLength repetition proposals and native same-buffer/different-range controls
are ignored/uncompiled. Loop subgroup-vote removal rejected:47 distinct real
loop predicates, no same-block repeated-vote reproduction. No push/issuepost.
Latest complete normal control remainsbc93628e age0 mean2.6148FPS;60/full effects/
actual1080 remain pending. See cycle6 for artifact references and boundary debt.

## Current checkpoint: generic format dispatch validated, 2026-10-13

Shared runtime formatted-buffer converter selects one mutually exclusive category
instead of six successive checks. Arithmetic/format metadata/keys, bounds, EXEC,
D16 and format specialization policy unchanged. Native intended RED235420/D38503A9
-> same GREEN235504/0F796FBD, decision budget6 ->1 (Float2). Native baseline235421
and candidate235505 each100requests/4800lanes are byte-identical;144 pre-existing
UNorm precision errors retained. Static neighbor235544 has0 golden errors,
codegen neighbor235507 PASS, focused CTests2/2 PASS0.79s. Peer review found no
contract change. Native core/private install and30-second game FPS pending.

Recording-age sourcebc93628e native core234318/install234404 PASS; installed
EXE80E746E146459D8D58E09189A0B42D5580718BFFA3DC70865EC45F25B558ABD4.
Neutral matched runs with same executable/full rendering/title30s fromframe650:
age1000 game234413-fa4053 mean2.7393/min2.6/max2.9; age0 game234809-2c5488
mean2.6148/min2.6/max2.7. Both exit0, no guards, no menu/buttons/images. Difference
4.76% in one pair is small; no useful sustained gain. Keep default age0 for the
next comparison. Stable60/actual1080/full effects remain pending.

Three-agent immutable audit: saved main journal492CS permutations/472hashes.
95.967ms/frame of146.43ms compute (=65.5%) is excluded by current pure-fill proof;
four complex ordinary-buffer kernels54.070ms include hottestfc6f35.137ms. Exact
remaining ordinary-fill hits unknown because runtimeSGPR/groups are absent.
Valid native ordinary-fill RED235325/A23D7856 retained for future work; production
port deferred to prioritize these measured hot kernels. Transfer queue remains
disabled pending native read/write dependency proof. No push/issue publication.

## Current checkpoint: recording-age policy validated, 2026-10-13

Opt-in shared scheduler budget `KYTY_RECORDING_AGE_US=1000` submits an aged recording
with actual draw/dispatch work through the existing ordered route. Default0 keeps
the prior policy. No new transfer queue, dropped guest work or cache-key change.
Native intended RED233238/408E8B32 -> unchanged GREEN233751/86C5F063;
final234008/F083CE9D preserves all64 initial+2 words and surrounding sentinels.
Final five focused CTests PASS2.00s onD228C047: budget, direct/async/disabled
threshold and empty-recording cases, native producer/indirect ordering with no
argument CPU publication/download-ring reservation. Active scheduler/WAIT/resume/
command-stream and registered EOP publication/IRQ/generation controls pass.
Two old indirect-count/unregistered-EOP failures also reproduce on archived
prepatch binaries; see test debt. Do not claim all affected tests pass.

Next native core build/private install, then one neutral30-second in-game title
FPS sample. Latest measured normal control remains2.472FPS, reserved core2.600;
no runtime improvement claimed yet. No menus/buttons/screenshots before gain.
Three agents continue proposing separate candidates; root owns serial execution.
Ordinary native buffer fill is the next smaller alternative. Transfer readback
requires actual-writer and reverse-copy dependency proof before any game enable.
Existing unhooked ComputePrefetchPermutationTests fixture preserved. No push or
issue update; actual1080/60FPS/full effects remain pending.

## New measured direction and upstream options, 2026-10-13

Read [new optimization options](yotei-optimization-options-2026-10-13.md) first.
Latest native source fix `ff1a70e5` publishes OS thread names with Tracy disabled:
CPU intended RED214049/58794710 -> unchanged GREEN214122/836A6F68; native build
214146/install214256 PASS, EXE SHA256
`BBBFE2070899F7436FCC84018F3EF1FF31E081900DEA17F6E7674CB78443758C`.

Fresh default control214304-cd4b8a:30-second world-title mean2.472 FPS/exit0.
Existing physical GPU-core reservation215150-1d8f79:30-second mean2.600,
min2.4/max2.7/exit0. Both full rendering, neutral input, no menu or images.
The5.2% single-run difference is small; no useful sustained improvement claimed.
Valid20-second named sample in the reserved run:86 threads,47 active,
Thread_Main87.462% of one core, Thread_Gpu34.875%, total2.707 logical cores.
Thread_Main includes guest execution, HLE and memory-fault work; CPU attribution
within it remains pending. Earlier unnamed88% must not be called Thread_Gpu.

Static-format profile2 independently passed all4800 lanes/100 requests; generic0
retains144 known UNorm8-one errors. These144 differences resolve the goldens,
other4656 lanes agree. Final static matrix223120 and generic codegen neighbor
223122 on5D01A13D PASS. No production shader-emitter change for these controls.
Both private profile2 game attempts reached their300s compile/warm bounds before
frame650:220310-20df2d forced cleanup/exit-1,221906-b32d7e exit0/frame429.
New variants compiled/saved, but no complete30-second world benchmark. Never
claim startup/menu15FPS as a gameplay gain. No more identical warming retries
without a new decision; default profile remains the measured comparison basis.

Important next candidate has a real native RED223050/5D01A13D: separate graphics
family0/transfer family1, AGE diagnostics disabled. Conservative warm fallback and
all native/backing controls pass; completed writer A is rejected by QueueReadback
while unrelated writer B remains open. Opt-in fixture is saved, not a completed
production correction. Before enabling this queue in game, cover binding-prep
flush, native owner migration, image-copy writers and later generations.

Other concrete new proposals: upstream recording-age submission to overlap CPU/GPU,
proven ordinary-buffer DMA fills/copies, and late EXEC-select optimization after
resource-plan detachment. Three-agent research saved exact upstream revisions and
baseline-compatible fixtures. Native FP16 and normal workgroup LDS already exist;
they are not missing toggles to port. Source-only findings and shader audits do
not establish60FPS, actual1080 or complete effects. No push/issue update.

## Current checkpoint: three proposals, 30-second title FPS tests, 2026-10-13

First proposal completed locally as `8c8d01319900427f3b61ab7d34e8c8649f27edd7`.
LiteralEopPublicationCost intended RED211508/BA99ECCF -> unchanged eight-case
GREEN211809/DD73A957; final GREEN212334/4BA4B219 and three span neighbors212333
PASS. Existing native EOP/IRQ/generation neighbors and fresh PM4 wait/resume/
command-stream neighbors PASS. The GPU store and native completion remain
required; known bytes alone avoid their redundant download. Tiny mixed split
spans can use more staging, so this is not a universal bandwidth improvement.

Native core build212431 and private install212539 PASS, EXE SHA256
`81F89A92F99A6636B2A860073EB11F45F9FED3A5699911C75A45A86D303F0638`.
Game212552-5ee908 sampled30 seconds from21:29:13UTC/frame650, mean window-title
FPS2.257 (23 logged values, min2.1/max2.4). Auto-closed21:29:46UTC/exit0 with no
guards; maxWS17.83GiB/minphysical26.37/mincommit24.69. Full rendering, neutral
input, no menus, no diagnostic GPU zones. No screenshots or image inspection.
Compared with earlier2.435 first baseline interval and later~2.55 warm titles,
this is no measured gain, with a possible slowdown requiring a fresh matched
control before attributing it to this correction. Passing tests is not a speedup.

Active native branch `yotei-integration-native`, local source fix `8c8d0131`; published
head remains `a73b9592`. PM4 clean-packet reads (`7823f07f`) and selective generic
format conversions (`faa5d3ac`) have recorded RED/GREEN evidence. Neither has
demonstrated a useful world FPS improvement. Native faa5 build/install passed;
installed EXE SHA256 `0F3F14B064DBA30E341CBE79335A20808D4732581649E4F7DC79D98C65FB5511`.

Latest explicit user constraints: three agents propose separate changes; root
implements/tests them sequentially. Measure FPS in the window title directly in
the game for **30 seconds** per variant. Do not navigate menus. Inspect images
only after FPS increases. Loading/cache warm-up precedes the measured interval;
keep it bounded separately. WSL was explicitly stopped; no native task remains
running at this checkpoint. Preserve complete rendering and existing guards.

Earlier same faa5 executable, title-only/full rendering, neutral input: direct recording
2.532 FPS, direct submission 2.542 FPS, speculation disabled 2.592 FPS (means of
logged title values over the first 30-second frame>=650 interval). Normal title
baseline first interval 2.435 includes a 1.1 FPS sample; its later warm interval
was about 2.55. These small differences do not establish a useful speedup.
All runs exited0, without memory guards; no screenshots were taken for these
variants. Exact records: performance-cycle2 `title-fps-config-comparison.json`
and `_Build/runs/game-20261012-{202909,203850,204403,204938}-*`.

The two remaining proposals are repeated wave64 subgroup votes and spatial
resource-cache invalidation. Each requires a production-path regression before fixing.
Unknown GPU bytes, native fences, writer generations and unsupported-case errors
must remain intact. Stable60 FPS, actual1080 rendering and full effect correctness
remain pending. No new push or issue publication is authorized by this cycle.

Next: map the88%-of-one-core unnamed hot thread using the saved native-name
diagnostic. Larger existing buffer-specialization mode2 requires independent
cross-profile numeric/coherence controls and a private cache before a title FPS
experiment. Avoid treating tiny static-work reductions as runtime progress.
See [cycle2 evidence](yotei-performance-cycle2-2026-10-13.md).

## GPU generic conversion validated; next world comparison, 2026-10-12

Shared generic formatted BUFFER helper now evaluates only the selected runtime
format's conversion. Intended cost RED195324/E74AEA7B -> unchanged9-path
GREEN195549/0ADF5A1B; codegen-size neighbor PASS. Native baseline195325/candidate
195550 each100requests/4800lanes are byte-identical (0changed/missing/extra), same
raw output SHAe167dc0e...; measurementexit2/144knownUNorm1ULP errors preserved.
No full numeric-suite PASS, precision fix, cross-game gameplay or FPS claim.

Next build/install this correction separately, refresh source key naturally and
retry full rendering with neutral input/unchanged guards. Shader compile/cache
warm-up time differs from steady frame cost; compare adjacent matched world
intervals and inspect pixels/RT dimensions. PM4-only predecessor7823/A990 yielded
2.43vs2.60baseline, no demonstrated gain. WSL remains stopped as explicitly asked.
Exact-prefetch/memo coherence proposals remain unimplemented/opt-in; sampler WIP
archive remains unapplied. No push or issue update during the current cycle.

## PM4 runtime checked; no FPS gain yet, 2026-10-12 19:50 UTC

Local source7823f07fd34f93117f23b248d36643b402c6b493 (not pushed) built194211
and installed194318 PASS. Private runtime-pm4-7823f07f EXE SHA256
A9907A86B7EA026256F740EC7B0CDB2B4178E591EF04A2872340690366E4A760.
Naturally compatible c0 cache was accepted (1897 replay permutations); no key
override. Game194349-c23fbc bounded360s/neutral/4workers/full rendering ended
19:49:53UTC/exit0, no memory guard; maxWS18.04GiB/minfree26.82/mincommit22.93.
90s screenshot withheld by foreground check; 180/300s show character/environment,
grass, fire and water pixels. Single captures do not prove all effect correctness.
No automatic input except neutral pad; post-timeout Options attempt sent nothing.

Adjacent warm world intervals73presents/30s (~2.43FPS), versus c0 baseline78/30s
(~2.60). PM4 header fault class disappears, but actual WAIT_REG_MEM waits increase;
GPUbusy233–235ms versus226–228. No world performance improvement demonstrated;
startup differs because the candidate uses a warmer compatible journal/cache.
Source removes a real false dependency but this does not prove a speedup.

Next sequential candidate: runtime generic formatted BUFFER conversion, where
UInt/Float paths currently evaluate other-format divisions/conversions. Three
agents supplied proposals; test-only cost fixture added, no GPU conversion
production change yet. Native bindless target build active; freeze inputs until
complete, then intended path-cost RED + native4800-lane numerical baseline before
body correction. Preserve strict144-lane UNorm8-one baseline debt separately.
WSL remains stopped under latest explicit user instruction. No push/issue post.

## PM4 false dependency proved/fixed; actual-world comparison pending, 2026-10-12

See [performance cycle1](yotei-performance-cycle1-2026-10-12.md) for current commands,
hashes, baseline and coverage. Current source fix removes unnecessary protected-page
PM4 reads through existing byte ownership. Controlled4-case RED192851/F1A166EF,
unchanged GREEN194021/002DECF3; real GPU-modified headers/payload still wait and
consume new bytes. PM4 WAIT/rewind/scheduler and command-stream neighbors PASS.
Aggregate performance-memory failureConservativeImagePressure independently
repeats with this production correction absent (control193905); no full-suite claim.

New WSL-off c0 baseline191212-e5d801 closed19:19:53UTC/exit0, world visible in
180/300/450s screenshots, no memory guard, maxWS17.91GiB/minfree27.86GiB. Two
adjacent world intervals:77presents/30s/~2.6FPS, GPUbusy229ms, gap160ms and
full-drain154–155ms (overlap). Actual main3328×1872 andpostprocess3840×2160.
Latest user explicitly authorizes WSL shutdown; do not apply old keep-running rule.

Next build/install this separately committed PM4 fix, then a bounded neutral world
comparison using compatible cache, identical4workers and full rendering. Inspect
DISPLAY settings afterward if needed. Other agent proposals are saved but not
production fixes; exact-prefetch reproduction RED is opt-in pending coherence
tests, and GPU formatted-conversion proposal requires cost/numerical regression.
Stable60/actual1080/completeeffects remain pending; no new status post or push.

## WSL stopped by new user instruction; three performance proposals, 2026-10-12

Latest user explicitly requested stopping WSL and continuing with three agents
proposing solutions while root fixes/tests them sequentially. This supersedes
older leave-WSL-running instructions below. Native wsl.exe --shutdown returned0;
vmmemWSL absent and free physical RAM39.33GiB. Active branch is published
a73b95921fb6c40111505ba7e539927df3b5ccbc; production source equals committed c0.
Installed c0 EXE SHA256 DC6B68AD628D172B718E0359FE2A20BD0BE7BB871CD2DC2763B43D8B7BB3A2AB
was checked again. No build/test/game was still running on resume.

Three read-only agents investigate warm PM4/readback waits, exact pipeline
prefetch/readiness and GPU tiling/transfer/resolution. Root owns all production
edits and the native slot. Required synthetic reproductions recorded in test debt;
no production fix applied yet. First collect a bounded neutral-input c0 baseline
with WSL off, normal complete rendering and unchanged8GiB/2GiB/42GiB guards.
Then evaluate proposals sequentially with intended RED/unchanged GREEN and actual
world comparison. Actual1920x1080/60FPS and full effects remain unproved.

## Useful changes retained; incomplete sampler archived, 2026-10-12

User authorized publishing useful work and cleaning the active branch. The fetched
origin/yotei-integration-native already points to c0b54a608c6ed794132d2aca2f92fcc32999fe96,
including verified BC0f40da8a, HTile63c97d04 and alpha-coverage14f2a886 fixes.
Keep that published history and the committed opt-in CPU sampler reproduction;
it describes an outstanding defect, not a completed sampler feature.

All eight changed/untracked files were copied and SHA256-checked before cleanup:
`_Build/analysis/branch-cleanup-20261012T1848474844736Z/manifest.json`,
`uncommitted.patch` and `working-tree/`. The three sampler planner/materializer
source edits, GPU-harness glue and untracked DynamicSamplerHeapGpuTests.inc were
removed from the active working tree. Source/tests now exactly match committed
c0. No reset of published history, game data/cache deletion or WSL changes.

Publication retains the threading/performance research, launch checkpoints and
corrected CTest timing. Stored native CTest154203 was rechecked: source14f2a886,
exit0, 11/11 PASS, 6.69s CTest /7.60s runner. Stored c0 build/install/help were
also PASS; no new build/test/game run was needed for this documentation/cleanup.
Latest installed EXE remains c0/DC6B; visible current-package effects, actual
1080p and stable60 FPS are still unproved. WSL must remain running.

Next resume starts from clean committed c0 source and the published research
priorities. Consult the archive for rejected/unproved sampler drafts; do not
restore them wholesale. A new sampler attempt needs a valid key-buffer fixture,
GPU positive controls and coherence proof before changing production behavior.
New performance work likewise starts with a measured cause and a regression.

## CPU/GPU parallelism research; execution unchanged, 2026-10-12 17:24 UTC

User asked whether rendering is restricted to one CPU/GPU core and requested
research. See [threading and performance research](yotei-threading-and-performance-research-2026-10-12.md).
Verified native source HEAD c0b54a608c6ed794132d2aca2f92fcc32999fe96 and saved
run.json/stdout evidence. No new build/test/game run or production edit for this
research. WSL must remain running; do not lower memory guards/repeat identical
memory failures. No issue update or push.

CPU has one main sequential PM4 processor, plus draw preparation, deferred
Vulkan recording/submission and compiler workers. Latest c0 log confirms shader
replay on 7 threads and queued compute pipeline creation on 2 threads. One main
universal VkQueue does not mean one GPU core. KYTY_GPU_CORE affinity is opt-in,
absent from recorded latest overrides; gpu_frames_ahead has no runtime consumer.

Two saved 30s loading profiles show full-drain 21.61/29.07 ms per present and
GPU gaps 37.93/35.73 ms; queue-lock waits only 0.13/0.17 ms. Separate cold
pipeline misses cost 18.061/16.864s. These categories overlap and loading does
not prove world FPS or GPU occupancy. Last confirmed world used targets up to
4K despite 1080p window; actual 1080p/60 FPS remains pending.

Priority: identify exact missing pipeline permutations/cache readiness, then
prove avoidable readback/release/wait dependencies before changing them. Add
parallel command preparation/recording only if CPU critical path is measured;
dedicated async-compute queues are a later, higher-risk option. Required
synthetic correctness cases and measurement plan are in the research document.

Sampler WIP is preserved and paused for this research: three planner/materializer
source files have been changed since the earlier 16:25 checkpoint. CPU regression
163727/E89AA passed, but GPU163953/80CD fixture produced image0/wrap for all keys,
including its image-key positive control. Its key-buffer descriptor needs legal
bounds/stride semantics plus raw GPU key witnesses before an intended sampler
GPU RED/GREEN claim. Host sampler mirror/coherence proposal remains unapplied.
Installed c0/DC6B runtime does not include this WIP. Next resume: finish those
fixture/control/coherence checks; apply the researched performance work separately.

## Latest native package; memory limit reached, 2026-10-12 16:25 UTC

Currentcommittedc0b54a60 (sourcefix14f2a886); native build-core154637, install155337,
help155338 PASS. `_Build/analysis/yotei-effects-20261012/runtime-c0b54a60/kyty_emulator.exe`
SHA256DC6B68AD628D172B718E0359FE2A20BD0BE7BB871CD2DC2763B43D8B7BB3A2AB.
Source-generatedkey6f054404...; privatefresh journal/cache, priorarchives preserved,
private savedata copied. NewflagsPassesOnly (noGPUtiming/zones) and2compilethreads.

- Run155535-three-fixes-live-journal-129dc5 closed15:56:40UTC/exit0 at physicalfree
 7.98GiB, commitheadroom8.7; maxownedWS10.64GiB. Loadingonly frame463/title2.9.
  Actualused-only sourcejournal1295977bytes; nocorrectnesscheck/draw disabled.
- Differentbounded profile161457-reserve6-fe3ed9 used6GiB physicalreserve, 1second
  checks, same42GiB ownedmax/2GiB commitguard, max900s. Closed16:16:02UTC/exit0 at
  physical5.68GiB andcommit2.5GiB; maxownedWS14.21. Do NOTlowerreserveagain or repeat
  unchanged memory failures. WSL explicitlymustremainrunning; noWSLprocess stopped.
  Loadingonly frame341/title12.0. Actualeffect/renderresolution/FPS comparisonpending.

SmallGPU sampler baseline161851/EDC2: all4 native staticwrap/clamp colors/once1
controls PASS. Dynamic8 cases fail materialization beforedispatch/once0; NOTintended
samplerGPU RED. Diagnostic162422 sameEXE confirms srt-node-fail Void,args0,
materialize-failed bufferindex1; actualimagepc40 doesreceive indirectplan.
Root/descriptor_phi_port prepares ignoredpacked16sampler planner/materializer and
independent source/DCE diagnosis, no production sampler edits yet. CPU152233 valid
plannedidentityRED remains. Test-only GPUfixture/glue are currentWIP; keep default
harness unchanged, only opt-in supporteddevicefeature mode enabled.

Currentno nativegame/build/test running. Next: resolve immutable SRT root/DCE and
sampler planning using unchangedCPU/GPUcontrols, mirrorcoherence regressions before
wideenablement. Preserve all failing inputs, supportedSrc guards and rawsamplerwords.
Newnative11FXCTest reporttiming corrected to6.69s CTest/7.60s boundedrunner.
LiveallrequestedFX/actual1080/60FPS remainsblockedbycurrentRAMheadroom, notachieved.

## Three shared FX corrections proved; WSL must stay running, 2026-10-12 15:43 UTC

Completedlocal0f40da8a BCpartialBUFFER/statistics,63c97d04 registeredHTile,
14f2a886 nativebasicAlphaCoverage. See [effect validation](yotei-effects-validation-2026-10-12.md)
for exact intendedRED/unchangedGREEN, hashes/artifacts, supportedscope and debts.
Hardened32A2C RED152819/5349 ->unchangedGREEN153233/9FB1;64earlyZneighbors153721,
60CPUrequestfields153722, depth/color neighbors153722/153723 PASS. Final native
CTest154203:11/11PASS,6.69s CTest /7.60s runner; no fullsuite/cross-game gameplay/effect claim.
Exactfractional/dither/EQAA nativecoverage is a logged diagnostic approximation;
actualsource routing/swizzle andforceddepthfirst unsupported errors preserved.

New dynamicS# planner152233/C7BD is intendedCPU RED: validindependent imageheap
control passes, sampler becomesdefault. Opt-in savedfixture/CLI; productionfix
and actualGPUwrap/clamp/coherence controls still pending. Source-causal water/
fog/vegetation attribution not established.

Last three newruntime9A17/e776 retries150515,151125,151503 all gracefullyexit0
on unchanged physicalmemoryguard7.42/7.88/6.48GiB, before worldcomparison. Third
retainsrecording/modules but creates actual GPU pipelines on demand; not enough
headroom. WSL had restarted~20GiB; clean-cache reclamation insufficient.
Latest user explicitly says **leave WSL running**. No shutdown/restart/otherprocess
stop or memorylimit changes. Prefer privatefresh scene journal/on-demand modules,
preserve archives/nativecache and recording. Test/game lifetimes and8GiB/2GiB/
42GiB guards remain. Old3-attempt cap removed, but do not repeat unchanged memory
failures without a materially different profile/new evidence.

Next nativebuild/install/game must include14f2a886 source. Its computedshaderkey
6f054404617705c2f27ebf7aed08c493be6328b4059c1c9eb75f47e459abd0ca differs frome776;
no keypin or incompatiblejournal/cache acceptance. Continue effectpass collection
when worldvisible, then next sampler/rejectedsource regressions. FPS60actual1080
and fullrequested effects remain unproved. Issue108game-entry comment posted once
https://github.com/KytyPS5/KytyPS5/issues/108#issuecomment-6126916747 ; no push.

## Native FX package and published game-entry milestone, 2026-10-12 15:10 UTC

Build-core145804 on e77684c9 PASS; private install145942 and help145944 PASS.
`_Build/analysis/yotei-effects-20261012/runtime-e77684c9/kyty_emulator.exe`
SHA2569A17D30BB5744AA90E737FCB509DE3EF1FF96BB662E5AD90121BF8F2A78C0A91.
Head5709d7bb differs only in a test-file ending. Recompiler/pipeline source key
387dd6feeabb9aebf9950db2dead1d17f45f33bc16554c441fe39ace8f59acef is unchanged;
prior compatible private native cache/savedata copied and original preserved.
Copied v9journal hash697AE223783348613D59C349BA98E5B91D2F64F8E0C7201ED9365433895D08B4.
Native CTest150447 on freshly built tests150213/150257:8/8 PASS,5.53s; real BC
partial writes+8neighbors+stats,121HTile state/GPU/registration,DCCstats,ordinaryCOUNT.
Test hashesC7C3F65F(bindless) and146E29FA(shader), full hashes in run.json.

Game150515-cycle1-bc-htile-effects-neutral-1080-234af3:9A17/e776,neutral,noauto
buttons,depth-only/samples diagnostics,drainstats30/GPUzones/framepass trigger2.
Closed gracefully15:06:15UTC/exit0 by unchanged8GiB physical-free guard at7.42GiB
(commit headroom9.49). WSL had independently restarted and now held20.57GiB;
Linux inspection shows~19.6GiB clean buffers/cache,~5GiB actual used. No other
user process stopped; safe cache reclamation investigated before retry.
Loading-only window30/frame186/title8.0FPS; visibleFX comparison NOTestablished.
Replay508compute/hits508; only2sync/taken2. BC6H102-layerreflectionarray now receives
nonbasebufferinvalidations in actual log(+0x6000/0x10000); notvisualproof.
All captured earlydepth-only draws haveA2M0, so syntheticA2Cdefect still lacks an
actual foliage/pass causal witness. Originalrun _kyty.txt absent; stdout/stderr/
run.json+window30 retained. No framepass trigger was created before guard.

Authorized game-entry milestone published once asfxpw after reading all5 latest
comments and verifying currentPR497head5664f3ff/effectiveaccount. PreviousOct5menu
checkpoint retained; no duplicate menu/intermediatefix posts.
[Game-entry comment](https://github.com/KytyPS5/KytyPS5/issues/108#issuecomment-6126916747)
reports tested5b231fdf/B284 village+character+SETTINGS,900s/manualcleanexit,local-only
scope,4Ktargets/lowFPS andeffectsremaining. It does NOTclaim newFXpackage visually
works or currentpublishedPRhead/CIpasses. Only next localverification work remains.

## BC/HTile committed; actual alpha-coverage RED, 2026-10-12 14:58 UTC

Completed local fixes0f40da8a (BC partialbuffer+publication statistics) and63c97d04
(actual HTile extent), on yotei-integration-native. Statistics-on BC RED144435
CE17 recursiveTrackingSpinLock -> same8cases GREEN144626/912D. GPU DCC-clear
statistics neighbor144900/6F850 PASS with unchanged numerical oracle.

New real depth-only alpha-to-mask endpoint fixture145449/63287313:32cases at1x/4x,
actual PM4 + DrawAuto + native depth readback. All disabled/override/alpha1 and
canary controls PASS; all enabled/nonoverride/alpha0 write depth0.25 instead of
preserving0.75, including EXEC_ON_NOOP retainedPS controls. Intended RED, cleanup
completed before assertions. First fixture build145112 had Vulkan-Hpp initializer
setup failure; corrected explicitstage fields, not counted as RED. No A2C production
patch yet: physicalCOL0 routing and exactfractional/dither/EQAA mapping require
careful handling. Ignored guarded draft is withdrawn (arbitraryraw2222/EQAA guards
not justified by endpointsemantics). Preserve originalcontrols; hostfractional
coverage would be a documenteddiagnosticapproximation, not exactguest proof.

Next: new private native BC/HTile package, same source-generated shadercachekey
(no pipeline/recompiler changes in these2fixes), preserve compatible prior native
cache/data and compare game effects. Bounded2-framepass/depthonly+actualsamples
capture added for attribution. Alpha/A2C RED mode is pending implementation and
is not registered as a passingCTest. Current8FX CTest names cover supportedBC,
HTile,DCCstats and ordinarySRDCOUNT. Existing unsupportedBDA/oldsyncdebt remains.

## Effects cycle: two GPU regressions fixed; third lead revised, 2026-10-12 14:43 UTC

Source3b819e1f plus preserved WIP. User confirms prior character/object artifacts
are gone; effects remain the active request. Native source inputs/build/test/game
are serialized. Nothing in this checkpoint establishes correct effects or60FPS.

- Cached BC partial BUFFER write: default pre-fix RED135900 has4 stale consumer
  DWORDs despite native producer/CPU-stale/guard controls PASS. Experimental flag1
  baseline135902 loses2 previously written GPU-image C DWORDs. Shared preparation
  publishes complete BC backing before a partial buffer overwrite, then invalidates
  byte-overlapping BC consumers. GREEN141815 and8 BC4/5/6HU/7 neighbors141816
  preserve C+B/all guards. Old BC alias/tail tests141818/141819 PASS.
- HTile121-layer actual depth registration: RED141111 tracks only32/121;
  native RED141113 fails to clear slices32/120 while31/untouched controls pass.
  Same unchanged fixtures GREEN142528/142529 clear all121 and31/32/120 correctly;
  laterGPU depth survives repeat acquisition. Registration neighbor143534 passes
  all/partial/expanded seeds,33->121 growth, smaller views, memo and strict bounds.
  Legacy owner-retirement fixture incorrectly expected slice1 with declared1layer;
  corrected real2-layer fixture144215 and layered/storage/DCC/depth/358tiler
  neighbors143536..143540 PASS. First correction used nonbase image type and
  failed setup143826; not a production RED or correctness proof.
- GPU counted draw lead: first fixture142530 had two test errors (readonly stream
  snapshot instead of writable owner; BDA LOAD0x0c instead of STORE0x1c). Corrected
  PeekPage GPUbefore/after + store witness143828 proves10 ordinary SRD indexed/
  nonindexed N0/1/2/3/5 cases already correct on original production. Explicit
  writable FLAT/GLOBAL guard rejects BDA store before execution; those cases are
  unsupported, not COUNT RED. Dedicated supported mode144216 PASS. No COUNT fix.
- Existing NativeIndirectDispatch descriptor-sync assertion fails both patched
 143536 and scoped production-absence143827. This is not introduced by this batch;
  its numerical output/underlying semantics still require an independent diagnosis.
- Read-only review found publication's DrainStats DCC query reacquires held texture
  lock. Bounded statistics-on regression and shared lock-aware query are next.
  Third effect candidate is DB_ALPHA_TO_MASK/required pixel-stage retention;
  endpoint alpha0/1 at1x/4x needs synthetic RED before any production implementation.

Evidence is under `_Build/analysis/yotei-integration-20261011/checks/`, with ignored
proposals/reviews in `yotei-effects-20261012` and `upstream-forks-research-20261012`.
Current runtimeB284/5b231fdf predates these effect fixes. Build/install new package
and retry actual vegetation/shadow/reflection passes after focused checks.

Visual correction to earlier summary: baseline131752 `window-780s.png` is a
visually confirmed in-game SETTINGS menu, opened by the single explicit Options
pulse; earlier village/character frames show world entry. Issue108 already has
Oct5 menu milestone; latest comments were read, so do not duplicate it. Game-entry
publication must clearly distinguish this local-only source/package from PR497.

## User confirms geometry artifacts gone; effects are the active target, 2026-10-12

Latest live user inspection confirms the previous character/object artifacts are
gone on the current package. New priorities: shadows, grass/vegetation, water,
particles, wind, volumetric fog, fire and reflections. Begin with vegetation and
shadows, preserving the user's recurring three-proposal/sequential-test workflow.
Approximately60FPS at actual1920x1080 remains a performance target, not a verified result.

Do not treat the old192 matrix holes alone as proof of a remaining visible bug.
They persist in fresh captures but their causality is unproved after the user's
visual correction. Earlier entry-fetch/loaded-leaf/merged-owner fixtures remain
saved ignored proposals; re-admit only if an actual effect/pass witness uses them.
Agents now research effects/indirect draw+dispatch, shadow/3D volume resources,
blend/image ownership; no production edits until each specific intended regression.

Native live inspection uses verified5b231fdf/B28453B2, manual input, one game
process. New runs:130603 stopped by WMI monitor error;131317 stopped gracefully
by commit guard at0.11GiB; user-authorized WSL shutdown freed headroom to56.57GiB.
131752 then completed900s/exit0 with village/character visible, selected DA4/VS
captures and neutral pad except one explicit Options pulse. User manual run
133526 ended13:38:50UTC/exit0 after user inspection. No game currently runs.
Actual captured graphics targets remain3840x2160 despite1920x1080 window;
VideoOut already advertises1080 under current flags, so do not fabricate a
resolution-API bug or blindly resize guest image backings. FPS around2.5..5.6
in recorded scene is preliminary/title timing, not a60FPS benchmark.

## User removed attempt cap; recurring three-candidate cycles active, 2026-10-12

The latest user instruction explicitly removes the previous original-game limit
and authorizes fixes, tests and game launches in recurring cycles. Historical3/3
attempts below are evidence only; they no longer stop current work. No renewed
permission is needed for routine bounded native launches within this task.

Workflow: agents independently propose three concrete ranked alternatives with
expected behavior, minimal synthetic regression and live witness. Root selects
three complementary candidates for the current observed blocker, tests them
sequentially (one native build/test/game at a time), records each outcome and
starts the next cycle using new evidence. Do not invent three speculative patches
merely to fill a quota: a falsified lead is a useful result. Production changes
still require intended RED->unchanged GREEN and shared semantics.

First step is a fresh bounded original baseline on the already verified native
5b231fdf/B28453B2 package with new BC/Phi/DMA ports. Keep inputs neutral, capture
body and surrounding objects over several frames, producer/VS evidence and actual
frame timing. Target remains1920x1080 stable picture and approximately60FPS;
menu, loading FPS and compiled pipelines do not prove that outcome. Existing
old journal/cardinality and broad-suite debts stay recorded separately.

Preserve every failed input before diagnostic fallback, group batch causes,
keep fallback observations separate, preserve caches/data and save a uniquely
timestamped handoff after meaningful work. No push or new external issue update
is implied beyond the existing visually confirmed milestone policy.

## Native package verified; corpus replay incompatibility recorded, 2026-10-12 12:38 UTC

Active branch yotei-integration-native. Tested production/test source
5b231fdf0a5e5cc42751f7281cbd0e21276382b7; local fixes3f98bd1d (Phi),
30db5f71 (DMA bases),2b5504f3 (BC/raw+partial copy), registration/evidence5b231fdf.
All12 focused CTest tests PASS; four intended native RED->GREEN mechanisms,
GPU neighbors,96 Phi CPU variants/6 GPU cases and358 tiler cases are recorded
in [validation report](yotei-upstream-ports-validation-2026-10-12.md).

- Native build-core122312 and private install122420 PASS. Runtime:
  `_Build/analysis/upstream-ports-20261012/runtime-5b231fdf/kyty_emulator.exe`,
  SHA256 **B28453B26DE56615B9A1DAEF1D5D153E8893F530593D92C40EB49996EFA779DA**.
  Bounded `--help`122603 PASS. This is not a game launch or visible frame.
- Full fresh CPU corpus finished12:30:28UTC without timeout:2999 manifests,
  2770 distinct inputs/executed, reused0,2524 PASS/246 FAIL;237.447s auditor,
 264.167s coordinator. All246 are vertex records failing the same strict
  recorded-specialization/resource-shape gate, before emission. All542 compute
  and1331 pixel distinct inputs pass;651/897 vertex pass. No decode/emit/SPIR-V
  failure in the successfully emitted inputs; full-corpus GREEN not claimed.
  Evidence `yotei-integration-20261011/batch-native-full-20261012-122604-0742614/`
  under `_Build/analysis/`; actual auditor FDF74819416AA97D05D57B7BD5B6579852D51A0F93B917D02C5E3765538D711E.
- Exact record238 also fails with only Phi behavior reverted to e5c6e346:
  native123522/C9118E81, saved translated IR. This excludes today's Phi port
  as the cause for that sample; the246-wide baseline/source attribution remains
  pending. Do not claim all246 were already failing on e5c6e346 or fabricate
  new specializations from missing guest inputs. Original records are preserved.
- Native replay checks the same cardinalities before ApplyResourceSpecialization;
  incompatible records are not installed. Live lookups use actual guest resources.
  The new source-generated cache key differs from prior original-run journals;
  private runtime has no copied pipeline cache. These are verified code/package
  facts, not proof that cold compilation or gameplay completes.
- Scoped absence sources restored byte-exact/touched; final auditor rebuilt123523
  as1D5D2420C6FF217340D5D41589A1089D4112197B8550D5C9E37CF67A502C39A9.
  Full audit is evidence for FDF74819, not a second whole-corpus run of1D5D2420.
  No native game/test/Ninja/MSBuild/auditor process remains; no push/issue comment.

Next required work: resolve the vertex journal cardinality/source attribution,
preserving strict validation; capture fresh current-source specializations when
an original-game launch is explicitly authorized. On that run use the private
native artifact, actual1920x1080 measurements, body/nearby-object screenshots and
DA4 producer/VS witnesses. Original192 matrix holes and stable picture/~60FPS
remain unproved. Historical original-game3/3 cap is retained; no fourth run occurred.

Record238 lead: pre-Phi translated IR has7 distinct live buffer-descriptor operand
groups/no image handles versus recorded6/0 (not a directly printed ShaderInfo count).
Its15-block CFG meets older f2593c61's multi-block embedded-fetch lifting exclusion;
the extra group appears to be retained vertex fetch. Verify actual resource counts
and a synthetic multi-block fetch regression before changing that safeguard; this
lead does not establish the cause of every246 record or the original192 matrix holes.

## Verified upstream mechanisms; final native audit preparing, 2026-10-12 12:22 UTC

New user instruction "делай" authorized implementation and bounded native tests.
Completed local commits: 3f98bd1d immutable scalar descriptor Phi, 30db5f71
known direct DMA bases, 2b5504f3 BC/raw ownership and partial-copy destination.
Each has intended native RED -> unchanged GREEN. All12 new focused CTest cases
PASS in4.10s; actual BC4/5/7 mip/layer readbacks,4080 untouched tail bytes,
first-dispatch DMA endpoints/atomic1/GPU-dirty neighbor,96 CPU Phi variations
and6 GPU writes are independently verified. No original game run occurred.

See [full validation record](yotei-upstream-ports-validation-2026-10-12.md) for
commands, exact executable hashes, scope, temporary source restoration and logs.
Current source mechanisms are2b5504f3 plus test-registration/documentation WIP.
Core executable build/install and fresh2770-distinct-input CPU corpus audit are
next; no success is claimed for these pending stages. Original192 matrix holes,
stable surrounding objects/body, actual1080p/~60FPS remain unproved. The previous
original-game budget stays3/3; no fourth run, push or issue comment is implied.

Full default CPU suite still hits recorded pre-existing mixed-numeric-image
policy failure. Six relevant older Phi neighbors pass; unrelated guarded-null
and relative-ConditionRef cases remain debt. Existing image-overlap GPU suite
now passes rasterization after a test-only MRT-state correction (same prior
failure proved with all three port behaviors absent), then stops on the separate
unaligned writable halfword/stencil binding case; whole-suite GREEN not claimed.

## Upstream ports in progress, 2026-10-12 11:20 UTC

The new user instruction "делай" authorizes implementing the researched fixes
and running bounded native regression tests. The previous three original-game
attempts remain recorded below; this checkpoint does not reset that budget.
No new original-game run, visual repair or performance result is claimed.

Three independently tested candidates are being prepared: official main
dad900c6 BC/raw image ownership, TheCruZ d5259de5 writable descriptor-word Phi,
and TheCruZ 19cc54d0 direct user-data DMA bases before any SRT read. Each needs
an intended RED on this branch before its production correction, unchanged
GREEN and neighboring cases. Builds are serialized and completed fixes will
be committed separately. Existing local TickFrame aging already addresses the
upstream GC issue; no GC rewrite is planned. Baseline source is e5c6e346.

Required reproductions are recorded first in the test debt. Synthetic success
will not establish the cause of the remaining 192 matrix holes or prove stable
textures, nearby objects, gameplay or 1920x1080 at approximately60FPS.

## Research-only checkpoint: fresh main/forks reviewed, 2026-10-12 11:08 UTC

New user instruction authorizes only inspecting main/fork fixes. The earlier3/3
visual budget remains exhausted; no new code implementation, native build/test or
game run occurred. Research completed against local72e3568c and officialmain
dad900c61d6c7ff8aefc9e666ca7c3de4bbf4176:60commits/19afterprior730 pin,
eightselectedfork branchsets/sevencodehistories,847publicdirectfork metadata.

Saved [Russian review and candidate checks](kytyps5-upstream-fixes-review-2026-10-12.md).
Main BC alias/copy-destination ownership and TheCruZ nested writable descriptor
Phi/DMA-base preparation are concrete absent comparison candidates; no verified
repair of actual192 first64/512 holes. GC ages already use localTickFrame;
fullwave branch is already local/attempt3 and unchanged in latestmain.
peepostarexx liveAPI404, historicalYOTEI6d041035 preserved. No blindmerge or
title/hash workaround; implementation/runtime remain stopped until a new user
instruction. Main/fork reports and unique shared handoffs preserve exactsources.

## STOP: all3 user-authorized attempts exhausted, 2026-10-12 10:10 UTC

The user said "у тебя 3 попытки, если не получится дальше не иди". All3
attempts have now been used. WORK STOPPED. Do not automatically continue from
older continuation requests, run another warmup/game/test, or implement another
fix/research pass without a new explicit user instruction. Recording these
results and preserving already validated changes is the final authorized work.

Stable picture and approximately60FPS at1920x1080 were NOT achieved. This is
partial emulator progress, not a menu/gameplay milestone or a completed visual fix.

| Attempt | Tested core / executable | Verified result |
| --- | --- | --- |
| 1: validated SRT read pages | 989390b2 / E4EF9163 | 092806 ended09:28:21.215Z exit321 after14.28s; virtual null read0x30 wrongly preacquired; no screenshot or shader capture. |
| 2: backed versus virtual reads | 7b89aee6 / 4858584F | 094309 ended09:46:13.730Z exit0/180s/graceful, min14.72GiB. Actual BAD DA4 fills943/1135 rows;192 remainzero, exactly first64 of each512 block. Targets2097/2098 stillzero and exact VS join. Captured150/170s screenshots black; stable scene not confirmed. |
| 3: existing full-wave branch mode | SAME7b89aee6 / SAME4858584F, KYTY_WAVE_BRANCH_FULL=1 | 100117 ended10:06:35.140Z after300s budget+boundedcleanup, exit-1/forcedcleanup/min18.99GiB. Six screenshots saved; actual290s frame155 screen black. No DA4/VS/liveSPV capture, so mode effect on matrix holes unavailable. Title30FPS is loading, not gameplay performance. |

- All owned native game/test/build processes ended; rootexec42858 fully drained.
  Exact installed SHA4858584F26F0AC0DD09D4032C7F2A7D16663F9A149BD1429368141B52D5528BF checked.
  Final evidence `writer-history-diagnostic-20261012/attempt3-final-stop-20261012T1009032364618Z/`.
- General corrections remain local commits989390b2/7b89aee6: first-use known
  metadata pages now resident; virtual null values remain values rather than
  backing. Intended GPU/CPU RED→GREEN, five CPU neighbors and GPU preservation
  checks passed. Earlier77098f2a late-image fringe also proved separately.
- Full-wave configuration was checked BEFORE attempt3:095617/6154D64B passes
  bounded Wave64RawMasksAndScalarBranch and1135-row strided loop. This is a
  configuration experiment only; default production branch behavior unchanged.
- Actual partial progress is943 newly populated matrix rows in the BAD producer,
  with unchanged original E386 SPIR-V.192 missing rows and affected surrounding
  objects remain unresolved; no sanitizer or title/hash workaround was installed.
- Existing full materialization suite failure at UniformVectorDescriptorRead
  was confirmed with the new observer absent; logged separately. Focused five
  residency cases pass. Cross-game runtime and true internal1080p/60FPS unproved.
- Tests, private runtimes, savedata, caches, screenshots, exact inputs/IR and
  uniquely timestamped shared handoffs are preserved. No push or external issue
  comment; verified menu/entry milestones were not reached during these attempts.

## Attempt1 failed; attempt2 preparing, 2026-10-12 09:38 UTC

Used1 of3 allowed original attempts; at most2 remain. Attempt1 original092806
on989390b2/E4EF9163 ended09:28:21.215Z exit321 after14.28s, min21.52GiB,
noRAMguard. Required ClampRangeSize rejected address0x30/size32 during new
metadata preparation. No screenshot, DA4 or VS capture exists; no visual result.

Source cause: the existing GPU metadata reader deliberately returns TRUE+zero
for guarded null paths starting below NullGuestRegionEnd. These are virtual
values, not backing. Attempt2 declares that provider contract in SrtRuntime and
excludes those reads from residency only; callback values and required memory
errors stay unchanged. Ahead reader accesses actual backing and keeps default0.
CPU RED093738/C2285CC5 -> SAME5-case GREEN093817/8284C5D4 verifies two bounds,
virtual starts/spans, exact real boundaries, mixed reads, memo/cookies and defaults.
Native GPU neighbors/build/original attempt2 stillpending. No game/hash/address
exception, silent Memory::Clamp error suppression, stage skip or command replay.

## Attempt1 of3: validated SRT-read residency correction, 2026-10-12 09:27 UTC

User budget is now3 total further visual attempts; attempt1 is about to launch,
with at most2 remaining if unsuccessful. Do not reset or add warmup retries.
Shared fix989390b2 retains this refresh's actual successful SRT reads for DMA
stages and acquires their cache pages before ordinary descriptors/BDA preparation.
No arbitrary pointer scan, game/hash branch, store suppression or replay.

- Intended GPU RED090350, EXE86E3B4AF, fixtureA960C26C: correct ordinary/SMEM/
  atomic-once/sentinel controls, root+childPTE0, dynamic pointer/leaf incorrectly0.
- SAME fixture GREEN091355/C9D9AE84: prepared PTEs populated, child pointer and
  leaf71335599 correct, atomic1 unchanged. GPU dirty-owner neighbor091657 on
  51F1CEBA preserves89abcdef; late-image091658 and rendering/chain091658 pass.
- CPU4 focused cases091928 then restored-source092300/3805F87C allpass: nested/
  repeated reads, strict-cookie separation, equal-value source relocation/memo,
  failure clearing and DMA-off. Existing fullsuite stops at UniformVectorDescriptorRead
  compiled/reference result mismatch; SAME failure confirmed with this patch absent
  in092111/7109BF0F. Native link/test-name harness repairs are not regression proofs.
- Native core092426/install092716 PASS, fresh E4EF916379D6FECD768E46BBCCEBC5A4CB15BD31E132D56879394E05E0CB62C9.
  Private runtime-attempt1-srt-residency, baselinePerformance/hardware bounds true,
  manual1920x1080/timestamp100/workers4/replay1/DA4 max2/noJOBfilter, bounded180s
  including warmup. Prior private data/cache copied unchanged; normal key validation.
- Picture/menu/gameplay and stable~60FPS remain unconfirmed until actual screenshots.
  No push or external issue milestone. Attempt2/3 only within the user's remaining budget.

## Active user limit: at most3 further attempts, 2026-10-12 08:46 UTC

The user now allows at most3 further concrete fix/configuration attempts with
original visual verification and says stop if they fail. No further original
attempt completed since that instruction; attempt1 is preparing the first-use
SRT metadata/BDA regression. Do not silently reset this budget across sessions.
After the third unsuccessful attempt, stop work and record the results.

Exact live DA4 code links root+372 to flatc1: BAD41 enters phase600..2294,
FINITE40 skips it. The entered phase handles first128 indices and loads by
address through actual userdata root+24 then child+152/+136. In all4 captures,
CaptureInputs' actual userdata-root page query found no host BDA owner; the
8192-candidate limit was not approached. This is stronger than an arbitrary
missing-page heuristic, but still not actual GPU PTE/load evidence. First attempt
will test retention/preacquisition of already validated SRT memory-read ranges,
with no game-specific flags, arbitrary pointer scans or command replay.

## Latest checkpoint: exact live producer module regenerated; simple loop passes, 2026-10-12 08:39 UTC

- TESTONLY ceeb543d: independent bounded persistent loop1135/WG512/guest64-on32
  baseline083006 passes all rows/counts/bailout/canaries on fresh E007BF8C after
  native082842 build. No loop fix follows from a passing probe. Secondary unused
  count512/1024/1135 draft remains ignored/uncompiled.
- Native shader_batch_audit083329PASS/EXE057C36EC; actual DA4 capture body A195AC
  matches prior journal bytes. Recompiled with actual hw-bounds/native-FP16 true,
  tablefalse/bindlesstrue and VTX_EXPLODE1 at083642, child0/CPU-only validation.
  Emitted3077040-byte SPIR-V SHA E386624DAAC172120B664ABB49B4200AEBEC999B9C16BB5B761362CBC8B3403A
  is BYTE-IDENTICAL to captured live module. Exact IR/CFG is now an appropriate
  code-ancestry reference. This is MODULE equivalence only: runtime materialization,
  actual executed masks/loads/stores and expected game output are not proved.
- Current installed runtime remains7cb371e7/PE5DC44E05; later ceeb543d only adds
  reusable test/CTest. Last original081727 ended321 and all task-native processes
  are gone. No further heavy original retries after semaphore failure.
- Current frontier: compare actual DA4 BAD/FINITE phase gates and executed inputs,
  including resource reads and restored EXEC after nested phases. Preserve
  stable instrument versus broken nearbyobjects/body as separate observations;
  no game-specific flag override or zero-matrix sanitation. Stableimage/~60FPS
  stillpending. Docs/debt and ignored evidence preserve continuation; no push.

## Latest checkpoint: missing producer identified and captured, 2026-10-12 08:23 UTC

- Core7cb371e7/PE5DC44E05: fullhistory original081201 ended08:15:06.515Z
  child0/180s/min14.15GiB, native processes ended. It exported all28 overlapping
  writers without truncation and revealed the previously hidden oldest shader
  da4ff1222c09f131 (slot2/25, groups1x1x1). Its loop advances each job by512;
  the earlier job filter's min(count,launchcapacity) is not an execution model
  for persistent loops. Disable that filter for this shader; do not equate
  an unadmitted capture with a skipped dispatch or missing producer.
- SAME core/PE targeted original081727, no job filter, max4 and written2:124224:
  four complete captures267/267/268/269,75 references/41 blobs verified. Live
  original code matches prior decoder; logical512, count1135/base1050, loop
  covers[1050,2185). First BAD bank all1135 matrix rows zero BEFORE AND AFTER;
  next FINITE bank all1135 zero -> nonzero. Both2097/2098 reader joins exactly
  match AFTER bytes (24 DWORDs). No later loss of those positive data shown.
- Inputs/executed masks still differ; zero classification is not an independent
  expected-value oracle or proof of absent stores. Scratch uses packed alignment
  prefix4; corrected firstcapture slot5 changes logical128..511, not128..512.
  No arithmetic/EXEC/loop production correction is justified yet.
- Targeted run ended unexpectedly08:20:15.844Z child321, min14.91GiB/noRAMguard,
  before180s deadline. Fatal masterSemaphore.cpp276/301 counter/wait result;
  actual VkResult is not logged, so device loss is unconfirmed. Streams drained,
  ownedPID34868 ended. Do not repeatedly retry heavy scene to force GPU resets.
- User asks to inspect surrounding objects as well as character, and confirms
  stable instrument versus broken body/clothes. Earlier210s063813 screenshot
  visibly contains intact instrument/board fragments with severely missing world;
  global texture-versus-skinned-only scope is not established. Need draw identity
  and transform ancestry for surrounding meshes; do not limit investigation to skin.
- F2 opens emulator settings; F3 toggles render resolution/FPS info. Async pipelines
  and relaxed readback OFF in our profiles. Settings file kyty_settings.ini in
  runtime working directory. No setting is verified to fix this defect.
- ps5rs master0df6569e researched: no compatible graphics fix (shader disassemble
  is hexdump, AGC/driver submit functions arestubs); SELF/ELF/NID useful only as
  optional CPU-analysis tools. Primary-source report ps5rs-review-20261012 preserved;
  no installation/execution or data upload. Current priority remains actualDA4.
- Next: bounded synthetic strided-loop/EXEC regression, then precise executed-input
  and output-store proof for BAD/FINITE DA4. Original stableimage/~60FPS1920x1080
  still NOT achieved. All original data/artifacts retained; no push/external milestone.


## Latest checkpoint: original picture still broken; preceding matrix producer search, 2026-10-12 08:12 UTC

- Shared residency correction77098f2a remains proved RED/GREEN; it did not fix
  the sampled original zero-matrix signature or visible geometry.
- Core e42b621c / PE5C6133BA: cold original075437 ended07:59:44.889Z exit0,
  300s, no RAM guard, min15.05GiB; actual210s frame349 screenshot shows severe
  stretched geometry, title3.2 FPS. Normal cache invalidation occurred.
- SAME PE warm original080225 ended08:07:29.254Z exit0, 300s, min14.03GiB;
  loaded176985706-byte cache, actual150s frame536 screenshot remains broken,
  title2.6 FPS. 90/210s captures withheld because owned focus was unavailable.
  Both runs manual/autoswipe0 with neutral input logs. Internal targets still
  exceed the requested1920x1080 window; no stable FPS claim.
- Warm CPU inventory4096:703 retained selected-hash records match124224-byte
  output SRD, no job covers2097/2098; all logged groupsX=1. Matching palette
  jobs cover[664,1050) and[2185,2588). Scope/cap hide other hashes/later keys.
  No final inventory summary was observed despite childexit0; final overflow
  totals unknown. No CS capture was admitted in either run.
- Pure-file matrix/scratch evidence: zero64-row slices begin1050/1562/2074,
  exactly512 apart. Row2097/2098 falls in the third slice. Inputs differ, so
  this pattern does not alone prove an EXEC/arithmetic or BDA-load defect.
- User confirms instrument remains stable while character/body/clothes deform.
  This favors investigating animated geometry/matrix data; exact instrument
  draw identity is still uncorrelated, so global texture fixes are not inferred.
- Local-only diagnostic7cb371e7 preserves default writer-query8 but explicitly
  exports up to64 older declarations in admitted VS trace (28 matches previously
  truncated). CPU080834PASS: default8, cutoff28, hardcap64/order and boundaries.
  Native core080842/install081013PASS, PE5DC44E05, unchanged private warmcache.
- ACTIVE bounded original081201 (PID5296, rootexec67621), source7cb371e7,
  runtime-full-writer-history-1080, timeout180/manual1080. Next inspect all28
  producer declarations and identify actual preceding writer of[1050,2185).
  No shader behavior fix justified until intended regression. Stablepicture/
  ~60FPS stillpending; no push or external milestone. Preserve originalartifacts.


## Latest checkpoint: late image-backed BDA fringe fixed, 2026-10-12 07:50 UTC

A native GPU regression now proves one real shared residency defect fixed.
A sampled image merges an aligned buffer after the early BDA pass; its mapped
CPU-dirty fringe was visible through a valid PTE but still read as zero.
Final BDA synchronization follows all stage image/buffer acquisitions in graphics
and direct/indirect compute, before command handles, chain decisions and captures.
Unchanged epochs retain the existing cheap return; GPU writes are preserved.

- Baseline source b7fb1c92 plus test-only changes: intended RED at
  `checks/20261012-074808-0238556-late-bda-production-phase-red` (C07E7549).
- Same fixture/oracle GREEN at `checks/20261012-074940-4760654-late-bda-production-phase-green`
  (D3CC0DE5). First fringe 0 -> 0x71335599; PTE/image/ordinary input/once-only
  atomic counter and prior GPU marker controls all pass.
- Four affected image/deferred/rendering-chain/EOP neighbor modes pass at
  075016/075025/075026/075027; native build 074848 succeeds.
- CPU job inventory b7fb1c92 was verified in seven runs 072104 through 072109:
  selected rejected job intervals are logged before capture quotas, bounded at
  256 retained keys, without new GPU work. Previous 063813 original run captured
  no matching CS with required_count=2; actual rejected shapes were unavailable.
- Next: fresh native core/install and bounded manual 1920x1080 original retry,
  inventory256 and range23:196:2097:1. This correction is not yet a visual fix
  claim. Stable picture and ~60 FPS remain pending. No push or external milestone.


## Latest checkpoint: output-job capture selector verified, 2026-10-12 06:36 UTC

- HEADca01a56e, local-only diagnostic. OptionalJOB_RANGE strictdecimal
  count_dw:base_dw:first:required count, known1D logicalcapacity, widened
  nonwrappingWHOLEinterval; matchesBEFOREchosenframe/hashMAX andCaptureInputs
  id/directories/drains. Directcaller passesactualinput/groups; indirectunknown
  callers declineenabledjobcapturebeforequota, normaldispatch unchanged.
  Runtimeconfig `KYTY_DISPATCH_SNAPSHOT_JOB_RANGE=23:196:2097:2`
  (flat0x17=23decimal, flat0xc4=196), composedwithslot2:124224. Metadata labels
  thisadmissionhint, notexecutedstoreproof. No title/hash/address behaviorfix.
- Nativebindlessbuild063054PASS,C64D8AB0. Threejobselector063435/063435/063436
  plusoldwrittenselector063437/063438/063438 neighbor modes allchildexit0,
 26malformedparser+stage/shape/flatbounds/U32capacity/wrap/wholeinterval;
  actualMAX1/frame/defaultcontrols pass, GPU_setup=0. SourcechecksPASS.
- Nativecorebuild ACTIVE sourcefrozen; nextfreshinstall/hash/privatecache/
  provenance, boundedoriginalmanual1920x1080withsamefourverifiedcoreports,
  worker4/replay1 andtargetedjobcoverage. Shadercachehash inputdirectories
  graphics/shader andrenderer/pipeline untouched by thisinstrumentation;
  no keypin/waiver. Originalpicture/~60FPS stillunachieved.
- BDAprecisemetadata research: guestPTE page16KiB (separate4KiBdirtytracking),
  PTE8bytes/faultword4bytes; CPUhostPagePeek isnotGPUtablevisibility. Capturing
  selectedACTUALaddresses before/after inline couldbebounded; no newBDAprobe/
  productionfix yet, source observer mustnotGetFaultBuffer/FindBuffer/drain.

## Latest checkpoint: REA plan committed; grouping hypothesis passed baseline, 2026-10-12 06:16 UTC

- HEADfa7ee9fb: reusable TESTONLY multiwavegrouping test committed after
  build060754 andnative061225PASS onC41A4AE1. SAME64866D5B independentoracles
  allsix64/128/512 full/sparse casesPASS: actuallogicalIDs, READLANE31/63/
  READFIRST, restoredEXEC andfullsentinels. No intendedRED, therefore no
  productiongroupingfix. Count349 is outputjobquota, notWG349. Native64 and
  arbitraryVulkan subgroup/local-index permutations remainseparatecoverage.
- Russian[REA plan](rea-yotei-debugging-plan.md) andpriorcompleteevidence
  committedc68d324c. Currentcorestillfourports1c9e3dfa; nativeoriginal054138
  failedstablepicture(~3.3diagnostic/internal>1080) butended0. No furthergame
  orproductionchangeafterit. Allnativeprocessesended; no activebuild/test/game.
- Nextrequiredwork: optionaldiagnostic outputjobcoverage beforequota to capture
  actual2097/2098 producer, includingdynamicSRT/writtenstate andactualBDA
  page/faultmapping. Missingcapturepage isnotunmappedhostpageproof. Generic
  first-use BDA behavior mayreturnzero untilnextframe (sourcelead), butactual
  currentcause needsmappedaddress/readback correlation andsyntheticRED.
- REA sourcepin/localPE-PDBbaseline complete; engine/provider executionpending
  prerequisites. No installation/globalconfig/publish/push, no automaticinput.
  Stableimage/~60FPS remainsunachieved. Sharedhandoffupdates preserve state.

## Latest checkpoint: REA research and concrete binary baseline saved, 2026-10-12 06:00 UTC

- Currentnativebranchb026e202/core1c9, fourports committed/tested; no game,
  test orbuild process active after054138 ended0. Stableimage/~60FPS remains
  unachieved; actual210s image stretched/title3.3. Same-run BADmatrixzeros
  predatecapturedwriters; nextmissingproducercoverage stillrequired.
- UserREAlead researched at3bdc8730/package.json6.4.0. Saved Russianplan
  [REA debugging plan](rea-yotei-debugging-plan.md), plusignoredsourceaudit.
  WindowsGhidra nativePE-only, requiresGhidra12.1/JDK21+, notfoundlocally;
  SELFdirectopen unsupported, oldSept6extractedSCEELF provenanceunverified.
  No npm/provider/setup/MCPconfiguration execution. CPUjob-builder dossier
  remainscandidate, notRDNA/BDAreplacement orFPSproof.
- ConcreteexistingLLVMbaseline onehiddenboundedPEreadobj exit0/~66ms:
  exactB048335FAMD64/ImageBase140000000; PDBCodeView GUID/ageMATCH. Native
  sourcefunctionTileGetTextureBlockLayout RVA49880 givesfirstdossiertarget.
  Evidence `rea-review-20261012/own-pe-readobj.run.json` andPDBidentity preserved.
- Newdata lead: first549count349 fillslast285rows, leavesfirst64zero; eighth
  fillsall349. CapturedreadonlyinputSRDbytes identicalinclfirst64, butdynamic
  SRT/writtenstate differ; nofullinputequivalence/pagefaultcausality claimed.
  Loopfree multiwaveREADLANE31/63/READFIRST+logicalIDs64/128/512 regression
  TESTONLY draftassigned, unused/uncompiled. Existing2wave47/LDS coverage
  retained; avoidduplicateoracles. No productionwavechangebeforeintendedRED.
- Docs anduniquehandoffs preserveevidence; no externalissuecomment orpush.

## Latest checkpoint: four ports tested in game; row producer remains missing, 2026-10-12 05:50 UTC

- HEADb026e202, core1c9e3dfa withfourselectiveupstreamports: R8sampling,
  packedprefixreuse, BC-R64 andactiveNGGcount. FocusedRED/GREEN/GPU/CPU
  evidence below. No source WIP beyonddocs, no push or externalmilestone.
- Original054138 ended05:46:43.443 exit0/300s/graceful/noRAMguard,min9.95GiB,
  root98703 drained andownedPID43164 ended. B048335F exactnativecore; worker4/
  replay1 trial. Actual210s screenshot hasseverestretchedforeground/small
  backgroundvisible,titleframe330/FPS3.3. Internal3328x1872/3840x2160 still
  differs1920x1080window. Stablepicture/~60FPS NOT achieved; no improvement
  claim fromunlikecache/diagnosticruns. Inputlog neutral/autoswipe0.
- EightcompleteCSsnapshots/150refs/98unique blobs PASS; readback nowcomplete:
  BADtoken1/frame288->289/base200a0d7bc0 first7CS, sixvalidrows2097/2098 zero;
  FINITEtoken2/frame289->290/base20032e2020 lastCS, same sixrows finite. All24
  readerDWORDs exactly equalbefore ANDafter capturedCS. BADzero predatesthese
  capturedwriters: no late nonzero-loss proof. FirstCS changes285 otherrows;
  selected2097/2098 outsideeverycapturedjobinterval. NEXT capture must select
  actualoutputjob coverage beforequota, notonly124224size. Do notsanitizeRSQ.
- Raw-rootproof sixofeight: root+24 child; child+56 descriptor=slot2/flat12,
  child+120=slot25/flat188; root396=flatc4. NativeboundflattenedSRT matches
  metadatainall8; actualprimaryhashes match decoded1158/549. BOTHbankoutput
  bases flatc4; firstgate initialactive &&j<count(flat17), finalgatealsoflatc5.
  Missingrawrootpages in2/6 andallcountchildpages explicit, no fabricateddata.
  Purefileevidence `writer-history-diagnostic-20261012/cs-review-game-20261012-054138-integration-upstream-ngg-1080-89c844-20261012T054551384243Z/`.
- Latestuser asksinspectmorluto/rea. Freshread-onlyaudit pins3bdc8730/package
  6.4.0; WindowsGhidranativeprovider PE x86/x64 only, originalebootSELF magic
  4F153D1D unsupported directroute. Usefulcandidate: targetedCPUfunction/job
  metadata investigation withnativePE/PDB/map orboundedexportedguestx64
  segment viaappropriateGhidraloader. DoesnotprovideRDNA/SPIRV/BDAsemantics.
  No setup/globalMCP config/install/provider execution; concreteplan pending.

## Latest checkpoint: four ports committed; NGG original retry active, 2026-10-12 05:42 UTC

- HEADb026e202 (CPU-neighbor CLI); productionNGG1c9e3dfa after unchanged
  native053122RED ->053446GREEN, all422records/12cases. Embedded053448PASS;
  focusedexistingNGGmetadata/export/cacheguards053823PASS. FullCFG053704 has
  EXACTold215846/6ba0 computeempty-declarations baselinefailure beforeNGG;
  unchangedassertions remain, fullsuite notgreen. Hostnative64/sparseinitial
  graphicsmasks andcrossgame runtime notcovered. No title/hash fix selection.
- Nativecore053904 PASS from1c9; install054009 PASS, src/CMake/3rdparty tree
  identical torecordHEADb026 verified (onlyCPUtestCLI differs). ExactEXE
  B048335F3F72D7EE90DB1EE976B0A496E324DD1BC8159D987A7D32ABA84390B0,
  runtime-upstream-ngg-1080 preservedsave/cachecopies,symbols/provenance/hashes.
- Original ACTIVE root98703/PID43164, `_Build/runs/game-20261012-054138-integration-upstream-ngg-1080-89c844`,
  300s/manual/autoswipe0/window1920x1080/timestamp100/selector2:124224/max4.
  Runtime-onlyworker4/replay1 trial reducescompilerconcurrency; no equal-cache
  A/B speedupclaim. Usual memory/cleanupguards, actualinternaltargets separate.
- Sourcefrozen untilrunends. Actualpixels/NGGgameeffect/paletterootjoin pending;
  stablepicture/~60FPS remainsunachieved. No push or externalmilestone update.

## Latest checkpoint: eight captured producers; NGG count RED to GREEN, 2026-10-12 05:36 UTC

- Three-port warm052352 ended05:29:06.754 exit0/300s/graceful, noRAMguard,
  min11.21GiB; cache116781013bytes. Latestowned210/late screenshots showblack
  loadingindicator (frame289/title12FPS), notmenu/gameplay/stablepicture.
  Framecounter alone remains no FPS proof; internalRT3840x2160.
- EightcompletefilteredCSsnapshots/136refs/87blobs checksum+sizePASS. First7
  base2003119d80/slot2 plusadjacentbank25 unchanged. Eighth549 base200a035650
  writes16698bytes(slot2)+12325bytes(slot25), row2239 zero-to-finite bothbanks.
  flagflatc5 belongs finalbank, notfirstslot2; exactresource/rootjoin needed
  beforeassumingstorefailure. Currentreaderframe294 samefirstbase butstatus
  PENDING/events0: zero/NF notobserved. Finalpurefileevidence/report saved
  `writer-history-diagnostic-20261012/filtered-capture-052352-final-review.md`.
- Nextupstreamcf77fb43 contract independentlyREPRODUCED: all12boundednative
  draws/wavecases053122 RED (96DFB5CC) reportES32/64 vsinitialactive1/17/31/32.
  SharedTranslate.cpp commoninitialmaskpopcount29C4916D replacesfullcapacity
  for BOTH ordinary/WaveHalves S3; SGPR2capacity andupperS3packing unchanged.
  SAME05B8A4A3fixture053446GREEN (71C43725), all12cases/zero mismatches;
  embeddedfetch053448PASS. CFGmetadata/exportneighbor buildACTIVE; commit/
  nativecore/originalretry stillpending. NoNGGgamecausalityclaim.
- Debugstartup: old403953/174082 compile49–71s now7.5–13.5ms withcompatible
  cachedartifacts, CPUreplay548ms; remainingnewdrivercompiles dominate.
  Runtime-only4workers/replay1 A/B candidate recorded; unmeasured. Separate
  Vulkanopaque compatibility domain fromSTRICTcompiler/v9journal key needs
  ownsyntheticheader+GPU regressions; no sourcekeywaiver applied.
- Manualinput/autoswipe0; stableimage/~60FPS unresolved. No push/publication.

## Latest checkpoint: cold-cache budget exhausted; same-core warm retry active, 2026-10-12 05:25 UTC

- Firstthree-port051816 ended05:23:20.639 exit0/300s/graceful,noRAMguard,
  minavailable17.07GiB. Four30/90/150/210screensblack; no selectedVS/CS captures.
  Lateframe179 exists inlogs butnotvisiblepixelproof.403953/174082 pipelines
  49–71s drivercachemiss vs03502822–31ms cachehit: unlikecachecomparison
  cannotprovea regression or speedup. Normal sourcekey changed afterports;
  no cachewaiver or arithmetic sanitizer. Coldrun didnotreachpalettecollector.
- SAME sourcef55ffc00/EXEc9d501f3/args warmretry ACTIVE root94033/PID25988:
  `_Build/runs/game-20261012-052352-integration-palette-writer-span-1080-08eb2f`,
  300s/manual/noinputautomation/window1920x1080/max4/selector2:124224.
  Around65–80s more sceneVS/PS/CS and1158drivercompile reached; actualcompleted
  snapshots/readerjoins/renderedscene/internalresolution stillpending.
- UnusedTESTONLY NggVertexCountTests.inc05B8A4A3 drafted separately, noglue,
  production or nativechange. LOOPFREE firstEXECcapture+rawS3, perinvocation
  atomicrecords/independentmaskpopcount; ordinaryVS/native32scope. Do not
  enableopt-in/default before intendedRED. Requireddebt/glue report saved.
- Stablepicture/~60FPS remainsunachieved, notinferredfromstartupframecounter.
  Allsourceinputs frozen untilwarmrunends; keepone native execution slot.

## Latest checkpoint: three-port native original retry active, 2026-10-12 05:19 UTC

- HEADf55ffc00 local-only, committed coreclean/docsWIP. Nativebuild051614 and
  privateinstall051725 PASS; exactEXEc9d501f339c051d409452c42012786f066f2524bc01c5f67ae0eb6a0ce0e3bfe,
  symbols/inputhashes/sourceprovenance preserved inruntime-upstream-ports-1080.
- Actualrun `_Build/runs/game-20261012-051816-integration-palette-writer-span-1080-dd4a83`
  ACTIVE root7611/PID47904,300s/42GiB+8GiBavailableguards, window1920x1080,
  timestamp100/manual/autoswipe0. Dispatchselector2:124224/max4 pereach1158/549;
  originalscreenshot30/90/150/210s planned, buffers forcesdrains/timingperturbation.
  Dryrun051809 didnotexecute. This is not cleanFPS benchmark. Sourcefrozen.
- Threeports RED/GREEN proofspreviouscheckpoint. Stableimage/~60FPS remains
  unproved; actualrender/internalresolution andnewCScoverage/readerjoins
  pending aftercaptures. No new menu/gameplay milestone or externalpost.

## Latest checkpoint: three upstream ports verified; native core rebuild, 2026-10-12 05:17 UTC

- HEADf55ffc00: R8sampling c38f1981; packedprefixloads3ebf42f8; BC-R64f55ffc00.
  Upstreammain730e1a3c selectively integrated, no wholesale merge or push.
  BC publiclayout intendedRED051015 -> sameCPU/GPU051227 GREEN onF6088272;
  BC1/BC7eightlayers/twodirections/fullpadding checked. Broad358cases/264pairs
  051559 PASS onfresh0202862E, including SAME14scratchnumericDWORDs. Updated
  obsolete grown==reused assertion to current scheduler-lived distinct-live
  allocation/spans/capacity/input-exclusion contract; no tilerproductionchange.
- Native corebuild ACTIVE with frozen source; next privateinstall andexact
  provenance/hash, then original300s manual1920x1080 windowed run. Actual
  internal targets recorded separately. WrittenSRDselector2:124224,CaptureMax4
  (up to8CS snapshots) increases chance of actual2097/2098 coverage; does not
  guarantee BADbasejoin. Snapshots force drains, so diagnosticFPS not benchmark.
- Private runtime-upstream-ports-1080 has preserved save/cache copies; runner
  guards source/PE/coreinputs/originaleboot. Generic purefile analyzer ready
  `writer-history-diagnostic-20261012/analyze-cs-snapshots-generic.py`; verified
  preserved035028 evidence only. No new game outcome yet.
- Next upstreamNGGpartial-wave count lead: ordinaryVS wave64/native32 has
  one physical lane/context (not computevirtualhalves); actual2a initialS3
  low8 affects subsequentEXEC construction. LOOPFREE synthetic test-only
  design captures initialEXEC beforeguestwrites andrawS3/VertexIndex; no
  productiondefaultenable before intendedRED. Staticusage is not causality.
- Stableimage/~60FPS stillunachieved. Manualinput/autoswipe0; no external
  milestone publication. Docs WIP persists with unique shared handoffs.

## Latest checkpoint: two upstream fixes committed; BC texture RED next, 2026-10-12 05:10 UTC

- HEAD3ebf42f8 on yotei-integration-native, local-only. R8 SNORM c38f1981
  seven GPU cases passed. Packed prefix loads13e3ca31 adapted to actual local
  memory_offset_count: cost RED050428 -> sameGREEN050649, 0/1/4/5 reads now
  0/1/1/2 for both push and storage. GPU050651 all20 complete byte/sentinel
  readbacks PASS, fresh9933603A. Originalcost fixture unchanged; GPU helper
  needed scheduler.Begin setup after nondispatch baselineguard050433.
- Existing numeric neighbors050700 fail known UNorm8-one oneULP; original
  000321 has identical actual/expected/SRD/prefix. Full100request/4800lane
  measurement050825 exit2, comparison is BIT-IDENTICAL to001759 baseline
  (both stdoutSHAe167dc0e, zerochangedlanes). Precision debt remains explicit;
  do not call full numerical suite green. No measuredFPS gain yet.
- BC-R64 reusable test-only include5B0B1CA7/giant858B8D61 frozen; build050742
  failed Windows small macro collision, not RED. Renamed only fixturevariable.
  Freshgiantbuild ACTIVE, publiclayout RED next before production default.
- Fresh guarded bounded runner prepared in ignored artifacts: window1920x1080
  (removedfullscreen), normal timestamp100, writtenSRD2:124224/MAX1 explicit,
  private copied saves/cache. Corebuild/game stillpending. ATTRIBUTE3bit4 is
  alreadyset: advertised1080 does not explain observed3840x2160 mainRT.
  No generic internal-render override found; measure actualtargets separately.
- Stablepicture/~60FPS stillunachieved; manualinput/autoswipe0. No push or
  external issue milestone. Native execution remains serialized.

## Latest checkpoint: upstream R8 SNORM extracted and GPU verified, 2026-10-12 05:05 UTC

- Native branch HEAD c38f1981: exact upstream7e4e142a R8 SNORM sampling
  admission enabled by default, redundant opt-in removed. Public admission
  RED045239 before correction; build045841 PASS, fresh CFDC2E53 executable.
  Run050238 exit0: native sample/gather, reordered components, linear filtering,
  128-thread wave32/wave64, R8/RG8 scaled neighbors all PASS. CTest registered.
  Original B37B7404 fixture preserved; swizzle neighbor plumbing corrected to
  use actual sampled image view, independent values unchanged. No game/FPS
  claim from format proof; original retry remains pending.
- Fresh main730e1a3c remains comparison source, not a full backend merge.
  Next separate BC-R64 public-layout regression is test-only work. Packed
  prefix-load13e3ca31 candidate has reusable 0/1/4/5 descriptor push/storage
  cost and bounded GPU tests; bindless native build ACTIVE, production emitter
  unchanged pending intended cost RED. Test debt recorded before correction.
- Next original capture uses validated written span selector2:124224 before
  quota consumption, fresh guarded core executable and1920x1080 window.
  Internal guest resolution must be recorded separately. Automatic input off.
  Palette-zero producer coverage, stable image and~60FPS still unresolved.
- No push or new external menu/gameplay milestone publication. Docs/test WIP
  preserved; source inputs for the current native build are frozen.

## Latest checkpoint: fresh upstream audit and first texture regression, 2026-10-12 04:53 UTC

- User asked to extract useful fixes from fresh peepostarexx/KytyPS5/main;
  fetched isolated ref `refs/remotes/peepostarexx/main-review-20261012` at
 730e1a3cf39d50c787d477e813e0bef92006a063, mergebase77d7722173. No branch
  checkout, merge/push or source overwrite. Work continues on native branch.
- DPP8/row_share/F64 work already present; missing scalarzero-count/search and
  signed64GT/GE not used by captured1158/549. Full-wave branch architecture
  differs, so no blind flag activation. NGG/R8SNORM/BC-R64 already partly
  present behind opt-in switches; latest original launch cleared those flags.
  First useful candidate: upstream7e4e142a R8SNORM default sampling admission,
  next separateb7b335ac BC-R64 layout. Staticpacked-offset reuse13e3ca31 is a
  smaller code-cost candidate, not demonstrated FPS gain. Reports saved under
  `upstream-main-review-20261012/`; minimaltests precede behavioral ports.
- SelectorWIP reviewed/frozen; build044652 PASS and three CPU-only modes
 044845/044846/044847 PASS on8323a52b (noGPUsetup). Wrong579024 span does not
  consumeMAX1/frame choice; true124224 admitted once; defaults unchanged.
  Diagnostic committed8995db65; targeted original capture still pending.
- Giant test-only R8 fixtures ported/frozenB37B7404; build045038 PASS,
  gate-unset public format RED045239 on774906dd:Unsupported0 vsFloat1 before
  textureallocation. Native owner now ports exact supported sampling admission;
  sameunchanged GPU GREEN/wave32+64/swizzle/bilinear neighbors pending.
  No claim R8 caused bonezeros or rendered~60FPS.
- Current HEAD8995db65, docs and R8test/port WIP. No active nativeprocess,
  no automaticinput or externalmilestone. FPS target1920×1080; actual guest
  targets stillrecord separately. Stable image/~60FPS remains unachieved.

## Latest checkpoint: first CS spans differ; generic admission filter and 1080p target, 2026-10-12 04:02 UTC

- User explicitly selected1920×1080 as the main FPS benchmark target. Future
  launches set that window target and record actual guest internal/main targets
  separately;3840×2160/3328×1872 are not1080p evidence. Generic existing guest
  display/resolution behavior is being audited read-only; no title-specific
  resolution/frequency hacks or~60FPS claim. Stable picture remains unachieved.
- Original035028 ended03:55:34.785Z graceful0/300s/noRAMguard,min23.21GiB,
  root75998 drained. Actual4screens30/90/150/210s and palette trace preserved;
  late title~2.9–3.3 diagnostic/cold. New first-binding guard does not recur.
- First two CS input/output snapshots are valid but DIFFERENT span:slot2
  stride48/count12063/579024bytes vsBAD2588/124224.1158frame150 base200292ab60,
 549frame153 base200962ab60, bothdisjoint from currentVS BAD200a16d8d0/
  finite2003485440. Flatc4(outputindexbase)11351/10535, flatc5(lastbankflag)0;
  flat17(count)24/396. Lastbank gate disabled; unchanged slot2 is not a proved
  failedstore.549anotherwrittenregionchanges142bytes, proving dispatch effect.
- Purefile37blobrefs/29uniqueblobs checksum/size checks PASS; capturedactual
  globals hw_bounds/nativeFP16true/tablefalse supersede reconstructedCPUflags.
  NativeLocalSize32/256 representslogical64/512 asconfigured. Bloboffset96 is
  artificialreadback-regionorigin, not shaderprefix. IncompleteBDA pagecoverage
  prevents complete replay. Evidence `cs-snapshots-035028-review/` and snapshot
  contract review; no new math/store defect proved from these data.
- Native owner implements OPTIONAL diagnostic writtenbuffer selector
  `KYTY_DISPATCH_SNAPSHOT_WRITTEN_BUFFER=2:124224`, actualSRD declaredsize/written
  predicate BEFORE frame/hash quota consumed and CaptureInputs id/readbacks.
  Default/history/workload unchanged; strictparse+CPU actualSRD/budget boundary
  tests first. This is instrumentation, not a rendering fix; no artificialRED.
  No nativeprocess now; sourcefreeze required before nextbuild. Next capture
  firstmatchingBAD-sized producer spans, then establish index/count/flag coverage
  and actualpostdispatch matrices before fixing outputorlatememoryloss.

## Latest checkpoint: caller fix committed; actual two-CS captures complete, 2026-10-12 03:53 UTC

- HEAD `e19d83fb1b000ac03c1720026ba904ade938e685`, coreclean/docWIP. Local
  caller correction5lines sharedPrepareBindings plus reusable tests. Unchanged
  deferredDF052D1B GREEN034220; rendering/chain034543 and init034544PASS.
  New giant034723/queue034847PASS (1faa2ddb), nativecore034855/install034952PASS.
- Current private EXE `083fa96b217fa7f0511676d5acc298b7548ed5f85317d688e181fe4133666acd`
  in `runtime-palette-writer-cs-v2`, source/provenance/15inputhashes/symbols
  preserved. Run `game-20261012-035028-integration-palette-writer-cs-v2-317cb7`
  ACTIVE root75998/PID30564,300s/manual/autoswipe0, no concurrent nativework.
- Previous nested-command abort no longer blocks capture. Actual first two
  completed input/output snapshots `00001_1158e78fa2c7a20e_f150` and
  `00002_54904fb419d79e49_f153` exist under run/dispatch-snapshots. First
  occurrence/MAX1 perhash; not guaranteed sameBAD palette span. Collectors
  explicitly force drains/perturb timing. Agents inspect actual userdata,
  descriptors/rootoffset/count/flags and output rows; no expectedvalues inferred
  from bare journal/reconstructed flags. Rootkeeps sourcefrozen.
- Observed frame496/title2.9FPS at~130s diagnostic/cold, manynewshaders;
  rendering/NF errors remain. Stable picture/~60FPS NOT achieved. Original
  palette trace/nextscreens/runtime cleanup afterboundedrun remain to check.
  No input automation, push or externalmenu/gameplay milestone publication.

## Latest checkpoint: deferred first binding RED→GREEN, neighbors next, 2026-10-12 03:43 UTC

- HEAD683172a8; five-line shared caller correction in PrepareBindings WIP.
  First FaultBuffer acquisition now happens with Gds/resource preparation
  before rendering/chain/reservation. No guard or bitmap lifetime changes.
  Stable image/~60FPS stillunachieved; no new game after032417 regression.
- Real deferred firstbinding DF052D1B oracle RED033923 on5cf93e43: positive
  routed/probe/planned controls all1, no shaderdispatch/gates, exact nested
  reservation guard321. Same unchanged oracle GREEN034220 on628251d9/exit0,
  build034023PASS. This verifies actual public PrepareBindings→planned
  CommitBindings under asynchronous recorder, not a synthetic getter alone.
- Native owner promotes separate graphics/compute state neighbors; first
  preparation may end previous state because it touches memory. AFTER prepare,
  graphics binding must retain active rendering, compute_chain binding retain
  pending chain. Original init a27c unchanged; no GPU arithmetic claim from
  no-dispatch state fixtures. CTest registration prepared; no nativeprocess now.
- Next bounded neighbors+existinginitchecks, separate callerfix commit, native
  corebuild/install then guarded originaltwoCShash snapshots max1perhash.
  Actualroot396/count/flag/descriptors/input rows and postdispatch palette
  still needed to distinguish input/EXEC resultzeros from later memory loss.

## Latest checkpoint: initialization caller regression blocks CS capture, 2026-10-12 03:25 UTC

- Init completed local683172a8 with valid samea27c GREEN030802; independent
  observation/rebinding/newbatch031437, NF11off/on031438 and quota031439PASS.
  Current giantbuild031659/queue031906PASS on3c378ffa; native core031913/
  privateinstall032310PASS, EXEdd175ab6 in `runtime-palette-writer-cs`.
- Original `game-20261012-032417-integration-palette-writer-cs-a1d6cc`
  ended03:24:23.867Z exit321 after6.38s,noRAMguard,min38.06GiB; stdout/stderr/
  kyty/run saved andstreamsdrained. No palette/CSsnapshots or screenshots yet.
  Regression: lazyGetFaultBuffer Fill records a GPU command inside deferred
  ReserveRecordedCall..CommitRecordedCall. Guard atcommandHooks.cpp647 correctly
  rejects nested packet recording. Deferred descriptor path reserves3542 then
  callsGet3595. Initialization must occur before this transaction; guard stays.
- New synthetic firstfault binding/deferred reservation regression recorded
  FIRST in testdebt; Native owner preparesTESTONLY actualbinding fixture before
  caller correction. Preserve bitmapfirst-use andpending-bit/queue contracts;
  do not revert initialization or fabricate successful payloads.
- No native process remains. Stable picture/~60FPS stillunachieved; previous
 69b/024230 showszero palette/corruptgeometry/3–4FPS diagnostic. Writerdata
  remains needed. CPU549decode031551PASS7844ISA/nohardwareID/clockops but
  reconstructed flags/no currentuserdata or GPUoutput equivalence. Actual549
  outputindex=rootDWORD396+groupX*512+localX; unknown actualoffset/count and
  branchselection prevent an expectedactive2098 result. Rawstore/noformat.
- Next same firstbindingRED→caller fix→GREEN+existinginitneighbors, native
  originaltwoCScapture, then distinguish producerzeros fromlaterloss. No push,
  input automation, externalmilestone or concurrency. Source683coreclean/docWIP.

## Latest checkpoint: unchanged initialization oracle GREEN, neighbors pending, 2026-10-12 03:12 UTC

- HEAD69b620a9/local only; shared initialization correction is WIP, not yet
  committed or retried in game. Stable picture/~60FPS remains unachieved.
  Latest original69b/024230 images remain corrupt with zero active palette.
- Build030624 PASS; unchanged controlled-reuse oraclea27c1e28 run
  `checks/20261012-030802-7513891-fault-bitmap-init-native-green` PASS/exit0
  on freshd10b98cf. Exact reused memory/offset/size, seeded/pinned checks pass;
  poisonRegistered0/currentRegistered1. Same actual parser and first-binding
  producer contract as valid RED025856. No initialization at first parser pass.
- Shared first GPU GetFaultBuffer initializes bitmap once with transfer
  visibility; observational debug-owner enumeration and snapshot size use
  PeekFaultBuffer. No-exposure parser returns because no producer has bound
  the allocation; later parsing retains pending faults. No perframeclear or
  Finish. Two metadata friends/test bootstrap remain test-only; source freeze
  before next native build. Native adds separate repeat-get/new-batch/observation
  neighbors, original oracle unchanged. CTest registration prepared by root.
- No native process currently. Next: neighbor/affected diagnostics checks,
  separate init commit and fresh native core; actual549+1158 snapshots max1
  perhash with original userdata/output. Decode549 exact journal body under
  explicitly reconstructed CPU profile only; this does not supply live input
  or GPU proof. Initial fixed quota/count and EOP proofs remain preserved.

## Latest checkpoint: controlled bitmap initialization RED proved, 2026-10-12 03:00 UTC

- HEAD69b620a9; original acab/024230 rendering remains corrupt,3–4FPS
  diagnostic outcome. Stable image/~60FPS unachieved. No native process now,
  push, automatic input or external milestone. WIP test-only two metadata
  friends/init fixture/bootstrap; native initialization correction starts now.
- Init build025735 PASS. `checks/20261012-025856-5730280-fault-bitmap-init-native-red`
  intended semantic RED on fresh31699757: actualKHR push_descriptor advertised
  and enabled/max32; predecessor/fresh VkDeviceMemory, offset12603456 and
 12582912-byte size MATCH, pinned/seeded/exactReuse all1. After first real
  binding, independent positive-page atomicOr producer and actual parser,
  poisonRegistered1/currentRegistered1. Drain/unmap/dispose precedes assertion.
  New fault is present but old allocation bit is incorrectly treated as current.
  Frozen oraclea27c1e28 unchanged; no production init correction beforeRED.
- Earlier025236 compiled but was NOT executed with missing standalone
  extension bootstrap; it is not a semantic result. Native mode-specific
  device extension query/limit/enable fixes test setup, other modes unchanged.
- Required correction: initialize transport before first GPU writer/binding,
  once per allocation, with transfer-to-shader read/write visibility. Inspect
  observational statistics getter calls and recording lifecycle; never clear
  on first parser pass because that would lose genuine new requests. Same
  unchanged controlled-reuse oracle GREEN and preservation neighbors pending.
- Real palette writer join remains1158 then549 slot2; source shapes contain
  raw matrix-shaped DWORDX4 stores, no proved conversion or half-mapping bug.
  Latest frame343/344 usesGuestGpu counter, reader369/370 usespresented counter:
  not26-frame-age evidence. Reliable CPU sequence/submit order only, no reader
  tick or GPU-completion proof. Next targeted two-hash input/output snapshots
  max1 perhash initially, with synchronization perturbation explicitly recorded.

## Latest checkpoint: current image still corrupt; actual palette writers identified, 2026-10-12 02:54 UTC

- HEAD69b620a9/local only. Original committed source69b with private EXE
  `acab5cf96871f57c9b4d0c313b6c38045aac904bf4cd14120a0b78c819237081`:
  run `game-20261012-024230-integration-fault-batches-a944b2` ended02:47:35.377Z
  graceful0 after300s/noRAMguard,min23.21GiB. Root5508 drained. Native core
 023834/install023934 PASS; current queue023826/scheduler023829 PASS on411b5774.
- Four actual captures30/90/150/210s preserved. Visually inspected90s: missing
  world/grey surfaces;210s: stretched triangles across image. Titles3.2–3.5FPS
  late in diagnostic/cold run,7.7startup. Stable picture/~60FPS NOT achieved;
  no performance improvement/regression inferred from unlike cache/scenes.
  Pad log stays neutral; no input automation or menu/gameplay publication.
- Same216751-word live2a SPV/map: BADtoken1/frame369/base2009efdf40 has zero
  six active palette rows2098/2097, RSQ(0)=Inf and NaNPosition. FINITEtoken2/
  frame370/base200334b6e0 has sameindices/nonzero rows/noNF. Quota fix does
  not by itself correct this rendering defect. History identifies prior exact
 124224-byte declared writes in BOTH: latestCS54904fb419d79e49 slot2,
  earlier1158e78fa2c7a20e slot2, direct1×1×1.28matches/8saved/truncated;
  eviction and CPU-order scope explicit. Different writer/reader nativehandles
  occur inBOTH, so this does not prove lost merge data or actual written values.
- Complete new original v9 journal:1881records/11052085bytes, allXXH3/tail
  checks pass in pure file analysis. Original bodies205/226 extracted with
  exact hashes under `writer-history-diagnostic-20261012/writer-code-024230/`.
 1158local64×1×1/LDS512 and549local512×1×1/LDS3200, bothwave64/host32;
  byteidentical earlier actual bodies. Live userdata/output still needs a
  targeted dispatch snapshot; no fabricated reconstruction is runtime proof.
- Source WIP now TEST-ONLY allocation metadata friends plus init fixture,
  no bitmap initialization behavior fix. Build025236 PASS, NOT executed:
  actual parser requires enabledKHR push descriptors, missing from standalone
  harness. Native owner adds mode-specific advertised/max2 extension bootstrap,
  then freezes. Controlled old-poison/new-real-page init RED remains pending.
- Next: bounded init regression without first-parser work dropping; current
  actual549/1158 input/output snapshots and wave/dirty-ownership diagnosis.
  Accurate source4 prototype is saved ignored/CPU-only; native conversion,
  calibration/ordering and real FPS optimization remain pending. No active
  native process currently; serialize next build/test/game and preserve WIP.

## Latest checkpoint: quota fix and neighbors pass; fixes committed locally, 2026-10-12 02:37 UTC

- Active branch `yotei-integration-native`, HEAD `69b620a9`; production source
  clean, docs WIP only. Local completed commits:8c6c9019 literal ordered EOP,
  5247bd14 bounded diagnostic writer history,69b620a9 lossless bounded BDA
  fault batches. No push or external menu/gameplay milestone. Stable picture
  and~60FPS NOT achieved; current candidate original retry still pending.
- Unchanged frozen726232c5 protocol oracle RED022201 -> GREEN022836/exit0
  on8b45438f: count1023/1023/3, retained pending1026/3/0, all2049 unique bits
  accounted for and guard bytes intact. Shared atomicExchange/CAS/residual
  atomicOr mechanism, CPU count precondition and explicit output HOST_READ
  dependency; no limit increase, lost work or page0 acceptance. Initial bitmap
  initialization is deliberately separate unproved regression debt.
- Build023327 PASS, EXE4c9db263. Quota neighbors023505 PASS/exit0:0/1/31/32/
 1023/1024, first positive/last represented/sparse-edge bits. EOP registered/
  native-consumer/IRQ/repeated-write/compute32 neighbors023506 still PASS.
  Input barrier queue families explicitly IGNORE; original oracle unchanged.
- Original candidate histories remain020757 (faultcallback fatal before
  palette capture) and021738 (60s nofatal, black/loading screenshots, no target
  palette token). Quota defect is proved, original exact causal join unproved.
  Do not present startup16–30FPS or cached pipeline success as gameplay.
- Root giant affected target build ACTIVE session41495, source69b620a9/frozen;
  no other native execution. Next run queue/scheduler once on new transport,
  build/install current private native core,300s original retry/screenshots
  and current palette-writer join. Old GC fixture mismatch and accurate source4
  remain explicit pending; no full numerical/cross-game suite claim.

## Latest checkpoint: bounded fault-parser quota regression proved, 2026-10-12 02:22 UTC

- TEST-ONLY actual production parser SPV build022056 PASS, fresh EXE
  `65577444f5c28f38defc9b5db80d35d9ee444cc7c97e7aea64c4962bc0e8da99`.
  `checks/20261012-022201-8595207-fault-buffer-capacity-native-red` intended
  semantic RED:2049 unique synthetic bits, pass0 count1087 despite1023 slots,
  exported1023/pending0/guards preserved;1026 requests lost. Bounded2 groups
  with every invocation/input in range; cleanup completed before assertion.
  Frozen test SHA726232c5; no production correction existed for this RED.
- Native owner now corrects shared saturating reservation and preservation of
  unexported bits, keeps CPU count/address validation and native barriers. Same
  oracle GREEN and quota neighbors required before new native original retry.
  No game/hash-specific logic, enlarged limits, dropped work or zero resources.
- Second original021738 ended02:18:42.375Z graceful0 at60s/noRAMguard,
  min29.5GiB free; root1767 drained. Existing BDA diagnostic first200 rows show
  small batches (e.g24/2), no captured overflow/fatal repeat. Four screenshots
 10/20/30/40s: black screen, then loading spinner. Startup titles28–30FPS and
  later~16FPS are not steady gameplay/60FPS proof. No selected palette token;
  zero-palette producer and exact original overflow causality remain unproved.
- All native processes ended; source inputs frozen except native owner's new
  parser correction following RED. No external milestone or push. Stable
  picture/~60FPS unachieved; root continues original retry after focused proof.

## Latest checkpoint: original retry faults in BDA batch callback, 2026-10-12 02:16 UTC

- Original020757 ended02:08:27.687Z, exit321 after29.66s, no timeout/RAMguard,
  min29.31GiB free; root70196 drained. Early frame200/title9FPS was startup
  only. Abort nearframe292 precedes requested palette trace and60s screenshot:
  no trace/tokenJSON or image captured, no stable picture/~60FPS proof.
- Fatal cached range0+8000/buffer1074; last-create request is logged AFTER
  alignment and does not reveal original byte range. Exact frozen coreEXE/
  matching PE/PDB GUID-age/link map proves FaultManager::ProcessFaultBuffer
  deferred lambda -> FindBuffer -> CreateBuffer, invoked by pending operations
  during DispatchIndirect. No direct WriteOrderedMemory/EOP frame in chain.
  Evidence `writer-history-diagnostic-20261012/failed-run-020757-readonly/stack-identity/`.
- Actual fault count/bitmap not yet captured; page0 origin and EOP causal
  involvement unknown. Concrete independent fault quota lead: GPU parser
  increments before limit, clears word before exhausted return; CPU trusts
  count. Required actual parser SPV bounded overflow/pending-bit regression
  recorded FIRST in test debt; native owner prepares TEST-ONLY fixture before
  correction. Range/count checks stay intact, no zero resources/work dropping.
- Literal focused/registered/native-consumer/IRQ and affected queue/scheduler/
  ring/range tests remain verified. Broader GC failure is an old fixture with
  two-range stride assumption despite current widened readback covering four
  ranges; no EOP call and unchanged RecordReadback/Download source. Actual
  reservation offset/atom not printed; no production GC defect established.
- Next: capture raw BDA count using existing diagnostic switch if useful,
  get intended bounded parser-protocol RED, fix shared saturation/retained
  bits after proof, then retry original/screenshots and palette-writer join.
  Accurate source4 and clean warm FPS remain separate pending work. No push,
  external milestone, automated input or concurrent native execution.

## Latest checkpoint: affected queue checks pass; original retry active, 2026-10-12 02:08 UTC

- Native original run `game-20261012-020757-integration-eop-history-53d889`
  ACTIVE, root session70196/PID39156,300s/manual/autoswipe0/privateEXEfc961019.
  Same palette125/NF trace plus bounded writer history; captures60/120 planned.
  Early title frame200/9.0FPS is startup evidence only; stable image/~60FPS
  NOT achieved. Do not run another native build/test/game until this ends.
- Giant test build020552 PASS, fresh EXE
  `6e16602f2d91179505e53fa771b53daac58c42b2cf5d0ef0b7ba649aa1169eed`.
  Queue020652 PASS including nonblocking parser, mapped EOP publication,
  exact literals, IRQ routing, polling/wait/rewind/CE and shutdown cleanup.
  Accurate source4 stage is deliberately still tested separately as pending;
  routine queue clock scope is broad preparse-to-postFinish publication only.
- Scheduler020709, stream-ring020710 and buffer ranges020711 PASS/exit0.
  Broader GC020713 fails adjacent download reservation stride expectation;
  native owner is diagnosing unchanged old mechanism/fixture, no correction
  or full suite claim. Native test fixture restoration is test-only: actual
  original vector definitions restored from581dad7d/0d0b215f/b8946fef,
  not FP64 production semantics; benchmark uses current indirect_descriptor.
- Next after bounded original completion: exact current palette/writer join,
  visual captures, neutral-pad and process cleanup audit; commit completed
  literal fix separately from diagnostic/test-plumbing changes and record
  actual rendering/FPS outcome. Preserve failed original inputs and debt.

## Latest checkpoint: native candidate ready; queue-test API restoration pending, 2026-10-12 02:04 UTC

- HEAD `b50bb1c8`, production literal fix and opt-in diagnostic remain WIP;
  all source owners frozen. Stable picture/~60 FPS NOT achieved. No native
  process or game currently running; no input automation, push or publication.
- Writer-history fixed32768-entry CPU ring joins admitted current reader SRDs
  to up-to8 preceding declared CS/PM4 writers per range, with CPU cutoff,
  eviction/truncation/native-identity limitations. No guest loads/readbacks or
  execution semantics changed. Target build015641 PASS, boundary/cutoff/
  relocation/wrap check015700 PASS, EXE `c7117b6d7fc3d8265ffc68407ff850535662b2ba8191ce9481e851e0e716a1cd`.
- Native core015853 and private install020135 PASS. Candidate EXE
  `fc96101919ca77f2455cf1ad675d9e5ed6a2db0f0a4b2f22b737d0575b3aa2a8`
  in `runtime-eop-history`; frozen tracked diff plus explicit10-file hashes
  cover new untracked writerHistory.h. Source/hash-guarded diagnostic and clean
  runners prepared;020228 was DryRun only. Original retry not yet executed.
- Existing giant affected target failed build015715 due stale test APIs
  (distinct sampled mip expectation field, mandatory actual image-buffer
  capacity); fixes are test-only. Next015942 failed missing actual synthetic
  vector fixture definitions and removed DescriptorSource benchmark member.
  Native owner restores original definitions from581dad7d, without FP64 core
  port or zero stubs. These are build limitations, not valid emulator RED.
  Queue/nonblocking/literal/IRQ checks remain pending until target builds.
- Standalone clock capability probe `clock-audit/runs/20261012T0156047541417Z-abfb455d`
  build/run PASS; EXE5cbca9d9. Actual RTX5060Ti supports both calibrated aliases,
  DEVICE+QPC domains, family0 64 validbits/1ns period, int64. KHR-enabled samples
  have2.752–2.816us reported max deviation and QPC10MHz anchors within observed
  CPU brackets. No GPU commands, guest KernelTSC link or implemented clock
  bridge; do not treat raw device epoch as guest100MHz. Source4 debt remains.
- Next: finish minimal affected-test restoration and run queue/scheduler/ring
  checks, then original bounded writer-history retry/screenshots; identify
  current zero-palette producer. Preserve independent rendering and clean warm
  FPS acceptance checks. Literal tests and existing pixel neighbors already pass.

## Latest checkpoint: literal EOP GPU regression and neighbors pass, 2026-10-12 01:52 UTC

- Shared literal correction remains uncommitted on HEAD `b50bb1c8` pending
  affected queue tests. Stable picture/~60 FPS and original retry still pending.
  No task native process remains. No push, input automation or issue publication.
- Focused `checks/20261012-014742-5354561-eop-literal-cleanup-green` PASS/exit0,
  EXE `9582a189dbb87e6c9aed7626f70bf0ec63314607f76641d79fc6d2940c2f2275`:
  unchanged literal semantic goldens now pass with correct renderer mapping
  setup/teardown and explicit priority-publication drain. Original two-case
  `cb1888cb` oracle preserved; accurate source4 remains separate pending debt.
- Build014928 PASS, fresh test EXE
  `795795a7be1b1b0ce7f1956ef6fc58649f3ce5d58780f48b3fc780f108c2b37c`.
  `checks/20261012-015005-5127285-eop-literal-neighbors-registered` PASS/exit0:
  registered clean64 plus same-command-buffer native consumer and real IRQ
  backing observer; prior GPU-dirty label with repeated writes, preserved newer
  dirty ownership and both native values; real bounded compute producer before
  public32 CS_DONE with exact-width result. All native work and mappings disposed.
  Prior014749 failed registration setup, not data semantics; fixture now uses
  actual FindBuffer/SynchronizeBuffersInRange rather than read-only stream upload.
- `checks/20261012-015045-6065118-eop-bindless-retirement-neighbors` PASS/exit0,
  actual red/green/blue pixel retirement neighbors on the same new executable.
  Existing unrefreshed resource_tracking EXE8acd47fc check014814 fails unrelated
  indirect-image numeric-class invariant; do not claim full suite pass.
- Root registered two bounded literal cases in CTest. Native owner is replacing
  old queue fixture host-stack EOP labels and immediate-value expectations with
  tracked guest labels and post-completion values while preserving nonblocking
  parser/submission checks. Writer-history diagnostic implementation is active
  in separate owned files; no build while inputs are being edited.
- Private `runtime-eop-history` SaveData/cache copy and source/hash-guarded runner
  prepared, no game executed. Next: freeze both owners, run focused affected
  queue tests, commit completed literal fix, build private native core and retry
  original with bounded current-reader/earlier-writer attribution. EOP/palette
  causality and performance gain remain unproved.

## Latest checkpoint: literal EOP ordering reproduced; candidate cleanup pending, 2026-10-12 01:45 UTC

- Branch `yotei-integration-native`, HEAD `b50bb1c8`; literal EOP source/test
  correction is uncommitted. No push or external milestone update. Stable
  picture/~60 FPS remains unachieved; last clean original run reached 4.7 FPS
  with missing/covering geometry. All native processes from the checks below ended.
- Unfixed native regression `checks/20261012-012308-3986084-eop64-publication-nofault-native-red`
  proves actual public64 BOTTOM literal publication before submission: backing
  already holds `1122334455667788` while native producer tick is pending and
  unchanged. Producer completes before assertion/cleanup. Timestamp source4 is
  also sampled before its required completion stage. Original frozen oracle
  SHA `cb1888cbade113045534d857fcda854703daacf9f825804bb4af14f352bde45b` preserved.
- Candidate uses shared BufferCache queue-ordered staging copy, tracked backing
  publication and existing owner/token retirement before IRQ. No title/hash
  guards, direct deferred guest-VA memcpy or global GPU drain. Source4 still
  uses the old parser-time sample and is explicitly not corrected by this work.
- Build `checks/20261012-014334-8744062-build-target` PASS, fresh test EXE
  SHA `e50b368e31999271795b0909d5daa92fea286a6f568e65bd0426b7a6d71bc9dc`.
  Focused `checks/20261012-014416-7029249-eop-literal-first-green` observes
  sentinel before submit, exact literal after Finish, completed real producer.
  Exit -1073741787: PageManager live-state destruction during fixture teardown;
  this is NOT yet a passing test. Native owner is correcting fixture cleanup
  without changing semantic expectations; registered/GPU-consumer/IRQ/repeated
  write/actual compute32 neighbors follow before commit and original retry.
- Zero palette producer remains unidentified. Existing write-watch diagnostics
  are the next low-cost attribution tool; EOP involvement in zero palette or
  poor FPS is unproved. Accurate source4 requires native stage timestamps and
  a calibrated clock bridge; proposal is saved in `clock-audit/`, capability
  probe and synthetic regression remain pending and separate from graphics proof.

## Latest checkpoint: valid palette loads return zero; EOP regression next, 2026-10-12 01:11 UTC

- Sourceb50bb1c8/native409065e9 remains current; no new production fix after
  the shared code-cost optimization and bounded diagnostic commits. Stable
  image/~60FPS still NOT achieved (clean late frame1099/title4.7FPS).
- Original palette005519 ended01:00:24.716Z exit0/graceful300s/noRAMguard,
  min21.84GiB. Root42514 drained; no emulator/test/build process remains.
  Neutral pad log; screenshot60s(frame294/coldblack) and120s(frame528/3.9FPS)
  preserved. No visual menu/gameplay milestone or automatic input.
- Actual SAME216751-word SPIR-V (SHA461bca6b16b790cd94afd5b8bcdea206ddf3ab8329d51a75d62ea3f501d0beec)
  selected125: BADtoken1 has six active raw palette rows/24 DWORDs allZERO,
  valid indices2098/2097, stride48/count2588/range124224, offsets0/16/32.
  FINITEtoken2 has sameindices but different palette base/native handle and
  finite nonzero matrices. Inactive indicesffffffff returnzero inBOTHdraws
  withzero weights. No missingPC/truncation. BADzero-length basis ->correct
  RSQ(0)=Inf ->NaNPosition; finite nextdraw has noNF and finitePosition.
- BAD actual guest base0x20032508c0; active rows0x2003269220/2230/2240 and
  0x20032691f0/9200/9210 fit declared span. Native views also contain these
  offsets. Zero return does NOT by itself prove expected nonzero source
  contents, a descriptor/producer/coherence defect or improper game initialization.
  Evidence `vertex-value-trace-stage1-20261011/live-005519-palette-review/`.
- Concrete shared source lead: EOP direct CPU memcpy before native completion;
  source4 timestamp is sampled during parsing, Sync records metadata/IRQ.
  Ordinary label pages are unprotected; GPU-dirty palette pages may still
  wait through fault/readback tracking. EOP involvement in the zero palette
  is NOT proved. A naive deferred memcpy can fault in completion callbacks;
  registered-label/GPU-consumer/IRQ neighbors are required for any correction.
- Primary PAL BOTTOM=0x28, CS_DONE=0x2f; do NOT call2f a bottom-of-pipe event.
  Public64 normalizes supportedBOTTOM index0; actual raw packet usesindex5.
  Public32 currently normalizes toCS_DONE/6. Initial test must use legal
  64BOTTOM with a queued unsubmitted native producer, or real rawBOTTOM
  packet. A transfer-only producer does not prove CS_DONE ordering.
- Next authorized TEST-ONLY native EOP64 literal/source4 ordering regression,
  no source correction until intended RED. Drain/dispose before assertions.
  Native agent owns fixture, Sharp primary/source audit only. Separate raw
  vector repeated-address cost fixture stays an ignored draft. Root executes
  serialized native checks. No source limits/sync/clock/resolution hacks.

## Latest checkpoint: zero-length basis identified; palette capture active, 2026-10-12 00:56 UTC

- Engine commitsde9a89cf (shared conversion cost) andb50bb1c8 (compact bounded
  diagnostics) are local. Native core003500/install003626 PASS, private EXE
 409065e99e608237a188e50acfcd29e05cdf9a75b0962dae0ab4748d3e4ccf46.
  Core source is clean; only plan/debt documentation remains WIP. No push.
- CompactNF GPU003151 fresh99081740 passes all11 off/on cases. Actual2a
  reconstructed-profile CPU003359 validates215142words instead of unused
  inline1942548; actual original live module is215292words. Build002923's
  enum spelling error was corrected before successful003112. Stale003029
  ran old23850 and is explicitly INVALID as compact-v2 proof inrun.json.
- OriginalNF003728 ended00:41:33.266Z exit0/graceful240s/noRAMguard,min26.61.
  Actual token1/frame292 vertex125: first recorded invalid expression is
  %2456 FPRecipSqrt32(%2455=+0) ->+Inf, ordinal2456/block12/map378cec32bd6e6bf4.
  Pre/post Position both7fffffff×4. Token2 has no invalid event and finite
  Position; exact liveSPV matches. RSQ(0)->Inf is CORRECT ISA behavior, not
  a reason to sanitize arithmetic. The squared-length input comes from a
  zero accumulated bone basis; palette values/indices are the next lead.
  Export predicate%21 is observedtrue for this invocation; static ancestry
  with its false arms excluded exists, other Phi/branch choices untraced.
- Clean original004532 disablesCHECK/NF/trace/snapshots/zones/drain stats;
  same EXE/manual/eager1/keybounds1/chain0/defaultformats,300s. End00:50:37.609Z
  exit0/graceful/noRAMguard,min22.22. Lateframe1099/title4.7FPS, image has
  character but missing background/covering geometry still visible. No
  demonstrated FPS gain; initial pipelines were cold, repeat warm measurement
  remains pending. Neutral pad log, no automatic input. Captures60s/120s/late
  preserved; NF003728 manual captures287/293 were black during cold startup.
- Same-profile top3 compute CPU004339/40/41 PASS:824238/783994/790817words
  ->796797/764833/771656, only~2.4–3.3% reduction for these reconstructions.
  Do not extend the synthetic32-load23.89% result to whole-game performance.
- ACTIVE original palette005519: PID46528/root42514,300s boundto01:00:19UTC,
  `_Build/runs/game-20261012-005519-integration-palette-trace-4a8b2d`.
  SAME EXE/sourceb50, selected125/instance0/NF2/frame0,16-PC selector
  980..9d8(raw12palette loads)+90c/914/10c/1dc. No new source or cache waiver.
  Exactly copied SRDs and returned row bits/indices/offsets will distinguish
  addressing/bounds, contents and initialization leads. Root alone executes;
  no native build/audit/test may overlap this game. Other agents read-only.
- Stable picture and approximately60FPS remain NOT achieved. Next verified
  semantic defect must first get an independent synthetic RED. Separate
  proposed raw-vector address cost regression and clock-unit audit are leads,
  not source corrections. No new issue/menu/gameplay milestone is authorized.

## Latest checkpoint: shared conversion code validated; compact diagnostic pending, 2026-10-12 00:28 UTC

- Local completed optimizationde9a89cf shares the existing generic formatted
  conversion body; address/bounds/EXEC/dst_sel/D16 remain at callers. Cost
  RED000048 -> SAME GREEN001747 retains all1/8/32 live loads/four outputs:
  words3349/13568/48632 ->3140/10783/37015, normalizationFDiv8/64/256 ->2/2/2.
  The32-load synthetic module is23.89% smaller; no game FPS improvement claimed.
- Actual baseline001124 vs candidate001759:100 requests/4800 lanes/25 cases,
  no missing/extra/changed results. Entire measured outputs are byte-identical
  SHAe167dc0e9bfebed2b9374a2eb1e0f14d1f714aaa7e2997f5a34dd25e6b165068.
  Both measurements exit2 for the SAME144 active UNorm8 fullscale discrepancies;
  strict numerical suite is NOT a pass, its oracle is unchanged. All24 other
  cases and inactive lanes match independent goldens. Precision debt remains.
- New diagnostic actual GPU001806 passes NaN/Inf/finite/output controls before
  a fixture expectation fails: legal SinkExecSelects makes inactive load0 ->
  MUL0 -> SQRT0, so no invalid expression is expected. Fixture is corrected
  to a finite inactive-load control (ED816263), old input preserved; this is
  setup correction, NOT a production semantic RED. Native rerun pending.
- Actual2a reconstructed-profile CPU002120 validates NFv1 but emits1942548
  words, roughly27x the earlier diagnostic module. Do NOT launch that version.
  NFv2 uses one shared bounded writer, caller invalid predicate, unchanged
  slot23/header9/6272bytes and explicit unknownEXEC/IR-map scope. Source WIP
  needs native GPU protocol and actual2a CPU size/validation before original retry.
- Native tests23850a04, auditor7866b795; compiled artifacts preserve exact
  source WIP at their times. Original235345 remains latest game, defective
  picture/title4.1FPS; clean warm223508 was5.1FPS. Stable image/~60FPS remain
  NOT achieved. Root executes alone; no active game, no autoswipe/push/issue post.
- Proof `_Build/analysis/yotei-integration-20261011/generic-buffer-output-comparison/bodyreuse-report.json`,
  `generic-buffer-baseline-immutable-20261012T0013051540004Z/`, checks001747,
  001759,001806 and002120. Next validate compactNF, build private native
  runtime, bounded bad-vertex retry, then clean warm performance measurement.

## Latest checkpoint: conversion cost RED; first-invalid trace in progress, 2026-10-12 00:08 UTC

- SourceHEADa1de46a5, branchyotei-integration-native; original235345 completed
  with an exact bad vertex125 capture as described below. Stable picture and
  approximately60FPS still NOT achieved. No native job currently active.
- TEST-ONLY generic-buffer fixture build235825 PASS. CPU/public-SPIR-V cost
  run000048 on54c1a177 validates actual1/8/32 live XYZW loads and all outputs:
 3349/13568/48632words,8/64/256 static normalizationFDiv. The predeclared
 1536word/read growth budget already passes; normalization budget<=16 fails
  as intended. This is code-cost RED, not an emulated ISA correctness defect
  or an original-game speed measurement. Memory.cpp remains unchanged.
- Actual GPU numerical baseline000006 and diagnostic-output-only rerun000321
  pass eight integer/scaled controls then fail strict UNorm8-fullscale:
  actual3f7fffff vs ideal3f800000. Updated EXEf257ff91, no timeout/hang/reset.
  Vulkan permits approximate FDiv; guest formatted-conversion endpoint
  precision is still unverified. Do NOT call this semantic RED or loosen the
  frozen strict oracle. Complete measurement of all unchanged cases next;
  keep any pre-existing precision discrepancy separate from body reuse.
- Diagnostic-only first-invalid scalarF32 hook is being implemented for the
  selected exact draw: actual result/typed operands, content-addressed emitted
  IR map and explicit unknown guestEXEC provenance. Preserve the fixed6272
  byte trace allocation; reserve slot23 for first invalid intermediate while
  images24..29 and pre/post30/31 remain bounded. No guest loads or output
  substitutions. Actual GPU protocol proof is required before original retry.
- Evidence `_Build/analysis/yotei-integration-20261011/checks/20261012-000048-5009848-generic-buffer-codegen-cost-red`,
  `20261012-000321-9148760-generic-buffer-numeric-baseline-detail`, and
  `generic-buffer-codegen-oracle-20261011T2359070566596Z/`. WIP test/diagnostic
  source edits are unvalidated until their native runs; root retains sole
  serialized execution, autoswipeOFF, no push/public milestone.

## Latest checkpoint: exact bad vertex captured, 2026-10-11 23:57 UTC

- Stable picture/~60FPS remain NOT achieved. Current manual screenshot
  `game-20261011-235345-integration-trace-joined-aa0568/window-joined-1.png`
  shows missing geometry/floating objects atframe560, title4.1FPS. It is a
  diagnostic run, not a clean performance measurement. Clean warm223508 was5.1FPS.
- Local45f855be copies decoded clip/rawVGT-wave settings and joins immutable
  legacy trace tokens. Locala1de46a5 clears optional legacy identity words9..12
  on every first record writer; FaultManager only clears counters, so unused
  words previously could retain stale identity. These are diagnostics, not
  claimed guest rendering fixes. Same six actual GPU protocol cases235212
  PASS on4d094e6d; no dedicated legacy-fault overwrite GPU case is claimed.
- Native configure235124/build235223/install235342 PASS. Private EXE
  12f57773204470f5152e951c95ad79f20f9ea35818db59760f2ba74180a585f5,
  sourcea1de46a5, unpinned computed cache identity; all old artifacts retained.
  Original235345 selected125/instance0 fromframe0,180s, explicit sync pipelines,
  eager1/keybounds1/chain0/manual/autoswipe0. End23:56:50.259Z exit0/gracefulbound,
  no RAM guard/min25.27GiB; root96926 drained, no native process remains.
- Token1 draw#19478/frame296 yields vertex125 pre AND post Position
  (7fffffff,7fffffff,7fffffff,7fffffff), all NaNs. Legacy fault independently
  joins the same token/draw, first writer109/hits178 atreadback297. Token2
  selected125 is finite. All seven recorded ordinary-load payloads and all
  three sample payloads match bit-for-bit between these two calls. Typed
  SInt70 negative values are integers, not float-NaN loads; selected reads
  are in range. Missing scalar/palette/SRT reads and arithmetic remain unobserved.
- Actual selected draw has both min/max Z planes ENABLED, DX clip space,
  effective native depthClipEnabletrue/depthClamptrue; rawGSwaveflag64 agrees
  with input/compiledwave64 onhost32. One-sided-plane gap is separate debt,
  not demonstrated as the cause here. No blanket NaN/Inf arithmetic repair.
- Next: locate first invalid intermediate/unrecorded reads with bounded
  diagnostics on the now-reproduced bad invocation. Parallel generic-buffer
  code-cost/numerical fixture remains test-only draft pending native RED.
  Root alone owns serialized native execution. No automatic input, push,
  issue milestone, stable picture or60FPS claim.

## Latest checkpoint: exact draw tracing works; visual defect remains, 2026-10-11 23:44 UTC

- User objective remains stable picture and approximately60FPS. It is NOT
  achieved: clean warm-cache run223508 observed5.1FPS; screenshot from233156
  still shows large stretched covering surfaces. No new public milestone.
- Native sourcea502617d, private EXEd37be5d75c5cebe4dd20f2f8c45a54efdd0c98f9033d732bccb28cb69e9a0cb8.
  Original bounded manual runs233156 (vertex14/frame298,300s) and233855
  (vertex0/frame0,240s) both ended gracefully with exit0 after their bounds,
  no RAM guard. All native processes are drained. Automatic pad input wasOFF;
  captured pad logs were neutral. No build/test overlapped either game.
- Actual GPU/host traces admit two immutable draws each, twelve events/draw,
  ready pre/post exports, no truncation. Selected14 and0 both export finite
  positions; all three selected volume samples have finite coordinates/red+0.
  Actual SPIR-V and copied draw/resource descriptions are saved beside JSON.
  Missing load PCs, inferred image-use predicates and uncaptured half1 remain
  explicit limits. These traces do NOT reproduce the bad vertex themselves.
- In233855 the first2a nonfinite record names vertex125/readbackframe300,
  unlike the earlier run's vertex0. Its heuristic lastdraw is insufficient
  attribution. Next diagnostic: expose already-written legacy trace-token
  words11/12 and copied decoded clip/stage flags; join GPU fault to exact CPU
  draw before selecting another vertex or changing arithmetic/clipping.
  Investigate bounded first-nonfinite capture to avoid chasing changing IDs.
- Evidence: `_Build/runs/game-20261011-233156-integration-vertex-value-trace-9a4dc2`,
  `_Build/runs/game-20261011-233855-integration-vertex-value-trace-v0-338bd3`,
  `_Build/analysis/yotei-integration-20261011/vertex-value-trace-stage1-20261011/live-233156-review/`
  and `live-233855-v0-review/`. Metadata changes pending validation are
  diagnostic only; no clipping fix or performance improvement is claimed.
- Parallel next work: independent generic-buffer codegen cost/numerical
  regression design for top compute shaders. Root retains the sole native
  execution slot. Resolution clarification remains unanswered; provisional
  performance target1080p, with guest internal render size recorded separately.

## Latest checkpoint: trace GPU protocol and selected shader validation pass, 2026-10-11 23:27 UTC

- GraphicsCLAMP correction is local699ececf, portable numerical RED231105 ->
  unchangedGREEN231751. Stable image/~60FPS still unachieved; no new runtime
  claim follows from that correction. All prior code/artifacts preserved.
- Native build232153 and real RTX5060Ti protocol232328 pass on EXE
  da3209491f4f574bf787c904f4892537b840a775651364c175441cbfa9d68851:
  six loop-free single-point draws compare trace-off/on payload/raw/high,
  token0/wrongvertex/directEXEC0 filters, two independent token slots through
  a reused pipeline, exact load index1/result11223344, distinct pre/postclip
  (0,0,0,1)/(+.5,-.5,0,1) and boundary sentinels. SPIR-V validates before
  execution; exit0/no timeout. This does not cover image predicate semantics,
  host admission/Rebind/FaultManager transport, cap overflow or virtualhalf1.
- Native worker build232533 and current2a818 trace-profile232704 also pass
  through SPIR-V validation (71688words). Original body preserved; selected
  live samples/loads instrumented without output substitution. Profile remains
  reconstructed; no materialization/live-module/GPU equivalence is claimed.
- Trace scope/labels are restricted: original decodedD3/LZ/red/32-bit plainXYZ
  only; original and effective dimensions both retained (2D placeholders
  allowed). Other shapes skip explicitly. Selected-use image filters do not
  prove guestEXEC, absent events do not prove inactivity. Exactly two immutable
  tokens/6272 appended bytes, no guestSRT/ObjectID overwrite, trace driver and
  journal names/signatures separate from normal caches.
- Next: native core build into new private runtime, explicit synchronous
  pipelines/manual input, bounded original retry and actual per-draw trace
  readback. Verify live pre/post position and sample/address results before
  further semantic corrections. Root owns all execution; no active native job.

Native continuation23:32UTC: trace source/test commit e4f586c0, test-only line
ending normalization a502617d (test semantic contents unchanged). Native core
configure232822/build232907/install233147 pass. New private
runtime-vertex-value-trace EXEd37be5d75c5cebe4dd20f2f8c45a54efdd0c98f9033d732bccb28cb69e9a0cb8,
sourcea502617d clean core. Original300s retry233156 ACTIVE PID10824/root
session33270, `_Build/runs/game-20261011-233156-integration-vertex-value-trace-9a4dc2`.
Explicit async-pipelinesfalse, eager1/keybounds1/chain0/manual/autoswipe0,
CHECK1 plus2a14/instance0 selected-PC trace fromframe298. No snapshots or
layout/provenance statistics this retry. Trace-specific caches/journal and
per-draw artifacts live in private runtime/run. Native slot occupied; no
build/test may overlap. Current visual/value outcome pending, noFPS claim.

## Latest checkpoint: instruction CLAMP portable GREEN; trace GPU validation next, 2026-10-11 23:22 UTC

- Intended CPU/SPIR-V numerical RED231105 on EXE1157b225: all10 finite and
  unclamped controls pass, but clamped qNaN exports7fc12345 instead of+0 under
  a Khronos-permitted NaN-propagating FClamp choice. Native build231607 and
  SAME oracle231751 GREEN all14 cases (EXE9c62ecbda2bd257c0af985d4b17cd4fba6ec25aab02ab30a8c974769e2b05a3a).
  Generated modules validate; this is a portable host model, NOT a failing
  RTX hardware result or an established original-game cause.
- Eight-line shared EmitFPSaturate32 correction guarantees NaN+0 ONLY for
  Vertex/Pixel, whose accepted guest graphics mode assertsDX10_CLAMPtrue.
  Ordinary arithmetic/unclamped NaNs and other stage modes remain unchanged.
  Original9+12 frontend tests also passed trace-disabled231105. Other stage
  mode metadata and numerical GPU CLAMP coverage are explicitly pending.
- Existing format-specialization2 A/B trials are measurements, not fixes:
 225304 ended22:56:18Z exit0/graceful180s bound; warm230409 ended23:07:28Z
  after timeout/owned forced cleanup(-1), noRAMguard. New variants repeatedly
  compiled~30s; no valid steady-scene FPS comparison or stable picture proved.
  Keep default format mode for next correctness retry. Real-device-selected
  hash-only dumps in runtime-formats-baked are overwritten by variants/startup
  replay; do not equate them with a uniquely identified dispatched module.
- Source-frozen opt-in value trace: exact2 immutable draw tokens, bounded6272
  appended bytes, live buffer results, decodedD3/LZ/red sample coords/results,
  separate pre/postclip exports. Images' selected-use filter is NOT proven
  guest EXEC; missing events are not inactivity proof. Effective2D placeholders
  are labelled separately from originalD3. Explicit --async-pipelines false
  prevents unused asynchronous preparations exhausting the admission slots.
- Native target bindless_lifecycle_tests build is ACTIVE since23:21:53,
  root session17571. New loop-free real-GPU trace fixture covers output
  preservation/token0/wrongvertex/EXEC0/tokens1/2/prepost/sentinels; validation
  is pending. No original trace game run before that check. Source inputs
  frozen. Stable picture/~60FPS remain NOT achieved; root continues work.

## Latest checkpoint: stable-picture and ~60FPS objective, 2026-10-11 22:43 UTC

- User explicitly requests continued work until stable picture and stable
  approximately60FPS. Correctness and speed are separate acceptance checks:
  inspect repeated rendered frames for missing/stretched objects, and measure
  actual warm-cache game frame intervals (16.7ms is60FPS). Never count passed
  shader tests, presentations, diagnostic fallback or display size as that result.
  Resolution preference was asked asynchronously; provisional target1080p,
  pending reply. Current guest targets reach3840x2160 despite1280x720 window.
- Native source clean cf882c52 at start; existing private EXE680632d7 is
  source-equivalent to its core, no rebuild needed for measurement. Run222854
  (240s, drain-stats10/GPUzones1, VTX/layout/provenance/snapshotsOFF) ended
  22:32:59.378Z exit0/gracefulbound/noRAMguard,min23.83GiB. Latewindows4.6–5FPS:
  compute77–93ms/frame, buffer-copy19–22ms, draw17–18ms, imagecopy~6ms;
  totalGPUbusy125–142ms, gap~74–76ms. CPU fault waits overlap these times;
  do not add them. Histogram>=100ms bucket is saturated, not true p95/max.
- Warm clean run223508 (180s, no VTX/zones/drain/layout/provenance/snapshots,
  same private regular cache, eager1/keybounds1/chain0/manual) loaded173.6MiB
  driver cache and replayed1971 records in~5s. Observed frame802 title5.1FPS;
  ended22:38:13.144Z exit0/gracefulbound/noRAMguard,min25.3GiB. This rules out
  cold startup or VTX checks alone as the speed explanation, not all profiler
  overhead or all scenes. Both native sessions drained; no game remains.
- Top measured compute shaders map to frozen v9 records215/214/213:
  fc6f8c56eb7e168f,1158e78fa2c7a20e,da4ff1222c09f131. All use guestwave64
  on host32 with12 generic formats and dynamic strides; older conservative
  modules~0.8M SPIR-Vwords are not current live-module proof. Need fresh
  module/dispatch measurements and independent format/stride/bounds/EXEC
  regressions before changing codegen. BDA_FAST_DWORD already defaultsON.
- Correctness follow-up: captured2a T#0 isR8UNorm8x11x23 volume; T#1/2
  are1x1RGBA8 placeholders, all normalized textures cannot encodeNaN texels.
  Check actual sample coordinates/results and executed branches. IEEE_MODE0
  does not authorize blanket zero-timesInf or NaN sanitation. A decoded CLAMP
  has an opcode-specific DX10 contract; its current runtime role is unproved.
- A staged opt-in bounded value trace is authorized as DIAGNOSTIC ONLY:
  selected live samples/loads and preclip export, exact immutable drawtoken
  via internal shader data, no guestSRT/ObjectID overwrite, bounded faultbuffer
  transport and cache separation. Production behavior fixes still require RED.
  Source tracing work is in progress; all native execution belongs to root.
  Automatic input staysOFF, no push or public progress milestone claimed.

## Latest checkpoint: actual masked-fetch GPU RED/GREEN and current v9 IR, 2026-10-11 22:21 UTC

- Same synthetic `--embedded-fetch-gpu-readback` on real RTX5060Ti:
  first current run221901 passes; scoped frontend-only reversal to6ba,
  native build221916/run221933 produces intended numerical RED (EXEd62a0445).
  Canonical positive control reads11223344/index1/high0; masked raw001d0001
  wrongly reads native raw-index sentinel55667788 (full raw/high29 preserved).
  No timeout/GPU reset/loop; one indexed point, real24-byte shader buffer,
  valid7.25MiB native buffer prevents an out-of-bounds failure masquerading as RED.
- Frontend restored byte-exact4f0c1f9d; build222040/SAME GREEN222119 passes
  both numerical readbacks on EXE8333f4ba77f3647064d45c8829e77f500571c69c7fa4feb19208f17185130995.
  SPIR-V validates before draw; actual public translation/materialization/
  compilation and real shader-store readback prove masked fetch ownership.
  Native-offset/guest-builtin equivalence and other games remain unproved.
  This validates f2593c61 but has NOT solved the observed world-output blocker.
- Journal worker now supports declared8/9 exact codec roundtrips and opt-in
  decoded/translated/compiled IR. Native build221552; v8 control and v9 PS/db
  audits221758 pass SPIR-V validation, v9 PS fields retained (no omitted fields).
  Old2a record824 fails expected specialization/resource-shape gate after the
  frontend change; gate preserved. Fresh current2a record818 passes222119,
  SPIR-V59745words and full IR saved. These are reconstructed CPU profiles,
  not original userdata/materialization/live-SPIR-V equivalence or GPU proof.
- Fresh frozen v9 journal: SHA f10250132c0d1858fa8752b9fcfa4ec93582119de8f6878a1642dfc64c93ba8b,
  1924 records fully framed/checksummed. Artifact manifests and metadata matches
  `journal-audit-current-220706-20261011T2218516940404Z-95df3ad1`; current2a818
  matches17 compiled buffers, captured db946 matches20 (db1607 another variant).
- Additional source limit: actual2a Position dependency union includes vertex
  ImageSample operations. Draw snapshots do not capture vertex-image contents.
  Matching finite stored vertex buffers therefore do not prove all Position
  inputs finite. Next required work: selected actual load/sample result and
  first nonfinite arithmetic trace with explicit branch/draw attribution.
  Original run220706's -3/-4 screenshots remain the latest visual evidence;
  partial objects/NaN output still unresolved. No native process remains.

## Latest checkpoint: requested fresh screenshots and exact input agreement, 2026-10-11 22:14 UTC

- Source f2593c61 plus opt-in diagnostic083aaeae; private EXE680632d7.
  Run220706 ended22:12:16.612Z, exit0/graceful300s bound, streams drained,
  no forced cleanup or memory guard, minimum free22.78GiB. No native process
  remains from this game. Pad recording contains only neutral input; automatic
  input OFF and root sent no keys.
- Fresh owned screenshots inspected/displayed: window-after-fetch-1.png
  frame299 is black, -2.png frame482 heavily streaked/blurred, -3.png frame673
  shows character/ground/buildings, -4.png frame1038 still shows partial world
  objects. Use -3/-4 as the requested later-scene comparison pair. Individual
  rendered frames do not prove stable visual quality or completed gameplay.
- Current exact-input review:
  `_Build/analysis/yotei-integration-20261011/prepared-input-review-220706`.
  All96 blob references/checksums/lengths and32MiB new-copy budget accounting
  verify, zero new skips.2a actual prepared source6 (range1228,adjust12) guest
  window1216bytes matches nativeVB0 EXACTLY. All10 prepared/native overlaps
  for2a and6 fordb7a agree, as do refound comparisons. Prior210717 divergence
  is absent here; synchronized first-match capture can hide a race. No buffer
  binding-byte defect is proved by the current capture.
- Actual2a NaNxyzw remains frame299/vertex14 with matching last-draw frame298
  metadata; attribution remains approximate. Its declared packed vertex inputs
  are finite; follow signed/packed addressing into stride48 input and actual
  Position dependency before any arithmetic fix. Do not interpret every raw
  DWORD as float or treat negative packed fields as invalid bone indices.
  Finalframe1200 diagnostic reports12,054,372 NaN/Inf events across68VS;
  counts are observations, not proof of one common cause or worse rendering.
- Completed frontend CPU correction remains f2593c61 (9+12 unchanged cases).
  Actual numerical GPU vertex oracle is drafted; first native compile221315
  failed test-only API setup (not RED), refinement pending. Fresh v9 replay/IR
  workflow is also drafted; neither has a native pass yet. Native slot idle;
  root owns execution. No push or issue milestone publication.

## Latest checkpoint: all unchanged frontend regressions GREEN, 2026-10-11 22:05 UTC

- Native build220439/run220533 pass all nine original cases and twelve
  neighbors on EXE97f31b90c688b88dec33c3f6d0fcffd81f46a12c6c156c33baeb2bdf773a5d36,
  exit0/no timeout. Original semantic functions127fa0cc and frozen neighbor
  oracles538e6908 are unchanged across the offset correction. Genuine CFG
  join/backedge decoder gates are included, not the former mistaken SOPC.
- Shared frontend patch4f0c1f9d tracks selected/ABI index provenance and
  offset pairs per current register value, invalidates actual write widths,
  rejects unproved CFG transforms, and retains both guest loads when native
  fetches would need incompatible draw offsets. Raw VertexID/high bits and
  ordinary once-only Vertex/Instance/Local controls remain intact in CPU IR.
- Intended REDs:212841 transformed indices;214615/214616 repeated add/wide
  overwrite;215803/220004 discarded offsets;220005 incompatible two-fetch
  offsets. These establish shared frontend defects, not original-game cause.
  shader_cfg empty-types budget fails identically with frontend patch absent
  (baseline215846), so no broad/full-suite pass is claimed.
- Opt-in draw snapshot diagnostic083aaeae separately saves exact prepared
  non-table descriptor buffer ranges and shader-data/layout metadata, with
  explicit32MiB aggregate new-copy cap and skip reasons. Existing refound
  observations remain separately labelled. This is instrumentation, not a fix;
  table BDA pointers are explicitly unavailable and first-match attribution
  plus FlushAndWait timing perturbation remain limitations.
- Local completed frontend commit f2593c61336d387e01cb6bc9bbd2958a78405e78.
  Native configure220612/build220619/install220658 passed. New private
  runtime-embedded-fetch-offsets EXE680632d783966cdfedc41dabde870042f887c0f22b7fd459215a396304b668bf
  includes the separately uncommitted opt-in diagnostic083aaeae.
  Original300s run220706 is ACTIVE, owned PID25136, session17063, directory
  `_Build/runs/game-20261011-220706-integration-embedded-fetch-offsets-geometry-847f15`.
  No current visual outcome yet. Earlier runtime-embedded-fetch-final is
  preserved/not executed and predates the offset-lifetime correction.
  GPU numeric vertex/readback and Vulkan builtin/draw-offset equivalence
  remain pending. Tests and game are authorized, automatic input OFF.

## Latest checkpoint: provenance neighbors and remaining offset proof, 2026-10-11 21:57 UTC

- Current branch yotei-integration-native, HEAD6ba0a504 plus frontend/test WIP.
  Build215358 and run215406 prove canonical/VertexOffset/InstanceOffset/Local,
  single/Local offsets, repeated same offset, wide second-DWORD overwrite,
  relative write and identity cases on EXE3dad51e2. Original nine cases already
  pass215045 on the same frozen production correction (EXE242eb8a6).
- The last neighbor failed due to a test-only SOPC/SOPP encoding mistake;
  corrected branch fixtures now require public decoder branch/target checks.
  Earlier join PASS did not exercise a CFG join. Corrected joins/backedges
  are pending; no production branch failure is claimed from that error.
- Independent review found a possible discarded-offset metadata leak. A new
  pre-CNDMASK add/overwrite regression is frozen, awaiting intended RED. Also
  inspect resetting an already selected/offset index and multiple fetches.
  Do not run the original game as a completed correction before these checks.
- Fresh native configure215424/build215431/install215528 passed. Private
  runtime-embedded-fetch-final EXE849dd11d7848bb0b7ced3dc48776254283263a9e1c511fb41547cb6e6904000f
  is preserved but NOT game-executed; it predates any discarded-offset fix.
  Actual GPU vertex-result equivalence and original geometry remain pending.
- Requested screenshots remain run205728/window-request-1.png and
  window-request-2.png, inspected/displayed at frames1006/1109. Game/test
  execution is authorized. Automatic input remains OFF; no root-sent keys.
  Native slot currently occupied by affected shader_cfg target build.

## Latest checkpoint: unchanged embedded-fetch CPU GREEN, 2026-10-11 21:40 UTC

- Native build213825 and SAME original fixture213941 pass all nine cases:
  canonical/VertexOffset/InstanceOffset/Local remain native controls, while
  masked/overwritten/noncanonical-CNDMASK/ABI-shift/ABI-mask fetches preserve
  their actual shader-buffer index expressions. Raw001d0001/high29 remain
  unchanged. CPU planning/IR proof, not GPU vertex-result proof.
- Original fixture127fa0cc remains byte-identical; GREEN EXE
  `7ef93f2bcb114d70842db04050eb08c87705fff1e4e86b82dd79ae7d14fba750`.
  Shared correction is ShaderRecompiler.cpp only: invalidate stale selected
  and ABI index provenance, preserve existing proved prolog offsets, reject
  unproved CFG branches conservatively while retaining Local terminal handoff.
- Focused neighbors for wider writes/relative indexing/repeated offsets/CFG
  boundaries follow. Do not extend FP64 semantics or change draw indices.
  Native core inputs frozen for fresh runtime-embedded-fetch build.
- Fresh v9 journal extraction for targeted snapshot is verified separately:
  `_Build/analysis/yotei-integration-20261011/journal-v9-targeted-210717`.
  All1960 envelopes/full records/checksums/tail verified. Current2a6a primary
  is8016bytes, db7a2240bytes, both match registered-code SHA. Earlier private
  attribution of v8 rows451/1416 to2a was wrong (those are fe3); no code-size
  regression is established from those rows. Snapshot record=false/liveSPV
  and first-match specialization remain coverage limits.
- Refound2a stage-buffer blob differs from same-address native VBO, but the
  collector refinds before final NativeStorageBuffer resynchronization; this
  is not proof of wrong final shader data. Actual geometry cause still needs
  verification with current correction and eventual committed-binding capture.

## Latest checkpoint: embedded-fetch eligibility intended CPU RED, 2026-10-11 21:29 UTC

- Targeted capture retry210717 ended21:12:23Z, exit0/graceful close at300s
  bound, no forced cleanup/RAM guard; streams drained. Two candidate draw
  inputs are saved at frame305, first-match snapshots (not exact-failure
  guarantees). Their metadata lacks fresh shader code/SPIR-V (`record=false`).
  Shader-byte provenance and the failing real-game variant remain unproved.
- Independent synthetic public TranslateProgram fixture
  `tests/EmbeddedFetchIndexTests.cpp`, original SHA256
  `127fa0ccf08edee1dca176f80ecf6c0fd0a1c4e0f5b048a68540eed916f56259`.
  Native build212514 and controls212814 pass: canonical GetAttribute4,
  supported Vertex/Instance offsets18 and actual Local handoff/ABI.
- Intended RED212841 for mask-dst and ABI-instance-shift: actual native
  GetAttribute4/shader-fetch0 incorrectly replaces a transformed guest index.
  Raw001d0001 and its high29 are preserved; expected guest fetch indices1/29
  cannot be represented by that native fixed-function attribute selection.
  Both fail exactly eligibility, no timeout; test EXE
  `b70ac1a0a0691f6f04bf334f735b9e286734a2ca4accb7e14fd778c11fd5fb17`.
- Shared frontend correction now authorized after RED. Track actual index
  provenance and overwrites, preserve proved offsets/Local, reject unproved
  CFG transforms conservatively. Never globally mask/change draw indices.
  The same tests must pass unchanged before neighbors and game retry.
  This is an independent correctness defect, not established original-game
  causality. Production owner limited to ShaderRecompiler.cpp; native slot idle.

## Latest checkpoint: requested screenshots and actual invalid VS positions, 2026-10-11 21:05 UTC

- Recorded-consumer correction is local commit6ba0a504, with actual native
  sampled RED/GREEN and neighbors. Original retry205728 on equivalent private
  EXE49d31a... still fails visually. User-requested owned screenshots:
  `window-request-1.png` frame1006/5FPS shows character/torii/building fragments
  with terrain missing; `window-request-2.png` frame1109/4.4FPS again shows
  covering stretched surfaces. Both were inspected and displayed. First
  foreground-capture attempt was withheld, then safely retried; no other
  application's image was captured.
- Run finished21:02:32.392Z, exit0/graceful close at300s bound, timedOut=true,
  no forced cleanup/RAM guard, streams drained. No stable gameplay milestone.
- Existing VTX_EXPLODE_CHECK startup/emission/cache separation proved active.
  Frame600 summary:8,344,554 events,3,853,828 NaN/Inf,984,924 WZero,199,779 huge;
  final distinct NaN/Inf report kinds cover68 VS. Legitimate cull/clip events
  must be kept separate. This is actual exported-position evidence, not a
  texture-only hypothesis or proof of a particular shader/input defect.
- Candidate direct32-bit index sample for VSdb7a7e115b3004a2 reads
  001d0000/001d0001/001d0002 with a six-record VBO. Other32-bit index controls
  are normal; do not change widths or mask all indices. Need actual guest
  fetch/index-expression contract and capture before synthetic RED/fix.
- Targeted capture retry now active: PID47580,
  `_Build/runs/game-20261011-210717-integration-retirement-geometry-2e5002`,
  same EXE/switches,300s bound, hashes db7a7e115b3004a2 and2a6a418c0787e9b9,
  MAX1 per hash. It captures first matches and synchronizes readbacks; this
  can perturb timing and may not be the exact failing draw. No behavior guard.
  Production/CMake frozen; native slot occupied by this game.

## Latest checkpoint: sampled-retirement neighbors pass; retry ready, 2026-10-11 20:56 UTC

- Intended native pixel RED203642 → SAME GREEN204246. Neighbor build205500
  passed; actual sampled red/green/blue neighbor test205604 passed exit0/no
  timeout: same-batch invalidation waits once, unrelated no-ref images avoid
  waiting, general deferred callbacks remain pending during the cache wait,
  fresh green replacement survives old deletion callbacks/repeated old-ID
  events, completed use avoids a wait, fixed-slot-only blue survives without
  translation/region leases. Both shaders validate Vulkan1.3 before dispatch.
- Final native test EXE
  `2d4a3943467491a9f1db6b13258ddec7bbee2046822aa6064ddb8b206a4a762f`.
  Earlier affected lifecycle/preparation/feedback/access/plan1/0 cases all
  passed205215 through205220 on the same production correction. No broad
  full-suite pass is claimed; existing resource-tracker baseline failures
  and absent cross-game workloads remain coverage limits.
- Current core configure204426/build204433/install204558 passed. New private
  `runtime-bindless-retirement` EXE
  `49d31a724d6c6d7917813af574ac2517e59cfb0ec4d9af6e5093a1fee1f8fa63`;
  source90c5820e plus archived11-line engine diff, computed/unpinned cache key.
  Exact patch SHA256 `62b390e81b057297fa89383a4146d4f7c87f8753d63b7952302a6b2f08e9c4cd`.
- Next original retry retains manual input and adds existing bounded exported
  vertex-position diagnostics. Its separate .vtxexplode cache avoids reusing
  unchecked shaders, but instrumentation can perturb timing. Position checks
  are a diagnostic lead; they do not clamp outputs or prove visible quality.
  Production frozen, native slot idle; preserve old installed runtimes.

## Latest checkpoint: recorded-consumer sampled-pixel GREEN, 2026-10-11 20:43 UTC

The SAME immutable native sampled-readback oracle now retains opaque red in
both the translation-selected and fixed sampled-array slot paths.

- RED203642 → native build204103 → unchanged GREEN204246, exit0/no timeout,
  no GPU gate/reset. GREEN EXE
  `6d28ef3d678e9d706b3ad67942b2ca24495036c44fbf8fe52d48ff19b0f7cf8e`.
  Original fixture `968bb009...` remains byte-identical.
- Shared correction is11 production lines in BindlessTable.cpp/.h: track
  actual whole-set recorded-use tick at commit and final preparation tick;
  before unregistering referenced images, wait only an incomplete recorded
  use via existing scheduler.Wait, then mutate descriptors/translation.
  This submits a current pending tick when necessary and does not drain
  general resource callbacks while cache/preparation references are held.
  Unknown images and already completed uses avoid this wait.
- Scope is recorded consumers. Earlier-stage preparation before recording
  remains unproved and is not fixed by this test. Original geometry causality
  is also unproved. Neighbor tests cover batching/replacement/no-region use
  next; then retry original game with bounded vertex-output diagnostics.
- Production/CMake inputs frozen for new native core build; test-only neighbor
  work is parallel. No native build/test/game overlap. Preserve old runtimes.

## Latest checkpoint: recorded-consumer sampled-pixel RED, 2026-10-11 20:37 UTC

- Original fresh two-fix retry202640 finished20:32:45Z, exit0 after reaching
  the360s bound (timedOut=true), graceful close, no forced cleanup/RAM guard.
  Owned414/1180 screenshots still show covering surfaces and missing world/body.
  Streams drained. Actual controller log has a brief mask4 host event; root
  injected no keys and auto swipe/press/replay remain off. No stable gameplay
  or new public milestone proved.
- Native synthetic sampling target build203417 passed. Raw SPIR-V validation
  against Vulkan1.3 and genuine opaque-red cache-image positive control passed.
  Actual FreeImage then OnImageUnregistered changed both old translation and
  sampled descriptor before a recorded consumer ran: both RGBA reads returned
  zero rather than red. Intended GPU RED203642, no timeout, two loop-free
  bounded dispatches, no GPU gate/reset. Test EXE
  `e2986bdaf2a85ed07055eac4dbcb5344da162bdb40c8e3a7d736ac0caabbf26b`.
- Unchanged oracle `BindlessDescriptorRetirementTests.inc`
  SHA256 `968bb009b3a91a7b6c82a078fa67541bff21284dcfb5d552a89f8d3d36f8f610`.
  Reproduction is actual sampled pixels and recording ownership; it does not
  establish that this defect alone caused the original stretched geometry.
- Shared production correction authorized after intended RED: protect recorded
  consumers via existing scheduler completion ordering before both descriptor
  and translation mutation. Avoid async stale heap callbacks or speculative
  rewrites. Unchanged GREEN, neighboring lifetime cases and game retry follow.
  Earlier-stage preparation ownership remains separate unproved coverage.

## Latest checkpoint: game retry reauthorized and active, 2026-10-11 20:27 UTC

Latest user instruction explicitly allows game launches and tests again.
Keep manual input: automatic swipe/press/replay and root synthetic keys remain off.

- Verified branch `yotei-integration-native`, clean engine diff, latest local
  documentation HEAD90c5820e. Current installed source is d7895a56; exact
  EXE `ad1e1c7d0d14170fa8b33dae2e265c235dfc9a7d0aba0b348896e27bcd38ee2c`.
  No native emulator/test/compiler was active before this launch.
- Bounded original retry started20:26:40 UTC, PID31996,
  `_Build/runs/game-20261011-202640-integration-access-age-manual-d22388`,
 360s limit, same private saves/cache,1280x720, autoswipe0/compute-chain0.
  This is the first game retry of BOTH completed lifecycle/access-age fixes.
  Visual correctness remains pending until owned-window captures are inspected.
- Native slot is occupied by the game. Production/CMake sources frozen;
  parallel work is limited to read-only diagnosis and test-only construction.
  Next required test is actual sampled readback across descriptor/translation
  invalidation with completion ownership, not another metadata-only claim.

## Latest checkpoint: both fixes committed; fresh EXE ready, game held, 2026-10-11 19:16 UTC

Latest user instruction: tests are allowed; **do not launch the game until the
user explicitly says to launch it**. Automatic pad swipe/press/replay stays off.

- Branch `yotei-integration-native`. Separate local fixes: `6d08d859` preserves
  live translation mappings; `d7895a56f34f1db10e3602490fc26019105327e5` refreshes
  actual bindless image access ownership. Both have intended native RED/GREEN;
  final neighbor scope and limitations are recorded below. No push.
- Native current-source configure191412, core build191425 and install191523
  passed. New private runtime:
  `_Build/analysis/yotei-integration-20261011/runtime-bindless-access-age`.
  Actual EXE SHA256
  `ad1e1c7d0d14170fa8b33dae2e265c235dfc9a7d0aba0b348896e27bcd38ee2c`.
  Exact source commit d7895a56, clean engine diff, computed/unpinned cache key,
  private saves/cache copied; old runtimes and original game data preserved.
- Prepared `run-bindless-access-age-manual.ps1` checks exact EXE/game/source
  and uses autoswipe0/compute-chain0 with no inherited autopress/replay. Only
  its nonexecuting DryRun preflight was performed; no game process started.
- Visual result remains unresolved: earlier first-fix manual comparison had
  severe covering stretched surfaces. The second correction has not been
  retried in the game. Current tests prove host binding/access and feedback
  behavior; they do not prove sampled pixels, a stable menu or gameplay.
- [Fork comparison and next work](yotei-texture-lifetime-research.md): bounded
  recorded/pending consumer sampling regression for descriptor/translation
  retirement, then separate protection during earlier-stage preparation.
  Native slot idle; no task-owned emulator/test/compiler remains running.

## Latest checkpoint: access-age correction and neighbors verified, 2026-10-11 19:11 UTC

Tests only remain authorized; game launches are forbidden until the user
explicitly asks. Native slot idle, all owned processes and streams drained.

- Source: local parent6d08d859 plus one-line shared CommitBindings access stamp,
  the unchanged age oracle and separate neighbor fixtures. Intended RED190144
  → unchanged GREEN190356; final unchanged age oracle191046 also passes.
- Native neighbor build190905 passed. Actual compute/graphics commit cases
  pass191010/191011 with binding-plan1/0: duplicate/shared image IDs, repeat
  same tick, next tick with already-read-only layout, genuine unregistered
  and later stale generational ID, existing target/null-data skip behavior,
  and zero-write descriptor payload early returns. No shader dispatch.
- Final native test EXE
  `eb86c7c482d3327a891e17d6e68a6f99f7d674f2a374e5199fa0713712921ea7`.
  Earlier actual-table lifecycle191047, preparation/growth191049 and feedback
  key-bounds-off191050 also pass this artifact. This is metadata/binding and
  actual feedback-copy validation, not sampled-pixel or cross-game proof.
- [Fork research and next plan](yotei-texture-lifetime-research.md) preserve
  exact primary revisions/links and identify completion-delayed descriptor
  retirement as the next independent regression. Earlier-stage overlap and
  pending old descriptor/translation consumers remain unproved.
- No game retry after the second correction, no push or issue108 comment.
  Old installed private game EXE still lacks the access-age correction.
  Only after explicit game authorization build latest core into another
  private runtime, retain source/EXE proof and use manual input.

## Latest checkpoint: bindless image access-age RED to GREEN; tests only, 2026-10-11 19:04 UTC

User now permits tests but explicitly forbids game launches until further
instruction. No game has run since the manual retry ended18:54:28.

- Native build190102 passed. Real tracked direct-memory texels, registered
  cache images/views,33 actual scheduler submissions and the direct reuse
  positive control passed. Intended RED190144 on local6d08 observes bindless
  access1 at current34, GC33/LRU0, while direct reuse is34. This is the actual
  public CommitBindings path without shader dispatch; PeekImage does not
  refresh the observation. First185759 attempt was unrelated fixture failure.
- Minimal shared correction adds one MarkImagesAccessed call after existing
  valid-image filters and before layout transition in CommitBindings. Native
  build190308 and the SAME unchanged oracle190356 pass: observed34 and LRU33.
  RED test EXE `16915ba8e3fd5cc3f62f8883dc3734368f2583e3f8cd6c42d7fd93412895df10`;
  GREEN `de560b6eff7842e24a2d551cea73fc2ea92405e1d28f78e39d131df87035f901`.
  Original age fixture SHA256 `476879b3e6f865d5dfb87e966754568fbe724e89b66b0d2c04d7558f87f90e2b`.
- Actual graphics/compute variants, duplicates, unchanged layout, skipped IDs
  and descriptor payload early-return neighbors are next. No original visual
  retry is authorized; this independently proven correction is not proof that
  stretched surfaces or all texture flicker are fixed.
- The installed game EXE `runtime-bindless-lifecycle/ac945386...` predates
  the new access-age correction. When the user authorizes a game retry, build
  current native core into another private runtime and capture exact source,
  EXE hash and manual-input settings; do not reuse the older binary as proof.
- Fork comparison: the verified peepo6d041035 has the same access-age omission.
  Sharp926 delays descriptor nulling/reuse to queue completion, unlike our
  immediate OnImageUnregistered slot rewrite. Pending-consumer sampling and
  preparation overlap need separate bounded regressions before porting this
  behavior. Preserve actual bounds, masks, failures and GPU lifetime checks.

## Latest checkpoint: manual synchronization comparison still corrupt, 2026-10-11 18:55 UTC

Completed lifecycle correction is local commit
`6d08d859c0026c3ba4609ace74a61be96dec9598`; installed core is the equivalent
precommit source2a599092 plus archived lifecycle diff, EXE `ac945386...`.
Do not claim visual correctness from its passing mapping tests.

- Retry `_Build/runs/game-20261011-185229-integration-manual-conservative-8902fe`
  used autoswipe0/compute-chain0, no autopress/replay or synthetic root keys.
  `pad-input.txt` contains only neutral buttons0/sticks128. Screenshots
  `window-manual-1853.png` (frame512,4FPS) and1854 (frame704,4.2FPS) show
  character/world and then severe covering stretched surfaces. User confirms
  textures still jump. This comparison does not resolve the artifact or prove
  synchronization is correct generally.
- Owned PID43444 was closed gracefully for the next regression; finished
  18:54:28.883Z, exit0, no timeout/forced cleanup/memory guard, streams drained.
  Native slot idle. Automatic controller injection remains disabled.
- Next required RED exercises actual public RenderExecutor::CommitBindings
  with a registered native bindless image and no ordinary image descriptors,
  after advancing actual scheduler ticks. Observe access age without a getter
  that refreshes it; use direct-binding positive control. Production is still
  committed6d08 and must not change before intended RED. Native test agent
  owns only the new fixture/mode; shared fix owner waits for regression proof.

## Latest checkpoint: artifacts remain; automatic input removed, 2026-10-11 18:50 UTC

User confirms disappearing/redrawing textures remain and asks about unsolicited
input. The184655 run retained the explicit debug touchpad swipe every4seconds;
it was stopped after this steering. Owned PID42816 closed gracefully, runner
finished18:49:40.754Z, exit0, no timeout/forced cleanup/memory guard, streams
drained. Actual screenshot `window-1848.png` (frame426,6FPS) shows severe
stretched surfaces; the lifecycle correction is not a complete visual fix.

Next retry uses the same exact EXE `ac945386...` with autoswipe0, no autopress
or replay inherited, and compute-chain0 as a synchronization comparison.
No further synthetic keyboard input will be sent in this manual retry. This
is diagnosis, not a proven synchronization fix. Native slot currently idle.
The next independent RED candidate is actual bindless CommitBindings access
age: successful initial resolution pins ordinary GC, but subsequent raw image
lookups do not refresh the access tick used by overlap retirement.

## Latest checkpoint: lifecycle neighbors pass; visual retry active, 2026-10-11 18:47 UTC

- Unchanged mapping oracle passed again at184625. Preparation/growth/abort
  neighbors pass with key bounds on184423 and off184627, using real mapped
  translation words and required allocation pressure in the unchanged arena.
- A separate scoped reproduction restored only the legacy conditional snapshot
  guard. Native build184506 passed; key-bounds-off run184536 failed exactly
  old-snapshot ownership rejection. Production file was restored byte-exact,
  then build184546 and the same unchanged feedback test passed off184623 and
  on184624. Real GPU feedback copies and both owner-positive controls passed.
  Final test EXE `b549fc77eef2108c0391225278cf52be854e306699ffaa46bf1b96a0f4811159`.
- Native core build183750 and private install183926 passed. Original retry
  `_Build/runs/game-20261011-184655-integration-bindless-lifecycle-f23933`
  is active, PID42816,660s bounded, same explicit experimental settings and
  private seeded saves/cache. Installed EXE
  `ac9453867ef5a97ebae3c9eac65a109a4e9676a806fb4ce7c46e416506ca8e42`;
  computed source cache key, exact source diff/provenance retained in
  `runtime-bindless-lifecycle`. Stable visual rendering is still pending.
- Pending coverage: real shader/SRT consumer and pending-tick transfer;
  self-pinned first-growth capacity corner and delivery of old-growth feedback.
  Independent image-access-age/overlap retirement lead needs its own regression.
  No push or issue108 comment. Do not start another native process during retry.

## Latest checkpoint: bindless lifecycle RED to GREEN, 2026-10-11 18:35 UTC

The unchanged actual-table regression now passes after safe region reclamation.
Source is local HEAD `2a599092` plus the lifecycle correction and test/CMake diff.
The original fixture remains SHA256 `c428093091e3502d18ea5bf4588a1d36bb01d62c2228ec49063679f574f6badf`.

- Native build: `checks/20261011-183359-8045246-build-target`, exit0.
  Bounded test: `checks/20261011-183446-0649646-bindless-lifecycle-green`,
  exit0, no timeout, actual RTX5060Ti/Vulkan/image setup, no shader dispatch.
  Test EXE SHA256 `679bf65eac55c962728ae7646ed37e2547254b9c2502b39e45fc66e7b1bf08f2`.
- The same fixed arena now reclaims completed, unleased extents rather than
  clearing every heap. Stable heap nodes and active mappings keep their offsets.
  Preparation scopes protect all stages; exact region generations and scheduler
  ticks retain older growth regions and pending feedback copies. Old snapshots
  are rejected independently of the optional key-range policy.
- This proves the host mapping-retention correction. Growth/preparation/feedback
  neighbors and original visual retry are next; stable game textures remain
  unverified. No new issue108 comment or push.

## Latest checkpoint: bindless lifecycle RED, 2026-10-11 18:20 UTC

Native actual-table reproduction succeeded onHEAD`2a599092` plus test/CMake
changes. New standalone `bindless_lifecycle_tests` uses real Vulkan/VMA/image
setup and production BindlessTable, with no shader dispatch. The original
large compute test TU has pre-existing API drift; its test-only additions were
reverted and this independent304-line bootstrap avoids those unrelated errors.

- Native build181815 passed; bounded run181957 onRTX5060Ti passed setup and
  failed the intended mapping-retention oracle at temporary30, arena request
  983168entries. Existing limits were unchanged. ResetHeaps removed live heap
  A and resolved slot7; prior prepared patch lost ownership.
- Exact artifact: `_Build/analysis/yotei-integration-20261011/checks/20261011-181957-8381841-bindless-lifecycle-red`;
  EXE`6f0d29e258ac19b91906d15b7a624867b945af4c5b1a0c796e9cac10918942e3`.
  Exit-1073740791 from the expected test assertion; no timeout/reset/hang.
- Shared correction is now authorized by intended RED. Reclaim completed,
  unpinned temporary regions while preserving live offsets/mappings and real
  GPU/preparation ownership. Preserve current mask/provenance and bounds;
  no limit increase, fake resource or rejection of required allocation.
- Tests/bootstrap are frozen for unchanged GREEN. Growth with earlier patches,
  in-flight consumers, late old feedback and stable visual retry remain needed.
  Native slot idle; production implementation in progress, no GREEN claimed.

## Latest checkpoint: unstable world rendering, 2026-10-11 18:03 UTC

- User now identifies periodic disappearance/rebuilding of all textures. Fix
  lifetime of live bindless mappings; preserve shader collection work.
- Run `_Build/runs/game-20261011-175232-integration-pointer-provenance-d8addc`
  tested0c201484 plus scalar-mask/source-identity and bounded provenance diff.
  Exact private EXE`7198f3f77d362bc5df3dce8c6ba7ce8f2ac73565efd9f2398ec9a39fa7b751eb`;
  diff/header/provenance retained in `runtime-pointer-provenance` analysis.
- Owned `window-now-1758.png` visually confirms character and village world
  with buildings/road/grass/light. `window-now-1756.png` and1800 show severe
  distorted surfaces. Entry into a world scene is visible; stable rendering,
  controlled movement and gameplay remain unproved. These images were shown
  to the user after the explicit screenshot request.
- Finished18:02:27.725Z after594.952s, graceful exit0 at freeRAM7.89GiB reserve;
  no timeout, forced cleanup, task32GiB guard or observed GPU reset. Streams
  drained; native slot idle. No issue108 checkpoint published from this run.
- New CPU address regressions171311 -> unchanged GREEN172445/172446; final
  scalar AND/BFE/identity neighbors174703/174704 passed. Vector transforms and
  host bindless GPU readback remain coverage limits. Existing full suite fails
  mixed-numeric policy; strict suite fails output-storage reuse. Scoped native
  0c baseline173620/174835 reproduced both; saved source/test files restored
  byte-exact. Do not claim either full suite passes.
-32 provenance chains (8 unavailable/24 readable) captured. All observed masks
  canonical, hash/actual reads agree, dirty hints zero. Current failure-time SRD
  and actual GPU issuer are missing. Actual845 artifact causality is not proved
  by independent mask correction. Artifact `observed-bindless-chains-20261011-175734-7020910.json`.
- Concrete flicker lead: translation allocation exhaustion calls ResetHeaps,
  clearing every live settled mapping. Recorded epochs30/57/85/111 show repeated
  resets; delayed feedback rebuilds mappings. No individual reclaim/fence
  retirement protects older regions or already prepared patches.
- Next required RED: actual host-table stable active mapping survives completed
  temporary-heap churn, prepared-stage pressure and old-feedback ownership.
  No capacity increase/rejected required work may substitute for safe reclaim.
  Test/development ownership and native serialization recorded in shared context.

## Latest checkpoint: visual correction requested, 2026-10-11 17:02 UTC

User now requests clear/correct texture rendering. Preserve shader batch work
and address real resource/graphics defects with regression proof.

- Tested local source`0c2014846dd71e59093289c2f757a9125412c850`; clean native
  core build/install passed. Private EXE`6dca3051e66d1d160dc7a208665e5ef077eb1a23eaed2c89d5588f8f51e0c76b`
  in `_Build/analysis/yotei-integration-20261011/runtime-fixed`.
- Original retry `_Build/runs/game-20261011-165145-integration-fixed-intro-5a4933`
  used computed cache key (old driver signature was invalidated), codec9 and
  checked volume admission, same experimental bindless/input settings.
  Owned1654 shows two names;1657 shows hand tying cloth/intro animation, with
  visible artifacts. J/Cross sent16:56:11. No character control/world/menu proof.
- Finished16:59:44.344Z after477.887s at8GiB freeRAM reserve (7.88GiB observed).
  Graceful close did not finish; owned forced cleanup exit-1, streams drained.
  No timeout, task32GiB guard, oversized-layout fatal or observed GPU reset.
  Driver cache saved200,549,514payload bytes; legacy journal retained alongside
  new14,361,737byte `.v9.shaders`. Native execution slot is idle.
- Requested bindless failures include repetitive invalid T# words. Existing
  pointer-record resolver uses plain backing reads; dirty backing, wrong
  source/offset and feedback epoch need bounded provenance capture.
- Separate concrete source lead: pointer-record analysis discards AND masks/
  unsigned extraction widths. Synthetic test-only ResourceTracking regression
  is prepared; the first build hit an existing missing standalone logging
  symbol, so this is NOT an intended RED yet. Resolve harness linkage only,
  run intended failure, then fix shared metadata/resolution and neighbors.
- Current845 pointer-record sample confirms the`+976` pattern but does not
  demonstrate destructive masks. Do not claim this independent defect explains
  its live image until capture/graph evidence proves it. Original failure
  archive, feedback/ownership GPU proof and final correct rendering are pending.
- Local commits only:02581d7f journal fields,6345a735 batch/probes,0c201484
  checked volume rejection. No push or issue108 checkpoint posted.

## Latest checkpoint: CPU fixes and full batch, 2026-10-11 16:45 UTC

Tested source: HEAD`66e073b2` plus working diff; original retry is next.

- Full journal batch:2999 manifests,2770 distinct inputs, all2770 passed native
  decode/translation/emission and SPIR-V Vulkan1.3 validation in234.233s.
  Two isolated CPU workers,15s per record,600s coordinator; no failures/timeouts.
  Artifacts: `_Build/analysis/yotei-integration-20261011/batch-native-full-20261011-163652-9854636`.
  Auditor SHA256`106b5d16a10977b21c4f703d5440d8dd20d3bbb0a0e6371b7ce167b382a4d2fd`.
  Conservative explicit profile and reconstructed v8 defaults: no original-input
  equivalence, optimizer, driver, GPU execution or gameplay proof.
- Pixel journal: native RED162451 -> unchanged GREEN162914; final native
  neighbors164451 passed. Version9 preserves sample VGPR, target mask and
  provoking vertex mode; explicit8 refuses values it cannot represent.
  Runtime filename `<title>.v9.shaders` preserves previous journals.
- Volume admission: real descriptor CPU RED163408, exact4GiB, fatal321 at
  tile.cpp1348 -> unchanged GREEN164349, explicit unsupported rejection.
  Neighbor modes164350 passed8MiB/2GiB geometry and4/8GiB soft rejection;
  ordinary4GiB and strict4/8GiB requested errors still exit321.
  Green auditor SHA256`e341e7c7ffa3619a8f61ed6b440c5e4ace3f640a9105e038f4f61ab3c6a78d58`.
- Existing fork fallbacks remain experimental. Checked rejection does not
  implement oversized images or prove feedback/slot ownership. Original raw
  oversized T# was not collected in the diagnostic retry.
- Tools: `tools/prepare-shader-journal.ps1`, `tools/audit-shaders.ps1`, explicit
  profile JSON and `tools/README.md`. Runner fixtures passed dedup/resume,
  environment isolation and bounded cleanup. Failed-attempt archive is pending.
- Next: native core/private install with computed key (no diagnostic pin),
  bounded original retry, v9 cache/memory measurements and owned screenshots.
  Native slot is idle; no menu/gameplay/issue108 update from these CPU results.

## Latest checkpoint: diagnostic retry, 2026-10-11 16:18 UTC

- Native configure/build/install passed from primary HEAD`66e073b2` plus
  diagnostic-only `descriptors.cpp` diff. Private EXE SHA256:
  `d0185f62e3c41b14d9ea65b1cc40c78892f8f848c79fbaced8061cc8b4b7b723`.
  Exact diff and provenance: `_Build/analysis/yotei-integration-20261011/runtime-built`.
- Run: `_Build/runs/game-20261011-161201-integration-layout-diagnostic-6358a8`.
  Same original game, autoswipe4, eager bindless and explicit diagnostic flag;
  old source-key pin preserved compatible cache because generated SPIR-V and
  journal codec were unchanged. Clear the pin for later semantic/codec changes.
- Owned `window-1613.png`/`window-1615.png` show hand/cloth/calligraphy;
  `window-1617.png` is black. One foreground/hash-verified J/Cross input at
  16:16:13UTC. No character movement or game-scene entry proved.
- Finished16:18:25.115Z, graceful exit0 after382.69s because available RAM
  reached7.76GiB (8GiB reserve). No timeout, task32GiB guard, forced cleanup or
  GPU reset. Driver cache saved274,931,081payload bytes. Counters998VS/2031PS/549CS
  indicate observed permutations, not unique shader failures or memory ownership.
- The earlier oversized-volume fatal did NOT reproduce and no diagnostic T#
  was emitted. Passing its earlier elapsed-time/count point is not a fix.
  Required admission CPU regression remains pending; additionally inspect
  actual cache/resource memory owners before choosing any memory correction.
- Native execution slot is idle after stream drain. Next: build one CPU batch
  target for checked journal reconstruction and the minimal volume regression;
  original-input attempt capture remains pending. No issue108 update published.

## Latest checkpoint: warm introduction, 2026-10-11 15:44 UTC

Primary branch: `yotei-integration-native`, documentation HEAD
`66e073b22f06b7080962a726a866e9038c3577ae`, engine baseline
`6d04103525b24559a496612ef8959a7666ce9a20`.

- Warm original-game run: `_Build/runs/game-20261011-154145-integration-warm-intro-9a03b0`.
  Used the verified baseline EXE `b3ed953555e91759b798aced8a04af324508ceaafd25a70a34d6d34c879c96ef`,
  private saves/cache and existing `KYTY_DEBUG_PAD_AUTOSWIPE=4` input simulation.
- Owned screenshot `window-1543.png` visibly shows a hand/cloth and Japanese
  writing in the introduction. Character movement, title menu and game scene
  entry remain unproved. The earlier SETTINGS/CONTROLS screen is recorded below.
- Natural exit321 at15:44:16.550Z after150.85s; no timeout, memory guard, GPU
  reset or forced cleanup. Minimum observed available RAM16.28GiB.
- Current fatal: `layout.total_size > UINT32_MAX`, `tile.cpp:1348`.
  Symbolized path: `TileGetTextureTotalSize -> ResolveTextureInto ->
  ResolveBindlessKey -> ResolveBindlessHeapEagerly -> PrepareBindlessHeaps ->
  PrepareBindings -> DispatchDirect`. This is an eager heap walk; an actual
  GPU request for this descriptor is not established.
- Next: build opt-in `KYTY_TEXTURE_LAYOUT_DIAGNOSTICS=1` instrumentation from
  the primary workspace, retaining the fatal. Collect original/effective T#,
  layout size/mips and eager/requested context. Establish a CPU regression
  before changing descriptor/layout admission. See `docs/emulator-test-debt.md`.
- Main private build directory: `_Build/windows-integration`; private install:
  `_Build/analysis/yotei-integration-20261011/runtime-built`. The ignored
  `_Build/windows-local.cmd` initializes vcvars64 and preserves prior installs.

## Shader collection and precompilation

The existing v8 journal has2999 successful permutation envelopes and2485
distinct code/stage identities (897VS/1560PS/542CS;20,679,749bytes). Read-only
framing, all2999 XXH3 checksums and an independent mirror of the current
Decoder/Valid contract passed; native ISA/IR/SPIR-V compilation is still pending.
Input SHA256: `c741440836772903e4ac6b29f9539afc35611aeeca659eaaf4c03b95e3169aff`.
There are2770 distinct record bodies,229 duplicate bodies and453 hashes with
multiple records. Journal writes happen after success and do not preserve
failed-generation inputs or every live userdata/memory callback needed for
exact replay. v8 also omits pixel-stage `sample_vgpr`, `target_shader_mask`
and `parameter_mode`, although current compiler paths consume them. Journal
replay must explicitly report reconstructed-default scope and no original
input equivalence; new captures must preserve these fields.

1. Validate journal framing/checksums and replay an exact record index in a
   bounded native CPU worker. Keep decoder, IR, SPIR-V and driver compilation
   phases separate; do not classify an unreadable input as a compiler defect.
2. Port compatible capture/runner diagnostics from the preserved prior branch.
   Archive original attempts before Translate/Emit and fallback, including
   main/back stage metadata, effective compiler policy, userdata and ordered
   memory callbacks. Mark incomplete speculative reads explicitly. Protect
   failure capacity from successful cached permutations.
3. Batch complete inputs, group overlapping failures by shared cause, then
   prove synthetic RED/GREEN and replay the affected batch after each shared
   correction. Preserve original errors and mark fallback-dependent rendering.
4. Precompile observed variants and reuse compatible SPIR-V/driver caches.
   New scenes can reveal new shaders, metadata variants and resources; an
   upfront list cannot guarantee that all future permutations are covered.

Current `--shader-bench` always creates a Vulkan device and lacks exact
record-index selection. CPU batch isolation and pre-failure capture are still
pending. Diagnostics do not authorize fabricated resources or success stubs.

Checkpoint: 2026-10-11. Primary execution path is now the main workspace branch
yotei-integration-native based on peepostarexx/YOTEI-BRANCH6d041035.
The previous branch remains intact as a source of verified tests and mechanisms.

## Open these paths first

- Primary worktree: G:\repos\KytyPS5
- Primary branch: yotei-integration-native
- Tested engine revision: 6d04103525b24559a496612ef8959a7666ce9a20
- Previous checkout: G:\repos\KytyPS5\_Build\worktrees\yotei-windows-bringup-5664f3ff, branch yotei-windows-bringup
- Previous completed workflow commit: 85494f562070a2d245e79dea4b43931d6d5d6baa
- Previous progress/history: G:\repos\KytyPS5\_Build\worktrees\yotei-windows-bringup-5664f3ff\docs\ghost-of-yotei-windows-plan.md
- Base research/decision: G:\repos\KytyPS5\_Build\worktrees\yotei-windows-bringup-5664f3ff\docs\yotei-base-comparison-plan-2026-10-11.md
- Shared context: C:\Users\fxpw\.codex\shared-context\README.md and newest KytyPS5 handoff
- WSL access: /mnt/g/repos/KytyPS5 and /mnt/c/Users/fxpw/.codex/shared-context

A session starting in the preserved previous checkout must use the primary
worktree for new integration work. Do not replace the previous branch or merge
the divergent histories wholesale. Documentation commits on the primary branch
do not change the engine code that was tested at6d041035.

## Main workspace relocation checkpoint

The user explicitly requested G:\repos\KytyPS5 itself on yotei-integration-native.
The former baseline worktree remains at _Build/worktrees/yotei-fork-baseline-6d041035,
detached at8ae1e676, with its independent dependencies/build files preserved.
Previous source branch5664f3ff is checked out separately at
_Build/worktrees/yotei-windows-bringup-5664f3ff. Its submodules are not initialized
there yet; initialize exact gitlinks only before a later old-branch build.

All original _Build artifacts and installed binaries remain in G:\repos\KytyPS5.
Main _Build/windows and its installed EXE belong to prior builds; they are NOT
validation of this newly checked-out fork. Do not copy the baseline CMakeCache
(it embeds the detached worktree path). Configure the primary native target
explicitly using vcvars64/local wrapper and a separate build directory before
new builds; retain old outputs/caches. No native build/game was run for relocation.

## Verified state

Fork native build and run:
- Private executable: G:\repos\KytyPS5\_Build\analysis\yotei-fork-verification-20261011\runtime\kyty_emulator.exe
- SHA256: b3ed953555e91759b798aced8a04af324508ceaafd25a70a34d6d34c879c96ef
- Run: _Build/runs/game-20261011-133628-fork-6d041035-baseline-fb3974 in the previous checkout
- Visually confirmed interactive introduction with writing-names prompt and
  SETTINGS/CONTROLS menu after Enter/Options. Character movement, open world and
  title screen remain pending. Observed6.2/9.1FPS is not a measured benchmark.
- Original game01.512, RTX5060Ti/Ryzen5950X, Performance1280x720,
  bindless + linear-image readback + raw image/buffer synchronization.
- Finished/drained14:06:33UTC:1800s timeout, graceful exit0, no memory guard,
  no forced cleanup/reset, minimum free RAM9.86GiB.
- Driver cache payload265360493bytes; disk265360640bytes.
- Full evidence: _Build/analysis/yotei-fork-verification-20261011/final-baseline-20261011T141306Z.md

Previous branch diagnostic workflow:
- Native capture6/6; unchanged fused front/back audit RED -> GREEN2/2;
  missing/corrupt inputs6/6 correctly classified as input_error.
- PowerShell5.1 orphan cleanup intended RED -> unchanged GREEN.
- Corpus825 distinct partial profiles:777 passes,47 failed+1timeout in39.048s.
  These are CFG/resource planning checks, not825 successful shader generations.
- Fifteen reason groups overlap68times. Reconciled48 inputs:
  RT19, HS-front input-boundary artifact6, resources9, dynamic PC7,
  FP64 literal4, FLAT D16 1, SharedBase1, timeout1.
- Reports and test-only FP64 draft: _Build/analysis/shader-batch-workflow-20261011.
  The FP64 literal draft is unapplied/unvalidated. The new fork already has a
  literal implementation; reassess groups on the new base before choosing fixes.

Previous native capture run:
- _Build/runs/game-20261011-142826-shader-batch-capture-warm
- Engine85494f56, private EXE975bdff7786276b8c96e182c858504e27c30d1f52bd3a0f404660f425327cdf5
- Finished/drained14:59:27UTC, graceful0 after1800s timeout; minfree8.88GiB,
  no memory guard/forced cleanup/reset. Owned screenshot remains black.
- Large modules346832/521303words; driver cache payload1236842005bytes.
  These measurements suggest expensive generation/compilation; causality and
  equal-interval performance comparison still need proof.
-5215 captures:4096 compiler requests +1119 runtime variants. Cached user-data
  changes exhausted the compiler quota before late real generation143514.
  Admission follow-up is now committed as ea9225c43dce9651a293bbb338b6c5b87235877c:
  archive actual Translate/Emit attempts, defer successful cached transcripts,
  protect failure capacity and preserve exact policy/push/back metadata.
  Native capture152027 passed6/6 with cache-flood/late-generation/reserve cases.
  Static review passed; the new original game retry remains pending. The old
  private EXE975b belongs to85494f56 and does not contain this follow-up.

## Next actions

1. Review the completed capture-admission commits and their exact validation state.
   Native CPU coordinator/reserve tests have passed; original retry is pending before
   claiming recovered late runtime captures. Transfer compatible diagnostics as small
   reviewed ports to the primary branch; keep original engine behavior unchanged.
2. Retry the primary native artifact with warm cache and bounded lifetime; verify
   mouse/touchpad input in the interactive introduction, then actual character
   movement and entry into a scene. Record screenshots, input, exact revision and
   executable hash. Bindless placeholders/default samplers remain experimental.
3. After the saved plan, implement the user-requested batch collection with
   diagnostic fallback. Capture the original failure and full input BEFORE using
   a fallback; retain stage, main/back bytes, user data, host/static policy,
   push offset and bounded runtime read transcript. Batch independent inputs,
   group shared reasons and preserve overlapping counts and replay commands.
4. Prefer existing valid alternatives such as structured-control dispatcher
   fallback or the original validated SPIR-V after optimizer failure. Keep
   experimental resource substitutions explicit. Never submit invalid SPIR-V or
   unbounded GPU loops. If no safe alternate path exists, record the case and
   continue independent offline workers; do not manufacture a correctness result.
   Runs/resources influenced by fallback are collection evidence, not proof that
   the original shader or game was fixed. Keep ordinary validation/error paths.
5. Fix one shared cause per batch with a synthetic intended RED -> unchanged
   GREEN, affected neighbors and GPU checks when needed. Rerun the affected group,
   then one original game retry. Reassess the old48 groups against fork output;
   do not reimplement fixes already present in this base.
6. Update this plan, test debt and shared handoff after each meaningful checkpoint.
   Commit completed changes separately. No push without current authorization.

## Constraints and recovery

Run one native build/test/game at a time. Check active owned processes before
resuming; prior sessions can leave bounded supervisors alive. Use the local
windows-local.cmd/vcvars64 wrapper, independent dependency worktrees, bounded
hidden workers, drained streams and owned-process cleanup. Preserve old worktrees,
original installed EXE454656, original eboot5178 and user SaveData.

Artifacts under the previous _Build are local evidence and are not in Git.
Do not remove that folder as routine cleanup. Preserve the seed manifests,
captures, private runtimes and compatible caches. Git stores the plan and source;
shared handoffs store the current checkpoint and artifact locations for Windows/WSL.

Issue108 publication is restricted to the user's menu/entry milestones with
accurate tested revision/scope and PR497 linkage. The upstream fork baseline does
not prove the published PR497 head. No new issue comment or push has occurred.
