# Launch exhaust and deluge bank

The main launch cloud is one continuous, bounded volume in the active pad's ENU frame. Its field joins a scalloped radial ground bank, stronger flow along the trench, and tall, noisy billows. The upper wet layer is white; the ground layer mixes brown dust and grey exhaust. A short sunward density probe shades the billows. Orange emission falls off in three dimensions from the pad's engine impingement region, independently of sunlight, and fades when the engines climb away.

## Previous renderer

`Site::exhaust()` in `src/render/scenery.cpp` filled 160 shared-sphere hot-steam slots: 18 parcels/s divided between trench outlets, plus 8 parcels/s recirculating at the mount rim. Emission was `sqrt(thrust) * heightFalloff * sqrt(water/41)`; height falloff began at 40 m nozzle height and ended at 150 m. Each parcel lasted 95–125 s, its initial 80–150 m/s outlet velocity decaying with a 1.65 s time constant, growing to roughly 70–120 m horizontal radii and 66–96 m vertical radii. This mainly made two overlapping outlet banks. The `albert:steam` shader marched eight samples per shell, with procedural self-shadowing and dust below 32 m, but no explicit orange impingement core. Quality did not reduce this bank's particle count or sample count. Another 72 water shells and 24 tank vent shells had separate quotas. `SceneObjects` advanced the effect using wall time capped at 0.1 s/frame and supplied the default `(2,-0.8,0)` m/s wind.

## Current behavior

Thrust and engine height still determine pad impingement, including its 40–150 m falloff. Impingement deposits a bounded thermal charge (24 full-thrust seconds) and a water-weighted charge. Consequently a dry launch can raise dust while the effects switch disables the complete launch/deluge effect. The remembered water fraction determines how white the upper bank becomes. Spray, trench mist, runoff, scheduled tank boil-off, vehicle cryogenic mist and Raptor plumes keep their existing renderers.

The bank starts at the midpoint and ground elevation of the actual scenery's `steam vent` outlets. Their heading supplies its trench frame; it is not hard-coded to Starbase's origin. `site`/`flight` and `site.<id>`/`flight.<id>` pairing remains the source of thrust and height.

The radial envelope approaches a finite radius with a 12 s time constant; buoyant height uses 8 s. Stored momentum continues the expansion after the stack clears the mount. For the capture's ignition/release sample, the visible bank reaches several hundred metres across within 20–40 s. The maximum enclosing shell has horizontal radii below 413/489 m and a vertical radius below 177 m; the actual density occupies a smaller, irregular ground-clipped region. There is no allocation proportional to burn duration, wind distance or cloud size.

The cloud follows the pad mast's measured NED wind converted to ENU. If no valid mast measurement exists, the flight's ground air model and gust supply the wind, respecting its atmosphere/wind switches. This is passed by site id on the existing pad sound/deluge snapshot, independent of whether audio playback is enabled. It never switches to the climbing vehicle's high-altitude wind. Non-RPC scenery retains the documented default breeze. Wind coupling grows continuously from 18% toward 100%, with the constant-wind integral evaluated analytically.

Simulation UTC now advances the bank and water effects: pausing freezes them and rewinding clears them. After impingement ceases, density decays over 95 s, with a further 55 s decay after 150 s; the nearly transparent remainder expires after six minutes idle. Orange illumination decays over 1.2 s, so the residual bank does not glow after the stack leaves. Turning the effects switch off hides existing water/launch effects even while paused; their stored cloud can resume if re-enabled before expiry.

One closed ellipsoid bounds the ray integration, including for a camera inside it. Density is evaluated in metres using continuous, deterministic integer-hashed noise. There are no respawned launch particles, camera-facing planes, per-frame random offsets, temporal history, automatic distance LOD transitions or transparent launch-particle sorting. Shader powers with negative bases are not introduced (the new shader contains no `pow`).

## Files

