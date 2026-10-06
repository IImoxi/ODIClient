# Uniform reuse regression analysis

Source: `Logs/terrain-v4.csv`. Phase boundary rows can contain mixed toggle state.

Session 1, uniform=0, terrain options=15: 19 rows (4 selected after edge trimming); median frame 1.280 ms (781 reciprocal FPS); 1616 intercepted uploads/frame; 0.0% skipped.

Session 1, uniform=0, terrain options=14: 2 rows (2 selected after edge trimming); median frame 2.888 ms (346 reciprocal FPS); 4607 intercepted uploads/frame; 0.0% skipped.

Session 1, uniform=0, terrain options=7: 1 rows (1 selected after edge trimming); median frame 2.595 ms (385 reciprocal FPS); 4465 intercepted uploads/frame; 0.0% skipped.

Session 1, uniform=0, terrain options=1: 29 rows (26 selected after edge trimming); median frame 2.574 ms (389 reciprocal FPS); 8992 intercepted uploads/frame; 0.0% skipped.

Session 1, uniform=1, terrain options=1: 31 rows (28 selected after edge trimming); median frame 5.928 ms (169 reciprocal FPS); 9168 intercepted uploads/frame; 85.0% skipped.

Session 1, uniform=0, terrain options=1: 5 rows (2 selected after edge trimming); median frame 3.047 ms (328 reciprocal FPS); 6903 intercepted uploads/frame; 0.0% skipped.

The enabled trial intercepted the native path and skipped many uploads. The regression is therefore not explained by an unused hook. The implementation adds thread/context queries, atomic generation/diagnostic operations and byte-cache inspection per upload. These costs can outweigh already cheap driver uniform calls; this is a code-based explanation, not a measured breakdown of those costs.

Counters themselves add overhead to both OFF and ON phases, with extra diagnostics when ON. The capture does not isolate trace cost from cache cost, so it cannot establish how fast the optimization would be without tracing. V2 removes per-upload EGL queries and replaces owner-thread atomic counters with local additions; its cost must be measured separately.

The first trial is withdrawn after this measured regression. Its replacement, Uniform reuse v2, is default OFF and retains native matrix uploads. Local benchmarking supports only a submission-level benefit for vector/integer reuse, not an in-game FPS gain.
