# Fixed FPS pacing with reduced input delay

2026-10-08. Minecraft 1.26.52.3 Android x86_64, GNU build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`, ELF SHA-256
`2540a1b65de5ed796215c33efc00e2f1dc7fdb8e806cb9783797557dd21fb002`.

The current FPS Limiter retains only fixed 30–480 FPS pacing and the
**Reduce input delay** option. Adaptive pacing, its workload observer relay,
pre-input GOT hook, capacity estimator and active wait have been removed.
Old saved adaptive values are read for compatibility, ignored, and normalized
to disabled on the next save. No settings format change is needed.

Native hooks install only when both the fixed FPS Limiter and Reduce input
delay are enabled in saved settings at startup. Otherwise game instructions
remain untouched. Enabling later requires restarting Minecraft; disabling an
installed override restores the native candidate/interval immediately on the
next frame callback. The two relays retain their exact-build gates, RX code
and separate RW data, without a launcher-specific input hook.

## Evidence and findings

The existing limiter runs in `client.cpp::onFrame`, the launcher swap-buffer
callback that also draws overlays over the completed game frame. Its sleep
holds that frame before presentation. This is source evidence; the user's
reported latency has not been independently measured.

Binary Ninja exists at `~/.local/opt/binaryninja/binaryninja/`, but the installed
distribution exposes no Python package/headless API. REA opened this ELF with
Ghidra, but `search_strings` failed during import before analysis became available.
No REA procedure evidence was returned. The following static observations use
GNU objdump, ELF exception-frame boundaries, and string/immediate references.
They do not claim runtime execution or recovered original source.

The exception-frame header bounds a main-update procedure at
`0xa993990–0xa9984c0`. Its profiling strings include `Minecraft Game - Update`,
`Frame rate limiter` (`0xa997ad9` string reference), and
`Minecraft Game - Frame update` (`0xa997c3d` string reference).

* Option registration at `0xc3e0e7e` references `gfx_max_framerate`, ID `0xf6`;
  `0xc3e0fb7` references `gfx_max_framerate_advanced`, ID `0xf7`.
* `0xa996437` obtains a candidate cap through option-reader vtable slot `0xaa0`.
  Subsequent branches can reduce it using advanced graphics/platform limits.
  The full getter semantics and launcher overrides were not recovered.
* `0xa9966a7` tests the final integer cap in `r15d`; the positive path converts
  it to float and computes `1000 / FPS` milliseconds. The numerator constant at `0x27be554`
  is 1000.0. The zero/negative path can obtain an alternate interval from a service.
* Both interval paths converge at `0xa996a57`:
  `movss %xmm0,-0x328(%rbp)` (eight bytes). No saved option is written here.
* `0xa996a6e–0xa996aaa` subtracts the elapsed monotonic time since global
  `0x17633340` from the requested millisecond interval (multiplied by 1,000,000), converting to microseconds.
* Native exemptions test main-object mode fields at `+0x2bc`, `+0x2b8`,
  `+0x2c4`, plus a service pointer. Nonpositive remaining time also skips waiting.
* `0xa996b05` selects a `sched_yield`/steady-clock loop for a native candidate
  cap of at least 21; otherwise `0xa996b34` calls the native sleep helper.
  `0xa996b48` records the next monotonic baseline. Later frame-update work remains
  native. Precise launcher event polling/input consumption order is unresolved.

Reproduce the principal disassembly:

```sh
objdump -d --start-address=0xa99641a --stop-address=0xa996c43 \
  ~/.local/share/mcpelauncher/versions/1.26.52.3/lib/x86_64/libminecraftpe.so
