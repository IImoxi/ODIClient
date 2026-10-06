# GPU multi-draw trial capture analysis

Source: `Logs/terrain-v3.csv`. Repeated headers divide two game sessions.

## Session 1

| Phase | Rows | Duration (s) | Median frame (ms) | Median reciprocal FPS |
| --- | ---: | ---: | ---: | ---: |
| GPU OFF; terrain options 15 | 21 | 21.37 | 0.926 | 1080 |
| GPU OFF; terrain options 14 | 21 | 21.03 | 2.522 | 396 |
| GPU OFF; terrain options 15 | 23 | 23.02 | 1.269 | 788 |
| GPU OFF; terrain options 14 | 23 | 23.03 | 2.524 | 396 |

Driver capability is available in every row. Converted batches, commands, avoided calls and enabled-but-forwarded batches are all zero.

## Session 2

| Phase | Rows | Duration (s) | Median frame (ms) | Median reciprocal FPS |
| --- | ---: | ---: | ---: | ---: |
| GPU OFF; terrain options 14 | 24 | 24.08 | 0.836 | 1197 |
| GPU OFF; terrain options 15 | 2 | 2.00 | 2.414 | 414 |
| GPU OFF; terrain options 7 | 1 | 1.00 | 2.214 | 452 |
| GPU OFF; terrain options 1 | 24 | 24.02 | 2.573 | 389 |
| GPU ON; terrain options 1 | 24 | 24.03 | 2.569 | 389 |
| GPU OFF; terrain options 1 | 17 | 17.02 | 2.565 | 390 |
| GPU OFF; terrain options 0 | 6 | 6.00 | 2.485 | 402 |

Driver capability is available in every row. Converted batches, commands, avoided calls and enabled-but-forwarded batches are all zero.

## Interpretation

The enabled trial did not observe either hooked CPU fallback loop. Capability detection succeeded, but no calls were replaced or rejected by its batching conditions. The capture therefore does not measure the performance of GPU multi-draw substitution. It is consistent with Minecraft selecting a different native submission path, potentially its existing multi-draw implementation; that path is not established by these counters.

Phase medians above include menu/transitions/startup where present and are descriptive rather than controlled FPS comparisons. Session 2 has a long GPU-ON interval and still zero intercepted fallback batches. The lack of visible FPS improvement matches the recorded lack of acceleration.

The next useful measurement is the active native draw dispatch path: actual target identity and batch/command counts, before attempting another substitution. Repeating this trial unchanged is unlikely to add evidence.
