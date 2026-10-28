# Starbase tank farm and launch support

The launch-site OSM tank/building centres, headings and body envelopes are retained.
The supplied Flight 11 photographs (ref-005/006/033) guide the long covered tank
bank and the exposed industrial fittings. The owner's description guides white
upright tanks, trestled propellant lines, equipment buildings and light poles.
The saved OSM layout is newer than those photographs; this is a visual
reconstruction on that layout, not a surveyed Flight 11 configuration.

## Geometry and finishes

`addStarbase()` sends compact `farm ...` scenery markers, expanded by
`industrialGeometry()` in the renderer. Marker coordinates are ground-level ENU;
their heading rotates the finished geometry once. These are ordinary drawn
scenery parts, never contact structures.

- Horizontal tanks retain their total OSM length and diameter. Short elliptical
  end caps meet cylindrical bodies, with concrete/steel saddles, retaining bands,
  elevated catwalks, guardrails, external end ladders, projecting ladder landings,
  end nozzles, valve handwheels and low transverse headers. LOX and methane are
  white and matte; nitrogen is bare steel. Water and unidentified horizontal
  tanks retain their original colour. Unidentified upright tanks use white
  insulation; identified nitrogen tanks remain steel.
- Upright tanks have shallow domed ends, concrete bases, shell bands, ladders,
  valve platforms and short access stairs. Vaporizers have twelve separate
  extrusions with crossed fin banks, inlet/outlet headers and valves. Frost
  accents are confined to cold inlet/valve regions and the lower vaporizer fins.
- One open-sided canopy covers the western long bank, centred at
  (-88.6, 116.2) m, heading 13.3 degrees, 98 by 54 m and approximately 10 m high.
  Shallow repeated roof pitches, ridge caps, standing seams, knee braces,
  purlins, edge beams and downpipes give its roof and open sides structure.
  Columns stand beyond the tank ends. The eastern methane bank stays exposed.
- Both existing farm-to-mount routes retain their four bundled pipe lanes,
  mitred corners and mount risers. Two-legged steel trestles now replace the
  single stanchions, with concrete feet, crossheads, bracing and pipe shoes.
  Valve manifolds replace the old solid manifold boxes. Four unlit industrial
  light poles stand near the farm edges.
- Support sheds have steel-grey panel walls, large roll-up openings, door jambs,
  roof seams, roof units, fan grilles, louvres and access ladders. Bunkers have
  matte concrete colours and horizontal construction joints. Control/security
  buildings use smaller doors and framed windows separated from their doors.
  Starhopper keeps its existing placeholder body rather than receiving a shed
  facade. The separate production campus is unchanged.

White/concrete surfaces and steel use distinct roughness/metalness. All geometry
uses the existing standard sun, shadow and atmospheric lighting with vertex
colours. No shader was changed and no `pow()` expression was introduced.

## Rendering and flight separation

The industrial geometry merges into three shadow-casting draws per site:
painted/concrete shells, exposed steel, and small fittings. Low quality hides the
fittings, keeping tank caps/saddles, buildings, canopy, trestles and fin banks in
two draws (322,384 triangles at high quality, 139,572 at low). Existing bundled pipe cylinders remain in their existing colour batch;
shadow passes are additional. There is no per-tank or per-building draw.

The request has 1016 scenery parts out of the 2048 limit and the same nine
structures. No new `columns`, `arms` or `works` are added. Source SHA-256 checks
confirm the entire region following `addStarbase()` is unchanged, including
`siteWorks()`, `addTower()` and the flight routines. The five protected guidance
headers and every `src/physics/` file are unchanged. No commit was made.

## Files

