# Starbase night lighting

`Site::lighting()` transforms the scene sun into the site's ENU frame. Lamps fade smoothly between solar elevations +3° and −4°, with no wall-clock or hard-coded sunset. Floating origins, Earth-radius scaling and site rotation use the same transform as the scenery. Existing steady red crown lamps remain; depth-tested screen-space halos add a simulated-UTC double pulse and a small minimum angular footprint for kilometre-distance visibility.

Two warm bounded sources per pad approximate the combined floodlight banks, using the existing four-source Standard/Phong diffuse/specular lighting from the Raptor work. They illuminate vehicle steel, tower members, concrete and the apron. They share the four slots with exhaust lights, selected by the renderer's existing contribution ranking. No local shadow maps are added. Low quality uses one stronger source per pad. The application exposes two quality levels (0 and 1); the bounded four-source path serves its normal/detail level, including the requested medium-cost use.

All other illumination is static spill, baked once into unused space in the existing 1536×1024 contact atlas. Two 256² RGB tiles cover the pads at 4 m/texel and the production campus at 24 m/texel. One bilinear lookup adds bounded, albedo-modulated irradiance to terrain/roads and low equipment; it fades between 6 and 32 m above the site. This avoids separate decal geometry, ground offsets, extra samplers, texture allocations and per-fixture lights. It follows the live terrain triangles as the roads do. It approximates diffuse spill and bounce; it is not a light transport bake or a shadow solution.

Existing pad and tower lenses and service-building windows become emissive at dusk. Existing farm light poles receive luminous lenses; sodium service lamps and LED canopy lights have matching pools. Selected stationary shoulder cars have head/tail lamps and small ground pools. Production perimeter fixtures merge into the same batch; factory glass and high-bay clerestories share a separate emissive window batch in both LODs. The highway has no continuous light strip.

Lenses use ACES exposure and a single merged additive halo mesh, with soft cores and bounded radii instead of a fullscreen bloom chain. Ordinary lamps conserve apparent energy below a pixel; only aviation beacons retain a minimum visible footprint. Low quality removes the halo skirt. This is deliberate local bloom approximation, not scene-wide bloom. Under Earth's atmosphere night exposure rises by at most 2.4× (about 1.26 stops), retaining the existing 0.65 s logarithmic exposure filter. Day exposure is unchanged. Vapour now scatters the actual sunlight/sky, baked spill and selected local lights once per shell fragment, rather than staying white in darkness. Its existing noise samples and puff pools are retained.

At full night the renderer skips the below-horizon sun shadow passes. The shadow allocation remains cached for sunrise. There are no new compute passes, full-screen passes, light shadow maps or per-lamp draws.

## Files

| File | Change |
|---|---|
| `src/render/site_lighting.hpp`, `src/render/site_lighting.cpp` | Merged fixtures/halos, static spill sources, combined pad sources and quality control. |
| `src/render/scenery.hpp`, `src/render/scenery.cpp` | Own lights, classify existing lenses/windows, and update from local solar elevation. |
| `src/render/lighting.hpp`, `src/render/lighting.cpp` | Shared dusk factor and bounded Earth night exposure. |
| `src/render/objects.cpp` | Feed the real scene sun to each site every lighting update. |
| `src/render/contact_shading.hpp`, `src/render/contact_shading.cpp` | Bake spill into unused atlas tiles without resizing the texture. |
| `src/render/production.hpp`, `src/render/production.cpp` | Separate glass from steel, add high-bay window strips, update window emission in both LODs. |
| `src/render/vehicle_surface.hpp` | Detailed stainless vehicle shader receives local lights and site spill, alongside its existing anisotropic sun reflection. |
| `src/render/webgpu_programs.cpp` | Batched fixture and halo shaders; vapour receives site and local light. |
| `src/gpu/programs.cpp` | Shared atlas spill lookup and local vapour irradiance. |
| `src/gpu/scene.cpp` | Pack the night factor and site transform at every quality; skip night sun shadows. |
| `src/render/application.cpp` | Bounded Earth surface night exposure, including focused flights. |
| `src/render/CMakeLists.txt` | Build the site-lighting implementation. |
| `tests/night_visuals.hpp` | Software pixel checks for receivers, clipping, day restoration, quality, transforms and five-kilometre beacons. |
| `tests/industrial_visuals.hpp` | Retain checks on the three industrial geometry batches alongside the new lighting node. |
| `tests/land_visuals.hpp`, `tests/visual_test.cpp` | Reproducible day/dusk/night capture matrix, matched lamp-disabled timing mode. |
| `docs/STARBASE_NIGHT.md` | Design, cost, evidence and limits. |

## Reproduction

