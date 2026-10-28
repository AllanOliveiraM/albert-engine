# Starship entry plasma

The renderer uses the existing vehicle sample's stagnation heat flux, angle of attack and sideslip. Guidance, physics, RPC and vehicle definitions are unchanged. No commit was made.

## Changes by file

| File | Change |
| --- | --- |
| `src/render/vehicle.cpp` | Replaces the old ellipsoid with one merged hull curtain, a curtain on each actual hinged flap, and one bounded wake volume. Smooth heat-flux response, airflow orientation, simulated-time advection, quality controls, and the local surface illumination uniforms. Calibrates tile incandescence so it remains red-orange beneath the gas instead of saturating yellow. |
| `src/render/vehicle.hpp` | Owns the curtains, wake and body-frame air-relative direction. |
| `src/render/entry_plasma.hpp` | WGSL: orange/pink/violet emission, windward exposure and stagnation weighting, moving filtered curtains, rare bright edge-like streaks, gentle flicker, and eight/four sample wake integration. |
| `src/render/vehicle_surface.hpp` | Adds a bounded orange-pink irradiance term on exposed steel/flaps and retains resolved hex variation in hot tile emission, at both quality settings. |
| `src/render/webgpu_programs.cpp` | Registers the revised plasma program and vehicle illumination uniform in matching CPU/WGSL order. |
| `tests/entry_visuals.hpp` | GPU regressions and the software WebGPU capture/timing fixture using actual example appearances with prescribed thermal/airflow samples. |
| `tests/vehicle_visuals.hpp` | Separately isolates tile emission from the new reflected plasma illumination in the existing thermal regression. |
| `tests/visual_test.cpp` | Runs entry regressions and exposes `entry-captures`. |
| `docs/STARSHIP_SURFACES.md` | Points the earlier surface report to this entry update. |
| `docs/STARSHIP_PLASMA.md` | Reproduction, capture inventory, cost and limitations. |

## Driving data and appearance

The sheath is off at or below **30 kW/m²**. Its strength is `smoothstep(0, 1, clamp((q - 30000) / 570000, 0, 1))`, saturating at **600 kW/m²**. It fades on the same continuous curve as heating falls, without an altitude gate. Thus both ship and booster use their own supplied heating; the booster fixture uses only 65 kW/m².

The body-frame velocity-through-air direction is reconstructed as `(-sin(beta), cos(alpha)*cos(beta), sin(alpha)*cos(beta))` in render coordinates, using the existing incidence telemetry. It includes the simulation's air-relative flow; inertial velocity and the body axis do not substitute for it. Its negative sets the downstream wake. A transverse basis aligns the broad wake with the projected belly, shrinking toward a round wake for axial flight. The bounded wake extends ten vehicle lengths downstream. It is a current-flow rendering approximation, not a stored trajectory.

The hull curtain is offset 28 cm along the actual surface normals. Flap curtains remain children of the original pivots, so they follow the existing hinges. Normal exposure concentrates the emission on the windward face and leading edges; an additional nose weighting strengthens the bow. Each flap also records its actual swept leading edge to brighten the gas along it. The colour runs from deep orange at low heating to pink with a violet outer/wake component at high heating. Continuous, metre-space waves travel downstream over the hull and flaps. Higher quality adds filtered narrow transient streaks; these suggest edge ablation without claiming simulated material loss. A four-percent sinusoidal modulation provides gentle flicker. Time is supplied from the sample UTC as a bounded phase with integer temporal harmonics, so paused rendering is deterministic and phase wrapping is continuous.

The existing equilibrium temperature proxy remains `T = (0.6*q / (0.85*sigma))^0.25`: about 948 K at 65 kW/m² and 1653 K at 600 kW/m². Tile emission uses the existing temperature colour curve with a smaller display radiance and resolved tile variation. It remains separate from plasma emission and from the plasma's illumination of the metal, whose broad reflection responds to the view direction. The illumination wraps slightly around exposed edges, then goes to zero on the sheltered side; it adds no scene light slots. Onboard views receive the same visible curtain and surface illumination, with ordinary depth occlusion.

No camera-distance quality switch, billboards, particle births or screen-space random numbers are used. Derivative filtering suppresses unresolved spatial waves and streaks. The additive curtains are order independent and retain depth testing without depth writes. The wake integrates an analytic, softly fading density along the visible chord, with no stochastic jitter. Its back boundary also supports a camera inside the volume.

## Captures and reproduction

From the repository root:

```sh
ALBERT_GPU=software ./build/linux/tests/visual_test entry-captures build/codex/entry-check
```

The direct GPU readbacks are 1280×960 `.ppm` files, with `.png` convenience copies in `build/codex/entry-check/`. They use 4× MSAA, ACES, the Earth atmosphere/transmittance tables and the actual example ship/booster appearances. There is no painted-in plasma or image-generation step. The dark background isolates the effect; these fixtures do not render Earth imagery or advance a mission.

