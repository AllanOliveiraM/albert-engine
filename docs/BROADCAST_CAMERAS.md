# Broadcast flight cameras

Select a flight, then use the compact **Camera (C)** menu under the object picker. **C** cycles orbit, tracking, drone, tower, onboard and auto. The menu also exposes individual onboard mounts and fisheye variants. **F** enters the existing free camera; an RPC `--orbit` command returns to orbit control. Selecting a different vehicle preserves the requested broadcast mode.

Commands go to an already running `AlbertEngine --rpc`. As with `--focus`, `--orbit` and `--shot`, camera commands are applied in order and then the client exits. Start the flight in another client invocation.

```sh
build/linux/src/rpc/albert_flight_client --camera=auto
build/linux/src/rpc/albert_flight_client --starship

# While a flight is running:
build/linux/src/rpc/albert_flight_client --focus=flight.booster --camera=tracking
build/linux/src/rpc/albert_flight_client --camera=tower-fisheye --shot=tower.png
build/linux/src/rpc/albert_flight_client --camera=orbit
```

| Name | View |
| --- | --- |
| `orbit` | Existing orbit/focus/zoom controls and preferred FOV. |
| `tracking` | Public beach tripod at nominal ENU (500, −2500, 1.85) m, raised above the drawn terrain when needed; lagged pointing, bounded deterministic jitter and distance-dependent long lens framed by the projected hull size. |
| `drone` | Behind the horizontal travel direction, above and offset to one side. Heading and position ease; vehicle translation feeds forward. |
| `tower` | Crown bracket outside the mount-facing corner. Pan/tilt follows the launch; during a catch it frames the incoming fittings and rail slot, tightening to 36° at contact. The eye stays fixed. |
| `onboard` | Engine mount for the booster or full stack; aft flap mount for the ship. |
| `engine` | Aft skirt bracket beyond the exit perimeter, looking forward across the bell mouths; works on booster and ship. |
| `hotstage` | Outside the top ring, looking down past the apertures and hull. Uses the ring's appearance markers, including in a stack. |
| `flap` | Outside the hull above the ship's aft left flap, looking aft across its moving surface. |
| `tower-fisheye`, `onboard-fisheye`, `engine-fisheye`, `hotstage-fisheye`, `flap-fisheye` | Same mounts with a genuinely distorted equidistant lens, 150° diagonally. |
| `auto` | Tower launch, tracking ascent, eight seconds of onboard staging, return tracking/drone, tower catch; ship entry over the aft flap and ship catch from the crown. |

Auto initially follows the selected flight. When the main flight stages into the ship and a separate booster appears, it holds the staging shot for eight simulated seconds, follows the booster through its return, holds the settled catch for six seconds, then returns to the ship. Starting auto explicitly on the booster keeps that as the primary subject. Clicking a different vehicle starts a programme for that selection. Breakup or removal of the returning booster hands back to the ship.

The camera reads scene attitude, velocity, separation, engine layout and heat. It sends no flight commands. All shot changes are intentional broadcast cuts, with no intermediate eye path through the tower, hull or cloud. Within a shot, camera integration uses site ENU metres and presentation time; its state survives floating-origin changes. Onboard pose and roll are rigid in the hull frame. Ground cameras use local vertical, with a north-up fallback for nearly vertical looks.

Scenery clearance uses conservative oriented part envelopes and current hinge angles, plus building boxes and structure capsules. A drone stays outside the actual wind-driven launch-cloud ellipsoids and, when volumetric weather clouds are shown, outside their entire altitude band. An attached mount obstructed by scenery cuts to the shore camera rather than sliding off the hull. The inset retains its own undistorted lens and normal framing.

## Changed files

