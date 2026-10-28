# Starbase sun shadows and contact shading

High quality uses four fixed-size, nested light-space shadow levels. Their half-widths are 100, 350, 1100 and 3400 metres; each gets a 2048² tile in a 4096² Depth32Float atlas. Texel widths are 0.098, 0.342, 1.074 and 3.320 metres. This gives the pad a fine inner level and a roughly 3 km outer reach without fitting one map to planetary bounds. Selection uses projected containment, not camera depth: even the ground below a camera 2 km high can select a larger level. Each level blends to the next over its outer 15%, and the last fades to unshadowed terrain.

The levels follow the eye with a fixed orientation relative to the sun. Their centres snap to texels relative to the site's physical origin. Moving the application's floating origin therefore does not move the grid on the site. Camera rotation cannot resize or rotate a level. Caster bounding spheres cull against each level; the existing merged geometry, including actual open lattice members, supplies depth. No solid tower proxy is used for sunlight.

The 3×3 comparison filter clamps taps to their tile. Receiver-plane correction accounts for each tap's offset on sloping surfaces, with a small half-texel slope allowance for bilinear filtering. A separate normal offset scales with that level's texel width. High quality ignores the old planetary-scale normal/depth bias; low quality retains the original map and biases. The depth span is 8 km, centred on the eye in light space.

The application now enables sun shadows when following a vehicle as well as a celestial body, and supplies its 6,378,137 metres per world unit. This fixes two application issues: the old Earth shadow camera spanned about 17,858 km (roughly 8.7 km per texel at high quality), and vehicle focus disabled the pass altogether. Existing callers can leave `shadowMetersPerUnit` at zero for the legacy map.

## Contact shading

A baked visibility atlas is the cheapest convincing fit for the static site. It adds no geometry draws, fullscreen passes, depth reconstruction or temporal history. It stays attached to the site and can shade offscreen occluders. A single filtered RGBA8 atlas contains a 1024² pad field at 1 metre per texel and a 512² campus field at 12 metres per texel. RGBA stores ambient visibility at heights 0, 2, 8 and 24 metres; visibility interpolates vertically and fades to clear by 48 metres. Mip selection follows the pixel footprint.

Construction uses the existing scenery parts and production building dimensions. Rotated boxes and round footprints supply short-range, height-aware visibility. Tower lattice markers contribute their four corner columns, rather than a solid tower volume. Ground slabs, trench markers, emission markers, and moving hinged assemblies are excluded. Vehicle furnishing markers use their drawn dimensions. Contributions combine with a minimum rather than multiplying into black seams. Interior samples do not darken an object's own roof.

The field attenuates only ambient/sky illumination in the shared standard material and the vehicle's diffuse/reflected environment. Sunlight still uses the dynamic depth maps. Consequently concrete, steel, tanks, mount, vehicles, roads and land cover receive it; the sky, emission and aerial perspective do not. There is no artificial light at night. `Site::quality(0)` disables the field, alongside the existing small fittings; shadow quality zero uses the old single map.

This is an approximate static ambient field, not ray-traced AO: it captures ground contact and broad shelter, not every bolt, pipe intersection, moving hinge or engine cavity. At 1 metre resolution the smallest feet blend softly. The campus field is deliberately coarse. Moving flight vehicles receive the field and cast live shadows but do not rebake it. Only one visible site's field is selected per scene render. The field's ENU frame and actual terrain receiver positions preserve its placement on terrain; steep terrain or surfaces displaced significantly from the authored site elevations need grading or a more general AO method.

## Files

