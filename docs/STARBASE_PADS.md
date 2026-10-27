# Starbase launch pads

The two pads retain their original ENU origins, apron footprints, mount deck/ring/clamp positions, plumbing, tower lattice, catch arms and contact structures. Only drawn scenery changes. All edits in `examples/starship_flight.hpp` are inside `addStarbase()`; `addTower()`, `siteWorks()` (including `columns`, `arms`, `works`) and the flight routines below it are byte-for-byte unchanged. The four protected guidance headers are also byte-for-byte unchanged. No commits were made.

## Surfaces and construction

The apron slabs now use `concreteSurface()` from the road shader, with 5.5 m saw-cut joints, broad discoloration, darker damp patches and distance-filtered grain. The same function shades solid concrete rather than introducing another apron texture. Its integrated periodic pulse filters joints below a pixel. Mount-centred radial scorch and widening deposits at both trench mouths are much stronger on the first pad than on the second; these are artistic launch-history cues, not measured burn maps. Coordinates are site-local metres, so a floating origin or the site's Earth-radius scale does not change the pattern.

Vertical concrete adds 30 cm board-form lines and long runoff streaks. Concrete uses the existing standard sun/sky, atmosphere and shadow lighting, zero metalness and matte roughness. Legs, plinths, T-walls, trench floors/walls/terraces, floodlight footings and tank saddles share one merged concrete mesh per site. The existing steel trench liners remain steel. Both concrete slopes run 42 m from the centre toward each trench outlet, falling 4.4 m, above the unchanged floor; terrain is not excavated.

Each mount has four drawn concrete piers centred at local across ±9.75 m, along ±8 m. Their 5.8 m square feet taper to 58% at the top; the piers are 9.5 m tall, from z=0.55 to 10.05 m under the original deck. Eight-sided rings bevel the vertical corners by 28 cm and the top/bottom edges by 12 cm. These are render meshes, never physics bars. The steel deck, opening ring, clamps and QDs retain their original positions and attachment indices.

Each tower gets a 13.8 m square concrete plinth and grey steel skin on its lower third, with narrow standing seams. The upper lattice and diagonal bracing remain exposed. Tower 1 gets a small two-storey service building west of the foot, with windows and a metal roof. Tower 2 uses its existing OSM service building at (-357,109.2) rather than inserting an overlapping building.

## Furnishings and placement

Positions below are metres across/along each pad's existing trench axes: Pad 1 at (0,0), bearing 33°; Pad 2 at (-353.2,70.7), bearing 123.3°. This preserves the existing reconstruction, including its difference from the older Flight 11 photographs.

- T-walls follow the outer apron edges, across -67/+77 m and along -147 m, with gaps for access and both exhaust exits. There are 48 sections after rejecting road conflicts. They have real horizontal feet and thin vertical stems, not flat barrier boxes.
- Two parking rows per pad use across -53/-42 m, along -118 to -88 m, facing across the apron. The final site has 16 pickups and three larger trucks; a fourth truck candidate conflicted with Pad 2's access road and was rejected.
- Eighteen cones mark the parking boundary at across -34 m, along -126 to -86 m. Each has a dark foot, tapered orange body and white band.
- Six equipment skids and six open ladder cable trays sit toward the farm side, at across -42 to -24 m, along 47/36 m. Road-conflicting candidates at Pad 2 are rejected. The existing pipe racks, manifolds, covered cable trenches and eight floodlight poles are reused.
- Seven cars follow the saved Highway 4 tangents, their centres 1.7 m beyond the full paved edge. They avoid service junctions, OSM structures and other furnishings. Full car corners, including tyre width, clear the pavement by at least 0.59 m. These cars stand on the flat ground/shoulder surface, outside the traffic lanes.

A conservative circumscribed footprint check keeps new walls and apron furnishings away from every saved road. The final capture audit checks rotated corners against apron footprints and road edges: zero placement conflicts. It includes the physical tyre width and the T-wall feet. It does not claim a surveyed parking layout.

`detail pickup`, `detail truck`, `detail car`, `detail cone`, `detail skid` and `detail cable tray` are drawn-only scenery markers expanded by `Site`. All their coloured pieces merge into one vertex-coloured, shadow-casting mesh, one draw per site (plus its shadow pass), regardless of vehicle count. `mount leg` and `perimeter T wall` similarly select their drawn concrete geometry. The full request has 1671 scenery parts, below the API limit of 2048, and the same nine contact structures. Low quality hides the merged furnishings and drops concrete grain; the aprons, joints, weathering, scorch, piers, trench, walls and towers remain.