| File | Change |
| --- | --- |
| `src/core/camera_mode.hpp` | Shared camera names and parsing. |
| `src/core/bridge.hpp` | Render-only `SetCamera` command. |
| `proto/albert/v1/engine.proto` | Additive `CameraModeRequest` / `CameraService.SetMode` RPC. |
| `src/rpc/server.cpp` | Forward the mode request to the render thread. |
| `examples/flight_client.cpp` | Only the new `--camera=NAME` camera option and its description. |
| `src/render/broadcast_camera.hpp` | Camera/director state and view interface. |
| `src/render/broadcast_camera.cpp` | Mounts, framing, smoothing, clearance and automatic direction. |
| `src/render/application.cpp` | Compact menu, C cycle, RPC application, flight focus handoffs and camera/control integration. |
| `src/render/vehicle.cpp` | Give the existing transonic condensation mesh an identifying name for isolation captures. Its rendering and activation remain unchanged. |
| `src/render/display.hpp` | Fisheye setter. |
| `src/render/display.cpp` | Set the lens on the scene renderer; keep the inset undistorted. |
| `src/render/CMakeLists.txt` | Compile the camera implementation. |
| `src/gpu/fisheye.hpp` | GPU lens resources. |
| `src/gpu/fisheye.cpp` | Equidistant resampling pass with resize and resource cleanup. |
| `src/gpu/scene.hpp` | Optional fisheye switch. |
| `src/gpu/scene.cpp` | Render the lens before UI composition. |
| `src/gpu/CMakeLists.txt` | Compile the lens. |
| `tests/camera_visuals.hpp` | Camera selection, lag/framing, horizons, rebasing, rigid attachment, cloud clearance, shot/focus direction and GPU lens checks. |
| `tests/camera_captures.hpp` | Reproducible software WebGPU capture matrix using actual example vehicles and tower scenery. |
| `tests/visual_test.cpp` | Register checks and `camera-captures`. |
| `docs/BROADCAST_CAMERAS.md` | Controls, implementation, captures and limits. |

## Verification and captures

From `build/linux`:

