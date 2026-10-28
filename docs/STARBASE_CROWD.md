# Starbase launch-day crowd

Starbase now has a render-only launch-day layer, generated from its saved roads and
land-cover mask. The fixture produces 903 parked vehicles (299 pickups, 295 SUVs,
222 sedans, 68 vans including four press vans, and 19 RVs), 12 press tripods, and
2,767 spectators (872 on the public beach/dunes). Parking spans 3.397 km east/west. The existing pad worker vehicles
remain; the seven static highway spectator cars have been removed.

The layer is enabled only for a flight with an armed launch count, from T−4 hours
through T+20 minutes. A HOLD uses its frozen count time. Scrub, an ordinary flight
without a launch clock, removal, or leaving that window disables it. Rewind and
quality changes are reversible. Nothing is added to physics, contact structures,
the RPC scenery-part budget, vehicle guidance or sound.

## Placement and appearance

Both shoulders of Highway 4 follow the saved polylines, with 8.2–12 m candidate
spacing, opposing headings and variable gaps. The asphalt village streets use the
same rule within the village envelope; private service roads are excluded. Vehicle
envelopes reject pavement conflicts on every road, intersections, buildings,
other vehicles, water, salt marsh, tidal flats and the near-water beach margin.
This intentionally leaves gaps around junctions, wet areas and buildings. An
independent nearest-road audit finds 732 highway vehicles (422/310 on opposing
shoulders) and 171 on village streets/pull-offs. The minimum full-envelope
pavement clearance is 0.475 m; the closest new instance centre is 2,003.62 m from
either pad.

An artistic 2 km exclusion around **both** pad centres keeps all new cars, people
and press equipment outside the launch complex and its fence. The beach east of
the pads is empty. Public beach/dune groups occupy dry sand and scrub 2.1–4.2 km
south, using the bundled shoreline distance field. Roadside groups stand outside
the parked cars. Four press vans and twelve tripods form a small western pull-off
outside the exclusion; the generated coordinates are in `instances.tsv`.

Vehicles use coloured low-poly hulls, sloping cabins, glass, roofs, wheel cards and
front trim. Pickups have short cabs and dark beds; SUVs, sedans, vans and RVs have
different proportions. People have varied shirts, skin tones, heights and hats,
with simple solid heads/torsos and flat limbs. Tripods have three splayed legs,
a camera body and lens. These are deliberately simple background scenery.

The live application samples the existing drawn Earth terrain for height and
slope. Cars use three ground samples and a 6 cm contact allowance; after the first
pass, 64 vehicle instances refresh per view. Nearby people are sampled each view.
Missing terrain falls back to the site plane, then follows arriving relief.
The tracking camera now stands on dry beach at nominal ENU (500, −2500, 1.85) m;
the live application raises it to at least 1.85 m over drawn terrain. Its former
(2600, −2100) mount was offshore against the saved coastline.

## Near LOD and cost

At the detailed/medium setting, the original far cars/press and spectators retain
their two draws and original geometry/shader. Within 135 m, selected instances
move into two additional instanced draws; they remain detailed through 150 m.
This hysteresis avoids repeated switching at the boundary. The total is **at most
four crowd draws**. Each instance belongs to exactly one LOD. Near selection is
capped at 48 cars and 128 people; overflow remains in the far level.

Near cars have rounded/bevelled bodywork, sloping tinted cabins, painted pillars,
roofs, mirrors, cylindrical tyres with rounded shoulders and alloy hubs, headlights,
tail lamps and number plates. Pickup, SUV, sedan, van and RV proportions differ.
Pickups selected for seated viewers use an open tailgate. Near people have oval
heads, tapered torsos, rounded hips, jointed arms/legs and shoes. Three shared poses
cover relaxed standing, holding a phone up to film, and sitting with legs hanging
from an actual pickup tailgate. Height, skin/hair and clothing vary by instance.
The sitters are a few near-only occupants attached to their car's terrain frame;
the original 2,767 roadside/beach spectator placements remain unchanged.

