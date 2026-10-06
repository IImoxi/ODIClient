# Uniform reuse v2 capture analysis

Source: `Logs/terrain-v5.csv`. Phase grouping uses the recorded uniform toggle and terrain-options bitmask. Row states describe the end of the window; phase-edge windows may be mixed. Two rows at the start and one at the end of each longer phase are trimmed.

| Phase | Rows | Selected rows | Median frame (ms) | Reciprocal FPS | Uploads/frame | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Uniform OFF; terrain bits 1 | 51 | 29 | 2.3642 | 423.0 | 6316 | 0.0% |
| Uniform ON; terrain bits 1 | 32 | 29 | 2.2735 | 439.9 | 6465 | 93.5% |
| Uniform ON; terrain bits 0 | 4 | 4 | 2.4443 | 409.1 | 6479 | 93.6% |
| Uniform ON; terrain bits 1 | 2 | 2 | 2.7865 | 358.9 | 6485 | 93.6% |
| Uniform ON; terrain bits 0 | 5 | 1 | 2.9857 | 334.9 | 1483 | 87.9% |

All rows report uniform capability available and zero owner-rejected calls. The v2 optimization entered the native vector/integer upload path and skipped duplicates while enabled. Matrix uploads are not included in these counters.

The selected comparable phases show a small apparent change from 423.0 to 439.9 reciprocal FPS (about 4.0%), with median frame intervals 2.3642 versus 2.2735 ms. There is no final uniform-OFF phase: later changes toggle the terrain master while uniform reuse remains ON. The capture therefore does not establish a repeatable FPS gain. Terrain state changes and gameplay/menu/movement differences can confound small frame-time changes. The trace does not identify the remaining dominant CPU/GPU stage. Unchanged visuals are expected for correctly skipped duplicate uniform writes and do not indicate an inactive trial.

Keep the trial OFF for normal play until a repeatable gain is demonstrated. The next useful investigation is a broad render-frame CPU timing breakdown rather than another uniform-reuse variant.