| Capture | View / sample |
| --- | --- |
| `peak-beside.png` | Beside the ship at 600 kW/m², 65 km atmosphere altitude. |
| `peak-behind.png` | Downstream view toward the sheltered steel side and wake. |
| `peak-onboard-flap.png` | Hull-mounted-style close view looking over an aft flap. |
| `peak-onboard-flap-next.png` | The same camera 0.12 simulated seconds later, showing curtain motion. |
| `onset-beside.png` | Same side camera at 65 kW/m², close to the lower end of the response. |
| `peak-beside-low.png` | Low quality at peak heating. |
| `peak-steel-lit.png` | Close to the tile/steel boundary with the sun disabled. |
| `ground-night.png` | Observer at 2 m looking to the ship at 65 km, sun disabled; 1.2° telephoto field resolves the streak. |
| `booster-brief.png` | Booster at 65 kW/m². |
| `peak-surfaces-only.png` | Peak tile incandescence and local plasma lighting, with curtains/wake hidden for comparison. |
| `contact-sheet.png` | Capture overview. |

## Validation and cost

Required build and checks, from `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

Validation results and final timings are recorded alongside the captures. The tests exercise threshold gating, heat response, deterministic paused rendering, time advection and phase-wrap continuity, small camera motion, quality restoration, air-incidence steering, Earth-radius scene units, and extinction after heating. The existing tile-incandescence isolation check remains independent of reflected plasma illumination.

At peak the example ship uses **six effect draws**: one merged hull, four hinged flaps, one wake. This replaces one old shell draw, a net increase of five. An untiled booster without flaps uses two. Below threshold there are no effect draws. The geometry and material count are fixed at vehicle construction; there are no per-frame particle allocations. No textures, render targets, additional shadow casters, scene lights or fullscreen passes are added.

High quality integrates **eight** analytic wake samples per covered fragment and enables filtered narrow streaks. Low quality integrates **four**, omits those streaks and uses the existing simpler vehicle surface shader. Both retain the sheath, heating response, tile emission, airflow orientation and local illumination. The curtain itself has no ray march. Work is bounded per covered fragment; fill cost still rises when the glow covers more of the viewport.

The final required build completed successfully by its tail. `visual_controls` and `visual_webgpu` both passed (2/2, 25.07 s total); the WebGPU test ran on llvmpipe. The capture run reported **zero GPU validation errors**. `build.log`, `ctest.log` and `captures.log` are in the capture directory.

The six effect meshes contain **2,928 triangles and 218,400 bytes** of vertex/index data in this appearance. Timings below are llvmpipe (LLVM 21.1.8, 256 bits), 1280×960, 4× MSAA, the same side camera, shadows off, four warm-up frames discarded and the median of eight subsequent frames. Surface-only runs keep the same tile emission and local plasma illumination; the difference isolates curtains/wake, not the illumination shader's separate overhead. CPU animation is outside these GPU timings. The first capture's shader-compilation cost is excluded from the medians.

| Quality | Surfaces with thermal lighting | With curtains/wake | Gas increment | Draws |
| --- | ---: | ---: | ---: | ---: |
| High | 64.86 ms | 80.88 ms | +16.02 ms | 25 → 31 |
| Low | 62.23 ms | 76.58 ms | +14.35 ms | 25 → 31 |

This software cost is measurable; it is not a hardware GPU performance claim. The camera's covered area matters, particularly in close onboard views. Low quality bounds wake sampling but retains the same geometry and coverage.

## Limitations

- The scene sample forwards heat flux and incidence, but **not thermal-node temperatures or positions**. Incandescence therefore uses the existing equilibrium proxy and cannot retain actual thermal inertia after a rapid flux decrease. No synthetic thermal history or physics changes were introduced.
- This is an artistic emission/irradiance model, not reacting-flow CFD, spectroscopic radiative transfer, tile erosion, or a physical ablation simulation. Streaks are cosmetic and not individual tracked tile-edge ejecta.
- The thin conforming sheath is a surface curtain; the long wake is a volume. There is no refractive shock, lens flare, sensor bloom or camera exposure simulation. Close silhouettes can reveal the curtain's finite offset. The wake's opaque-scene occlusion uses ordinary mesh depth testing rather than depth-clipped volumetric integration.
- Plasma lighting is local to vehicle surfaces. It does not illuminate terrain, other vehicles or scenery and does not produce reflected images of the gas. The light field is steady for a fixed heat flux; flicker is confined to the visible gas.
- The wake follows the current airflow and rotates with the vehicle; it does not preserve a curved historical ground track or advect detached parcels. Its length is a bounded visual scale, not recombination-time telemetry.
- Prescribed peak heating and poses are render diagnostics. A full trajectory, hardware GPU performance, physically calibrated radiance, and Flight 11 camera matching remain unverified. The night ground capture uses telephoto magnification; at a normal wide field the vehicle itself is subpixel at that range.
