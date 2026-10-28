# Starship and Super Heavy drawn geometry

The renderer now builds actual open hardware around the existing `VehicleLook`. No flight definition, aerodynamic surface, mass property, contact pin, collision hull, guidance or physics code changes. The attached Flight 11 frames guide the appearance; the installed definitions supply V3's three booster fins, 33 booster engines (3 centre / 10 middle / 20 outer), and six ship engines (three sea-level / three vacuum).

## Files and behaviour

- `src/render/vehicle.cpp`: replaces the coarse grids with framed, deep steel lattices; adds spindle bearings at the fin roots; draws two tapered booster chines; replaces the opaque staging cylinder with open ports, rims, ligaments and an inset dome. Adds flap hinge knuckles, actuator bosses and fixed aft aerocovers alongside the declared forward covers. Builds a perforated aft heat shield, skirt lip, open bells, chambers, external feed lines and reinforcing bands. Joins compatible hull sections. The existing tiled flap faces, surface shader and conforming plasma sheaths remain attached to their original joints.
- `src/render/vehicle.hpp`: owns the two hardware batches and their LOD node.
- `src/render/vehicle_geometry.hpp`: joins indexed rigid pieces into a single hardware draw per vehicle. Position and normal ranges follow the actual fin, flap and engine pivots. Per-vertex thermal emission follows each engine's existing burn/lip state and the shield's entry glow. Unchanged joint positions do not upload again. Named engine markers preserve the client gimbal and engine-camera interfaces.
- `src/render/webgpu_programs.cpp`: lights the batched steel and bells using the existing sun, sky, point lights and shadows, with independent bell emission and the existing waterline convention. The new shader contains no `pow()`.
- `src/gpu/scene.cpp`: updates LOD before collecting visible meshes and shadow casters, compensating for object scale and camera zoom. Vehicle thresholds are in local metres, including when the scene uses Earth radii.
- `tests/vehicle_geometry_visuals.hpp`: definition-backed actuator fixtures, open-cell/port ray checks, engine counts, independent bell heat, frozen uploads/images, quality and scaled-scene LOD checks, capture scenes and cost measurements.
- `tests/visual_test.cpp`: registers those checks and the `geometry-captures` command.
- `docs/STARSHIP_SURFACES.md`: links this subsequent geometry implementation; its original surface-only measurements are historical.

Hardware switches to the coarse mesh when local camera distance divided by camera zoom reaches 650 m, using the vehicle's scale. Low quality forces that coarse mesh even at close range. Full grids have 16 × 10 cells; coarse grids have 6 × 4. Near bells use 32 radial × 16 axial segments; coarse bells use 12 radial × 6 axial segments and omit the external feed lines and reinforcing bands. All fins, engines, open ports, main flap silhouettes and bearings remain. Both pose buffers stay current so a paused camera move can restore the near mesh immediately. Plume, plasma, frost and water effects retain their existing separate draws.

## Captures

All captures use software WebGPU, 1280 × 960, 4× MSAA and the real example appearances. Actuator angles, engine flow and the staging separation are explicitly supplied diagnostic samples; no flight guidance is advanced. PPM originals and lossless PNGs are in `build/codex/geometry/`.

| View | PNG |
| --- | --- |
| Deflected grid fin, full / low | [full](../build/codex/geometry/grid-fins-deflected.png), [low](../build/codex/geometry/grid-fins-low.png) |
| Open hot-staging ring and dome | [ring](../build/codex/geometry/hot-staging-ring.png) |
| Ship exhaust visible through staging ports | [staging](../build/codex/geometry/hot-staging-open-plumes.png) |
| Both booster chines, opposite views | [chine](../build/codex/geometry/booster-chine.png), [opposite](../build/codex/geometry/booster-chine-opposite.png) |
| Forward flap with its cover and hinges | [forward](../build/codex/geometry/ship-forward-flap.png) |
| Aft flap with its cover and hinges | [aft](../build/codex/geometry/ship-aft-flap.png) |
| Booster bay from below, engines off / centre gimballed / close | [off](../build/codex/geometry/booster-engine-bay.png), [gimbal](../build/codex/geometry/booster-engine-gimbal.png), [close](../build/codex/geometry/booster-engine-bay-close.png) |
| Ship bay from below, off / selected engines lit / close | [off](../build/codex/geometry/ship-engine-bay.png), [selected](../build/codex/geometry/ship-engines-selective.png), [close](../build/codex/geometry/ship-engine-bay-close.png) |
| Full stack, camera 200 m from its centre | [full](../build/codex/geometry/stack-200m.png), [low](../build/codex/geometry/stack-200m-low.png) |
| Full stack at 2 km, automatic / forced near LOD | [automatic](../build/codex/geometry/stack-2km.png), [forced near](../build/codex/geometry/stack-2km-forced-high.png) |