| File | Change |
|---|---|
| `src/render/launch_cloud.hpp` | Owns the bounded launch bank's state and mesh. |
| `src/render/launch_cloud.cpp` | Charge, growth, wind integral, decay, reset, quality and premultiplied volume shell. |
| `src/render/launch_cloud_shader.hpp` | Continuous density, radial lobes, billows, vertical steam/dust mix, sun self-shadowing, orange core, finite ray integration and interior camera handling. |
| `src/render/webgpu_programs.cpp` | Registers `albert:launch-cloud` with its material layout. |
| `src/render/CMakeLists.txt` | Compiles the new volume component. |
| `src/render/scenery.hpp` | Replaces 160 hot-puff slots with one bank; exposes reset and cloud quality. |
| `src/render/scenery.cpp` | Derives the bank from the active outlets; updates it alongside existing water effects; removes hot-parcel spawning. |
| `src/render/objects.hpp` | Stores wind by site and the simulation clock; forwards cloud quality. |
| `src/render/objects.cpp` | Supplies the matching flight's thrust/height, site's wind and effects switch; simulation-time aging and rewind reset. |
| `src/rpc/flight.hpp` | Adds pad ENU wind to the internal sound snapshot. No wire API change. |
| `src/rpc/flight.cpp` | Copies measured mast wind, or reads the ground model, into that snapshot; no dynamics or control change. |
| `src/render/application.cpp` | Forwards per-pad wind; persists the launch-cloud quality setting and exposes its three tiers. Global low detail forces the cheapest tier. |
| `tests/launch_cloud_visuals.hpp` | Software GPU regressions and reproducible capture fixture using the actual site and stack appearances. |
| `tests/visual_test.cpp` | Runs regressions and exposes `launch-cloud-captures DIRECTORY`. |
| `docs/LAUNCH_CLOUD.md` | Design, reproduction, captures, cost and limitations. |

## Reproduction

```sh
cd /unreal/allan/albert-codex/build/linux
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
ctest -R visual --output-on-failure
cd /unreal/allan/albert-codex
ALBERT_GPU=software ALBERT_AUDIO=0 LP_NUM_THREADS=2 \
  build/linux/tests/visual_test launch-cloud-captures build/codex/launch-cloud/final
```

The fixture renders real WebGPU at 960×720 on llvmpipe with one sample/pixel, sun shadows, atmospheric lighting and saved coarse site imagery. T=0 is release; ignition ramps from T−3 to T−1 s. The explicit nozzle-height sample is `13 + 1.15*max(T,0)^2` metres, water is 41 m³/s until T+25, and wind is `(3,-1,0)` m/s ENU. These are controlled rendering inputs, not a flown guidance/physics sequence. The camera stays aimed at the cloud as the stack climbs out of frame. Tower supplements use a 95° perspective, without fisheye distortion.

PPM files are the renderer's readbacks; PNGs and the contact sheet are lossless conversions. All artifacts are local ignored build outputs.

## Captures

[Contact sheet](../build/codex/launch-cloud/final/sheet.png). Individual PNGs are under `build/codex/launch-cloud/final/`:

| Time | Above (1,300 m) | Drone (500 m altitude) | Eye level (1 km range, 1.7 m high) |
|---|---|---|---|
| T+2 s | [t2-above.png](../build/codex/launch-cloud/final/t2-above.png) | [t2-drone-500m.png](../build/codex/launch-cloud/final/t2-drone-500m.png) | [t2-eye-1km.png](../build/codex/launch-cloud/final/t2-eye-1km.png) |
| T+8 s | [t8-above.png](../build/codex/launch-cloud/final/t8-above.png) | [t8-drone-500m.png](../build/codex/launch-cloud/final/t8-drone-500m.png) | [t8-eye-1km.png](../build/codex/launch-cloud/final/t8-eye-1km.png) |
| T+20 s | [t20-above.png](../build/codex/launch-cloud/final/t20-above.png) | [t20-drone-500m.png](../build/codex/launch-cloud/final/t20-drone-500m.png) | [t20-eye-1km.png](../build/codex/launch-cloud/final/t20-eye-1km.png) |
| T+60 s | [t60-above.png](../build/codex/launch-cloud/final/t60-above.png) | [t60-drone-500m.png](../build/codex/launch-cloud/final/t60-drone-500m.png) | [t60-eye-1km.png](../build/codex/launch-cloud/final/t60-eye-1km.png) |

