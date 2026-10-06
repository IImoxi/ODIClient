# Terrain rendering investigation

## In-game terrain trace v2

`ODI_TERRAIN_TRACE=/absolute/path.csv` enables timing around the existing list
callback after its build/ABI gates pass. It preserves native behavior and works
with Render disabled. V2 adds section counts, the enclosing terrain preparation
call, and four game GL import wrappers. The frame callback drains counters once
per second; no hook writes logs. Timing windows can straddle concurrent callbacks.
Wall time includes waits/preemption. The new preparation-thread CPU counter
excludes worker threads and helps distinguish execution from wall-time waits.
These counters overlap; they cannot be added into a whole-frame CPU breakdown.
See README for the CSV column definitions and an OFF/ON/OFF capture protocol.
No offloading or speedup is claimed by this diagnostic.

Inspected binary: Minecraft 1.26.52.3 Android x86_64, GNU build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`. Addresses below are ELF-relative;
the supported values and byte signatures live in `minecraft_build.h`.

### V2 hook observations and remaining coverage

Direct `objdump -d -M intel` on the exact ELF established the following:

- At `0xcdbb3b5`, the caller sets RSI/RDX to two stack Vec3 addresses, RDI to
  its builder pointer, and XMM0 to a scalar float. `0xcdbb3d2` calls `0xcd99720`;
  the next instruction reloads R14 and does not use a return value. RTTI naming
  identifies the known float/bool preparation operation; the wrapper forwards
  all three pointers, float and bool, without inspecting their contents.
- The callee entry saves XMM0, RDX and RSI in its native frame. Its previously
  inspected coordinate consumer (`0xcd9ab09` onward) performs section lookup
  and work/mesh-list selection. The broad call encloses more than the small
  lambda, but is not a measurement of all mesh generation or all world rendering.
- Only the five-byte call displacement is redirected. A nearby RX relay
  tail-jumps to the diagnostic wrapper; the original function entry is untouched.
  The full argument setup, entry and call/next-instruction signatures are gated.
  Failure keeps the original path; a retained patch keeps the relay alive and
  its wrapper forwards without tracing when diagnostics stop.
- ELF JUMP_SLOT imports map `glDrawArrays`, `glDrawElements`, `glBufferData`
  and `glBufferSubData` to the four recorded GOT/PLT pairs. Native wrappers at
  `0x1522bad0`/`0x1522bae0` tail-jump to the imported draw paths. The diagnostic
  batches GOT replacements through the manager and validates original executable
  pointers, rejecting unresolved PLT and self-recursion targets.
- Extension/proc-address and indirect/multidraw paths also exist in the binary.
  Their contracts have not been established here. GL counters cover only the
  named imports; zero/small counts cannot rule out heavy GPU or submission work
  through those other paths. UI/entities also use these imports, so no terrain-only
  draw attribution is claimed. Buffer bytes are request sizes, including null-data
  storage allocations, not measured transfers. Every 64th call has a wall-time
  sample; a sampled maximum is not the maximum of all calls.

REA opened the same SHA-256
`2540a1b65de5ed796215c33efc00e2f1dc7fdb8e806cb9783797557dd21fb002`.
A focused assembly request did not return while the provider prepared analysis
and was cancelled; the native session was closed. No REA procedure evidence or
Evidence ID is claimed. The observations above are bounded direct ELF/objdump
inspection. `test_native_build.py` checks actual signatures, call targets and
import relocation names. Executable fixtures exercise original argument
forwarding, the actual call-site/GOT patches, inactive forwarding, counter drains,
sampling, and logging failure. Real gameplay remains the next verification step.

## Implemented: camera-relative vertical section limits

RTTI identifies a `std::function` lambda from
`LevelBuilder::_prepareRenderChunkRenderList(const Vec3&, const Vec3&, float, bool)`.
The containing function begins at `0xcd99720`; the callback begins at `0xcdd6080`.
Its vtable is at `0x170362a8`, with the invocation slot at +0x30.

Closure construction at `0xcd99e85` establishes the following captures:

| Offset | Meaning |
| --- | --- |
| +0x18 | Pointer to the separate-camera-list boolean |
| +0x20 | Separate-camera output vector |
| +0x28 | LevelBuilder pointer |
| +0x48 | Reference world position (three floats) |

The callback at `0xcdd60c0` selects either that separate vector or
LevelBuilder +0x480. Each entry is three signed 32-bit coordinates, with a
12-byte stride; the vector header is begin/end/capacity (24 bytes). Native
coordinate-to-world calculations use 16-block sections.

The callback performs native frustum/distance checks, appends selected sections,
and processes dirty-section records. The hook always invokes it, then compacts
only its newly appended suffix. It retains preceding contributions and handles
native vector reallocation. Multiple-camera lists are subsequently merged and
their visibility masks are generated by native code, preserving alignment.

At `0xcd9ab09`, the consumer walks the coordinate vector and calls `0xccd43f0`
to look up each terrain render chunk before preparing mesh lists and selected
chunk work. This is the reason for filtering before that loop. Native cleanup
at `0xcd9b10c` resets the coordinate and visibility-mask lists for the next pass.

The implemented below/above toggles exclude sections whose entire vertical
extent is outside the selected distance from the reference position. They are
distance cutoffs, with no occlusion test. They can remove visible terrain.
The owner reports approximately 6% higher FPS with aggressive vertical limits.
No controlled gameplay benchmark has been performed here.

## Rear-camera filter removed after gameplay feedback

The client briefly summed each view's inward-facing left/right frustum planes to
exclude terrain sections behind a camera-relative plane. The owner tested it with
a zero-block margin and reported no visible or FPS difference. Bedrock already
performs frustum culling before this callback, so the additional filter did not
remove useful work in that test. The code, menu controls, and persisted fields
were removed; older `AUTOGG10` settings migrate while retaining vertical limits.

## Existing culling and further investigation

The callback already invokes frustum checks, including the six-plane sphere
test at `0xcce62f0` and an AABB test at `0x16b045d0`. Adding another generic
frustum test at this stage would duplicate existing work.

RTTI exposes None, NoneAsync, Manhattan, and DistanceFieldPerspective
LevelCuller implementations. The selector at `0xcce4df0` has multiple cases;
its callers and configuration semantics need more investigation before a safe
switch can be offered. No culler enum was forced and no algorithm was bypassed.

A visibility-based cave optimization would need conservative connectivity or
occlusion information plus correct invalidation after block edits, movement,
and camera changes. A vertical cutoff alone does not establish that information.
Entity/block-entity culling and rebuild throttling also need verified native
ownership and timing contracts before implementation.

Profile the render-list consumer, section rebuild scheduling, entity submission
and frame submission before choosing the next hook. Compare identical scenes
with Render off/on, including looking down from height and entering caves.
At high FPS the extra filtering pass may outweigh savings in an already sparse
scene; only measured frame times can establish a benefit.

## Added: horizontal terrain radius (2026-10-05)

Uses the same exact-build callback and copied reference Vec3. Sections are kept
when the squared distance from reference X/Z to the nearest point on their
16-block horizontal bounds is at most radius squared. This retains intersecting
and tangent sections and supports negative world coordinates. No extra native
hook, culler mode change, or mesh format change is needed. Radius is 16–256
blocks, defaults OFF at 128, and persists in AUTOGG18; older settings default it
OFF while retaining existing values. Vertical and horizontal limits compose.

Expected saving (inference): fewer terrain sections enter downstream mesh-list
preparation and drawing, reducing submitted geometry and pixel work. This does
not reduce the vertex count of each retained mesh, mesh storage bytes, chunk
loading, or the original callback's frustum/bookkeeping cost. Minecraft's own
render distance can also reduce loaded terrain; this control only filters the
render list. Visible terrain can disappear abruptly at section boundaries.
No gameplay speedup is established without identical-scene frame-time testing.

Verification includes tangent/diagonal bounds, negative coordinates, invalid
camera values, reallocated lists with previous-camera contributions, independent
packed settings, persistence/migration and existing executable ABI gates.

Further bounded native observation: direct objdump at 0xcce4df0 confirms a
six-case jump table (enum 0–5), allocations and distinct vtable assignments.
It does not establish which mode is active or which is faster. No enum is forced.
Per-face omission, vertex simplification and packed-vertex changes remain
unresolved: they require mesh builder/material contracts and invalidation after
camera or block changes. No such optimization is claimed by this cutoff.

REA attempt: opened the exact ELF with SHA-256
`2540a1b65de5ed796215c33efc00e2f1dc7fdb8e806cb9783797557dd21fb002`.
A focused `procedure_pseudo_code(0xcce4df0)` request timed out after 300 seconds;
no decompilation or Evidence ID was returned. Closed the session. The selector
observation above is from fallback objdump, not returned REA evidence. This
pass implemented the horizontal cutoff through the already verified hook;
face omission, mesh byte/vertex reduction, alternate culler selection, and
outside-game optimizations remain unimplemented and unverified.


## GPU multi-draw experiment (v3 trace)

The exact binary above was opened and closed in REA for this task. The evidence
below comes from direct ELF disassembly and byte checks, not REA decompilation.
`tests/test_native_build.py` checks both complete loops, their single-draw targets,
stride-32 callers and the fallback initialization sequence against the shipped ELF.

Arrays fallback at `0x1522ba20` loops over count in EDX, calling the pointer at
`0x1781cb20` with mode and command offset, adding signed stride ECX each time.
Elements fallback at `0x1522ba60` does the same through `0x1781cb28`, with count
ECX, index type ESI and stride R8D. Both return immediately for nonpositive count.
Verified callers at `0x15227076` and `0x15226ef0` pass 32-byte command stride.
Initialization at `0x1521bb35` assigns these fallbacks to arrays dispatch slot
`0x1781ca90` and elements slot `0x1781ca88`. Those slots can also receive native
extension implementations. They are deliberately left untouched.

The trial installs entry jumps into both fallbacks through the shared manager at
mod_init. Full loop signatures include all RIP-relative calls. Nearby RX trampolines
replay the stolen prologue and resume the original loops for positive count;
wrappers retain the original count guard. Relays survive Retained installation,
which never publishes acceleration readiness. No runtime patching or unhooking.

For an EGL GLES 3.1+ context advertising `GL_EXT_multi_draw_indirect`, the trial
loads the EXT array/element functions. Enabling it converts batches of at least
four commands, provided stride is aligned and at least command size, the offset
is aligned, VAO and indirect buffers are bound (index buffer for elements), and
transform feedback is inactive. Version, extension, unsupported state and a
changed context retain the original native loop. Stride zero must retain it:
the original repeats one offset, whereas EXT defines zero as tightly packed.
Function pointers/context are immutable after release-publication. No GL state
changes, glGetError consumption, buffer registration, copies or readbacks occur.

The operation is described by the [Khronos EXT specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_multi_draw_indirect.txt).
This uses existing GPU-resident indirect commands; it reduces CPU submission
when this fallback is active. It does not migrate meshing, use CUDA/tensor cores,
or prove which callers render terrain in the active user's context. Desktop GL is
excluded from the initial trial; its shader draw-ID contract is not established.
The driver-state queries cost CPU and can cancel any submission savings.

V3 appends capability/status, enable state, converted batches/commands, avoided
single calls and enabled-but-forwarded batch counts to the v2 CSV columns.
New helper captures use `Logs/terrain-v3.csv` to avoid mixing schemas. Counts drain
once per log window and can straddle toggle/window boundaries. A supported status
with zero avoided calls can mean Minecraft already uses another dispatch path.
Executable mock tests prove the actual patched entry, original trampoline, native
argument/offset progression, capability gates, state fallback and counters. They
do not establish NVIDIA rendering correctness or an FPS gain. Compare OFF/ON/OFF
in game with terrain distance filtering disabled and the same camera/settings.


## V3 user capture: no fallback substitution

`Logs/terrain-v3.csv` contains two sessions, 88 and 98 one-second rows. Session 1
kept GPU multi-draw OFF. Session 2 includes 24 consecutive enabled rows; all rows
report capability available, but every substituted-batch/command/avoided-call and
enabled-but-forwarded-batch counter is zero. Thus neither hooked fallback was
entered in the enabled interval. The unchanged image and FPS are consistent with
no substitution. These counters do not identify the actual selected draw path.
Comparable full-terrain phases in session 2 have median average-frame intervals
2.5725 ms OFF, 2.5686 ms ON, 2.5652 ms OFF (roughly 389/389/390 FPS). Some rows
include menu/transition activity; these are descriptive, not proof of a small gain.
The fuller session breakdown is in `Logs/terrain-v3-analysis.md`.

## Uniform reuse trial (v4 trace)

A bounded direct-ELF follow-up found instanced submission slots bypassing the
plain draw imports: elements `0x1781ca98`, arrays `0x1781caa0`. No runtime use is
claimed from static presence. Native occlusion queries use desktop SAMPLES_PASSED
and have inverse-condition consumers as well as normal visibility; their shared
query-result pointer also services timing queries. A global fake-result patch
would conflate those contracts, so this trial does not modify query behavior.

The shader-uniform decoder directly calls glUniform1iv at `0x15228130`, glUniform4fv
at `0x15228141` and glUniformMatrix4fv at `0x15228154`, after decoding native uniform
count/location/payload. The observed vector/matrix branches submit without a byte
comparison. Other direct uniform call sites exist in the enclosing submission
path. The active capture has not yet counted these imports; v4 does so in OFF and
ON phases. Current upstream [bgfx OpenGL source](https://github.com/bkaradzic/bgfx/blob/master/src/renderer_gl.cpp)
provides vocabulary/context only; it is not proof of this Minecraft build's code.

The opt-in cache hooks eight exact-profile GL imports through the shared manager,
including observed uniform setters and program bind/link/delete lifecycle calls.
Each original pointer must be executable and outside the PLT/unresolved/self
ranges. Shader uploads use exact bytes, not approximate float comparisons. Small
single-value updates are eligible; larger or unsupported writes invalidate cache
state before native forwarding, preventing overlapping array writes from leaving
an old cached element. Binds, link/delete, toggles and frame boundaries invalidate.
The first eligible upload in an interval queries the actual current program.
Caching is restricted to the observed EGL context and frame-thread identity;
other contexts/threads forward native calls and invalidate the cache generation.

This reduces CPU submission when repeated identical uploads exist. It does not
move meshing to CUDA, change shader computation or reduce render distance. Its
performance and visual correctness require the user's driver/gameplay capture.
The more invasive assumption is that native shader writes use the covered imports;
another mod or unobserved proc-address write can defeat tracking. Default OFF,
bounded lifetime and immediate OFF forwarding keep the experiment reversible.
The cache also assumes native uploads use valid uniform locations/types; it does
not consume glGetError to determine whether a forwarded call was accepted.
V4 appends enable/availability, intercepted uploads, skipped calls/bytes and status
plus eligible/owner-rejected/uncacheable counts to explain a zero-skip capture,
without mixing schema into the previous file. Trace counters are disabled in
normal launches, independent of whether the optimization toggle is enabled.

REA opened and closed the same native ELF for this follow-up. No deep-provider
procedure evidence is claimed; address/ABI observations are direct objdump/ELF
checks centralized in `minecraft_build.h` and `tests/test_native_build.py`.

Mock import/lifecycle tests and real headless GLES pixel checks pass. The real
check verifies skipped duplicate matrix/vector uploads, changed red/green pixels,
toggle/reset behavior and no GL errors. This is Mesa test evidence, not NVIDIA
in-game rendering or measured FPS evidence.


## V4 regression and narrower v2 replacement (v5 trace)

`Logs/terrain-v4.csv` records one session with 31 GPU-cache-ON rows. After trimming
phase boundaries, median frame interval rose from 2.574 ms OFF (~389 FPS) to
5.928 ms ON (~169 FPS). The active interval intercepted about 9,168 uploads per
frame and skipped 85.0%; owner rejections were zero. The regression occurred on
an active optimization path, not an unused hook. The capture does not isolate the
cost of the per-upload EGL/thread checks, atomic diagnostics and byte comparison.
`Logs/terrain-v4-analysis.md` contains the phase calculation and limits.

V1 is replaced by **Uniform reuse v2**, default OFF. EGL-current-context checks
occur at frames and program binds. A seventh installed hook observes the game's
`eglMakeCurrent` import (slot `0x17597128`, PLT `0x16c642f0`), invalidates the cache
around a switch and records whether the owner context is current. Per-upload
thread checking remains; unobserved external context switches remain a limitation.
Only the established frame thread touches cache entries or local trace counters;
other threads use separate atomic counters, drained together on the frame thread.
Word loads via memcpy preserve exact bits/alignment semantics, including signed
zero. No per-upload EGL query or atomic diagnostic increment remains on the owner
path. Epoch loads still protect native lifecycle/cross-thread invalidations.

The local headless software-GL benchmark exercises raw calls, intercepted OFF and
cache ON with tracing both OFF/ON, three repetitions of 204,800 calls per API and
program/frame invalidation every 4,096 uploads. It includes identical values and
values changing every seventh call (85.7% duplicates), matching the measured reuse
rate approximately. On llvmpipe, vector/integer reuse beat raw API submission;
matrix reuse was marginal or slower. Therefore only vector and integer setters
are intercepted in the installed v2 trial: program link/delete/bind, glUniform4fv,
glUniform1i/glUniform1iv, and EGL make-current, seven patches total. Matrix GOT
entries stay original. The exact profile still validates all nine related imports.
Valid matrix setters cannot modify cached vector/integer-typed uniforms within a
linked program; v2 retains the valid-native-call assumption stated above.

This benchmark is CPU submission evidence on Mesa, not an RTX/NVIDIA FPS result.
Real GLES pixels, program lifecycle and patched imports are checked separately.
New captures use `Logs/terrain-v5.csv`, preserving all earlier captures. Existing
uniform counter columns now cover the three intercepted vector/integer setters,
not native matrix submissions. Use OFF/ON/OFF with other Render trials disabled.


## Broader frame-stage profiling (v6)

Uniform reuse v2 remains available, default OFF. The v5 capture showed about
93.5% of intercepted uploads skipped and selected medians 423 versus 440 reciprocal
FPS, but no final uniform-OFF interval. The small apparent gain is provisional;
keep the feature while measuring the larger frame, rather than claim a repeatable
speedup from that capture. Detailed phase selection is in `Logs/terrain-v5-analysis.md`.

The exact ELF unwind data identifies the native GL submission function at
`0x15221e70` through `0x15227e5f`, with normal return at `0x15227cdb`. RTTI at
`0x3679006` names `bgfx::gl::RendererContextGL`; constructor `0x15219a3d` loads
vtable `0x17484990`. Relocation at vtable slot `0x17484b28` (+0x198) targets the
submission entry. Caller `0x15204208` through `0x1520422d` loads four pointer
arguments (renderer, frame, clear quad, text blitter), calls vtable +0x198 and
ignores the return. Entry, constructor, complete caller and normal-return bytes
are centralized and gated. The diagnostic patches only that vtable pointer at
mod_init and always forwards the original four arguments. No native objects are
retained beyond the call; only the stable original code pointer survives.

The stage covers the native GL submission pass, including GL driver work, context
changes, query/debug handling and any nested swap/callback work. It excludes
command processing around the caller. `Context::renderFrame` at `0x15203ff0` was
identified as a broader lead, but its integer-return/timeout ABI is not fully
validated and it is not hooked. The game import for `eglSwapBuffers` is slot
`0x175970f0`, PLT `0x16c64280`. Its passive wrapper returns the original unsigned
EGLBoolean unchanged. Submit and present install independently; failed/retained
stages remain inactive while their permanent original forwarding stays usable.

Host frame instrumentation records wall/CLOCK_THREAD_CPUTIME_ID stamps around
ODIClient's entire callback, software FPS limiter and rendering-overlay block.
A same-thread gap from previous callback completion to next entry measures work
and waits outside ODIClient. Thread identity changes discard that gap, incrementing
a reset counter rather than subtracting unrelated CPU clocks. No blocking GPU
queries, fences, per-upload timestamps or GPU readbacks are added.

All instrumentation is enabled only by the existing trace environment. The native
hooks are installed only for that opt-in capture; ordinary launches do not patch
these sites. Disabled callbacks avoid clock reads. CSV writes remain once per
second in the frame callback; all new hooks only update counters. Counts and CPU
sums exclude worker threads, and nested stages cannot be added into a whole-frame
breakdown. Presentation may route elsewhere or include the client callback. Wall
minus thread CPU is an approximate waiting/preemption indicator, not GPU busy time.

Executable fixture tests cover native prologue/epilogue and four-pointer forwarding,
all gates, unchanged EGL return, independent hook failures, precise fake wall/CPU
stage totals, gap thread resets and disabled no-clock forwarding. The selected
profile checker validates ELF relocation, signatures and EGL import symbol.
REA opened and closed the same binary; these native observations come from
bounded direct ELF/unwind/disassembly evidence, not a deep-provider decompilation.
