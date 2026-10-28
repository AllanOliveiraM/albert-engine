# Super Heavy V3 grid fins

Rendering only. The appearance's 6.1 m width, 3.7 m radial span, 0.6 m flow depth, pivot, radial hinge axis and sampled actuator gain/offset are preserved. No example definition, guidance or physics file changes.

## Files

- `src/render/grid_fin.hpp`: procedural hexagonal outline with bevelled tip corners, diagonal thin sheets, concave lips, rounded crown ends and boundary-weld fillets. Produces one geometry per tier, with geometry counters for inspection.
- `src/render/vehicle.cpp`: generates each size once per vehicle and shares that source among its three fins before joining their independently posed ranges into the existing hardware batch. Adds the turning root plate, radial spindle and knuckle, and the hull-fixed buried actuator and smooth light-grey cover. Uses existing PBR, depth and shadows, with steel colour (0.105, 0.11, 0.12), roughness 0.29 and metalness 0.92.
- `src/render/vehicle_geometry.hpp`: carries optional per-vertex colour into the hardware batch so bright lips and dark well faces share the same PBR draw.
- `tests/vehicle_geometry_visuals.hpp`: ray checks for diamond holes, missing rectangular corners, solid root, concave lip versus straight LOD, dimensional bounds, bright-lip/dark-well colours surviving batching, and covers staying fixed during deflection. Existing actuator, frozen-upload, rendering, quality and Earth-scale LOD checks remain active.
- `tests/grid_fin_captures.hpp`: reproducible 15 m three-quarter and grazing, 10 m square-on, and 60 m top inspection cameras and actual broadcast tracking camera during a prescribed coast pose; writes PPMs and metrics.
- `tests/visual_test.cpp`: registers `grid-fin-captures` and includes the generator for the geometric checks.

## Geometry and cost

The lattice has 0.48 m crossing-to-crossing cells at 45 degrees to the root, about nine diamond diagonals across the widest section. There are 62 complete interior cells plus boundary-cut cells and 162 surviving sheet spans per fin. Sheets are 22 mm thick. Their leading and trailing lips each recede 0.13 m between welded crossings from the 0.6 m total depth. A subtle 55 mm opposing bow at the lips makes the cusped openings visible face-on as well as obliquely. Smooth interpolated sheet normals preserve narrow metallic highlights. The wall depth is split into three bands: narrow lips use a 1.8 colour multiplier, the inner well faces 0.24, retaining metallic PBR speculars and ordinary shadows. This is baked vertex shading, not emissive light or a new shader.

The main hexagon runs from a short 2.44 m straight root through shoulders at 32 degrees to that edge, then along two parallel long sides to the outer tip. Both tip corners have 0.55 m chamfers. The crown uses smooth scallops up to 65 mm deep. The 20 cut sheet ends flare locally from 22 mm to 80 mm and close with semicircular caps (8 / 4 segments). The 16 final welded junctions have 110 mm diameter bevelled fillets (16 / 8 sides), rounding the V-shaped full-cell crests as well as the cut ends. Outer lip peaks are trimmed 55 mm. Spans shorter than 180 mm at the boundary, or with both welds outside the fin, are dropped. There is no continuous outer frame bar. The root alone has a 0.22 m radial-depth plate. The fixed cover is a smooth flattened ellipsoid over the inboard actuator and root, with 24×12 / 12×6 tessellation.

| Detail | Segments between crossings | Lattice triangles / fin | Whole booster hardware triangles |
| --- | ---: | ---: | ---: |
| Tier 0 | 10 | 31,264 | 161,349 |
| Tier 1 / low quality | 1 | 5,568 | 28,151 |

The existing distance switch remains 650 m, keeping the curved geometry throughout the requested 200 m neighbourhood. Tier 1 retains the pitch and hexagonal envelope with straight-cut sheet edges. Low quality forces that same simple tier. Both pose buffers follow actuator changes, allowing immediate LOD restoration while paused.

All three fins, their roots and the other vehicle hardware occupy **one colour-pass hardware draw** at either tier. A cold detached booster has **two total colour draws**, including its hull; all six captures report two. Shadows reuse the same posed batch in the existing shadow passes. The source is shared during generation; the merged batch necessarily stores three vertex ranges for independent deflection. No per-cell meshes or draws.

## Captures and checks

Software WebGPU (llvmpipe), 1280×960, 4× MSAA, existing PBR and shadows. PNGs are lossless conversions of the original PPM readbacks in `build/codex/grid-fin-v3/review/`.

- [15 m three-quarter close-up](../build/codex/grid-fin-v3/review/fin-close-15m.png): curved cell lips, diamond crossings, crown and root cover; [same view at low quality](../build/codex/grid-fin-v3/review/fin-close-low.png).
- [10 m square-on face](../build/codex/grid-fin-v3/review/fin-face-10m.png): root shoulders, parallel long sides, chamfered tip and rounded boundary junctions.
- [15 m grazing view](../build/codex/grid-fin-v3/review/fin-grazing-15m.png): bright curved lips over dark deep walls.
- [60 m booster top](../build/codex/grid-fin-v3/review/booster-top-60m.png): all three declared fins and the open staging ring.
- [Coast on the tracking camera](../build/codex/grid-fin-v3/review/booster-coast-tracking.png): actual `BroadcastCamera::Tracking`, 3,914.64 m range, 3 km altitude, engines off, sampled ±0.45 rad fin deflection. This is a prescribed visual sample, not an advanced guidance simulation.

The close-up and grazing view were inspected for deep thin walls, concave cell rims, bright lips over dark wells, visibly thick rounded ends and no needle tips or straight perimeter frame. The square-on view checks the short root, 32 degree shoulders, long sides and chamfered tip without oblique perspective. The top view checks the short-root/tapered outline and relationship to the staging ring. The distant view exercises the real tracking lens, deflection and coarse tier. Cell-scale curves are naturally below pixel resolution in that distant shot.

Reproduce from this worktree:

```sh
cd /unreal/allan/albert-codex/build/linux
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
LP_NUM_THREADS=2 /usr/bin/ctest -R visual --output-on-failure
cd /unreal/allan/albert-codex
ALBERT_GPU=software LP_NUM_THREADS=2 build/linux/tests/visual_test grid-fin-captures build/codex/grid-fin-v3/review
```

Build, ctest, capture, geometry metrics and the protected-source audit are retained beside the images; starting hashes are in the parent directory. The required build succeeds by its final lines; both visual tests pass without skipping. Captures report zero WebGPU validation errors. Protected definitions and physics match their starting SHA-256 hashes. No commit was made.

## Limits

The owner confirmed the existing **90°/90°/180° layout**: two opposite fins over the catch fittings and the third between them. That layout, the 7.5 m mounting station, pivots and radial hinge axes are retained.

This is a procedural interpretation of the supplied description; no owner photographs or manufacturing CAD were attached. Wall gauge, arc sag, sheet bow, outline proportions and root cover shape are visual estimates. Interior sheet joins intersect at crossings; boundary fillets and capped ends are visual approximations rather than manufacturing weld geometry. There is no added flight-history heat tint or soot shader, and no new actuator mechanism or physics. The low tier deliberately loses the concave lips and can obscure deep cells at oblique angles. LOD remains a discrete existing switch. Captures use controlled lighting and a simple background; no hardware-GPU benchmark or full live flight was run.
