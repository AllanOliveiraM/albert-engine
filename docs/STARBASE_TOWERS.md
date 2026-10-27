# Starbase launch towers

The tower scenery uses merged render meshes for the lattice, crown, carriage, tapered catch arms and QDs. Both towers share the geometry builders. Only `addStarbase()` changes in the flight example; the complete region following it, including `siteWorks()`, `addTower()` and the flight routines, is unchanged. The four protected guidance headers are unchanged. No commits were made.

## Construction and references

| Part | Drawn construction and comparison |
| --- | --- |
| Lattice | Four 72 cm corner columns on 8.78 m centres, retaining the original 9.5 m outside width. Sixteen roughly square sections have horizontal girts and two crossing diagonals on each of the four faces. The diagonals are 28 cm thick; their crossings are separated by 4 cm. Small splice collars and connection plates add sunlit edges. This follows the open, square tower in ref-001/002 and the owner's photographs. |
| Lower cladding | Grey steel covers the original lower third. Vertical standing seams are 1.35 m apart, with horizontal panel joints every 3 m. The existing plinth, service building, lift enclosure, landings, guide rails, service pipes and cable trays remain. |
| Crown | Open framing surrounds the existing drawworks and drum. A 20 m open-truss crane boom, counterweight, derrick, stays, hanging hoist block and slender lightning mast replace the solid cap. The crane and mast strengthen the silhouette seen in ref-001/002. Three small steady red emissive beacons per tower remain visible without sunlight. |
| Chopsticks | Four longitudinal chords, vertical members and alternating side diagonals form black open beams. Across eight bays, the chord spacing decreases to 42% across the width and 35% through the depth, while the top remains level. Top/bottom cross-bracing, narrow grated walkways, inner rail beds, rubber shock pads and the existing end bumpers and actuators complete the arms. This reproduces the tapered, open silhouette in ref-002/007. Tower 1's four contact rails are still rendered directly from its original physics bars. Tower 2 gets drawn-only rails. |
| Carriage | Open horizontal frames, corner posts, side diagonals, guide shoes, roller blocks and a front crosshead wrap around the tower instead of a solid box. Tower 1's arm roots remain at the original hinge coordinates. |
| Ship QD | A dark open truss replaces the closed girder and its redundant cladding stiffeners. The original hinge, diagonal support, head, plate and hose routes remain; the long hoses are darker. Tower 2 uses the same truss/head/hoses builder. |
| Booster QDs | Open dark truss supports replace the solid LOX/methane hoods, retaining a connector shield, the existing umbilicals, sagging flex runs, sleeves, bindings and valve cabinets. Existing hose markers are reused. |

The galvanized lattice uses the existing sun/sky/shadow material with metallic shading; the arms are dark painted steel. Only the small beacons emit light. The daytime and low-sun captures show the change from lit grey members to a dark silhouette. The top-down perspective captures correspond to ref-007/032/034; the steam view uses the existing deluge renderer.

## Attachments and cost

The arm mesh and its fittings carry the same scenery structure IDs, 1 and 3. The ship QD remains on 8, and the booster QDs remain on 9. Every new geometry piece is transformed into site coordinates and then into its carrier's original pivot frame before merging. `Site::animate()` is unchanged. Collision bars, radii, contact properties, start angles, hinge axes/coordinates, motion limits, rates and guidance obstacles are unchanged.

The new steel is vertex-coloured and merged by carrier: one main mesh and one optional fitting mesh for each of the fixed site, south arm, north arm, ship QD and booster QD. The two fixed towers share their merged meshes. All beacons share one material and mesh. Existing scenery continues to merge by colour and carrier. There is no draw per diagonal, joint, grating bar or pad.

Low quality hides splice/connection plates, panel seams, crane stays and hook fittings, walkway grating and rail shock pads, as well as the existing small pad furnishings. It retains the four-face X lattice, cladding shell, crown/crane/mast, carriage, tapered chords and side diagonals, rails and QDs. Switching quality restores the same meshes without rebuilding them. The full request has 1,259 scenery parts, below the 2,048 limit and down from 1,671 before this work.

The capture fixture counts 45 nonempty visible site meshes and 132,944 triangles at high quality, versus 39 meshes and 105,540 triangles at low quality. These totals include the existing pads, tanks, buildings and furnishings. The new marker geometry contributes 11,136 main steel triangles and 17,496 optional fitting triangles, plus 1,152 beacon triangles. Shadow passes add their normal rendering cost; these counts are not hardware GPU timings.

