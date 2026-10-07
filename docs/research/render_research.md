> Historical experiment record: the failed trial implementations, regression tests
> and runtime native profiles were removed. Native findings below are retained
> as research, not active features.

# Render investigation

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27. Exact addresses and signatures live
in minecraft_build.h. These notes retain findings and limits; README documents
the opt-in CSV format and capture protocol.

## Terrain section filters

The verified render-list callback already performs native frustum and distance
checks. The implemented filters compact only sections appended by that
callback, preserving earlier camera-list entries and native visibility-mask
handling.

- Vertical limits exclude sections wholly beyond the chosen distance; the
  optional horizontal radius keeps any section whose 16-block X/Z bounds
  intersect the radius, including tangent bounds and negative coordinates.
- These filters can remove visible terrain. They do not reduce chunk loading,
  retained-mesh size, or the native callback's initial work. The owner reported
  about 6% FPS gain with aggressive vertical limits, without a controlled
  benchmark; horizontal-filter benefit is unmeasured.
- A rear-camera plane filter was removed after no visible or FPS difference;
  native culling already handled the tested case. No alternate culler, cave
  occlusion, mesh simplification, or face omission is implemented.

## Opt-in render trace

Set ODI_TERRAIN_TRACE to an absolute CSV path to install diagnostics. The frame
callback drains once per second; diagnostic-only hooks are absent in ordinary
launches. Shared sky draw hooks do no timing without opt-in. V7 includes render-list/preparation, selected GL imports and supported
instanced/indirect EGL proc-address draws,
ODIClient callback/limiter/overlay, native GL submission, and EGL swap stages.

Wall time includes waits and preemption; thread CPU counters exclude workers.
Stages overlap and must not be summed as a whole-frame breakdown. GL counters cover the whole game, not terrain alone. Previously cached proc
pointers, multi-draw extensions and unrecognized aliases are outside coverage; buffer bytes
are requested sizes, and sampled API times are not GPU execution. Wall-minus-CPU
is only an approximate wait/preemption indicator. This trace does not skip work
or establish a speedup. Findings are bounded ELF/objdump analysis; REA
decompilation attempts timed out.

## GPU multi-draw trial

The default-off GLES 3.1/GL_EXT_multi_draw_indirect trial substitutes the two
native CPU fallback loops only for eligible batches (at least four commands
with supported stride, buffers, VAO, and no transform feedback). Unsupported
state forwards to native code. A capture had 24 enabled, capable rows but zero
substituted batches or avoided calls: the hooked fallbacks were not entered in
that interval. This provides no evidence of an FPS gain; Minecraft may use
another dispatch path.

## Uniform reuse trial

Uniform reuse v1 regressed in one capture: median frame interval changed from
2.574 ms OFF to 5.928 ms ON while about 85% of uploads were skipped. V1 was
replaced by default-off v2, which only checks small vector/integer uploads and
observes program/context lifecycle; matrix uploads remain native. A Mesa
software benchmark favored vector/integer reuse but found matrix reuse marginal
or slower.

One later capture skipped about 93.5% of intercepted uploads and showed selected
medians of 423 versus 440 reciprocal FPS, but it lacked a final OFF phase. The
apparent gain is provisional, not a repeatable performance result. Real
headless GLES pixel/lifecycle checks pass on Mesa; in-game driver behavior and
performance remain unverified. The cache assumes relevant native writes use the
observed imports; other write paths can cause stale uniforms.

## Frame-stage measurements

V6 hooks are installed only for the opt-in trace and always forward native
calls. Host and native stages use wall and calling-thread CPU clocks without GPU
queries, fences, or readbacks. Native submission may include driver work and
nested swap activity; the broader renderFrame path was not hooked because its
ABI was not established. Timing is diagnostic only and cannot identify GPU busy
time.

## Investigation after sky optimization (2026-10-07)

Owner reports all three sky controls improved FPS by roughly 10–50%, with the
atmosphere lookup likely the largest gain. Clouds are disabled. Existing Render
trials have no reported improvement except render distance. These observations
do not establish the current terrain/entity CPU versus GPU bottleneck.

