# Starbase coastal sky and light

The drawing now adds a humid marine boundary layer around Starbase, warms direct and diffuse light at low solar elevations, and feeds low coastal clouds from flight weather. The global Bruneton clear-air model and the physics atmosphere remain unchanged.

## Weather and drawing

Previously `earthAtmosphere()` used a fixed clear-day aerosol optical depth of 0.1 at 550 nm, with a 1.2 km scale height. The volumetric renderer read the optional MODIS visual mask, not flight weather. Only the GFS flight's near-ground wind reached water roughness and cloud drift. GFS downloaded specific humidity, but did not request cloud cover.

The bridge now carries near-surface specific humidity, temperature, pressure and low-cloud fraction, with the flight position. The renderer converts specific to relative humidity. It accepts fresh telemetry within 50 km of the eye; otherwise it uses a coastal default of 78% RH and 38% broken low cloud. The default applies fully within 40 km of Starbase, fades to zero by 100 km, and fades between 10 and 30 km altitude. Other locations keep their existing atmosphere and satellite clouds.

The GFS provider makes a separate, optional TCDC request for the low-cloud layer. It reads instantaneous or averaged fields, interpolates the grid bilinearly and forecast hours linearly, and preserves the additional GRIB messages in the existing replay bytes. The physics grid ignores these fields. Cloud download/encoding failures do not invalidate the flight's weather; old caches and logs without TCDC retain the fallback cloud cover. A reported zero cloud fraction stays clear. Forecasts outside the downloaded cloud times are unavailable.