| File | Change |
|---|---|
| `src/gpu/shadows.hpp` | Fixed, snapped light-space projections anchored through floating-origin changes. |
| `src/gpu/programs.cpp` | Atlas selection/blending, receiver-plane PCF, ambient-field sampling; guards colour-transfer `pow` bases. |
| `src/gpu/scene.cpp` | Atlas passes, per-level caster uniforms/culling, contact marker binding, shadow draw/memory counters; GPU timestamps now include shadow passes. |
| `src/gpu/scene.hpp` | Metres-per-unit and quality controls plus shadow statistics. |
| `src/render/contact_shading.hpp`, `src/render/contact_shading.cpp` | Bake visibility from static scenery/building footprints into one atlas. |
| `src/render/scenery.hpp`, `src/render/scenery.cpp` | Own the atlas on the draw-free site root; disable ambient shading at low quality. |
| `src/render/vehicle_surface.hpp` | Apply the field to vehicle sky/environment illumination. |
| `src/render/display.hpp`, `src/render/display.cpp` | Pass local shadow scale and quality into the renderer. |
| `src/render/application.cpp` | Enable the pass with vehicle focus; set Earth-radius world units and quality. |
| `src/render/CMakeLists.txt` | Compile the contact bake. |
| `tests/visual_test.cpp` | Shadow/contact GPU assertions and projection stability checks; capture command. |
| `tests/land_visuals.hpp` | Identical before/after views, quality controls, component-disabled views, night and timing. |
| `docs/STARBASE_SHADOWS.md` | Design, evidence, cost and limitations. |

## Verification and capture reproduction

```sh
cd /unreal/allan/albert-codex/build/linux
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
cd /unreal/allan/albert-codex
ALBERT_GPU=software build/linux/tests/visual_test shadow-captures build/codex/shadow-check/after
ALBERT_GPU=software ALBERT_SHADOW_BASELINE=1 build/linux/tests/visual_test shadow-captures build/codex/shadow-check/baseline
```

The baseline switch selects the original single-map path and disables contact shading while preserving high-quality geometry. Original pre-change captures also remain in `build/codex/shadow-check/before/`. The fixture uses the real Starbase request, merged site geometry, production campus, stack, roads/land-cover material, atmosphere and ocean renderer. All views use 1280×960, 4× MSAA, and llvmpipe software WebGPU. These are flat ENU ground captures using the terrain's material and saved coarse imagery, not live application DEM captures. No flight guidance or physics file was modified; no commit was made.

Each shot renders six frames, discards the first for timing and reports the median of five synchronised render/readback durations and software-adapter timestamp intervals. Readback overhead remains in the synchronised number. GPU timestamps now start at the first shadow pass, whereas the original implementation started at the main colour pass. Use the reproduced baseline for comparable GPU timing. Driver compilation/startup is excluded from these warmed measurements; hardware GPU performance is not established.

The tests check subtexel projection stability and floating-origin covariance, a rendered subtexel camera move, metre/Earth-radius scaling, sharp shadow coverage with acne-free concrete, open-truss shadow holes, a shadow at 2 km altitude, ambient-only contact darkening, and darkness without illumination. `visual_controls` and `visual_webgpu` both pass, without a software-adapter skip.

## Captures and measured cost

[Before/after overview](../build/codex/shadow-check/comparison.png). Original baseline images are in `build/codex/shadow-check/before/`; the identically reproduced timing baseline is in `build/codex/shadow-check/baseline/`. Every original baseline, including low quality, is pixel-identical to the reproduced legacy path. The final low-quality image is also pixel-identical to the original.

| View | Before | After |
|---|---|---|
| Pad, eye level | [PNG](../build/codex/shadow-check/before/pad-eye.png) | [PNG](../build/codex/shadow-check/after/pad-eye.png) |
| Tower top | [PNG](../build/codex/shadow-check/before/tower-top.png) | [PNG](../build/codex/shadow-check/after/tower-top.png) |
| 300 m oblique | [PNG](../build/codex/shadow-check/before/oblique-300m.png) | [PNG](../build/codex/shadow-check/after/oblique-300m.png) |
| 2 km altitude | [PNG](../build/codex/shadow-check/before/altitude-2km.png) | [PNG](../build/codex/shadow-check/after/altitude-2km.png) |
| Low sun | [PNG](../build/codex/shadow-check/before/low-sun.png) | [PNG](../build/codex/shadow-check/after/low-sun.png) |