## Cost

One hardware draw per vehicle. Cold booster: two total draws. Cold ship: six total draws when all faces are in the frustum. Full stack: eight total draws, including its existing frost layer. Lit exhaust adds its existing effect draws.

| Scene | Draws | Triangles | Geometry MB | llvmpipe GPU median |
| --- | ---: | ---: | ---: | ---: |
| Cold booster, aft close view | 2 | 67,037 | 3.53 | 133.67 ms |
| Full stack, 200 m, high | 8 | 70,957 | 3.84 | 127.38 ms |
| Full stack, 200 m, low | 8 | 15,495 | 1.01 | 37.14 ms |
| Full stack, 2 km, automatic LOD | 8 | 15,495 | 1.01 | 21.47 ms |
| Full stack, 2 km, forced near mesh | 8 | 70,957 | 3.84 | 65.22 ms |

In the controlled aft comparison, showing the hardware adds one draw and 115.33 ms on llvmpipe (133.78 versus 18.45 ms). Booster `animate()` takes a median 47.81 µs with frozen joints and 373.84 µs while moving all three fins and the thirteen gimballed engines. These CPU figures include the existing engine effects and maintaining both geometry tiers.

See [capture log](../build/codex/geometry/captures.log) and [machine-readable metrics](../build/codex/geometry/metrics.json).

The capture log reports five GPU timestamp samples after three warm-up frames at each frozen pose. Timings include the entire scene, existing hull/flap shading and 4× MSAA. The hull-only comparison hides the new hardware batch, including the bells and shield; it is an incremental hardware cost measurement, not a comparison against the old renderer. Geometry bytes count float attributes plus 32-bit indices on scene-visible meshes before frustum culling. Pose source arrays, piece metadata and driver allocations are additional. Both LOD buffers remain on the CPU; a changed position/normal/emission attribute uploads its active mesh buffer. The CPU measurement includes both geometry tiers and existing engine effects. Stationary GPU timings do not measure animated upload bandwidth. These are llvmpipe measurements, not hardware GPU performance guarantees.

## Verification

The required three-target `/usr/bin/cmake` build with `-j2` succeeds; its tail ends with the renderer library, `AlbertEngine` and `visual_test` links. `albert_flight_client` is included in the requested build and already up to date. `/usr/bin/ctest -R visual --output-on-failure` passes both `visual_controls` and `visual_webgpu` without skipping (28.23 s total). The final software capture run reports zero WebGPU validation errors. All 32 protected guidance/physics files match their initial SHA-256 hashes; no example definitions or physics files are in the diff. `git diff --check` passes. No commit was made. Evidence: [build tail](../build/codex/geometry/build.log), [ctest](../build/codex/geometry/ctest.log), [protected-file audit](../build/codex/geometry/invariants.txt).

Reproduce from the repository root:

```sh
(cd build/linux && /usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2)
(cd build/linux && /usr/bin/ctest -R visual --output-on-failure)
ALBERT_GPU=software ./build/linux/tests/visual_test geometry-captures build/codex/geometry
```

## Limits

This is procedural visual geometry, not manufacturing CAD. The photographs do not provide blade counts, wall gauges, chamber contours, feed routing or fairing dimensions; those details are approximated. Nominal nozzle exits (about 1.30 m sea-level / 2.30 m vacuum) and engine locations come from the existing appearance. The booster's approximate outer-ring layout consequently has slightly intersecting nominal nozzle lips; it has not been silently repositioned. The protected simulation definitions are unchanged.

The staged plume capture shows existing exhaust through real apertures; it adds no gas redirection, pressure model or CFD around the dome. Existing transparent plume volumes can extend across the hull silhouette and do not terminate their ray contributions at scene depth. There is no flexible plumbing, hinge linkage simulation, fin folding mechanism or mechanical damage. Hardware uses the engine's reported gimbal whether burning or off, without inventing actuator states. The complete stack draws the stage-zero engine list; detached ship views provide its six engines.

LOD changes discretely at its threshold without cross-fading or hysteresis; coarse cells have a different pitch and silhouette. The new opaque hardware participates in normal depth and shadow passes; captures isolate it with controlled lights and no atmosphere, terrain or tower. Existing full-scene visual tests cover the other effects, but no full flight, hardware GPU benchmark or cross-adapter image comparison was run.