python3 tests/test_native_build.py
```

The owner's manual Binary Ninja export is `build/fps-native-ps.txt`.
Its displayed addresses are 0x400000 above the ELF addresses used here.
At displayed `0xad966b1` it loads float 1000, and at `0xad96aa6` it
converts milliseconds to nanoseconds, subtracts elapsed nanoseconds and divides
by 1000 to obtain microseconds. The raw ELF constant confirms these units.
The first experimental implementation incorrectly supplied `1 / FPS`, making
the native budget 1000 times too short. The owner reports that experimental mode does cap FPS and overrides the vanilla
setting, but its input lag remains worse than vanilla pacing. The unit bug alone
does not explain that observed cap: the standard callback fallback can also pace
frames when no native override is observed. Which path ran has not been measured.
The corrected implementation supplies `1000 / FPS`.

The owner subsequently verified reduced input lag with vanilla capped at 60
and experimental set to 120, but unlimited FPS when vanilla was unlimited.
The positive-cap branch at `0xa9966a7` explains this dependency: unlimited
can follow a service path that bypasses the interval store. A second relay now
substitutes the local candidate in r15d before that branch, without writing
saved options. It replays the native test/conversion and both branch targets.
Executable fixtures verify unlimited, positive and negative vanilla candidates,
requested caps and restoration when disabled. Runtime results remain to verify.

## Current fixed limiter

The saved, default-OFF **Reduce input delay** toggle substitutes the local interval store
with two near relays gated by the exact build ID, interval conversion, store and
wait signatures in `minecraft_build.h`. Its RW data page contains the requested
float interval and a count of overridden stores; its code page is RX. There are
no native object pointers, ABI calls, GPU flushes, timer changes or saved native
option writes. The interval relay preserves registers, flags and XMM state; the candidate relay
changes r15d and replays the native test/conversion for that candidate. Disabling stores
zero in the relay and replays the original instruction; the hook remains installed
until process exit. An incomplete rollback retains an inactive relay.

The overlay samples the native store count for status. An installed native hook
now always avoids a second wait over the completed frame, including callbacks
without a new store observation. Only an unsupported hook uses the original
limiter. Previously, a missing observation triggered the late wait; this could
contaminate the native-pacing comparison. Whether it happened in the owner’s
trial remains unmeasured. Native-exempt paths may run without the requested cap. This count observes interval overrides, **not actual
sleep duration**. Native exemptions and already-over-budget frames still skip
waiting. Native yield-vs-sleep selection still uses the native candidate cap,
so the experiment does not change that policy when overriding the interval.
VSync and compositor/GPU queues remain unchanged.

Inference: reusing the wait stage that the owner finds responsive may improve
latency compared with sleeping in the overlay callback. That improvement is
unproven; this stage's position does not establish where every input is consumed.

## Verification and remaining checks

`tests/test_fps_limiter.cpp` executes the real relay in a native-stack fixture,
checking candidate branch activation/restoration, inactive replay, float override, preserved RAX/flags/XMM0, build/signature
rejection, duplicate rejection, clamp bounds, missing-callback avoidance of late waits, standard
pacing overruns, and overlay clock continuity. Settings tests cover persistence,
old-version default-OFF migration and malformed-flag rejection. These checks do
not measure hardware input-to-photon latency.

For in-game comparison, enable FPS Limiter, select the desired FPS and toggle
Experimental native pacing. Restart Minecraft after rebuilding. Compare both
modes at the same achieved FPS with VSync disabled, first at 60 FPS against
Minecraft's own cap, then at 120/144/240 FPS. Check camera response, frame-time
consistency and CPU usage in gameplay, inventory and custom-menu screens.
Verify that disabling restores Minecraft's saved cap. For a measured claim,
use repeated high-speed input-to-photon measurements; average FPS alone does
not measure latency. Background/loading/native-exempt modes may not follow
the requested native cap. Input order, effective pacing and latency remain
unverified in the actual launcher/game runtime.

## Previous GPU queue drain experiment (replaced)

The owner reports that native pacing improves perceived response while the game
holds its cap, but delay returns when FPS falls below that cap. This is consistent
with GPU render backlog, but does not prove it: CPU frame duration or compositor
queueing could also contribute.

The previous saved, default-OFF GPU queue drain toggle called host `glFinish` after
all ODIClient overlays, before returning from the swap-buffer callback. It only
runs with FPS Limit and Experimental native pacing enabled, a supported native
hook, and focused rendering. The shared EGL adapter resolves the function from
the host GL context. Missing API/context skips the wait and exposes a settings
notice. No textures, sync objects or context resources are retained by the drain;
turning it off stops calls immediately on the next callback.

`glFinish` completes commands previously submitted to that GL context. Waiting
there prevents those commands carrying a backlog into subsequent callbacks.
It does not drain compositor/display queues, track another context, or relocate
Minecraft input polling. Serializing CPU/GPU frame work may reduce throughput;
no improvement is promised for CPU-bound drops. The existing native cap remains
an upper bound, without adaptive-cap heuristics or GPU timing estimates.

This is an intentionally simple queue-draining experiment, not NVIDIA Reflex.
NVIDIA describes Reflex as just-in-time CPU/GPU scheduling with render queue
control and latency markers:
https://developer.nvidia.com/blog/optimizing-system-latency-for-esports-with-nvidia-reflex-sdk/
GL completion semantics:
https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFinish.xhtml

Verification: real GLES pixel/state tests exercise the drain before blur setup,
with hostile GL state, absent contexts and context replacement. Client callback
tests cover default-OFF behavior, native support/master/toggle/focus gates,
unavailable status and immediate disabling. Settings tests cover round-trip,
malformed flag rejection and version 28/27 default-OFF migration. No hardware
input-to-photon improvement has been measured.

Compare the same demanding scene below the configured cap with GPU queue drain
OFF and ON. Record achieved FPS and camera response; a responsiveness gain with
some FPS cost would support render backlog as a contributor. Existing
`ODI_TERRAIN_TRACE` timing counts the drain in FrameLimiter totals, so waiting
there is not mistaken for CPU game work. Those timings still do not establish
input-to-photon latency or GPU execution duration.

### Owner trial: GPU drain did not improve response

The owner reports no perceived latency improvement with GPU queue drain enabled.
The implementation already uses full `glFinish` completion of submitted commands
on the current context, so there is no stronger completion level to select.
This negative result does not identify the bottleneck or prove the render queue
was empty. End-of-frame completion occurs after input for that frame may already
have been sampled and can reduce CPU/GPU overlap.

The next useful investigation is a verified wait point before launcher input
polling/simulation, with previous-frame GPU completion/timing as feedback. The
current exact-build native interval hook is not verified to precede launcher
input polling. The installed `/usr/bin/mcpelauncher-client` is stripped
(build ID `6ae7b170284e1379605edfe736ea4d61d5a57899`) and its dynamic symbol
search exposes no FakeLooper/pollAll/startSendEvents entry; no early-input hook
has been established by this bounded search. No more aggressive wait or unsafe
hook was added in response to this trial. Native pacing can remain enabled;
The end-frame GPU drain was unsuccessful for the owner's reported workload and has now been replaced.

## Previous early GPU pacing experiment (removed)

2026-10-08. The installed debug package supplied matching source and symbols,
resolving the earlier dynamic-symbol search limitation. Local evidence:

* `/usr/src/debug/mcpelauncher-linux/mcpelauncher-manifest/mcpelauncher-client/src/fake_looper.cpp`
  maps ALooper_pollOnce to the pollAll lambda. pollAll calls startSendEvents,
  processes Android descriptors/input queues, and then window pollEvents.
* `/usr/lib/debug/usr/bin/mcpelauncher-client.debug` identifies the resolved
  pollOnce lambda at `0x2a3e80`, pollAll at `0x2a3370`, and startSendEvents at
  `0x12cf70`. Installed executable disassembly corroborates the lambda's tail
  jump to pollAll, its call to startSendEvents at `0x2a33ab`, and window
  pollEvents through vtable slot 6 at `0x2a34d3`.
* Minecraft imports ALooper_pollOnce through GOT `0x17557830`, PLT `0x16be5100`.
  The first main-loop call is `0x8b2a090`; repeated calls at `0x8b2a0b2` drain
  more events. At `0x8b2a0d3` it subsequently swaps Android input buffers.
  A third caller `0x8553496` is Swappy's NDK choreographer thread and is excluded.
* The original poll arguments/results remain unchanged. The wrapper checks the
  return address for the first main-loop call, so other loopers or repeated
  calls cannot consume a frame marker. Runtime installation checks Minecraft's
  build ID, call/PLT signatures, resolved launcher target signature and launcher
  GNU build ID `6ae7b170284e1379605edfe736ea4d61d5a57899`. Unsupported launcher
  builds leave early pacing unavailable without disabling native FPS pacing.

REA opened the launcher, but headless string analysis did not return within the
bounded wait; cancellation and close were requested. No REA procedure evidence
is claimed. These findings come from the shipped ELF, matching packaged debug
source/symbols, and focused objdump output.

The existing child setting is now **Early GPU pacing**, retaining the same saved
flag. At the end of a focused callback it creates an EGL_KHR_fence_sync marker
and explicitly flushes its GL context; it no longer calls glFinish or waits at
frame end. Before the next first input poll, the wrapper waits for that marker
on its EGLDisplay. EGL client waits work without a current GL context, and the
marker was already flushed by its producer. Only one pending marker and one
in-flight waiter are owned. Repeated callbacks replace pending markers;
waiters take sole ownership under a short lock and release it before blocking.
At most twenty 2.5 ms waits are requested (50 ms total); driver scheduling can
overshoot requested timeouts. Cancellation generations stop stale waits without
consuming replacement markers. Missing context/API, creation failure, or wait
error/timeout is reported; native input forwarding continues.

Master/native/child disabling immediately cancels the mode. Focus is checked
using existing cached launcher focus state before input polling and each frame;
a focus transition not yet delivered can still incur the bounded wait.
An active settings notice means a marker was consumed by the intended input
hook and completed. This confirms routing, not input-to-photon improvement.
Early wait timing contributes to FrameLimiter trace totals, outside callback
wall timing. Compositor/display queues, previously queued events, other native
input consumers and other rendering contexts remain outside this experiment.
No GPU-duration prediction or adaptive cap is implemented.

Tests execute the actual installed GOT wrapper from a synthetic first-poll call,
checking wait-before-original ordering, all output pointers/return forwarding,
unrelated/disabled callers, and game/launcher signature/build rejection. EGL
fixtures cover one wait per marker, no GL context on the waiting thread,
wait error/timeout, cancellation/re-enable during a wait, display replacement,
missing extension and creation failure, and exact marker ownership. Real GLES
checks cover pixels/state and actual fences across context changes. Settings
persistence is unchanged from version 29.

After restart, enable FPS Limit, Experimental native pacing and Early GPU pacing.
Confirm the **Early GPU pacing active** notice, then compare OFF/ON in the same
scene below the configured cap, checking both FPS and camera response. The
placement is now before the verified input route; actual latency benefit remains
unmeasured until owner testing or external input-to-photon measurement.

## Removal validation

Executable fixtures cover fixed cap/store relay register and flag preservation,
vanilla unlimited/positive candidates, disabled native replay, 30–480 limits,
unsupported-hook fallback and absence of an additional overlay wait. Client
checks exercise the master/Reduce input delay combination. Settings checks
load an old enabled adaptive config without losing native or unrelated options,
then verify that retired slots save as disabled. The native ELF checker now
validates only the two retained FPS relay sites and their interval/wait gates.

Earlier GPU pacing sections above describe removed experiments only. No GPU
wait or input-poll interception remains in the current limiter. Actual input
delay and FPS require an otherwise identical restarted-game comparison.
