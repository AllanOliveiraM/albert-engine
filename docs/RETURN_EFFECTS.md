# Return gas and condensation

The render-only effects follow the same `Sample::parts` tail as entry plasma: pressure, heat flux, tank fill, Mach, alpha and beta. Engine flow and starts come from each bell's existing `EngineCycle`. No guidance, physics, mission events or scripted flight timers are changed.

## Files

- `src/render/return_effects.hpp`, `return_effects.cpp`: three preallocated shells per vehicle, pressure expansion, pressure-management estimate, condensation gates and quality budget. Vent positions reuse `core::stackVents`; filaments follow the animated fin/flap/chine pivots.
- `src/render/return_gas_shader.hpp`: the camera-independent WebGPU volume and analytic cold-gas filaments. Depth-tested, premultiplied gas, no textures, particle spawning, history buffers or screen passes.
- `src/render/vehicle.hpp`, `vehicle.cpp`: collect engine flow, weighted exit position, independent start ages and animated surface tips. Anchor the crisp analytic condensation cone at the first full-diameter shoulder (the booster hot ring). Fade out the existing per-engine outer envelope as the shared rarefied plume takes over; retain individual cores, diamonds and gimbals.
- `src/render/webgpu_programs.cpp`: register the gas shader and its uniforms.
- `src/render/objects.hpp`, `objects.cpp`: pass regional weather to vehicles and apply quality when a vehicle is first created.
- `src/render/application.cpp`: pass fresh weather humidity within 50 km of its reported location; otherwise use the existing marine fallback.
- `src/render/CMakeLists.txt`: compile the new render component.
- `tests/return_visuals.hpp`, `tests/visual_test.cpp`: GPU/state regressions and `return-captures` fixture using the actual booster, ship and tower appearances.

## Drivers

| Effect | Driver |
| --- | --- |
| Rarefied plume | Sum of burning engine flow; width/reach grow with inverse square root of ambient pressure. The shared envelope fades in below 4 kPa and fully replaces outer per-engine envelopes by 500 Pa. Reach is capped at 6 km, radius at 2.4 km. Blue-white at the apex, tan farther out; atmospheric sunlight and Earth shadow modulate scattered light, with faint residual emission. Applies on ascent as well as boostback. |
| Coast vents | Cold inventory from fill, engine-off state, smoothed fill/ambient-pressure change. Short independent UTC phases model unresolved valve releases at the shared tank/hot-ring outlet positions. They are free-running advection phases, never mission event clocks. |
| Transonic vapor | Relative humidity, ambient pressure and Mach. Hull condensation rises over Mach 0.82–0.89 and falls over 1.02–1.08. Its thin shoulder cone has a crisp leading face and a feathered trailing edge; scalar flicker follows simulated UTC within that Mach gate. Short 4–8 m downstream filaments leave animated grid-fin/flap tips and chines as air thickens; those wisps can exist away from the transonic hull cloud. |
| Ignition | Sum of individual engine-start envelopes, weighted by actual engine flow. Approximately 1.1 seconds per start; rapid relights and staggered starts each contribute. Orange/yellow billows share the atmospheric burn shell. |
| Retropropulsion | Opposing axial airspeed / 3300 m/s effective exhaust speed, with an ambient-pressure gate and actual burning flow. The collar spreads near the engine bay and rises outside the lower hull. Advected 3D value noise replaces angular harmonics. Each ray stops at the first finite-hull intersection; yellow gas at the base cools through orange and dull red to faint smoke as it rises. It vanishes as opposing speed falls; individual 13/5/3 engine plumes remain. The same calculation follows the ship through its tilted flip. |
| Post-catch | Previous observed burning, engines now off, Mach below 0.03 and cold inventory. Top outlets and a base parcel produce faint drifting haze. |

## Incremental cost

| Quality | Draws per vehicle | Fragment work |
| --- | --- | --- |
| High (2) | At most three pooled shells; normally one, occasionally two during pressure transitions | 12 bounded chord samples for exhaust/burn; burn samples use two noise octaves. Two exact cone crossings, up to six analytic tip filaments, four vent parcels and base mist |
| Medium (1, current application default) | Same draw cap | Six chord samples, one burn noise octave, two exact cone crossings, three tip filaments, two vent parcels and base mist |
| Low (0) | At most one rarefied envelope; other new effects off | Six chord samples |

