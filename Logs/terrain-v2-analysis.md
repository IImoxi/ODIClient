# Terrain trace v2 analysis

Source: `terrain-v2.csv`. User reports RTX 5060, render distance 17, standing and moving with Render ON/OFF. Both optional hook groups installed. The user confirms the approximate order: standing ON, standing OFF, moving OFF, moving ON. Exact movement boundaries remain inferred from section-count changes; the CSV does not record movement.

| Selected interval after capture start | State / scene | FPS | Frame ms | Preparation wall ms/call | Calling-thread CPU ms/call | Sections before → after |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 19.2–35.2 s | Standing ON | 796.5 | 1.256 | 0.046 | 0.045 | 622.0 → 63.0 |
| 39.2–46.2 s | Standing OFF | 398.8 | 2.507 | 0.196 | 0.194 | 622.0 → 622.0 |
| 46.2–63.2 s | Moving OFF | 365.6 | 2.736 | 0.219 | 0.215 | 661.0 → 661.0 |
| 64.2–72.2 s | Moving ON | 728.5 | 1.373 | 0.054 | 0.052 | 625.0 → 75.4 |

FPS is the reciprocal of the frame-count-weighted average interval, not an average of per-window FPS. Startup/exit and toggle-transition windows are excluded; the first settling windows after entering the world and switching OFF are also excluded from the stable-scene comparison. All remaining active windows should be retained for a broader workload comparison. These sequential intervals do not prove equal scene/workload conditions.

The stable ON scene retains 63 of 622 candidate section contributions (10.1%). Preparation falls from about 0.196 ms OFF to 0.046 ms ON, a saving of 0.150 ms/call. The stable-frame difference is about 1.252 ms, so this measured stage accounts for only about 12% of that observed difference. Preparation wall and calling-thread CPU times are close: this stage is mostly executing rather than waiting. Calling-thread CPU excludes worker threads.

DrawArrays and DrawElements counters are zero across the entire capture. This establishes a coverage gap: the active draw path bypasses the instrumented imports. It does not establish zero rendering or a particular draw extension. Static investigation previously found proc-address/indirect draw paths; cached function pointers are another possible bypass. No terrain draw-submission or GPU execution cost is established.

BufferData had 35,664 calls and 558 timed samples, averaging 0.981 microseconds/sample. BufferSubData had 684,690 calls and 10,699 timed samples, averaging 0.900 microseconds/sample. Some individual samples are slower; the largest BufferSubData sample (0.342 ms) is during startup. Sampled timings exclude counting overhead and do not capture every stall. BufferData request sizes include allocations with no source data and are not transfer bandwidth.

Next target: verify and instrument the actual draw-dispatch path, then establish GPU timing if supported without blocking results. Avoid choosing GPU offloading from the two measured terrain-preparation stages alone.
