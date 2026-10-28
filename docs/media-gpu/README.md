# Local media GPU comparison

1920×1080, quality 1, 4× MSAA. Adapter: **llvmpipe (LLVM 21.1.8, 256 bits), software Vulkan**. These are GPU timestamp durations on the software adapter, not MX350 measurements or a thermal test. No working NVIDIA driver is available here.

Frozen renderer telemetry uses the example stack, booster and ship appearances. The shore eye is at (750, 1.7, 661.44) metres; the orbit eye is near the stack. Boostback includes both large envelopes. Scenes use fixed lighting and a flat ground fixture, without the full live mission terrain, atmospheric tables or launch tower. Frames are identical between runs; no dynamics or guidance advance during capture.

Each reported number is the median of 8 synchronized frames after 3 warmup frames. Runs were sequential. Repeated passes of one category are summed per frame before taking the median. The native baseline was captured before the final empty-lighting and face-culling optimizations. `ALBERT_MEDIA_NATIVE=1` remains a full-resolution visual reference; its current timings include the later shader algebra changes.

[Interactive capture comparison](index.html) · [Raw before timings](before/passes.csv) · [Raw after timings](after/passes.csv)

## liftoff-shore-tracking

[Before](before/liftoff-shore-tracking.png) · [After](after/liftoff-shore-tracking.png). RGB mean absolute difference: 0.0648/255; channel p99: 1; pixels with any channel differing by >10: 0.153%.

| Pass | Before ms | After ms |
|---|---:|---:|
| cluster-plume | 45.749 | 43.958 |
| burn / collar | 8.936 | 1.920 |
| launch-cloud | 164.393 | 13.584 |
| media-composite | 0.000 | 29.561 |
| media-depth | 0.000 | 11.176 |
| opaque | 56.072 | 55.467 |
| clear / resolve | 9.560 | 9.330 |
| transparent | 8.150 | 6.921 |

Sum of pass medians for local media, including new depth and composite passes: **173.330 → 56.241 ms** (3.08×).

## liftoff-orbit-near-stack

[Before](before/liftoff-orbit-near-stack.png) · [After](after/liftoff-orbit-near-stack.png). RGB mean absolute difference: 0.0286/255; channel p99: 1; pixels with any channel differing by >10: 0.004%.

| Pass | Before ms | After ms |
|---|---:|---:|
| cluster-plume | 53.356 | 53.001 |
| burn / collar | 11.389 | 2.605 |
| launch-cloud | 198.548 | 13.967 |
| media-composite | 0.000 | 34.146 |
| media-depth | 0.000 | 11.806 |
| opaque | 369.653 | 382.711 |
| clear / resolve | 9.367 | 9.712 |
| transparent | 8.657 | 8.659 |

Sum of pass medians for local media, including new depth and composite passes: **209.937 → 62.523 ms** (3.36×).

## boostback-near-booster-two-envelopes

[Before](before/boostback-near-booster-two-envelopes.png) · [After](after/boostback-near-booster-two-envelopes.png). RGB mean absolute difference: 0.0023/255; channel p99: 0; pixels with any channel differing by >10: 0.000%.

| Pass | Before ms | After ms |
|---|---:|---:|
| cluster-plume | 17.242 | 18.785 |
| bloom / vacuum envelope | 102.236 | 8.105 |
| media-composite | 0.000 | 27.547 |
| media-depth | 0.000 | 5.715 |
| opaque | 459.745 | 497.554 |
| clear / resolve | 9.366 | 9.560 |
| transparent | 6.911 | 7.108 |

Sum of pass medians for local media, including new depth and composite passes: **102.236 → 41.367 ms** (2.47×).

## landing-ignition-near-booster

[Before](before/landing-ignition-near-booster.png) · [After](after/landing-ignition-near-booster.png). RGB mean absolute difference: 0.0150/255; channel p99: 0; pixels with any channel differing by >10: 0.014%.

| Pass | Before ms | After ms |
|---|---:|---:|
| cluster-plume | 50.360 | 54.909 |
| engine-cloud | 5.268 | 5.381 |
| burn / collar | 40.961 | 8.684 |
| mist | 37.356 | 6.718 |
| media-composite | 0.000 | 31.658 |
| media-depth | 0.000 | 6.038 |
| opaque | 233.464 | 274.868 |
| clear / resolve | 9.539 | 10.003 |
| transparent | 13.508 | 13.977 |

Sum of pass medians for local media, including new depth and composite passes: **83.584 → 58.479 ms** (1.43×).

## landing-burn-near-booster

[Before](before/landing-burn-near-booster.png) · [After](after/landing-burn-near-booster.png). RGB mean absolute difference: 0.0138/255; channel p99: 0; pixels with any channel differing by >10: 0.008%.

| Pass | Before ms | After ms |
|---|---:|---:|
| cluster-plume | 43.490 | 43.444 |
| burn / collar | 31.024 | 6.489 |
| mist | 36.341 | 6.476 |
| media-composite | 0.000 | 28.117 |
| media-depth | 0.000 | 5.986 |
| opaque | 233.489 | 277.189 |
| clear / resolve | 9.575 | 9.722 |
| transparent | 7.470 | 7.269 |