The existing **Vapor e spray** quality setting selects these tiers; global low quality forces the low tier. Only visible shells submit draws. Hardware, individual engine cores, ignition attachment/purge parcels and existing launch-cloud/plasma costs are separate. Rarefied replacement removes one outer-shell draw per lit engine once fully expanded. Sphere geometry is shared across the three shells; all state and uniform updates are bounded and allocate no meshes per frame. Cost still scales with covered pixels, especially for a very close camera inside a large plume.

## Validation and captures

Build in `build/linux` with `/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2`. Run `/usr/bin/ctest -R visual --output-on-failure`. The added tests cover humidity, subsonic cutoff, opposing-flow direction, flash duration, pressure expansion, cutoff/coast, rewind, low-quality limits and metre/Earth-radius scale invariance. Image assertions also require white vapour at the upper shoulder rather than halfway along the hull, and check the narrower Mach window.

Generate the prescribed, render-only telemetry captures with:

```sh
ALBERT_GPU=software ./tests/visual_test return-captures ../codex/return-review
```

`build/codex/return-review` contains the source PPMs, PNGs and contact sheet, plus build, test and capture logs. Primary views: `boostback-ground-dusk`, `coast-vent-puffs`, `transonic-humid`, `landing-ignition-1200m`, `retropropulsion-250ms`, `landing-3-near-tower`. Supplemental views cover dry air, medium/low quality, five engines, post-catch venting and the ship's three-engine flip ignition/collar.

Validated in this worktree: all three requested build targets succeeded; both visual tests passed (38.73 s total). All 16 views rendered on llvmpipe (LLVM 21.1.8) with zero GPU validation errors. Capture logs and source PPMs are retained beside the PNGs; `comparison.png` shows the three reviewed views before and after. Protected guidance/physics sources have no diff; no commit was made.

## Review corrections

The shroud is a ray-integrated gas field with front-to-back attenuation and a finite steel-hull stop. Noise is rotated and advected in metres, with no angular/axial sine lattice. The already accepted rarefied plume and individual engine-plume shapes are retained. The ignition fireball shares the shroud draw.

The grey coast floor was the 300 km `PlaneGeometry` created only in `tests/return_visuals.hpp::returnCaptures`. It was at ground level, but its flat extent read as an elevated floor from the nearby high-altitude camera. It is now hidden throughout boostback, coast and transonic shots; the low-altitude views retain their ground backdrop. The production return component creates only three sphere shells; `vehicle.cpp` and `objects.cpp` create no such plane.

`landing-ignition-ground-tracking` uses the same 1.2 km ignition state as the close view, with a prescribed ground eye at (-900, 3, -1200) m, target (0, 1225, 0) m, and 6° field of view. `retropropulsion-250ms-next` advances the noise by 70 ms; `retropropulsion-250ms-reverse` checks wrapping from the opposite side. `coast-vent-puffs` verifies the pad plane is hidden. The gallery includes the corrected `landing-ignition-1200m`, `retropropulsion-250ms` and `transonic-humid` views.

## Limits

The exact reference glob was absent in this checkout. Available Flight 11 frames were reviewed read-only from `/unreal/allan/albert-engine/build/codex/referencias/ref-*.jpg`; the boostback jellyfish follows the requested description. These are bounded analytic gas fields, not CFD. Hull clipping uses a finite cylinder; this is conservative around a tapered ship nose and does not trace individual hardware apertures. The rarefied envelope follows the hull axis and weighted engine exit, rather than individual jet interactions or a persistent detached vacuum cloud.

No tank pressure, vent-valve state, local humidity aloft, or catch-contact flag reaches this renderer path. Vent demand is a visual estimate; release phases are cosmetic. Humidity uses the same fresh near-surface weather as coastal haze, with 78% fallback when unavailable. Airspeed uses the existing renderer's Mach × 340 m/s approximation. The post-catch estimate can also appear after another nearly stationary engine cutoff; joining a settled catch without observing its preceding burn lacks that history. Haze drift is bounded local advection rather than a world-space, wind-advected parcel simulation. Ground tracking captures use a prescribed ground telephoto pose, not a simulated camera operator.