Tower views: [T+2](../build/codex/launch-cloud/final/t2-tower.png), [T+8](../build/codex/launch-cloud/final/t8-tower.png). The medium tier is used for the capture matrix.

## Cost

| Tier | Maximum primary samples/ray | Density noise octaves / sun probe octaves | Main cloud draws |
|---|---:|---:|---:|
| Low | 12 | 1 / 1 | 1 |
| Medium (default) | 20 | 2 / 1 | 1 |
| High | 32 | 2 / 2 | 1 |

Empty samples avoid shading work; optically thick rays terminate early. CPU state and shell storage are constant per pad. No full-screen pass, density texture, simulation grid, history buffer or cloud shadow map is allocated. The unchanged water effects retain at most 72 shells, and tank boil-off 24. The main bank replaces up to 160 separate draws, rather than adding another particle pool. The bank is ray-marched at the scene's pixel resolution, so cost still increases with screen coverage and resolution; this is not a half-resolution post-process.

Measured at T+60 in the same drone view, on **two-thread llvmpipe software WebGPU**, 960×720, 1× MSAA. The median of three warmed frames is compared with one warmed cloud-hidden frame (353.7 ms, 114 draws):

| Tier | Whole scene timestamp ms | Difference with cloud hidden | Whole scene draws |
|---|---:|---:|---:|
| Low | 433.6 | +79.8 ms | 115 |
| Medium | 500.8 | +147.1 ms | 115 |
| High | 571.6 | +217.8 ms | 115 |

These are software-adapter costs, including the scene and sun shadows, not laptop hardware GPU budgets. Differences are approximate component costs from separate warmed frames. The medium volume remains a significant software cost; no hardware frame-rate or thermal target is established. Readback-inclusive medians are 439.0/506.3/581.4 ms. Cold shader compilation is excluded. [Capture/timing log](../build/codex/launch-cloud/captures.log), [machine-readable metrics](../build/codex/launch-cloud/metrics.json), [conversion/audit script](../build/codex/launch-cloud/collect.py).

## Verification

The requested three-target `/usr/bin/cmake` build with `-j2` succeeds; its tail links the renderer, application and visual test. `ctest -R visual --output-on-failure` passes both `visual_controls` and real llvmpipe `visual_webgpu`, without a skip. The regressions check expansion after impingement ends, persistent cloud at T+60, exact stationary-frame equality outside/inside the cloud, pause and effects switching, measured-wind direction, Earth-radius scene scaling, a displaced pad's outlet placement, quality caps/restoration, internal orange lighting without sunlight, removal of that light after cutoff, and eventual expiry. The capture run reports zero GPU validation errors.

All 32 files under the protected guidance/physics paths are byte-identical to their starting hashes. No commit was made. `git diff --check` passes. Evidence: [build](../build/codex/launch-cloud/build.log), [visual tests](../build/codex/launch-cloud/ctest.log), [protected-file audit](../build/codex/launch-cloud/invariants.txt).

## Limits

This is a procedural single-scattering visual approximation, not a fluid/deluge simulation or a reconstruction of Flight 11. The enclosing volume uses the existing transparent-shell depth test; it does not read opaque scene depth to stop each ray at an interior vehicle or building. Consequently partially embedded objects can be over-attenuated. It self-shadows from the sun but does not cast a volumetric shadow onto the terrain or model every tower member's shadow inside the bank. The radial front follows trench orientation, but does not collide with individual walls or buildings. Ground clipping uses the pad's tangent plane, not DEM excavation.

The fixed sampling grids and deterministic field remove particle birth/sort pops and temporal noise; finite ray sampling can still soften or alias small detail during motion, particularly on low quality. These captures and stationary-frame regressions do not establish flicker-free behavior on every hardware adapter. A forward seek cannot reconstruct exhaust never rendered; a rewind clears previous history. One bank per site's exhaust marker set is supported; unrelated active mounts in one scenery object would need separate marker groups. No hardware GPU frame-time, power or laptop temperature measurement is claimed.