Evidence is bounded static disassembly of the supported Minecraft ELF (build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`); REA/Ghidra procedure search timed out.
No runtime access to the owner's open Binary Ninja session was available.

- Native submit `0x15221e70` uses dynamic function slots for indexed instanced
  draws (`0x1781ca98`), array instanced draws (`0x1781caa0`) and indirect
  multi-draw (`0x1781ca88`, `0x1781ca90`). Older named-import counters cannot
  establish how many draws this renderer submits. V7 extends the existing
  shared EGL lookup wrappers to count and sample instanced/indirect calls.
- Native submission already caches GL state and some camera matrix calculations.
  Query-result retrieval is present at `0x15226e00`; its runtime use and stalls
  are unmeasured. Adding another generic culler/cache lacks evidence of benefit.
- A conditional transient-buffer path deletes/recreates index and vertex buffer
  names before allocation/upload (`0x15222370`, `0x15222396`, `0x15222452`,
  `0x15222479`). Reusing names with native orphaning is a possible follow-up,
  but this branch's activity and cost have not been measured. No patch installed.
- DataDrivenRenderer render entry `0xbcee2d0` includes controller/attachment
  work and ownership/allocation operations. Skipping alternate render calls
  or caching its whole result has unproven side effects. Bounds helper
  `0xbcf0790` merges attached children; simple base bounds could clip them.
  This does not establish a CPU skinning bottleneck or a safe GPU replacement.

Next evidence: fresh V7 capture with the usual sky optimization enabled, both
Render trials disabled, FPS pacing disabled where practical, and comparable
stationary/walking/entity-heavy intervals. Sampled API timings remain CPU-side,
not GPU execution. Zero extension counters may indicate missed cached pointers;
no performance gain is claimed by this diagnostic change.

## Replacement experiments (2026-10-07)

The owner reports no improvement from the uniform/multi-draw experiments.
They are retired from the Render menu. Native multi-draw hooks and its frame
maintenance no longer initialize. Uniform vector/integer/EGL cache hooks and
cache frame maintenance are also absent in normal launches. The shader-source,
compile and program link/delete/use services remain for Sky.
Terrain-distance controls remain. Six new independent switches default OFF
each launch. No FPS gain, including the requested 5% or 2×, has been measured.
Visual evidence is recorded separately in [render_visual_trials.md](render_visual_trials.md).

`C1` — Bounded ELF disassembly of native submission `0x15221e70` shows
transient index-buffer unbind/delete/reset/gen/bind at `0x1522235c–0x152223a8`
and vertex-buffer equivalent at `0x15222436–0x1522248e`. Both then call
`glBufferData(NULL, capacity, GL_DYNAMIC_DRAW)` and `glBufferSubData`.
Reuse switches branch at `0x15222368` / `0x1522244a` only when the existing
name is nonzero. They preserve capacity setup, the vertex target assignment,
all registers and flags, and native orphaning/upload/cleanup. Initial allocation
and OFF behavior use the original sequence. This avoids GL name churn without
adding context queries, allocation or a second ownership cache. The renderer's
branch activity was not observed; `vertex_buffer_reuses` / `index_buffer_reuses`
in V8 establish whether it ran. Retaining an object changes deletion's effects
on GL container references; native cache/VAO behavior still needs in-game
verification, so disable if geometry changes. No direct GLES/driver-performance
gain is claimed from the headless test.

`C2` — Native occlusion draws use `GL_SAMPLES_PASSED (0x8914)` at
`0x15226e49–0x15226e69`, with unsigned result retrieval at `0x15226e00`
through dynamic slot `0x1781ca30`. The result is written to the native renderer
array at `+0x20300`; native query-ring handling retires the request afterward.
The earlier native query drain (`0x15222da5`) already uses availability
`0x8867`; this overflow/reuse path instead requests result `0x8866` directly.
The trial intercepts only this latter call and checks availability first. An
unfinished result becomes 1 (visible), keeping native retirement/reuse and all
other queries unchanged. This sacrifices occlusion efficiency for less waiting;
more GPU work can outweigh the saved CPU wait. V8 counts checks and conservative
results. Timing remains CPU-side; a trace has not established a stall or gain.

`C3` — `tests/test_render_trials.cpp` executes the actual gated instruction
sequences and new relays, including ON/OFF/initial-zero buffer transitions,
capacity/target preservation, signature rejection, available/nonavailable
occlusion responses and activity drains. A surfaceless real GLES test runs the
patched creation/reuse sequences then native-style orphan/upload/draw, checks
pixels through five transitions and verifies GL errors remain absent. This
covers basic GL behavior, not Minecraft's complete cache/renderer lifecycle.
`tests/test_native_build.py` checks every new sequence/call/slot against the
installed supported ELF. Visual proxy/weather bridge tests are separate.

The first pass is bounded to these verified paths. Moving chunk meshing or
skeletal animation to compute, renderer-wide batching, distant-entity culling,
name-tag suppression and shadow cuts remain unresolved. Binary Ninja's open
view was inspected, but the relevant functions were not analyzed there;
relocations and bounded ELF disassembly supplied the verified ABI evidence.
No runtime profiling or exhaustive renderer audit is implied.