Supplemental matching views: [tank contact before](../build/codex/shadow-check/baseline/tank-eye.png) / [after](../build/codex/shadow-check/after/tank-eye.png), [low quality](../build/codex/shadow-check/after/oblique-300m-low.png), [contact disabled](../build/codex/shadow-check/after/contact-disabled.png), [sun shadows disabled](../build/codex/shadow-check/after/sun-shadows-disabled.png), [night](../build/codex/shadow-check/after/night.png).

| View | Legacy frame ms | New frame ms | Change | GPU ms, legacy / new | Shadow draws, legacy / new |
|---|---:|---:|---:|---:|---:|
| pad-eye | 986.9 | 1154.9 | +17.0% | 968.0 / 1136.4 | 99 / 307 |
| tank-eye | 938.0 | 1163.8 | +24.1% | 921.9 / 1145.5 | 99 / 388 |
| tower-top | 932.0 | 1138.7 | +22.2% | 915.3 / 1121.1 | 99 / 381 |
| oblique-300m | 886.0 | 1052.1 | +18.7% | 867.1 / 1035.1 | 99 / 294 |
| oblique-300m-low | 633.8 | 638.4 | +0.7% | 615.7 / 622.1 | 92 / 92 |
| altitude-2km | 1015.0 | 1134.7 | +11.8% | 996.8 / 1117.0 | 99 / 107 |
| low-sun | 1272.2 | 1449.8 | +14.0% | 1255.8 / 1435.2 | 99 / 311 |

The 300 m view costs 1005.8 ms with the new shadows and AO disabled, versus 1052.1 ms with both: approximately 46.3 ms for contact shading on this CPU adapter. Shadow-disabled with AO costs 829.3 ms. These component differences are measurements from separate warmed runs of the same view, not hardware pass profiler results. The main scene draw count remains unchanged (101 in this view). Both capture runs report zero validation errors.

High-quality shadow storage increases from 16 to 64 MiB; the visibility atlas adds 8 MiB including mipmaps, confirmed by texture residency rising from 120.9 to 128.9 MiB. The shadow plus AO allocation is therefore 72 MiB, 56 MiB more than the legacy high shadow map. Low quality uses a 4 MiB shadow map and bypasses AO sampling. After a quality toggle the previously uploaded atlas can remain cached until the renderer's normal texture eviction; its CPU image is retained by the site. No geometry draws or fullscreen passes are added for AO. Four sun-depth passes replace one at high quality, with casters culled per level.

Contact-only image comparison at 300 m finds 53,173 pixels darkened by more than two channel values, and none brightened. Night has only ten pixels above intensity 20, from existing beacons. Sun, sky and emissions keep their existing illumination model. The extra shadow levels still render in the artificial zero-intensity night capture (765.7 ms versus 529.5 ms legacy); this work does not add a horizon/night scheduling optimisation.

Final repeat verification: the exact three-target `/usr/bin/cmake` build with `-j2` succeeds, and both visual tests pass again in 22.58 seconds. See [final build](../build/codex/shadow-check/final-build.log) and [final ctest](../build/codex/shadow-check/final-ctest.log).

Evidence: [build tail/log](../build/codex/shadow-check/build.log), [ctest](../build/codex/shadow-check/ctest.log), [full test output](../build/codex/shadow-check/visual-tests.log), [original capture log](../build/codex/shadow-check/before/captures.log), [comparable baseline timings](../build/codex/shadow-check/baseline/captures.log), [final timings](../build/codex/shadow-check/after/captures.log), [machine-readable metrics](../build/codex/shadow-check/metrics.json), [protected-file audit](../build/codex/shadow-check/invariants.txt). PNGs are lossless conversions of the fixture's PPM readbacks; all artifacts are ignored build outputs.


Limits: fixed light-space levels are not a perspective frustum CSM, so effective resolution depends on the sun/view geometry. Thin members become subpixel in outer levels; low sun stretches the shadow footprint and can exhaust the outer reach. Snapping prevents grid crawl but does not eliminate subpixel silhouette changes, or changes caused by a moving sun. Full application flight/DEM captures and hardware orbit recordings remain unmeasured.