The decoder uses [NOAA's low-cloud surface code 214](https://www.nco.ncep.noaa.gov/pmb/docs/grib2/grib2_doc/grib2_table4-5.shtml) and [template 4.8](https://www.nco.ncep.noaa.gov/pmb/docs/grib2/grib2_doc/grib2_temp4-8.shtml) for averages. An averaged cloud fraction is attributed to its interval end for visual interpolation. No new weather value enters forces, guidance or integration.

## Appearance

- The additional sea-salt aerosol extinction grows with humidity and falls exponentially with a 0.8 km scale height. The default adds approximately 0.055 /km at sea level. An analytic, bounded column estimate includes spherical endpoint curvature; no haze ray march or atmosphere-table rebuild is added.
- Surface aerial perspective, the sky, ocean/steel sky reflections, sunlight, additive effects and stars use the same extinction. Weak wavelength dependence gives whitish daytime distance and warm low-sun haze. Extra diffuse scattering warms open shade instead of only removing direct light.
- Low cloud bases follow an approximate warm-air condensation height (450–1500 m); broken cumulus has a deeper domed profile, while high cloud fraction becomes a shallower stratocumulus deck. These tune the existing Perlin-Worley volume and its lighting; no replacement cloud renderer is introduced.
- Existing wind-dependent Cox-Munk sea glint and FFT-wave shading receive the filtered, warm sun and hazy sky reflection. The Gulf is east of the pad: the supplemental glitter view deliberately puts the low sun over the sea. The matched sunset views put it west.
- ACES and logarithmically filtered exposure remain in use. The near-horizon exposure boost is reduced slightly, keeping sunset colour while preserving the existing final night adaptation and site-light calibration. Stars already used atmospheric extinction and sky brightness; the new layer further suppresses them near the horizon without adding night sky emission.

## Files

| File | Change |
| --- | --- |
| `src/render/lighting.hpp` | Coastal visual parameters and humidity conversion declarations. |
| `src/render/lighting.cpp` | Bounded hygroscopic aerosol growth, coastal cloud heights/defaults, sunset exposure. |
| `src/render/application.cpp` | Nearby flight-weather selection, geographic/altitude fade, cloud handoff and setting description. |
| `src/render/display.hpp` | Coastal light interface and cloud budget documentation. |
| `src/render/display.cpp` | Preserves haze across air selection, supplies coastal layer settings, bounds cloud work. |
| `src/gpu/programs.cpp` | Shared analytic coastal extinction, in-scatter, sunlight, diffuse illumination and local cloud cover. Uses the previously reserved uniform slot. |
| `src/gpu/scene.hpp` | Visual haze/cloud fields and optional cloud ray budget. |
| `src/gpu/scene.cpp` | Uploads visual parameters, permits local weather clouds without a satellite texture, applies ray budget. |
| `src/gpu/clouds.cpp` | Weather coverage, coastal deck profile, filtered sunlight and reduced low-step sampling stipple. |
| `src/core/bridge.hpp` | Drawing-only surface weather metadata appended to internal telemetry. |
| `src/rpc/flight.cpp` | Reads immutable surface weather and retains cloud metadata for the renderer. |
| `src/providers/gfs.hpp` | Low-cloud interpolation API and supported cloud-average format. |
| `src/providers/gfs.cpp` | Optional cloud download, restricted cloud-average decoding, spatial/time interpolation. |
| `tests/visual_test.cpp` | Humidity/fallback/exposure bounds and coastal capture entry point. |
| `tests/land_visuals.hpp` | Matched nine-view captures, component timings and supplemental weather/night/glint views. |
| `tests/weather_test.cpp` | Cloud average decoding, interpolation, optional-download failure, replay preservation and unchanged physics air values. |
| `docs/STARBASE_SKY.md` | This implementation, verification and capture record. |

## Captures and checks

Artifacts are under `build/codex/coastal-check/`. The original nine frames were captured before changing rendering code, using only the new capture entry point. `before/` and `after/` use the same cameras, solar directions, 1280×960 targets, 4× MSAA, scenery, imagery and shadow configuration. Names are `{pad-eye,production-west-500m,tower-sea}-{noon,golden,sunset}.png`; elevations are approximately 64°, 6° and 0.5°. PNGs are lossless conversions of the software WebGPU PPM readbacks.

These use the actual example site/stack/roads/land-cover materials and full atmosphere tables on a flat ENU ground fixture, not the running application's DEM. Surface wind roughness is enabled; FFT waves are disabled in both runs, as in the software application's water fallback. Volumetric clouds are explicitly enabled in the offscreen after fixture so software captures can validate them; the interactive software adapter still disables them. Shadows use the existing single 1024² map in both runs. Site and vehicle detail remain enabled.

Reproduction from the repository root:

```sh
ALBERT_GPU=software ALBERT_AUDIO=0 LP_NUM_THREADS=2 build/linux/tests/visual_test coastal-captures build/codex/coastal-check/after
```

Build and checks from `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
LP_NUM_THREADS=2 ctest -R visual --output-on-failure
```

Additional targeted checks build/run `weather_test` and `clouds_test`. The [capture index](../build/codex/coastal-check/index.md) links every full-resolution before/after pair and supplemental frame, logs, clipping statistics, timings and the protected-file SHA-256 audit. No commit was made.

## Cost

Haze adds bounded arithmetic and table lookups to the existing shaders, with no new textures, passes or LUT precomputation. It occupies three floats in the existing reserved frame uniform. The existing cloud volume is limited to 32 view steps, at least a four-pixel divisor and approximately 80,000 rays maximum; higher resolutions increase the divisor. At 1280×960 that is 320×240 rays (2,457,600 maximum view samples), with the existing five-step sun-depth estimate only at occupied samples. Cloud output textures consume approximately 0.88 MiB and the reusable noise approximately 0.28 MiB at that resolution. Low detail disables the volume. The existing frame cap and `volumetric_clouds` switch remain available.

Measured on llvmpipe (LLVM 21.1.8), with `LP_NUM_THREADS=2`:

| Noon view | Before | After | Change |
| --- | ---: | ---: | ---: |
| Pad eye level | 1468.7 ms | 1868.2 ms | +27.2% |
| Production west, 500 m | 856.3 ms | 1019.3 ms | +19.0% |
| Tower over sea | 417.0 ms | 520.7 ms | +24.9% |

Across all nine pairs the increase is 15.0–38.8%. The matched component control at the production view measures 860.8 ms with the coastal layer disabled versus 964.8 ms with haze/illumination and cloud marching disabled: +104.1 ms (+12.1%). Draw counts and opaque scene resources stay the same in matched views. [Full timings](../build/codex/coastal-check/timings.csv) include GPU timestamps. Software timings include synchronized readback, discard one warm-up frame and report the median of three measured frames. They are not hardware GPU or laptop thermal measurements.

The exact requested build completes with successful `AlbertEngine` and `visual_test` links (the flight client is already current). Both visual tests pass, without a skip; the targeted cloud and weather suites also pass. Both capture runs report zero validation errors. All 32 protected guidance/physics files retain their original SHA-256 values, and `git diff --check` is clean. The nine matched final frames have zero pixels with all three channels at or above 254; the golden production view has 265 pixels reaching that threshold in an individual channel, recorded separately in the statistics.

## Limits

The boundary layer is a local visual approximation, not a weather/fog or aerosol retrieval. No aerosol concentration is fetched. Low-cloud base and thickness are estimated, and a GFS grid cell's cover is distributed procedurally, not a reconstruction of actual individual clouds. Local cloud shadows use the existing coverage approximation, not volume-marched ground shadows. There is no temporal cloud reconstruction; low-step edges may still show spatial sampling or bands. The analytic haze source/column is approximate, with no additional multiple-scattering solution or local lamp scattering. Twilight below cloud decks can remain dark. These captures establish software output, not live DEM contact or hardware performance. The optional live NOAA cloud request was verified with replayable fixture data, not a new live Starbase forecast.
