# Starbase production site

The Boca Chica production campus is drawn from the supplied OpenStreetMap extract,
in the same WGS84 east/north frame as the pads: 25.996128 N, 97.154517 W. No
coordinates are hand shifted. The main Starfactory is approximately 3.3 km west
and 0.94 km south of that origin. The factory, village, high bays, rocket garden,
six storage tanks and roads now travel with the existing Starbase scenery request.

## Source and geometry

`assets/geography/starbase-build-site.json` retains the supplied Overpass JSON
byte-for-byte, including its OSM timestamp, tags and geometry. Source SHA-256:
`0de1c14ef8a91e9a725db1ebc944041d43201fe90b1a351348c121abf4db3671`.
© OpenStreetMap contributors, [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/),
[attribution](https://www.openstreetmap.org/copyright).

The offline sibling of the pad converter, `scripts/starbase_production.py`, uses
the shared `scripts/osm_enu.py` projection. Reproduce the generated header with:

```sh
python3 scripts/starbase_production.py > examples/starbase_production.hpp
```

It requires only Python's standard library. Regeneration compares byte-for-byte.
The extract has 451 building features, 235 road ways and six storage tanks. The
Starfactory relation has two disjoint outer rings, so the generated scenery has
458 building/tank records, 459 rings and 3047 footprint vertices. Multipolygon
fragments are joined, courtyard holes retained, and coordinates rounded to the
nearest millimetre. Roofs use polygon triangulation with holes; walls follow every
ring edge. The L-shaped halls and the Hikaru courtyard retain their actual outline
at both detail levels. Bounding frames only orient architectural fittings.

Tagged heights include Mega Bay and Mega Bay 2 at 100 m, Gigabay at 116 m,
Starfactory at 16.3 m, Raptor Nest at 15.8 m, S20 at 50.3 m and B12 at 69 m.
Untagged industrial sheds default to 10 m; offices/clinic/city hall to 8 m;
houses/caravans to 3.5 m; small unclassified outbuildings to 4 m. Tagged storeys
override defaults at 3.3 m per storey. The noted six-storey parking structure uses
20 m. Tank heights and the HLS nosecone's height are inferred where untagged.

The Starfactory has dark glass, a light panel roof, mullions and a white X on a real
east-facing perimeter segment of its main hall. The bays have grey cladding,
horizontal bands, broad dark doors and jambs. Rectangular village homes have
simple pitched roofs at high detail; caravans and complex footprints retain flat
roofs. S20 and the HLS display have stainless bodies/nosecones; S20 adds a dark
heat-shield half and projecting fins. B12 has its booster cylinder and shallow
crown, rather than a ship's pointed nose. All six source tanks are included.

The merged road set retains 274 distinct OSM ways, 2198 points and 1924 segments;
five build-site ways already occur in the pad extract and are sent once. Boca Chica
Boulevard continues west along the supplied geometry. The existing terrain road
material paints asphalt, concrete and tracks, with the same highway lines and
land cover underneath. Roads add no draw calls. The enlarged index has at most
39 candidates in a tile; lists remain untruncated. Opaque buildings hide any road
under their footprints; courtyard openings remain visible.

## Rendering and flight separation

The campus adds three merged draws at high detail (shells/roofs, glass/stainless,
fittings), or two at low/distant detail. The complete high mesh has 50,507
triangles; the distant mesh has 10,943. Shadow passes are additional. Low detail
retains the polygon outlines, factory glass/X, large doors, bay silhouettes, tanks
and prototypes, while removing small windows, mullions, gables and fittings and
reducing cylinder segments from 32 to 12. Both LOD meshes are prepared once.

Distance is evaluated in site-local metres after the node's world transform,
including its Earth-radius scale and floating origin. High detail switches off
above 2200 m from the campus centroid and returns below 1800 m. Low quality
forces the distant mesh. The meshes use the pads' assisted standard lighting,
shadows and atmospheric perspective. A vertex-colour variant adds filtered panel
seams and cladding bands, using the road shader's integrated periodic pulse. It
introduces no new `pow()` expressions.

`SceneryBuilding` is a separate drawn-only request/list, never passed to contact
generation. It is bounded to 1024 buildings and 16384 ring vertices. The road cap
increases to 512 ways; its 4096-point limit remains. Child flights clear the new
list along with the existing scenery/roads. The original pad fixture still has
1259 scenery parts and nine structures. No new columns, arms or works are added.
The protected guidance files and every file under `src/physics/` retain their
initial hashes. `addTower()` and all subsequent flight code retain their initial
hash too. No commits were made.

## Files

| File | Change |
| --- | --- |
| `assets/geography/starbase-build-site.json` | Unmodified supplied OSM source. |
| `assets/geography/README.md` | Source attribution, licence and offline reproduction. |
| `scripts/starbase_production.py` | Ring assembly, height/style defaults and road conversion. |
| `examples/starbase_production.hpp` | Generated footprints, heights, landmark styles and roads. |
| `examples/starship_flight.hpp` | Adds buildings and deduplicated roads inside `addStarbase()`. |
| `proto/albert/v1/engine.proto` | Drawn polygon/ring messages and bounded road-count extension. |
| `src/core/scene.hpp` | Internal drawn building/ring data. |
| `src/rpc/flight.cpp` | Validates/transfers buildings, includes their bounds, clears them on child flights. |
| `src/render/production.hpp` | Merged campus renderer and LOD state. |
| `src/render/production.cpp` | Polygon roofs/walls, landmarks, materials and two detail levels. |
| `src/render/webgpu_programs.cpp` | Standard-lit vertex-colour campus material with filtered panel seams. |
| `src/render/scenery.hpp` | Owns the optional production renderer and exposes camera updates. |
| `src/render/scenery.cpp` | Attaches the campus node and forwards quality. |
| `src/render/objects.cpp` | Updates production distance LOD with the active camera. |
| `src/render/CMakeLists.txt` | Builds `production.cpp`. |
| `tests/production_visuals.hpp` | Source/request checks; concave roof, courtyard, LOD/scale and unlit pixel checks. |
| `tests/land_visuals.hpp` | Full-campus software WebGPU capture views and mesh statistics. |
| `tests/visual_test.cpp` | Runs production checks and exposes `production-captures`. |
| `tests/rpc_test.cpp` | Polygon-only site transfer and malformed footprint rejection checks. |
| `docs/STARBASE_PRODUCTION.md` | This implementation, verification and limitations. |

## Verification and captures

Build in `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The build tail links successfully. Both visual tests pass, including the actual
software WebGPU test. New pixel checks cover an L-shaped roof and a courtyard at
both quality settings, distance LOD under a scaled/translated site, and darkness
without illumination. Source checks cover landmark heights, factory outer rings,
courtyards, request budgets and the unchanged pad/contact counts.

Generate the captures from the repository root:

```sh
ALBERT_GPU=software build/linux/tests/visual_test production-captures build/codex/production-check
```

The capture fixture uses the real `addTower()`/`addStarbase()` request, terrain road
and land-cover material, atmosphere/ocean, sun/shadows and stack. It runs on
llvmpipe and reports zero WebGPU validation errors. Output is PPM; the adjacent
PNG files are lossless format conversions for review.

| Capture under `build/codex/production-check/` | View |
| --- | --- |
| `production-pad-west.png` | Production skyline from the pads, 20 m above local ground. |
| `production-ascent-1600m.png` | Pads, campus and roads from 1600 m. |
| `production-starfactory-close.png` | Factory glass/X and high bays from the east/southeast. |
| `production-starfactory-low.png` | Same camera at low quality. |
| `production-plan-1800m.png` | Campus and village overhead at 1800 m. |
| `production-rocket-garden.png` | S20/B12 garden displays. |
| `production-pad-west-sunset.png` | The distant production skyline under a low western sun. |

`build.log`, `ctest.log`, `captures.log` and `invariants.txt` retain build/test,
adapter/count and preservation evidence. The optional RPC test target also builds;
its runtime check is blocked here because the sandbox forbids binding the local
gRPC socket (`Operation not permitted`). This does not affect the offscreen visual
tests, which instantiate the actual request and renderer without a socket.

## Limitations

OSM supplies footprints/heights, not facade elevations. Door layouts, cladding,
glass, windows, X dimensions, roofs, display stands and untagged heights are
plausible scenery, not a survey. Gigabay is tagged under construction in the
source and rendered with a complete clad shell for its requested skyline. The
Port of Brownsville and its cranes lie outside this extract and are not added.
The supplied photos show an earlier Flight 11 arrangement; the saved OSM layout
is retained rather than forcing it to match those photographs.

Buildings share the pad's local ground plane; there is no individual DEM height
sampling, foundation grading, interiors or collision. These captures are the real
software WebGPU renderer over a flat ENU terrain fixture with coarse saved imagery
and the production land-cover/road shaders. They do not verify foundation contact
over the live application's DEM. Hardware GPU timing, live DEM contact and
temporal behavior under a moving application camera remain unmeasured. The
existing road layer still selects the first visible road-bearing site.
