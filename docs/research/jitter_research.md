# Projection jitter experiment

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27.

Observed through `readelf -rW` and `objdump -d -j .plt`: the
`glUniformMatrix4fv` JUMP_SLOT is 0x175579f8 and its PLT is 0x16be5490,
beginning `ff 25 62 25 97 00`. `tests/test_native_build.py` checks the symbol,
relocation type, signature and PLT target against the installed ELF.
The REA target was opened and identified, but its focused procedure search
timed out; no decompilation or runtime evidence is claimed.

Observed in shipped ESSL 3.10 sources extracted with `strings` from
`assets/assets/renderer/materials/RenderChunk.material.bin` and
`Actor.material.bin`: both use `u_proj` for clip position. RenderChunk separately
modifies projection column 2 from `SubPixelOffset`. Existing native jitter remains
untouched; this experiment adds its offset to the uploaded projection.
The existing custom Sky uses `u_viewProj`, so the wrapper supports that and
`u_modelViewProj` when `u_proj` is absent. Selecting the first active projection
uniform avoids modifying multiple projection-bearing matrices in one program.
Shaders that consume more than one such uniform remain outside verified coverage.

Design: shift clip x/y by 2 × pixelOffset/viewportDimension times clip w.
The Samples slider chooses 2, 4 or 8 positions: the original diagonal ±¼-pixel
pair, four quarter-pixel square corners, or eight dispersed positions within
±7/16 pixel. Every pattern has zero mean. Sample mode and phase publish together
once per frame, so all eligible draws in that frame receive the same offset. The phase advances once per launcher finished-frame callback,
including while disabled. Native input matrices and clip depth/w are untouched.
Only the default framebuffer, full-window viewport and focused gameplay are
eligible. Nonfinite matrices, affine/orthographic matrices, matrix arrays,
transpose uploads, unknown uniforms and other passes are forwarded unchanged.
There are no histories, textures, camera writes or native targeting changes.
Live uniform-location queries avoid relink/context/program-ID cache invalidation.

Real software GLES checks execute a fixture-installed import wrapper, verify
distinct raster samples for all 2/4/8 positions, centered repeating cycles,
consistent offsets within a frame (including setting changes), byte-identical
native input/HUD matrices and exclusions for other viewports/framebuffers.
Client tests check the enable, focus and menu gates. Native program routing,
per-frame upload frequency, default-framebuffer world coverage and performance
remain unmeasured. The owner reports good smoothing resembling 2× AA from the
original pattern; higher-count appearance needs verification. This is a
perceptual experiment, not conventional accumulated TAA or guaranteed N× MSAA.
