# Sea arrivals

Splashdown is a rendering response to an observed flight. It does not steer, rotate, float or sink a vehicle. The flight environment must have `water` enabled; its configured ground height is the water datum, just as in its buoyancy model. A dry flight at the same altitude produces no steam, foam or waterline clipping.

A landing plume aimed down toward water charges a white steam bank below 140 m nozzle height. Its footprint follows the projected exhaust axis, with a three-second response; wind advects the accumulated bank. Steam becomes denser and taller as the vehicle descends. Engine glow warms the bank independently of sunlight and decays with a 0.35 s time constant after cutoff. The field uses the launch cloud's deterministic noise, finite ellipsoid integration, sunlight density probe, premultiplied blending and interior-camera handling, with a separate water density and lighting model. There is no launch dust or trench shape.

Sixteen render-only hull slices estimate immersion. First contact triggers one spray crown, with strength scaled by observed entry speed. A later broadside immersion (axis below 65% vertical and more than 18% wet) triggers a larger crown. Radial sheets rise and fall ballistically, broken into angular fingers and noisy mist. These are one-shot events, so paused contact does not continually create spray. The physics remains the source of all vehicle motion.

A second mesh draws expanding foam rings, ripples and irregular foam rafts. It drifts more slowly than the steam and decays over 95 s; it remains visible at 60 s. Steam fades smoothly between 35 and 55 s after impingement ceases, then its draw is culled. Hull materials clip submerged fragments and darken and smooth a wet band above the waterline, including hinged surfaces and scaled tank frost. Wave heights are sampled at vertices and interpolated along the hull, keeping WebGPU's existing 16-fragment-texture budget.

Sunlight lights the white spray. An optional faint analytic primary rainbow depends on the viewing ray's angle to the antisolar direction; it is not a camera-attached graphic. It can be absent from these particular views. No negative-base shader powers are added; the new water shaders contain no `pow`.

## Files

| File | Change |
|---|---|
| `src/core/scene.hpp` | Optional internal `WaterSurface` telemetry: rotating water origin, east/north axes, wind and contact. No public wire change. |
| `src/core/scene.cpp` | Validates, interpolates and carries the water frame during Earth rotation extrapolation. |
| `src/rpc/flight.cpp` | Publishes the water environment and its contact state in scene samples. A local frame is anchored near arrival and follows the rotating body. Reads physics; does not modify it. |
| `src/render/splashdown.hpp` | Bounded response state and interface in sea-frame ENU metres. |
| `src/render/splashdown.cpp` | Steam charge, footprint, wind drift, entry events, decay, quality and two fixed meshes. |
| `src/render/splashdown_shader.hpp` | Water-specific steam, ballistic spray, sun shading, night glow, rainbow, foam and ripples, reusing the launch bank's ray integrator/noise. |
| `src/render/objects.hpp` | Owns per-flight water responses and propagates effects/quality settings. |
| `src/render/objects.cpp` | Places effects independently of vehicle attitude and floating origin; observes motion and resets/removes effects with their flight. |
| `src/render/vehicle.hpp` | Hull-local water plane interface. |
| `src/render/vehicle.cpp` | Transforms the plane for each surface and hinge without world-space float subtraction. Also guards empty cryogenic-mist slots on rewind, exposed by the new integration regression. |
| `src/render/vehicle_surface.hpp` | Submerged-fragment removal and wet darkening/roughness, in both quality paths. |
| `src/render/webgpu_programs.cpp` | Registers both water shaders and samples hull wave height in the vertex stage. |
| `src/render/application.cpp` | Clarifies that the existing water effects switch and three-tier setting cover splashdown too; saved setting keys are preserved. |
| `src/render/CMakeLists.txt` | Compiles the new response. |
| `tests/splashdown_visuals.hpp` | CPU integration, real WebGPU regressions, controlled capture sequence and warmed tier timings. |
| `tests/visual_test.cpp` | Runs the regressions and exposes `splashdown-captures DIRECTORY`. |
| `docs/SPLASHDOWN.md` | Behavior, captures, costs, reproduction and limits. |

## Reproduction

From `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

From the repository root:

```sh
ALBERT_GPU=software ALBERT_AUDIO=0 LP_NUM_THREADS=2 \
  build/linux/tests/visual_test splashdown-captures build/codex/splashdown/final
