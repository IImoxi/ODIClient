# Physically inspired sky

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27. Exact gates live in
minecraft_build.h. Evidence is material extraction and bounded ELF/objdump
inspection; no REA decompilation evidence is claimed.

## Essential findings

- The old shader treated the native fog blend weight as ray elevation; a
  constant weight could collapse the whole sky to the horizon color. Compiled
  source also did not establish a linked, active draw.
- Sky has standard and instanced ESSL 3.10 vertex variants. Sky and Clouds
  share the same simple fragment interface, so replacements must preserve
  v_color0 and require both custom vertex/fragment markers before activation.
- The new replacement consumes the verified sky draw and renders a full-screen
  triangle. Rays come from inverse u_viewProj, including infinite-far projection;
  native mesh colors/shape and clear-color recoloring are no longer involved.
- It supplies its own single-scattering atmosphere, sun, moon and stars. Native celestial draws are suppressed only after a custom
  sky draw in the same frame/context. Disabling restores native shader behavior.
- Shared hooks observe program binds on the actual GL thread, direct draws, and
  instanced/indirect pointers returned by the game EGL lookup. The verified
  eglGetProcAddress slot/PLT are 0x175970d8/0x16c64250. Already cached pointers
  remain outside interception coverage. Shader matching alone never enables the
  full-screen vertex path.

## Evidence and remaining checks

Real Mesa GLES pixels verify full coverage with constant native colors, both
vertex layouts, day/night variation, rotated/translated cameras, infinite far
planes, foreground depth, native Clouds compatibility, toggle fallback and GL
state restoration. Environment separately reports compilation, linking and an
observed full-screen draw. The owner confirmed the replacement looks good in
Minecraft. The atmosphere now blends across the apparent horizon over one
degree; that adjustment still needs visual confirmation. Optional cached GPU clouds now add two separate layers: lower volumetric cumulus and higher wispy cirrus. See the cloud section below.
The custom sun has an angular glow; a saved vanilla sun/moon toggle defaults
OFF and forwards native SunMoon materials while suppressing procedural disks/glow.
Sun disk/glow fade with sun elevation and are softly masked below the horizon.
Twilight sky colors come from the Rayleigh/Mie integration; the added artistic
lower-sky haze was removed. Deep-night atmosphere brightness scale
to 55%, preserving star and moon brightness.
Sky controls share a group without intervening descriptions. Camera/sun
orientation, additional depth/view passes and GPU cost remain unverified.
The model does not implement multiple scattering.

Sky, the ignored legacy Clouds slot and Vanilla sun/moon persist in AUTOGG24 config fields 47–49;
older versions migrate with sky OFF, clouds ON and vanilla sun/moon OFF.
The artificial twilight color boost was reverted at the owner’s request.
Appearance needs owner review.

## Always-enabled atmosphere optimizations

The production renderer always enables all three optimizations. Atmosphere lookup renders the
same integration and horizon blend into a 256×256 RGBA16F GPU texture, using
sun-relative horizontal direction and squared elevation spacing to concentrate
samples near the horizon. It refreshes when the celestial angle moves more than 1/4096 of a day
(about 0.088 degrees), or the reduced sample setting changes. The angle delta
wraps across the day boundary; sun/moon directions still update every draw. Half resolution atmosphere
renders only that background at ceil(viewport/2) each draw and samples it with
linear filtering during full-resolution compositing. The half pass samples the lookup. Reduced atmosphere samples uses 6 view and
3 sunlight samples rather than 8 and 4 in either direct or cached integration.
Sun/moon disks, glow, stars remain in the final full-resolution
pass. Interpolation and fewer samples can change gradients/colors; these are
quality tradeoffs, not pixel-identical replacements.

Texture passes preserve framebuffer, viewport, program, VAO, texture unit,
texture/sampler binding, unpack buffer, color masks, depth, scissor, stencil,
dither and multisample coverage state. Unsupported RGBA16F render targets or
programs fall back to the direct shader. Private resources are bounded to four GL contexts per thread,
reuse resized targets, and live until their owning context is destroyed; they
never delete GL objects from a thread destructor or another context. Lookup
metadata survives native program invalidation and is isolated by context.

AUTOGG25 appends lookup, half resolution and reduced samples at fields 50–52;
These legacy fields remain readable/writable for config compatibility but no longer control rendering.
GLES tests alone can vary quality to compare all seven combinations across day, twilight and night,
standard/instanced parity, state restoration, cached lookup reuse across small time changes and day rollover, changed time
and sample quality, fallback, offset/odd viewports and restoring the baseline.
Hardware performance and visual quality still require in-game comparison.

## Optional cached cloud layers

Clouds defaults OFF, including migration of the previously ignored cloud flag. AUTOGG31 keeps that toggle at field 48 and appends detail, view samples and resolution at fields 60–62. Controls are conditional on Environment, Physically inspired sky and Clouds. The existing 16-control Environment page accommodates all controls, including status.