```sh
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

The required build succeeds (all three requested targets); both visual tests pass, including software WebGPU. The 68 capture renders report llvmpipe (LLVM 21.1.8, 256 bits) and zero validation errors. `protected-sources.txt` verifies all 32 protected source files against HEAD.

Generate the render fixtures from the worktree root:

```sh
ALBERT_GPU=software build/linux/tests/visual_test camera-captures build/codex/broadcast-cameras
# Local artifact helper (Pillow): PPM -> PNG, contact sheets and gallery.
python3 build/codex/broadcast-cameras/convert.py
```

The output directory is `/unreal/allan/albert-codex/build/codex/broadcast-cameras/`. For every camera name in the table, there are four captures:

* `liftoff-NAME.png`
* `staging-NAME.png`
* `landing-burn-NAME.png`
* `catch-NAME.png`

`catch-approach-high-tower.png` and `catch-approach-low-tower.png` show the incoming fittings above the rails. `ship-entry-{onboard,flap,flap-fisheye,engine,auto}.png` and `ship-catch-{tower,tower-fisheye,onboard,engine,auto}.png` add the ship's return. `index.html` links all images; `overview.png` compares the five main broadcast modes across all four phases. `manifest.csv` records actual shot, lens and ENU eye coordinates. The conversion helper, source PPMs, `build.log`, `ctest.log`, `protected-sources.txt` and `captures.log` are retained beside the PNGs.

These are 1280×720 software WebGPU render fixtures with prescribed scene samples, actual `stack()`, `superHeavy()`, `shipAlone()` and `addTower()` geometry, existing plumes/clouds/plasma, shadows and atmosphere. The capture fixture does not advance flight guidance or simulate a catch. Its ground and shore are simple planes; the application uses its existing terrain and ocean. Staging is a near-separation sample and landing burn/catch are separate samples, not frames extracted from a complete mission recording.

## Capture review corrections

Both reported shapes came from incorrect capture setup, with no clearance/debug geometry involved:

* The teal spike was `capture ocean`, an upright ocean plane. `sea->rotation.x=ground->rotation.x` copied a threepp `float_view` angle without notifying its quaternion. The recorded plane normal was (0, 0, 1), despite its Euler angle. `rotation.set(-pi/2,0,0)` updates the quaternion; its normal is now (0, 1, 0). The upright plane also intersected and hid half the staging vehicle. Normal land/sea surfaces remain in the corrected fixture.
* The frustum was the existing **transonic condensation** collar, not an engine plume, entry plasma or launch cloud. The fixture hardcoded Mach 1 and 101325 Pa in every non-entry phase. It now uses the sampled velocity and the existing read-only standard-atmosphere lookup at the sampled altitude. The 55 m/s descent at 500 m supplies Mach −0.162544 and 95461.3 Pa, so the collar is inactive. A regression check also supplies a real transonic sample and verifies that condensation still works; the effect has not been hidden globally.

The tower camera previously aimed at the booster centre (72.3 m at contact), which filled too much of the frame with pad and lower body. It now reads the appearance's catch fitting positions and the currently hinged rail endpoints. Above the crown it follows the incoming fittings; once both fittings and slot fit, it frames their angular bounds with margin. Zoom narrows smoothly. At contact, the eye remains ENU (−14.45, −7.75, 148) m, the aim is approximately (0, 0, 100.325) m and vertical FOV is 36°. Launch framing remains centred on the stack. Projection tests cover approach from above the crown through contact and verify the fixed mount and visibility of pins/slot.

The capture's six arm carrier angles now agree during the catch, and QDs use their declared stowed angle of zero after launch. These are prescribed scenery samples, with no changes to structure definitions or flight control.

Artifacts under `build/codex/broadcast-cameras/`:

| Artifact | Evidence |
| --- | --- |
| `review-isolation/isolation.png` | Legacy frame, ocean hidden only, condensation hidden only, and corrected frame. |
| `review-isolation/isolate-{legacy,no-ocean,no-condensation,fixed}-drone.png` | Full-resolution single-source isolation renders. |
| `review-isolation.log` | Source normals, pressure/Mach values, visibility-isolation pixel deltas and zero WebGPU validation errors. |
| `review-catch-sequence.png` | Fittings approaching from 39 m and 20 m above the rails, then contact. |
| `manifest.csv` | All 68 corrected captures, actual shot/FOV and eye **and aim** ENU coordinates. |
| `review-before/` | Original reviewed images retained for comparison. |
| `review-build.log`, `review-ctest.log`, `review-captures.log` | Required build/test and software recapture results. |
| `review-protected-sources.txt` | All 32 protected files verified unchanged against HEAD. |

To reproduce the four isolation renders:

```sh
ALBERT_GPU=software build/linux/tests/visual_test camera-diagnostics build/codex/broadcast-cameras/review-isolation
```

Review changes are confined to `tests/camera_captures.hpp` (setup, telemetry, isolation and approach fixtures), `tests/camera_visuals.hpp` (catch projection regression), `tests/visual_test.cpp` (registration), `src/render/broadcast_camera.cpp` (catch framing), `src/render/vehicle.cpp` (mesh name only) and this document.

## Limitations

Camera bracket positions and optics are artistic approximations of the supplied webcast frames. The current V3 models differ from Flight 11. Fisheye resampling uses a wide perspective source; it cannot see behind the source camera and has less edge resolution than a multi-face lens render. The extra colour target/pass is allocated only after a fisheye view is used.

Automatic direction infers phases from render telemetry, not authoritative guidance phase enums. It is tuned for the existing two-vehicle Starship example; concurrent unrelated missions can confuse booster selection. A new selection, rewind or reused stack restarts direction. Very large telemetry/time jumps use cuts instead of trying to integrate missing mount motion.

Ground/drone/tower views require site scenery; unsupported targets retain orbit behaviour. Clearance envelopes are conservative visual approximations, not swept mesh collision tests. They can hide a theoretically available view inside open steel, and cloud-band avoidance can cause an intentional position cut at the band boundary. They do not model arbitrary third-party meshes or terrain relief outside the existing launch area. A fixed tower camera can naturally lose a very distant vehicle behind scenery or the horizon; auto uses tracking for distant flight.

No flight guidance or physics files were changed. No commit was made.

The launch-crowd work moves the tracking tripod onto the saved dry shoreline south of
both pad closures. Its former (2600, −2100) position lay offshore in the bundled
coastline. Launch spectators can now occupy its foreground; the same mount is used
by clearance fallback shots. See [launch-day crowd](STARBASE_CROWD.md) for current
captures and cost. Earlier camera capture artifacts above retain their historical mounts.
