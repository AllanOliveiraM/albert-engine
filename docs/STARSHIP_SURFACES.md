# Starship and Super Heavy surfaces

Subsequent drawn-only hardware, open staging ports and geometric LOD are documented in [STARSHIP_GEOMETRY.md](STARSHIP_GEOMETRY.md). The surface-only geometry limitations and timings below describe the earlier implementation.

The subsequent [entry plasma update](STARSHIP_PLASMA.md) replaces the old shock shell, recalibrates tile emission and adds local plasma illumination. The surface implementation and earlier captures below predate that update.

The vehicle renderer now uses metallic stainless steel, a procedural tiled windward surface and frost driven by the existing flight samples. Changes are confined to rendering, visual tests and this report. The four protected guidance headers, physics, vehicle definitions and RPC data are unchanged. No commit was made.

## Files

| File | Change |
| --- | --- |
| `src/render/vehicle.cpp` | Creates the surface materials on the existing sections and hinged parts. Each hull section uses one cylinder, with steel and tiles joined in its shader. Replaces the twenty small hot-ring markings with a single procedural vent band. Adds darker fin/flap metal, shaded edges, per-tank frost masks and cosmetic soot from observed engine flow. Frost receives scene shadows. |
| `src/render/vehicle.hpp` | Owns the surface materials, exposes quality switching and keeps the cosmetic soot history. |
| `src/render/vehicle_surface.hpp` | WGSL for anisotropic GGX steel, directional atmosphere reflection, ring seams, welds, hexagons, tile variation, the wavy boundary, vent slots, soot and frost. |
| `src/render/webgpu_programs.cpp` | Extends the assisted vertex shader to interpolate smooth sky reflection, and registers the vehicle uniforms. Adds filtered cooling-rib/band shading to the existing dark Raptor bells. |
| `src/render/objects.hpp` | Passes the application's quality setting to vehicles. |
| `tests/visual_test.cpp` | Supports variable-size offscreen targets and the optional capture command; invokes the vehicle graphics checks. |
| `tests/vehicle_visuals.hpp` | Checks steel/tile contrast, quality restoration, supplied fill levels, empty tanks, darkness, thermal emission, scene scale and booster frost beneath a tiled ship. Captures the actual example appearances and mounted Starbase scenery, and compares shader timing. |
| `tests/CMakeLists.txt` | When RPC is enabled, gives the capture fixture access to the existing example definitions. |
| `docs/STARSHIP_SURFACES.md` | This report and capture inventory. |

## Lighting and data

Previously, `Vehicle` split each section into a grey assisted-material arc and an offset black arc. `assistedMaterial()` uses the shared metal/roughness GGX light in `src/gpu/programs.cpp`; its environment contribution is averaged irradiance. The new surface shader keeps the frame's directional sun, atmospheric transmittance/irradiance, PCF shadows, aerial perspective, exposure and tone mapping. Its metal additionally samples the same Bruneton sky used by the background, in the reflected direction. The smooth environment colour is sampled at vertices and interpolated; the sun highlight, Fresnel, roughness, surface patterns and shadows remain per pixel. This avoids a scattering-table lookup per fragment, using the frame's existing vertex-readable tables without additional GPU resources. An approximate roughness blend approaches hemispherical irradiance. Ground reflections use the atmosphere's mean Lambert albedo. There is no constant brightness floor or new auxiliary light.

The barrel's metallic reflectance retains its declared slightly warm steel colour. A tangent along the barrel controls the anisotropic GGX highlight. Ring seams repeat every 1.85 m, with faint staggered longitudinal welds. Analytic pixel coverage and footprint fades suppress subpixel lines; tile variation and grain also fade before they alias. Relief is shading-only and scaled from metres to the scene's world units.

The hexagons are approximately 29 cm across flats. Deterministic per-cell shades include a small darkened population and rare brown-black missing-tile recesses. The windward mask follows the declared tile arc with a wandering edge, leaving leeward steel. Flap tiles remain attached to their windward faces as their original hinges move. The existing heat-flux-driven black-body emission is applied only to the tiled faces.

`src/rpc/flight.cpp::parts()` already sends `remaining propellant / stage propellant` as the third element of the seven-value environment tail. `Vehicle::animate()` reads it with `tail(2,0)`. Appearance tanks already provide `from`, `to` and `full`. Frost therefore uses existing data, settles toward each tank's aft bound, shrinks with fill and fades near empty. Each tank gets its own tile mask, so ship tiles no longer remove the booster's windward frost in the combined stack. Frost has a softened uneven top and filtered millimetre-scale shading. Individual LOX/methane fill fractions and frost temperatures are not available.

Soot is cosmetic deposition integrated from observed engine flow over simulated time, concentrated in the bottom seven metres of untiled hull. It clears on rewind and remains after shutdown. It does not alter forces, heat transfer, mass or flight state.

## Reference comparison and captures

All captures are real 1280×960 software WebGPU renders in `build/codex/vehicle-check/`. The `.ppm` files are the direct GPU readbacks; `.png` copies provide convenient viewing. The fixtures use the actual `stack()`/`superHeavy()` appearances and `addTower(request,true)` scenery, explicit fill/flow/heat samples, the engine atmosphere, ACES and sun shadows. They do not advance a mission.