Two RGBA16F GPU atlases store a broad cumulus coverage field and detailed 3D density field as 16 height slices, a wispy anisotropic cirrus field, and a fixed periodic small-scale detail volume in alpha. Cumulus combines coverage, fractal value noise and cellular edge erosion, with a 1.2–4.0 directional height range (3.5 times the original depth) and density-dependent tower tops; cirrus uses elongated noise with curved, patchy coverage. All generation runs in a fragment shader. Atlas tile size is 64/128/256 for Low/Medium/High, with total density storage 1/4/16 MiB per owning context. Seeds smooth-blend over forty seconds; after the initial pair, only one atlas redraws at each boundary. Camera rotation does not invalidate density. Skipped periods regenerate the pair; blending stays continuous at normal boundaries. Animation reuses the frame limiter timestamp, without a clock syscall or GPU readback. Fixed celestial time does not freeze cloud evolution.

A quarter/half/full-resolution RGBA16F layer is shaded for the current camera each draw (default half), using 8–64 view samples (default 24). Two distance detail levels blend detailed/coarse density and reduce distant sample counts; horizon/distance fades prevent a hard cutoff. Three bounded sun-path density samples shade cumulus interiors; forward/back scattering lobes enhance sun-facing light, with three exponential attenuation terms approximating softer multiple scattering. Light transitions from warm sunrise/sunset to daylight, dim twilight and dark blue-grey ambient/moon illumination at night. Cirrus sits at a higher directional altitude of 6.2 and composites behind cumulus. Clouds obscure procedural celestial details and follow the Weather changer night/storm darkening. The layer is composited before gamma conversion. Two extra cached alpha samples provide fixed cellular fine-scale density erosion, fading with distance; sunlight samples retain coarse density for lower cost. The alpha volume uses fixed seeds and is identical in both animated atlases, including at transitions. High detail strengthens erosion. Per-pixel march offsets reduce visible horizontal sample bands; low-resolution temporal sampling cycles their phase. No extra texture, render pass, texture unit or CPU noise work is added for this detail.

This is a direction-anchored sky approximation: no wind translation, camera-position parallax, terrain shadows, physically integrated multiple scattering or full weather simulation. Density generation is cached, but view-dependent shading runs each draw; Quarter/Half resolution reuse GPU screen history with camera reprojection, while Full renders directly. CPU work is limited to bounded settings/time bookkeeping and GL calls. GPU cost and frame pacing remain hardware-dependent, including the periodic density refresh. Cloud passes reuse the atmosphere's offscreen GL-state scope and the existing four-context resource bound; unsupported cloud resources leave the sky running without clouds and report a settings status.

GLES checks cover both layouts, day/dawn/dusk/twilight/night pixels, detail/sample/resolution choices, identical static alpha detail in both evolving seed fields, resource reuse under camera changes, exactly one density draw at a normal forty-second boundary, smooth boundary pixels, skipped-period recovery, toggle/failure fallback, hostile texture/sampler units and offset/clipped viewport restoration. Temporal checks also cover accumulation, reduced error against full-resolution rendering, camera rotation, FOV/time/weather cuts, quality/toggle resets, frame gaps and fallback. Larger software-rendered previews are generated in build/sky-cloud-*.ppm. In-game appearance, native celestial routing, sun alignment and RTX frame pacing still need owner review.

## Photon styling and low-resolution reconstruction

Reference inspected: sixthsurge/photon commit `15458c0937f8647c37eb6a501bef5eb3bf3da31b`.
The independent implementation uses rounded coverage banks, cellular erosion,
curled cirrus, forward/back light scattering and softer interior illumination.
No Photon source or assets are copied. Reference techniques are visible in its
[cumulus shader](https://github.com/sixthsurge/photon/blob/main/shaders/include/sky/clouds/cumulus.glsl)
and [cloud upscaler](https://github.com/sixthsurge/photon/blob/main/shaders/program/d2_clouds_upscaling.fsh).

Quarter/Half shade a cycling subpixel grid, then resolve into ping-pong full-size
RGBA16F history. Previous camera matrices reproject the direction; neighborhood
clamping and opacity rejection limit stale edges. Projection changes, celestial
or weather jumps, quality changes, resize and gaps of 250 ms reset accumulation.
This adds one fullscreen GPU pass and 16 bytes per display pixel of history
storage (about 32 MiB at 1080p) per owning context. CPU work remains bounded matrix
and settings bookkeeping with GL calls; no readback or CPU cloud generation.
Unsupported resolve resources fall back to the ordinary cloud layer.
Visual similarity and RTX performance require an in-game comparison; this is
Photon-inspired styling rather than an identical renderer.
