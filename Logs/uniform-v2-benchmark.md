# Uniform reuse v2 local benchmark

Command: `LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-gpu-uniform-cache --benchmark`

Renderer: llvmpipe (LLVM 23.1.1, 256 bits). Three repetitions of 204,800 calls per API; frame/program reset every 4,096 calls. Units: ns per loop iteration, including upload selection and API/cache submission. This is a CPU submission microbenchmark on software GL, not NVIDIA hardware or in-game FPS.

| Input | API | Raw | OFF, no trace | ON, no trace | OFF, trace | ON, trace |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Identical | 4fv | 20.42 | 23.73 | 11.69 | 27.81 | 10.74 |
| Identical | Matrix4fv, untouched | 26.03 | 19.05 | 18.96 | 19.07 | 19.00 |
| Identical | 1i | 20.97 | 25.50 | 10.24 | 29.16 | 9.48 |
| 85.7% duplicate | 4fv | 23.17 | 27.37 | 16.86 | 30.80 | 16.01 |
| 85.7% duplicate | Matrix4fv, untouched | 28.37 | 21.18 | 21.06 | 20.94 | 20.96 |
| 85.7% duplicate | 1i | 25.64 | 29.43 | 16.51 | 33.60 | 15.92 |

Vector/integer caching improved this local loop. Matrix entries are not patched in the final trial; all matrix rows invoke the same native function. Differences between matrix columns reflect benchmark variance/loop cost and cannot demonstrate optimization. OFF uniform wrappers still have overhead relative to raw calls. The earlier broad cache failed in game; only an OFF/ON/OFF user capture can establish whether the narrower v2 helps the actual workload.

Tests also passed for native import patching, original matrix slots, EGL switches including failure/restoration, shader lifecycle and array invalidation, signed-zero exactness, no EGL-current-context queries during uploads, and real GLES red/green pixels.
