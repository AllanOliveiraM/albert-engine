# Raptor landing plumes

The sea-level envelope was 160 bell radii long and only three radii wide at its downstream end. Its root also contracted by the square root of throttle. The shader coloured almost the entire plume with its edge colour after the first four metres, while the shock-diamond gate began at 72% throttle. Together these made low-throttle landing clusters resemble a long coloured stick. There was no camera-distance or lit-engine-count multiplier on dimensions; count only reduced overlap opacity above six engines. The existing two per-engine meshes and gimbal samples were already present.

## Changes

| File | Change |
| --- | --- |
| `src/render/vehicle.cpp` | Sea-level envelope length 48 radii, downstream radius six radii; core length 18 radii. Near-bell width stays between 88% and 100% of the physical bell radius. Pressure still expands both shells gradually into the existing high-altitude/vacuum shape. White-yellow cores, orange dense-air sheath and violet thin edges; diamonds settle at landing throttle after ignition. A single bounded orange point light follows the throttle-weighted lit bell positions per stage and turns off at cutoff or breakup. |
| `src/render/vehicle.hpp` | Owns the stage's exhaust light. |
| `src/render/webgpu_programs.cpp` | Soft radial density across the finite cone chord, sampled three times at high quality and once at low quality; bell-sized diamond spacing and periodic core contraction. Translucent sheath, brighter core, soft axial and side silhouettes. The diamond `pow` base is explicitly clamped nonnegative. |
| `src/gpu/programs.cpp` | Up to four bounded point sources contribute warm directional diffuse/specular light to Standard and Phong shading, including assisted vehicle/scenery materials. |
| `src/gpu/scene.cpp` | Collects visible point lights, selects up to four by their contribution at the camera, and packs positions, ranges and radiance into the shared frame buffer. Range-relative attenuation preserves lighting when nodes scale from metres to Earth radii. |
| `src/gpu/scene.hpp` | Documents the bounded local-light support. |
| `tests/plume_visuals.hpp` | Software pixel regressions for low-throttle width/brightness, axial and low-quality views, warm receiver lighting, scene-scale invariance and cutoff; repeatable captures of actual example geometry. |
| `tests/visual_test.cpp` | Runs the regressions and exposes `plume-captures DIRECTORY`. |
| `tests/atmosphere_test.cpp` | Keeps the direct atmosphere probes' frame buffers at the enlarged shared frame size. |

`engine_cycle.hpp` is unchanged, including the fuel-rich start flash, torch, detached cutoff gas and purge timing. Flight guidance, physics and all example headers are unchanged. No commits were made.

## Captures and reproduction

From the repository root:

```sh
ALBERT_GPU=software ALBERT_AUDIO=0 ./build/linux/tests/visual_test plume-captures build/codex/plume-check
python3 build/codex/plume-check/contact-sheets.py
```

Raw PPM frames and PNG copies live in `build/codex/plume-check/`. The contact sheets are `booster-sheet.png`, `ship-sheet.png`, `detail-sheet.png`, and `tower-lighting-sheet.png`. Filenames such as `booster-13-1000m-day.png` encode vehicle, lit count, bell-exit altitude and lighting. The matrix includes booster 13/5/3 and ship 3/2, each at 1,000 m and 100 m, by day and at dusk (20 frames). Five-engine samples use the centre three and two opposing middle engines. Each sample burns at 55%, with a small gimbal on the first engine; pressure is supplied as 101325 exp(-height/8500) Pa. These are explicit visual samples, not a physically flown landing sequence.

Additional frames inspect side/aft/axial views, low quality, tower/arms with the exhaust light enabled and disabled, a low-pressure comparison and startup. The scene uses actual `superHeavy()`, `shipAlone()` and `addTower()` appearance, atmosphere scattering, and a flat ground plane; it does not use the application's live DEM, HUD or guidance. Detail views hide the site to expose the bells and plumes.

## Verification

The required build completed successfully, judged by its final link output:

```sh
cd build/linux
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

Both visual tests pass (39.47 s total), including real software WebGPU rendering. The additional `webgpu_atmosphere` compatibility test also passes (58.93 s), after building `atmosphere_test` with `/usr/bin/cmake` and `-j2`. All 29 captures ran on llvmpipe (LLVM 21.1.8, 256 bits), with zero GPU validation errors. `git diff --check` is clean. A byte comparison to HEAD confirms all 33 protected guidance/physics/engine-cycle files are unchanged; hashes are in `build/codex/plume-check/invariants.txt`. Build and visual-test logs are `build/codex/plume-build.log` and `build/codex/plume-ctest.log`; capture and atmosphere compatibility logs are in `build/codex/plume-check/`.

## Limits

The shells approximate emitting gas with a few chord samples, not combustion or shock-flow simulation. Diamond spacing, colours and local-light power are visual estimates. Per-engine cores remain distinct near the bells; transparent sheaths overlap downstream without a separate merged fluid mesh. One light approximates the entire stage, with a 120 m range, inverse-square falloff and a softened centre; there are no local shadow maps or volumetric illumination. At most four stages illuminate a frame. Software captures validate appearance and shader execution, not hardware frame time. High-altitude and vacuum behavior still need a full-flight hardware review. Near-axial plume columns can obscure the engine bay as real luminous exhaust does.
