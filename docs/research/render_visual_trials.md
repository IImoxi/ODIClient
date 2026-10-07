> Historical experiment record: the failed trial implementations, regression tests
> and runtime native profiles were removed. Native findings below are retained
> as research, not active features.

# Native visual render trials

Target: Minecraft 1.26.52.3 Android x86_64, ELF build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`. These are bounded static findings,
not measurements of execution or FPS. Profiles are in
`minecraft_build::current::visualTrials`; all controls are OFF each launch.

## Evidence and behavior

`V1` — ELF `.rela.dyn` identifies RTTI `28ParticleSystemInterfaceProxy` at
`0x16fe81d0`, its vtable at `0x16fe7fe0`, legacy particle creation at slot
`0x16fe8008` (`0xc37a250`) and modern emitter creation at slot `0x16fe8010`
(`0xc37a280`). The backend owns both interception points and exposes typed
settings through `render_visuals.h`. No existing backend owns these slots;
Particles owns the separate attack hooks and calls the native critical emitter.

`V2` — Legacy proxy `0xc37a250` forwards its six arguments to ParticleEngine
`0xc421ea0`, supplying the modern engine as an additional native argument. It
returns null itself when its legacy engine is absent. The engine also has
ordinary null-return paths. Reducing legacy particles samples creation before
native allocation, simulation and rendering; it does not skip gameplay ticks.
Density 0–100 defaults to 50, distributed deterministically over each 100
requests. Existing particles expire normally. This affects legacy numeric
particles, not every resource-pack effect. Null is native behavior, but in-game
coverage and the cost avoided remain unmeasured.

`V3` — Modern emitter proxy `0xc37a280` has four pointer arguments and no
returned object. It checks engine `this+0x28`, copies its final parameter into
a temporary, calls engine `0xc3714c0`, and destroys its temporary. The absent
engine branch goes directly to its epilogue `0xc37a328`. Hide emitters returns
before temporary creation, matching that branch; it suppresses new data-driven
particle effects. Existing emitters continue until their ordinary expiry.

`V4` — WeatherRenderer RTTI `15WeatherRenderer` points to typeinfo
`0x17032b50` and vtable `0x17032900`. Its render preparation function
`0xcd330b0` receives four pointer arguments through three direct call sites
`0xccdf65e`, `0xccdfa46`, `0xccdff6f`. Its entry tests weather intensities at
render-data offsets `0x2a2c`, `0x2a30`, `0x2a38`, `0x2a3c`, `0x2a40`,
`0x2a44` and a render-context flag. The empty-weather branch returns before
mutating state or building its seven effect groups. Hide weather skips these
three calls. Other weather/world updates and the separate native WeatherRenderer method
`0xcd35230` remain intact. Rain, snow and related weather visuals disappear;
there is no reason to expect a dry-scene gain.

## Verification and limitations

Actual ELF entry and call bytes were checked. Runtime installation validates
build ID, executable mappings, signatures, the two expected proxy vtable
pointers, and all weather call sites before the shared hook manager installs
one five-patch batch. A near relay preserves the original call ABI and trailing
instructions. Unsupported builds leave behavior native.

`tests/test_render_visuals.cpp` exercises OFF/not-ready forwarding, density
limits and distribution, six legacy arguments and return value, emitter
arguments, every signature/build/vtable rejection gate, and executable native
fixtures through the installed weather calls and proxy vtable hooks.

These changes trade particle/weather visibility for less CPU and GPU work.
Their activity and FPS effect need in-game checks with comparable weather and
particle-heavy scenes. Entity ticking, mesh generation and world updates are
not intercepted. Binary Ninja's existing ELF view was inspected, but focused
function queries returned no analyzed function at these locations; the ABI and
behavior evidence above comes from ELF relocations and bounded disassembly,
not an invented decompilation or observed runtime.

V8 exposes weather calls skipped, legacy particle requests/skips and modern
emitters skipped, drained once per trace row. Counters update only while the
corresponding switch is ready and ON. The tests cover exact counts and drains.
