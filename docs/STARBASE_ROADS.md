# Starbase roads

Roads are analytic ground decals in the Earth's terrain material. They use the terrain's own triangles, normals, LOD morph and depth; there is no separate road mesh, elevation offset or polygon-offset workaround. This adds zero draw calls and uses the existing sun, shadow, atmospheric perspective and night lighting. Roads have no emission or collision geometry.

The saved Overpass JSON generates 44 ways, 752 points and 708 segments. A 16 m spatial grid points to short lists of capsule segments and grounded scenery footprints. Signed-distance union joins bends and intersections without coplanar overlapping triangles. Highway pavement takes precedence at service junctions. Fixed, grounded scenery boxes mask the decal beneath apron slabs and buildings. The full Starbase fixture has at most 22 candidates in a tile; lists are never truncated. Lookup textures hold coordinates and indices, not downloaded surface artwork. The index occupies approximately 3 MiB for this dataset; hardware GPU timing remains to be measured.

Widths are 10.5 m for two-lane TX 4, 7 m for service roads unless OSM gives a width, 6 m for the single-lane concrete roundabout, 3.5 m for tracks, and 1.5 m for footways/paths. Untagged service roads are assumed concrete; untagged tracks/paths are assumed sand. All saved ways are retained without geometric simplification, and carry their OSM IDs.

Asphalt has procedural aggregate, four tyre streaks, repair patches, intermittent cracks and frayed edges. Concrete has 5.5 m transverse and longitudinal joints, stains and soot near apron footprints. Tracks blend partly into the existing imagery with a softer edge. Low quality drops the fine aggregate and cracks. Highway paint uses two solid 12 cm yellow lines, 12 cm white edge lines around a 7.2 m carriageway, and approximately 1.5 m shoulders. Solid yellow is a conservative assumption around the launch site's entrances; passing zones are not inferred. Service stop bars are 40 cm thick, approximately 7.5 m from their OSM connection to the highway. Paint wear and all surface aging are procedural approximations, not surveyed features.

Marking coverage is integrated analytically over its pixel footprint. Repeating concrete joints use an integrated periodic pulse, so subpixel joints average out rather than form moire. Local coordinates are computed in the vertex shader from geodetic terrain UVs plus their double-to-float residuals, avoiding metre-scale UV quantization of centimetre-scale paint. The GPU supports exact, unfiltered float lookup textures even when filtered float32 textures are unavailable.

The concrete albedo and antialiased joint helpers are now shared with the solid pad aprons, piers and walls; see [Starbase pads](STARBASE_PADS.md). The road geometry, geographic alignment and pavement widths are unchanged.

## Changed files

| File | Change |
| --- | --- |
| `scripts/starbase_osm.py` | Emits road points, IDs, widths, surface choices and service junction flags using the existing WGS84 ENU conversion. Reads only the supplied JSON. |
| `examples/starbase_osm.hpp` | Regenerated from `build/codex/osm/starbase-2026-10-24.json`; existing scenery rows remain intact. |
| `examples/starship_flight.hpp` | `addStarbase()` sends the generated roads without shifting their geodetic coordinates. |
| `proto/albert/v1/engine.proto` | Adds `SceneryRoad` and request field 25, preserving existing field numbers. |
| `src/core/scene.hpp` | Adds the internal road representation and site latitude/longitude in radians. |
| `src/rpc/flight.cpp` | Validates road sizes and finite coordinates, transfers roads into scenery, includes them in site bounds, and clears them when spawning a child flight. |
| `src/render/roads.hpp` | Owns the road spatial index and terrain material wrappers. |
| `src/render/roads.cpp` | Builds the index and footprint masks, and extends the standard surface shader with procedural roads and antialiased paint. |
| `src/gpu/programs.hpp` | Exposes the shared standard surface program and exact data-texture declarations. |
| `src/gpu/scene.cpp` | Reuses the surface program and supports unfilterable float32 data textures. Existing sampled textures keep their current behavior. |
| `src/render/terrain.hpp` | Adds a base-material setter without replacing terrain geometry. |
| `src/render/terrain.cpp` | Stores geographic UV residuals and supports swapping the base material. |
| `src/render/visuals.hpp` | Owns the road layer and accepts the current registry. |
| `src/render/visuals.cpp` | Applies the road material to base and regional Earth terrain. |
| `src/render/application.cpp` | Synchronizes the road layer from visible scene objects. |
| `src/render/CMakeLists.txt` | Builds `roads.cpp`. |
| `tests/visual_test.cpp` | Renders a synthetic highway/service intersection, checks asphalt/concrete contrast, apron clipping, yellow paint, night darkness, and site removal. Shader compilation and rendering are covered by the existing visual WebGPU test. |
| `docs/STARBASE_ROADS.md` | Records design, verification, screenshots and limits. |