## Changed files

| File | Change |
| --- | --- |
| `examples/starship_flight.hpp` | Drawn pad piers/ramps, tower plinths/cladding/building, perimeter walls, apron furnishings and highway parking with road clearance. |
| `src/render/roads.hpp` | Exposes the shared procedural concrete WGSL. |
| `src/render/roads.cpp` | Extracts the existing noise/joint helpers and concrete albedo for reuse; adds damp patches and uses 5.5 m joints. |
| `src/render/scenery.hpp` | Owns the concrete material and optional merged furnishing mesh; accepts quality updates. |
| `src/render/scenery.cpp` | Builds chamfered tapered piers, T-walls and low-poly furnishings; merges concrete and vertex-coloured details. |
| `src/render/webgpu_programs.cpp` | Concrete and vertex-colour variants of the existing assisted standard material; same sun/sky/shadow path. |
| `src/render/objects.hpp` | Forwards quality to existing site renderers. |
| `src/render/application.cpp` | Applies the current quality to sites, including newly received flight scenery. |
| `tests/visual_test.cpp` | Exercises both pad shaders, stronger flown-pad scorch, low/high quality switching, retained low-quality aprons, unlit darkness and a ground-level pier view. |
| `docs/STARBASE_ROADS.md` | Updates joint spacing and links this shared concrete implementation. |
| `docs/STARBASE_PADS.md` | Placement, construction, verification, captures and limits. |

## Verification and captures

Built only in `/unreal/allan/albert-codex/build/linux` with:

```
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The last build lines link successfully; both visual tests pass, including the actual software WebGPU test (not skipped). The full-site offscreen preview uses `addTower(request,true)`, the actual scenery/road request, the existing vehicle appearance, one llvmpipe WebGPU context, and sun shadows. Ten 1280×960 captures complete with zero validation errors. It uses an ENU plane and the saved coarse Earth imagery, not the live application's DEM or atmosphere; the sunset view uses a warm background and low sun to inspect silhouettes, not a simulated sunset sky.

All local capture artifacts are under `build/codex/pad-check/`:

| Capture | Check against supplied references |
| --- | --- |
| `starbase-pads-plan.png` | ref-005/006: two-pad relationship, highway curve, tank rows, unobstructed lanes/outlets and apron boundaries. |
| `starbase-pads-oblique.png` | ref-006: site scale, towers, surrounding GSE and pad silhouettes. |
| `pad1-mount-concrete.png`, `pad1-empty-mount.png` | ref-033 and owner description: concrete supports, original steel deck/ring, ramp and service equipment; empty view exposes the ring. |
| `pad2-mount-concrete.png` | Same construction on Pad 2, with lighter scorch. |
| `pad1-piers-low.png` | Owner description: tapered/bevelled legs, board-form concrete and the open trench below the deck. |
| `pad1-tower.png` | Lower cladding, plinth/building and exposed upper bracing. |
| `pad1-apron-detail.png`, `pad1-apron-low.png` | Joint grid/weathering, pickup/cone scale and the quality switch in identical framing. |
| `pad1-sunset.png` | ref-001: tower/stack silhouette with low sun. |

`preview.cpp` and `build-preview.py` reproduce the captures using this worktree's built libraries; `preview.log` records the counts, adapter and zero errors. `parts.tsv`, `audit.py` and `placement.txt` record the placement audit. `invariants.txt` records protected-file hashes and the unchanged flight/structure region. `build.log` and `ctest.log` record the final build/test results. These are local ignored artifacts.

## Limits

The photos show Flight 11's older launch arrangement; the requested V3 concrete mount/trench is reconstructed from the owner's description on the existing pad coordinates. Dimensions, stains, parking, wall gaps and service-building details are plausible approximations, not a current site survey. Existing apron extents and the tower-origin offset documented in `STARBASE_ROADS.md` remain. Existing tank rows/GSE and the coarse tidal-flat imagery are not rebuilt. The shader darkens wet-looking patches but does not simulate puddle reflections or standing water.

The captures validate real renderer output and placement on a flat site. Live DEM contact, hardware GPU cost, distant/grazing temporal stability and the live atmospheric sunset still need the planned application captures. There is no new terrain grading or excavation, and highway cars are not individually height-sampled from the DEM. Flights/contact geometry were preserved by construction and source comparison; full flight scenarios were not run for this drawn-only change.