From `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

From the repository root:

```sh
ALBERT_GPU=software ALBERT_AUDIO=0 LP_NUM_THREADS=2 ./build/linux/tests/visual_test night-captures build/codex/night-check/after
ALBERT_GPU=software ALBERT_AUDIO=0 LP_NUM_THREADS=2 ALBERT_SITE_LIGHTS_OFF=1 ./build/linux/tests/visual_test night-captures build/codex/night-check/disabled
```

Captures use the actual example scenery, vehicle appearance, scheduled tank boil-off, atmosphere tables, roads and contact field on software WebGPU. Ground is a flat ENU plane carrying the saved Earth imagery, not the live application's DEM. The software runs use `LP_NUM_THREADS=2`. Each view warms once and records three synchronized frames; reported time is their median, including readback. The comparison disables lamps in the same final renderer and keeps the night sun-shadow scheduling identical; it measures enabled lighting cost, not a historical executable.

PNGs are lossless conversions of the PPM readbacks. The twelve required frames use `pad-eye-{day,dusk,night}`, `tower-top-{day,dusk,night}`, `oblique-300m-{day,dusk,night}`, and `production-pad-{day,dusk,night}`. Supplemental frames show low site quality, the farm, production close-up and five-kilometre beacons. The low comparison switches site/road/shadow quality but holds vehicle detail constant to isolate lighting; the application also simplifies the vehicle in low quality. The complete linked capture matrix is [build/codex/night-check/index.md](../build/codex/night-check/index.md), with [overview](../build/codex/night-check/overview.png).

## Measured cost

The final fixture uses llvmpipe (LLVM 21.1.8, 256 bits), `LP_NUM_THREADS=2`, 1280×960, 4× MSAA. Times are median synchronized CPU-adapter frame/readback times after one warm-up; they are not laptop hardware GPU timings. Enabled and disabled night runs both omit sun shadows.

| Night view | Lamps disabled ms | Enabled ms | Increment | Scene draws off/on |
|---|---:|---:|---:|---:|
| pad-eye-night | 1359.5 | 1613.6 | +254.1 ms / +18.7% | 125/126 |
| tower-top-night | 1918.5 | 2190.5 | +271.9 ms / +14.2% | 116/117 |
| oblique-300m-night | 1332.5 | 1595.7 | +263.2 ms / +19.8% | 125/126 |
| oblique-300m-night-low | 951.5 | 1037.9 | +86.4 ms / +9.1% | 118/119 |
| production-pad-night | 1046.2 | 1076.2 | +30.0 ms / +2.9% | 12/13 |
| tank-farm-night | 1749.9 | 1960.8 | +210.9 ms / +12.1% | 39/40 |

The 300 m night view costs 1595.7 ms versus 1611.6 ms by day in the enabled run: skipping the existing sun shadow passes offsets the night-light work on this software adapter. The lamps-disabled comparison isolates the incremental illumination/halo cost; it includes the final geometry and shader architecture, so it is not an old-executable baseline. Both complete capture runs report zero validation errors.

Runtime budget: four bounded pad sources at normal/detail quality, two at low; one added merged fixture batch, one halo batch at night, and one separate production-window batch when the campus is visible. Existing pad/tower lenses retain batching. The halo contributes exactly one draw in each paired view. There is one additional atlas sample per lit surface fragment in the site; it exits by day. Vapour evaluates incident site/local light once outside its existing density loop. No new texture storage at detail quality, samplers, frame-buffer bytes, fullscreen/compute passes or light shadow maps. GPU texture residency is 128.9 MiB in both runs. The enabled halo adds about 0.1 MB of uploaded attributes in this fixture. The 64 MiB high sun-shadow map stays cached after nightfall, with zero night shadow draws.

The engine has two quality modes. The low timing switches site/road/shadow detail while retaining detailed vehicle shading for a matched light-cost comparison; the live application simplifies vehicle shading too. A cold low-quality night can upload the already-existing 8 MiB contact atlas. Hardware cost and thermals are unmeasured.

Timing data: [metrics.json](../build/codex/night-check/metrics.json), [enabled](../build/codex/night-check/after.log), [disabled](../build/codex/night-check/disabled.log). Fewer than 0.1% of pixels in each of the four main night views have all channels at least 250; see [pixel metrics](../build/codex/night-check/image-metrics.json).

## Limits

Fixture placement, power, pools and clerestory dimensions are visual estimates, not a current survey or photometric calibration. Static spill has no occlusion and can bleed through low walls; the combined pad lights have no local shadows or spot cones. The atlas supports one active site, following the existing contact/road selection. Multiple co-located flight sites can duplicate lighting nodes and compete for the same four slots; exhaust can displace pad sources. Production spill is intentionally coarse at distance. Traffic is the existing parked scenery, not simulated vehicles. There are no fog beams, volumetric shadows or reflections of individual fixtures in steel; the steel receives directional specular highlights and diffuse spill. Halos are depth-tested but their minimum beacon size is an artistic visibility aid. The atlas is already resident at detail quality; a cold low-quality night can upload its existing 8 MiB GPU image, whereas low daytime bypasses it. Hardware GPU timings, thermal behavior and full application flight/DEM review remain unmeasured.

The required build succeeds and both visual tests pass on llvmpipe (24.28 s, no skip). The new software checks include actual detailed vehicle steel receiving floods, a clipped-pixel guard, exact day restoration, low-quality source count, scaled/rotated-site night switching and red beacons at 5 km. `build/codex/night-check/build.log` and `ctest.log` record the checks. All 32 protected guidance/physics files match their starting SHA-256 hashes (`invariants.txt`); `git diff --check` is clean.

No guidance, physics or example files are changed. No commits were made.