## Verification and alignment

- Regeneration from the saved JSON compares byte-for-byte with the generated header. Centre lines are emitted to millimetres with no manual offsets; the maximum rounding error against the saved JSON is 0.000691 m. The same origin and WGS84 meridional/prime-vertical scales are used by the converter and the terrain shader.
- Built `AlbertEngine` and `albert_flight_client` using `/usr/bin/cmake --build . --target AlbertEngine albert_flight_client -j2` in `build/linux`; the last lines link successfully or report no work.
- Built `visual_test` and ran `ctest -R visual --output-on-failure`: both visual tests pass on llvmpipe with zero WebGPU validation errors. The PATH wrapper for `ctest` has a broken interpreter; `/usr/bin/ctest` works directly.
- Ran `terrain_lod` after building its target to check the modified terrain path.
- A local offscreen preview uses the real `addStarbase()` parts and road request, the saved visual pack, one software WebGPU context, audio disabled, and detail setting 1. The plan and oblique views were inspected for continuous junctions, pad/apron boundaries, building cutouts and the roundabout. The close view was inspected for highway widths, paint and the service stop bar. This fixture uses a flat ENU plane; it is not a capture of the full application running over the live DEM. The application decal is evaluated directly on the DEM triangles.

Generated screenshots and the preview source/log are under `build/codex/road-check/` (local artifacts, not committed):

- `starbase-roads-plan.png`: roads and the actual site parts from above.
- `starbase-imagery-plan.png`: identical scene with the road decal disabled.
- `starbase-roads-oblique.png`: roads and pads from an oblique view.
- `starbase-road-paint-close.png`: double yellow, white edges, frayed asphalt, concrete joints and service stop bar.
- `preview.cpp`, `preview.log`: source and results of the short offscreen check.
- `alignment.txt`: point count and measured rounding error against the saved JSON.

## Limits and uncertainty

The bundled `earth-day` layer declares an October 2004 epoch and approximately 500 m/pixel resolution at the equator. That imagery cannot verify a 7 m service road or the reconstructed V3 pads. The screenshots verify geographic placement against the available imagery, not metre-level agreement with a current aerial survey. No newer imagery was downloaded.

There is a pre-existing coordinate inconsistency: the stated geodetic origin is Tower 1, but its drawn centre is `towerX=-22`, while the other OSM scenery uses its generated coordinates directly. Road coordinates remain tied to the requested origin; the tower and flight guidance are untouched. The apron footprints are those of the existing reconstructed scenery, and may disagree with current pavement visible in other imagery.

Roads require Earth terrain to be enabled. The current layer selects the first visible road-bearing site in registry order; simultaneous flights with the same Starbase scenery work, but distinct road-bearing sites need a multi-site extension. The ground must remain the terrain surface: roads do not grade or cut the DEM, add curbs, drainage, surveyed superelevation, or model new aprons. Footprint clipping currently handles fixed grounded boxes; future non-box buildings need additional mask primitives. GPU cost and temporal stability at grazing angles still need a hardware check in the full application; the software tests verify shader validity and offscreen pixel behavior.

No commits were made. The concurrently edited guidance files were not changed by this work.