| File | Change |
| --- | --- |
| `examples/starship_flight.hpp` | OSM industrial markers, canopy, light poles and replacement trestle/manifold markers; only inside `addStarbase()`. |
| `src/render/industrial.hpp` | Drawn industrial geometry builder declaration. |
| `src/render/industrial.cpp` | Tanks, caps, saddles, access/fittings, fin banks, canopy, trestles, equipment buildings and finishes. |
| `src/render/scenery.cpp` | Expands markers in their ENU frames, merges three industrial meshes and switches fittings with quality. |
| `src/render/scenery.hpp` | Owns the optional industrial fittings mesh. |
| `src/render/CMakeLists.txt` | Builds the industrial geometry implementation. |
| `tests/industrial_visuals.hpp` | OSM completeness/finish/contact checks; real WebGPU visibility, merged draw count, finite vertices, low quality and unlit checks. |
| `tests/production_visuals.hpp` | Replaces the obsolete exact pad scenery count with budget/contact checks. |
| `tests/land_visuals.hpp` | Seven reproducible full-site industrial captures, camera positions and triangle counts. |
| `tests/visual_test.cpp` | Runs industrial tests and exposes `tank-farm-captures`. |
| `docs/STARBASE_TANK_FARM.md` | This implementation and its verification/limits. |

## Verification and captures

From `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The build tail completes successfully. Both visual tests pass, including software
WebGPU. The industrial graphics fixture checks three merged draws, retained low
quality silhouettes, removal/restoration of fittings, visible lit geometry and
darkness without illumination. Source tests preserve all OSM tanks/buildings,
propellant colours, scenery limits and the nine contact structures.

From the repository root:

```sh
ALBERT_GPU=software build/linux/tests/visual_test tank-farm-captures build/codex/tank-farm
```

The capture fixture uses the actual `addTower()`/`addStarbase()` request, roads,
land cover, stack, standard lighting, sun shadows and atmospheric renderer. It
uses a flat ENU ground fixture and saved coarse Earth imagery. The sun is angled
from the southwest to expose facade detail, and from the northeast for the
vaporizer close-up. The adapter is llvmpipe (LLVM 21.1.8,
256 bits), with zero WebGPU validation errors. PPM outputs are converted losslessly
to PNG for review.

| Capture under `build/codex/tank-farm/` | View |
| --- | --- |
| `tank-farm-tower-top.png` | From the Tower 1 crown height, at (-22, 5, 146) m, toward the long farm. |
| `tank-farm-300m.png` | Both pads, covered and exposed rows, support buildings and trestled routes; camera altitude 300 m. |
| `tank-farm-300m-low.png` | Identical camera at low quality. |
| `tank-farm-eye.png` | 1.7 m eye height along the exposed tank ends and manifold area. |
| `tank-farm-canopy-eye.png` | 1.7 m eye height toward the open canopy, capped tanks, ladders and catwalks. |
| `tank-farm-gse-eye.png` | 1.7 m eye height toward GSE doors, panel seams and neighbouring bunkers. |
| `tank-farm-vaporizers.png` | Upright tanks, fin banks and frost beside the canopy. |

`build.log`, `ctest.log`, `captures.log`, `parts.tsv`, `before.json` and
`invariants.txt` retain the local verification evidence. The captures log records
actual merged triangle counts. All capture artifacts are ignored build outputs.

## Limitations

Canopy dimensions, facade openings, roof equipment, ladders, valve layouts, light
poles and frost are plausible photographic approximations. OSM supplies centres
and envelopes rather than fabrication drawings. Pipe routes retain the existing
reconstruction; the small headers are scenery, not a complete process piping
diagram. There are no interiors, operating valves, dynamic frost or new lights.
Starhopper remains an unrelated box placeholder.

All industrial feet share the existing site ground plane. No DEM sampling,
terrain excavation/grading or new road crossing is introduced. These captures
verify the real software renderer on a flat site, not foundation contact over the
live DEM. Low quality removes small fittings but retains the cylindrical shells
and detailed vaporizer fins. Industrial geometry has no distance LOD. Hardware timing and temporal aliasing
under a moving camera were not measured. Fine metal features can still shimmer
at long distances; shadowed steel depends on the scene's sky/sun illumination.