| Reference | Captures | Comparison |
| --- | --- | --- |
| ref-001 | `stack-day.png`, `stack-sunset.png`, `stack-loaded.png` | Steel takes the directional sun/sky colours, with low-sun warmth and a darker tower silhouette. Loaded frost interrupts the reflective barrel. |
| ref-033 | `tower-top.png`, `hot-staging-fins.png`, `hot-staging-fins-low.png` | Tower-height perspective, dark open fin lattice and a vented-looking hot-staging band. The supplied V3 dimensions and three declared grid fins remain. |
| ref-024 | `raptor-bells.png`, `raptor-burning.png` | Existing open bell contours with dark alloy, filtered cooling ribs, luminous cores and the original thermal lips. No camera housing, lens distortion or bloom is added. |
| ref-026 | `ship-leeward.png`, `ship-orbit.png` | View-dependent reflective leeward steel, ring seams and sky/ground contrast. The orbit fixture uses bundled Earth imagery when available; reflections use average ground albedo rather than that image. |
| ref-028 | `ship-windward.png`, `tile-edge.png`, `tile-edge-low.png`, `tiles-entry-glow.png` | Black hexagons, shade variation, rare recesses, a wavy steel boundary and the existing entry heat glow. |
| Owner's description | `booster-clean.png`, `booster-weathered.png`, `booster-base.png`, `booster-frost-full.png`, `booster-frost-draining.png`, `ship-night.png` | Clean versus deposited soot, shrinking loaded frost and unilluminated metal darkness. |

`contact-sheet.png` provides an overview. `build.log`, `ctest.log`, `captures.log` and `invariants.txt` record validation and reproduction details.

## Verification and cost

Build only from `build/linux`:

```
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The build tail completes successfully. Both `visual_controls` and `visual_webgpu` pass; the latter runs on llvmpipe rather than skipping. Capture validation reports zero GPU errors. `/usr/bin/ctest` is used because the PATH wrapper has a missing Python interpreter.

Reproduce the captures from the repository root:

```
ALBERT_GPU=software ./build/linux/tests/visual_test vehicle-captures build/codex/vehicle-check
```

Surface details add no textures, meshes or rendering passes. Combining the steel/tile arcs saves one draw per tiled section, and replacing the twenty vent markings saves twenty draws, also reducing shadow casters. Cylinders use 64 radial segments instead of 48 per previous arc. High quality interpolates smooth sky radiance from vertices. Low quality takes a uniform early-return path through the existing isotropic lighting, skipping cell variation, seams, grain, anisotropy and directional reflection, while retaining averaged sky illumination, tile coverage, soot, frost and the hot-ring pattern. Raptor detail is analytically filtered at either setting.

The capture log contains a controlled timing comparison of the same booster geometry, camera and atmosphere using averaged standard lighting, the new high-quality shader and the new low-quality shader. It excludes scenery, the sky background and shadow passes, drops four warm-up frames and reports the median of eight subsequent samples. These are software-adapter timings, not a hardware performance guarantee.

| Isolated booster, 1280×960, 4× MSAA, 36 draws | llvmpipe median |
| --- | ---: |
| Standard averaged lighting, same geometry | 37.17 ms |
| Vehicle high quality, interpolated sky | 45.79 ms |
| Vehicle low quality, isotropic fast path | 38.79 ms |

The shader overhead is measurable on the software adapter: approximately 8.62 ms high and 1.63 ms low in this fixture. Negligible overhead on the intended hardware GPU remains unverified. These figures compare shaders on identical final geometry; they do not include the draw-call savings relative to the previous vehicle construction.

## Limitations

- The installed example describes V3: a 124.4 m stack with a 72.3 m booster and three grid fins. Flight 11 shows older hardware. Fin count, flap outlines, chines, hinge locations, nozzle shapes and every simulation dimension are preserved.
- Reflection interpolates directional sky samples at mesh vertices, with approximate roughness filtering and mean-ground bounce. Fine horizon detail can soften in close views. Buildings, the tower, nearby vehicles, cloud geometry and the detailed Earth texture are not reflected. No environment cubemap, ray tracing or reflection pass is introduced.
- Hot-ring vents and missing tiles are shaded recesses; they are not holes in geometry and do not cut holes in the shadow map. Fine cooling ribs and welds are also shader detail.
- The existing emissive exhaust does not cast light onto nearby bell walls or scenery. The aft capture shows luminous cores and dark bell silhouettes rather than reproducing all the reflected flame light in ref-024.
- Soot history starts when the renderer observes the vehicle. Joining a flight late or seeking cannot recover past deposits from the current protocol. Persistent, vehicle-specific soot would require an additional appearance/history value from the client.
- Frost assumes the existing tank spans and shared stage fill. Separate tank levels, skin temperature and a physical thaw/icing history would require additional telemetry. No such values are invented here.
- Capture lighting and poses are controlled diagnostics. The orbit background has no live cloud layer; the entry sample is prescribed heat rather than a simulated trajectory. Full-flight scenarios and hardware GPU timing were not run for this rendering change.