The near meshes use **1,112 triangles/car** and **572 triangles/person** (maximum
padded template counts). Far instances retain 36 and 34 triangles respectively.
The conservative combined bound is 186,732 triangles with all near slots occupied;
normal views use fewer. Shared exact float mesh-data atlases hold six vehicle
variants and three human poses in **1,260 KiB (1.23 MiB)**. These are geometry data,
not downloaded texture artwork. They are built/uploaded lazily only on the first
near view. Near instance buffers add **9,152 bytes (8.94 KiB)** to the far layer's
89,180 bytes (87.1 KiB). There are no new shadow-map draws, local lights, collision
objects, simulation bodies or network updates.

Far people retain their stable screen-door fade from 550 to 800 m, then leave the
draw. The whole layer stops drawing beyond approximately 16 km. Low quality and
inactive launch phases submit **zero crowd draws**, with no crowd height sampling
or instance updates. Starting at low quality does not build either LOD. Switching
down retains the small resources for reuse. The embedded land mask is decoded
once during placement and released.

`build/codex/crowd-near/captures.log` records paired identical crowd-on/off views,
nine synchronized software frames each, omitting the first for timing. The camera
and existing scene/shadow passes stay fixed. Hardware GPU cost remains unmeasured;
software timings are fixture measurements, not a hardware frame budget.
The final near-LOD run measures 712.04 ms versus 588.31 ms for synchronized full
frames, or 709.46 ms versus 584.38 ms in GPU timestamps: **125.07 ms total crowd
overhead on llvmpipe**. The preceding far-only implementation measured 64.30 ms
under the same fixture, so near detail adds approximately 60.8 ms on this software
adapter; this cross-run difference is indicative. Both current on/off samples use
55 existing shadow draws; the crowd adds four scene draws.

The recorded among-cars budget uses 37 detailed cars and 79 detailed people in
the paired cost view. `comparison.txt` verifies zero changed pixels for both
`tower-highway.png` and `beach-500m.png` against the reviewed far-level images;
their near meshes are absent. `comparison.png` puts the requested three images
beside their original versions. The additional `tailgate-poses.png` verifies the
seated pose on an actual parked pickup.


## Files

| File | Change |
| --- | --- |
| `examples/starship_flight.hpp` | Remove seven static spectator-car placements inside `addStarbase()` only. |
| `src/gpu/programs.hpp` | Fixed procedural vertex count for the existing instance-buffer path; point quads retain their default six vertices. |
| `src/gpu/scene.cpp` | Draw that vertex count per instance. |
| `src/render/launch_crowd.hpp` | Render-only crowd state, instance layout, launch window and ground callback. |
| `src/render/launch_crowd.cpp` | Seeded placement, grounding, near/far partition and hysteresis, seated pickup attachments, instance budgets and resource cleanup. |
| `src/render/launch_crowd_detail.hpp` | Shared near mesh atlases and vertex counts. |
| `src/render/launch_crowd_detail.cpp` | Rounded vehicle and human pose meshes; exact mesh atlas packing and instanced standard-lighting shader. |
| `src/render/launch_crowd_shader.hpp` | Procedural instanced vehicle, tripod and figure geometry; sun/sky/atmosphere lighting and distance fade. |
| `src/render/scenery.hpp` | Site owns the optional crowd and forwards phase/view updates. |
| `src/render/scenery.cpp` | Attach only to Starbase scenery and apply quality. |
| `src/render/objects.hpp` | Carry launch-clock status and the drawn-terrain source. |
| `src/render/objects.cpp` | Feed phase gates, ground-height callback and initial quality to sites. |
| `src/render/application.cpp` | Connect launch-clock arming and terrain; keep the public tracking camera above ground. |
| `src/render/broadcast_camera.cpp` | Move tracking and its fallback onto dry public beach. |
| `src/render/CMakeLists.txt` | Compile the crowd renderer and near mesh generator. |
| `tests/crowd_visuals.hpp` | Placement, type/budget, exclusion, beach, slope, phase, removal, rewind, quality, near/far partition, 135/150 m hysteresis, lazy allocation and software pixel/draw checks. |
| `tests/land_visuals.hpp` | Add reproducible crowd capture/paired-cost fixture using real site, roads, atmosphere, stack and tracking rig. |
| `tests/visual_test.cpp` | Register checks and `crowd-captures`. |
| `docs/STARBASE_PADS.md` | Point former static highway parking to this layer. |
| `docs/BROADCAST_CAMERAS.md` | Document the corrected public tracking mount. |
| `docs/STARBASE_CROWD.md` | Design, file inventory, cost, validation and limitations. |