Sum of pass medians for local media, including new depth and composite passes: **67.365 → 47.068 ms** (1.43×).

## Validation

The requested build completed successfully ([build log](build.log)). `ctest -R visual --output-on-failure` passed `visual_controls`; `visual_webgpu` failed at the unchanged pending assertion, “Vent became invisible or an opaque white ball” ([CTest log](ctest-visual.log)). The complete `webgpu-keep-going` run reached the end with exactly that one failed assertion ([all checks](all-checks.log)). No assertion was removed or relaxed.

The reduced-media checks passed with 1×/4× MSAA, outside/inside views, deterministic repeated frames, hidden effects, steam, and analytic mist. Cloud RGB MAE: 0.0367 outside / 0.1465 inside; steam MAE: 0.01143; mist intensity ratio: 1.01224 ([media checks](media-check.log)). The capture runs reported zero WebGPU validation errors. Captures were also inspected visually.

Worktree: `/unreal/allan/albert-codex`, branch `codex/visual`. No commit was made. `examples/starship.hpp`, `examples/ship_return.hpp`, `examples/descent.hpp`, `examples/countdown.hpp` and `src/physics` are unchanged.

## Changes by file

- `src/gpu/local_media.hpp`: persistent RGBA16F scratch targets, nearest-depth MSAA snapshot, reduced depth test, four-tap depth-aware upsample, premultiplied composite. Each target is allocated on demand and reused; source depth bindings are cached.
- `src/gpu/scene.cpp`: projected footprint and inside-camera detection; half resolution for broad shells and quarter resolution above 65% of screen at 1080p; native resolution below 64 pixels or on targets below 480 rows; independent half/quarter layers; flush at transparent sort boundaries; composite scissor; one-sided shell rasterization; offscreen shell culling. Detailed timing splits are enabled only for profiling.
- `src/gpu/scene.hpp`: native-reference switch and last-completed per-pass timing accessor.
- `src/gpu/pass_timing.hpp`: optional timestamp-query instrumentation, asynchronous readback and per-pass averages printed every two seconds with `ALBERT_GPU_TIMING=1`; explicit unsupported-feature message.
- `src/render/return_gas_shader.hpp`: projected-size sample budget with six-step minimum at quality 1; negligible-transmission exit only when no unattenuated ignition remains; exact empty axial support skip; defer atmospheric lighting for empty bloom/mist rays and remove unused hot-stage lighting.
- `src/render/launch_cloud_shader.hpp`: bounded step budget (20→19 for an inside quality-1 bank; exterior rays keep 20), exact empty half-space skip. Existing optical-depth termination retained. Splashdown inherits this integration.
- `src/render/splashdown_shader.hpp`: exact empty lower half-space skip for steam.
- `tests/media_captures.hpp`: repeatable five-view 1080p benchmark with explicit poses/flow/pressure and per-frame CSV.
- `tests/media_visuals.hpp`: native/reduced comparisons for outside/inside cameras, MSAA 1/4, steam, repeatability and invisible effects.
- `tests/visual_test.cpp`: capture/check entry points and an optional `webgpu-keep-going` mode that reports failed assertions and still exits nonzero. The vent assertion is unchanged.
- `scripts/media_gpu_report.py`: converts captures and generates the timing/image comparison artifacts.

## Reproduction

From `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
ALBERT_GPU_TIMING=1 ALBERT_MEDIA_NATIVE=1 ./tests/visual_test media-captures ../../docs/media-gpu/reference
ALBERT_GPU_TIMING=1 ./tests/visual_test media-captures ../../docs/media-gpu/optimized
ctest -R visual --output-on-failure
ALBERT_GPU=software ./tests/visual_test webgpu-keep-going
```

Do not run the two benchmarks concurrently. Omit `ALBERT_GPU=software` to use the hardware adapter. `ALBERT_BENCH_FRAMES` changes the measurement count (default 8). The native-reference environment variable disables reduced targets, adaptive counts, culling and opacity termination for comparison. On a live client, `ALBERT_GPU_TIMING=1` prints categories while the mission runs.

## Limits

Quality-1 vacuum envelopes retain six integration steps: reducing them to five produced a visible change and was rejected. High-quality gas can reduce twelve steps to ten/eleven. Quality-1 exterior cloud and steam rays retain their sample positions because changing their phase grid moved visible lobes. Interior rays reduce conservatively by one; higher-quality rays can use thirty of thirty-two steps. Exact empty support is skipped without shifting any retained sample. Thin analytic jets and tiny effects stay native when their footprint is small.

Unattenuated emission cannot terminate on opacity alone without changing the image. It retains the complete bounded integration. Geometry, physical telemetry, guidance and mission examples were not changed.

The pass count and depth/composite cost matter: reduced shading does not promise a fourfold reduction of the entire frame. Profiling introduces render-pass boundaries for category timings. Software adapter timings include large raster/resolve overheads and cannot predict MX350 speed or prove that thermal shutdowns are eliminated. Live MX350 timings, a thermal soak, full mission scenery and motion comparisons remain to be done on hardware.