## Changed files

| File | Change |
| --- | --- |
| `examples/starship_flight.hpp` | Emits merged-geometry markers for both towers, preserves attachment IDs, shares the ship QD assembly, reuses hoses, and supplies aviation beacons and Tower 2's visual rails. All edits are inside `addStarbase()`. |
| `src/render/scenery.cpp` | Builds the tower steel and optional fittings, merges each under its existing carrier, supplies metallic vertex-colour materials and emissive beacons, and switches optional fittings with quality. |
| `src/render/scenery.hpp` | Owns the optional merged tower meshes. |
| `tests/visual_test.cpp` | Exercises tower rendering, both catch attachment IDs, the ship QD, horizontal booster QD folding, low/high quality restoration, beacon emission and unlit steel darkness. |
| `docs/STARBASE_TOWERS.md` | Construction, reference comparison, verification, capture paths and limitations. |

## Verification and captures

Built from `/unreal/allan/albert-codex/build/linux` using exactly:

```
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The build tail links all three targets successfully. Both visual tests pass; `visual_webgpu` runs on software WebGPU rather than skipping. Source comparison verifies the protected headers and the entire post-`addStarbase()` region. The capture fixture also compiles the original contact builder from HEAD and compares all nine serialized structures in both mounted and stowed requests byte-for-byte.

All capture artifacts are in `/unreal/allan/albert-codex/build/codex/tower-check/`:

| Capture | View/check |
| --- | --- |
| `tower1-day.png` | Complete Tower 1, stack, lower cladding and open lattice. |
| `tower1-lattice.png` | Two visible faces, X diagonals, girts, splice plates and service landings. |
| `tower1-crown.png`, `tower2-crown.png` | Crane, mast, stays, hoist and aviation beacons. |
| `tower1-chopsticks-open.png`, `tower1-chopsticks-closed.png` | Original swinging pivots, tapered chords, side diagonals, rails and pads; ref-002/007. |
| `tower1-chopsticks-low.png` | Same open-arm framing at low quality. |
| `tower1-ship-qd-connected.png`, `tower1-ship-qd-stowed.png` | Ship QD truss and hoses in both original hinge poses. |
| `tower1-booster-qd-connected.png`, `tower1-booster-qd-folded.png` | Booster truss supports and existing plumbing folding about the original horizontal hinge. |
| `tower1-from-crown.png`, `tower1-from-crown-steam.png` | Perspective looking toward the ship and arms from the tower's mount-facing edge; ref-007/032/034. |
| `tower2-day.png`, `tower2-chopsticks.png` | The same detail on Tower 2 at its OSM location. |
| `tower1-sunset.png`, `tower1-silhouette.png` | Low-sun tower/stack and close perspective silhouette; ref-001/002. |
| `tower1-night-beacons.png` | Only the small red lamps remain visible without illumination. |

`preview.cpp`, `build-preview.py` and `preview.log` reproduce and record the captures using this worktree's libraries and the actual `addTower(request,true)` scenery. The preview logs mesh/triangle counts, the software adapter and validation errors. `structures.txt` records the unchanged contact request. `invariants.txt`, `build.log` and `ctest.log` record the source audit and required checks. These are local ignored artifacts.

## Limitations

Dimensions and fittings are visual approximations from the supplied frames and descriptions, not surveyed hardware. The existing V3 pads and vehicle remain; these differ from Flight 11's older installation. The ship QD keeps its existing 90 m hinge height below the 100 m catch rails: changing that relationship to match a photograph would violate the protected structure positions. Tower 2 remains static scenery, with no new catch physics. Its shared crown and QD details approximate the first tower rather than a documented tower-specific equipment survey.

The previews are 1280×960 real llvmpipe WebGPU renders with shadows, on the existing flat ENU preview fixture. They use coarse saved ground imagery and solid-colour sky backgrounds. They do not reproduce the live DEM, atmospheric sunset, ocean horizon or the fisheye lens distortion in ref-007. The steam view holds the stack on its mount while exercising the existing deluge; it is not a simulated liftoff. The lights are steady, without a flash schedule or bloom. Hardware GPU timing, distant temporal stability and full flight scenarios were not measured; contact preservation is checked by source and serialized-request comparisons.