## Verification and captures

The required three-target build succeeded; the final incremental build reports
only the static-asset copy, with no compilation or link failures. Both visual tests passed (32.50 s total), including software
WebGPU, without skips. Final captures report llvmpipe (LLVM 21.1.8, 256 bits) and
zero WebGPU validation errors.

Run from `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

Reproduce the 1280×960 software WebGPU captures from the worktree root:

```sh
ALBERT_GPU=software build/linux/tests/visual_test crowd-captures build/codex/crowd-near
python3 build/codex/crowd-near/convert.py
```

Current review artifacts are in `build/codex/crowd-near/`; original captures and
placement audits remain in `build/codex/crowd/`. `before/` preserves the three
reviewed images. Current captures:

| Capture | View |
| --- | --- |
| `tower-highway.png` | Tower crown, looking west along the highway with a long lens. The initial 2 km remains closed. |
| `beach-500m.png` | 500 m over the public beach/dunes; people are naturally small at this height. |
| `among-cars.png` | Eye level beside an actual parked instance. |
| `liftoff-tracking.png` | Actual tracking rig at prescribed T+3 s liftoff, with viewers in the foreground. |
| `tailgate-poses.png` | Close actual pickup, seated occupant and neighbouring viewers. |
| `press-pull-off.png` | Press vans and camera tripods outside the western exclusion. |
| `cost-crowd-{on,off}.png` | Identical scene/camera, isolating crowd pixels and cost. |
| `after-launch.png`, `scrubbed.png`, `low-quality.png` | Disabled phase/quality checks. |
| `overview.png` | Four requested views together. |

The fixture renders the actual `addTower()` scenery, roads and production models,
with atmosphere and existing shadows. Ground is a flat ENU plane carrying the real
road/shore shader. Liftoff is prescribed render telemetry using the existing stack
appearance and tracking camera; no guidance is advanced. The captures therefore
check renderer output and placement, not live DEM contact or a complete mission.
`manifest.csv` gives exact eye/aim/FOV; broadcast clearance raises the nominal
tracking eye to 2.6001 m in this fixture. `instances.tsv`, `parts.tsv`, `audit.py`, `placement.txt`, build/test logs
and `protected-sources.txt` preserve the audit inputs and results. All 32 protected
source files and the structures/guidance region after `addStarbase()` match HEAD.

## Limits

The 2 km closures, village envelope, crowds and press pull-off are artistic layout
rules, not a surveyed fence or an authoritative launch-day closure map. The saved
land mask is 4 m; narrow driveways, dunes and minor wet patches can be absent.
Spectators do not animate, navigate or arrive/depart individually. Far wheel and
limb cards can look thin edge-on; near figures and tyres are solid meshes. There
are no crowd shadows. The near caps and nearest-800 far selection can change
membership in dense views. LOD transitions are hard switches with hysteresis, and
near-only seated occupants can appear at the threshold. Dither remains visible on
subpixel far people. Glass is opaque tinted material with a glossy response.

Terrain grounding follows the renderer's available relief, including coarse or
provisional nodes; it does not grade terrain or add physical wheels. Cars refresh
in batches when relief changes, and the existing site transform has float-scale
precision limits. Live DEM contact, hardware performance and moving-camera
stability still need application review. Multiple independently visible Starbase
site objects each own a crowd batch, like the existing site scenery.

No commits were made. No flight guidance or physics source was changed.