python3 build/codex/splashdown/collect.py
```

The capture fixture uses the actual `shipAlone()` appearance, the existing FFT ocean, atmosphere and sunlight at 960×720, 1× MSAA on llvmpipe. No imagery/network requests are needed. These are controlled render inputs, not a flown physics replay: nozzle height is `120 - 6*T` m until touchdown at T=20 s; the axis tips between T=21 and 26 s and the centre subsequently lowers slowly. Wind is `(2,-0.6,0)` m/s ENU. The chase camera is `(145,-185,110)` m; the buoy camera is `(75,-215,2)` m. Both cameras keep a fixed aim through the sequence. The main matrix uses medium quality.

PPMs are direct renderer readbacks; PNGs are lossless conversions. The local conversion/contact-sheet/timing script is [collect.py](../build/codex/splashdown/collect.py). All captures and logs are ignored build outputs.

## Captures

[Contact sheet](../build/codex/splashdown/final/sheet.png).

| Stage | Chase | Buoy |
|---|---|---|
| Burn, 60 m nozzle height (T=10) | [PNG](../build/codex/splashdown/final/burn-60m-chase.png) | [PNG](../build/codex/splashdown/final/burn-60m-buoy.png) |
| Burn, 15 m nozzle height (T=17.5) | [PNG](../build/codex/splashdown/final/burn-15m-chase.png) | [PNG](../build/codex/splashdown/final/burn-15m-buoy.png) |
| Touchdown burst (T=20.4) | [PNG](../build/codex/splashdown/final/touchdown-chase.png) | [PNG](../build/codex/splashdown/final/touchdown-buoy.png) |
| Tip-over/broadside burst (T=26.5) | [PNG](../build/codex/splashdown/final/tip-over-chase.png) | [PNG](../build/codex/splashdown/final/tip-over-buoy.png) |
| 60 s after touchdown (T=80) | [PNG](../build/codex/splashdown/final/after-60s-chase.png) | [PNG](../build/codex/splashdown/final/after-60s-buoy.png) |

Supplemental night burn: [chase](../build/codex/splashdown/final/burn-night-chase.png), [buoy](../build/codex/splashdown/final/burn-night-buoy.png).

## Cost

Each affected vehicle adds at most two draws: one steam/spray volume and one foam mesh. After steam disperses, only the foam draw remains. There is no particle pool, simulation grid, additional texture, history buffer, full-screen pass, shadow map or allocation proportional to time. The fixed meshes contain 693 + 9,409 vertices and about 0.53 MiB of vertex/index data per active vehicle. The outer shell stays below 142 m horizontal and 101 m vertical radii, and foam below 230 m radius. CPU work includes 16 immersion slices. Costs scale with the number of vehicles; there is no global cap across an arbitrary fleet.

| Tier | Maximum primary samples/ray | Density / sun-probe noise octaves | Draw cap |
|---|---:|---:|---:|
| Low | 12 | 1 / 1 | 2 |
| Medium (default) | 20 | 2 / 1 | 2 |
| High | 32 | 2 / 2 | 2 |

The existing global low-detail setting caps the tier; restoring detail restores the chosen tier. Empty volume samples skip lighting, and opaque rays terminate early. Foam keeps its fixed 96×96 grid at all tiers; it uses the existing FFT displacement textures in the vertex stage. Hull waterlines add three texture fetches per vertex near water, rather than per fragment.

Measured on **llvmpipe (LLVM 21.1.8), two threads, 960×720, 1× MSAA**. Frozen chase views; milliseconds below are the whole scene and the difference from effects hidden.

| Tier | Burn at 15 m, scene / added ms | 60 s after, scene / added ms |
|---|---:|---:|
| Low | 162.3 / +35.9 | 163.2 / +33.0 |
| Medium | 171.5 / +45.1 | 162.6 / +32.4 |
| High | 187.7 / +61.3 | 162.8 / +32.6 |

The corresponding hidden baselines were 126.5 ms (36 draws) and 130.2 ms (27 draws). Water effects add two draws during the burn and one at 60 s. Residual foam has the same shader/grid at every tier, so its measured tier differences are timing variation. Empty foam fragments skip atmosphere lighting.

Software timings are whole-scene GPU timestamps: median of three warmed frames at a frozen instant, compared with three warmed frames hiding both water-effect meshes. The baseline still includes the ship, plumes and ocean; waterline shading remains enabled. Screen coverage and resolution affect volume cost. These measurements do not establish hardware GPU frame-rate, power or thermal performance. See [capture log](../build/codex/splashdown/captures.log) and [metrics](../build/codex/splashdown/metrics.json).

## Verification

The requested three-target `/usr/bin/cmake` build with `-j2` succeeds (judged by its final link output). `/usr/bin/ctest -R visual --output-on-failure` passes both `visual_controls` and `visual_webgpu`, with no skip (32.52 s total). The final capture run reports zero GPU validation errors. All 32 protected guidance/physics files are byte-identical to their initial hashes. `git diff --check` passes; no commit was made. Evidence: [build](../build/codex/splashdown/build.log), [visual tests](../build/codex/splashdown/ctest.log), [protected-file audit](../build/codex/splashdown/invariants.txt).

The CPU regression checks dry-land exclusion, water telemetry integration, floating origin, pause/toggle, tier restoration, rewind and object removal. The WebGPU regression checks burn steam, contact/broadside changes, 60-second foam, exact stationary-frame equality, pause, quality, effects toggle, rewind, warm illumination in darkness, its removal after cutoff, and partial/full hull submersion.

## Limits

This is a visual approximation, not CFD or a reproduction of the Flight 11 trajectory. One connected steam bank and two entry events are supported per arrival; detached splashes and repeated wave slaps are not simulated. Hull immersion uses a cylinder envelope, so protruding flaps and gimballed nozzles do not each trigger separate splashes. No buoyancy, attitude or guidance is added. The capture motion is explicitly supplied by the fixture.

The current ocean renderer resolves wave normals on mean-level geometry. Foam follows crest relief with a small lift above that depth surface, preventing trough clipping; it does not deform the global ocean or drive its FFT spectrum. Ripple relief is cosmetic. Waterlines interpolate FFT heights across existing hull vertices, so short waves along long hull panels are approximate. The wet band follows the current waterline; there is no persistent wetness texture, underwater refraction, caustics or submerged hull rendering.

The volume uses the existing transparent-shell depth test and does not terminate individual rays at scene depth. An embedded hull can therefore be over-attenuated; the broadside burst can obscure it completely. It self-shades from the sun but does not cast a volumetric shadow on the water. Spray fingers and the faint rainbow are analytic, not resolved droplets or spectral scattering. The local water datum must agree with the flight's configured environment and the displayed ocean.

Noise and sampling are deterministic, with no random frame jitter, respawn/sort pops, automatic LOD transitions or temporal history. Stationary frames are tested exactly; motion on very small features can still alias, especially on low quality. Forward seeks cannot reconstruct unobserved impingement; rewinds clear history. Captures validate software WebGPU, not every hardware adapter.
