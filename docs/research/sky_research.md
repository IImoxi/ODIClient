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
degree; that adjustment still needs visual confirmation. Procedural clouds and
its menu toggle have been removed, including cloud noise and clock reads.
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
