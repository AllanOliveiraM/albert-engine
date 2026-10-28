# Boca Chica land and water

The existing `Roads` terrain material now also draws Boca Chica's natural cover. There is no
second decal mesh, coplanar layer, or extra draw call. Roads still paint over the cover, and
the existing scenery supplies pads, towers and vehicles. Flight guidance, contact geometry,
physics, DEM providers and CPU terrain heights are unchanged.

## Files

| File | Change |
| --- | --- |
| `src/render/roads.hpp` | Owns the cover atlas and its high/low variants alongside the road index. |
| `src/render/roads.cpp` | Decodes the built-in mask, shades natural surfaces over imagery, flattens drawn water vertices, adds calm lagoon reflection/ripples and coastal surf; preserves standard lighting, shadows and atmosphere. |
| `src/render/starbase_cover.hpp` | Generated embedded PNG: OSM classifications and a signed coastline distance, with source hash and geographic bounds. No external runtime file. |
| `scripts/starbase_cover.py` | Offline polygon/ring assembly, hole preservation, rasterization, waterways and Gulf classification. Requires Pillow and numpy at generation time. |
| `scripts/osm_enu.py` | Shared WGS84 metres-per-degree projection. |
| `scripts/starbase_osm.py` | Uses that shared projection; its coordinate formula is unchanged. |
| `assets/geography/boca-chica-natural.json` | Unmodified supplied Overpass data, including original IDs/tags/geometry. |
| `assets/geography/README.md` | Source epoch, ODbL attribution, reproduction. |
| `src/gpu/scene.cpp` | Supplies the existing ocean anchor/basis even with FFT waves disabled. |
| `src/render/application.cpp` | Maintains that visual sea frame below 30 km independently of the wave preference. |
| `tests/visual_test.cpp` | Checks geographic land/water samples, low-quality coverage and darkness without illumination; exposes `land-captures`. |
| `tests/land_visuals.hpp` | Reproducible offscreen fixture using actual Starbase scenery, satellite imagery, atmosphere, shadows and the existing FFT ocean. |
| `THIRD_PARTY.md` | Natural-cover attribution and data licence. |
| `docs/STARBASE_LAND_COVER.md` | This report. |

## Geography and imagery

The origin is Tower 1, 25.996128 N, −97.154517 longitude. Both OSM converters use the same
WGS84 meridional/prime-vertical scale as the roads. The terrain's global geodetic UVs plus
their float residuals reproduce those east/north coordinates in the vertex shader. Regional
imagery keeps its own UVs. No hand translation, rotation, imagery-dependent fitting, or
change to the tower's existing offset is applied.

The source declares an OSM base timestamp of **2026-07-24T11:04:51Z** and SHA256
`b06a0cdaf8dcbab6017d80c911c9569c05de00fd19ecd1ba92c2a99cef6d6345`.
The generator retains 52 water features, 15 saltmarsh features, 9 tidal-flat features,
6 sand/beach features and all 187 scrub polygons, plus streams/canals/ditches/river lines.
It joins relation member endpoints, including reversed members, and subtracts inner rings.
Tagged scrub islands inside wetland relations are retained. The open coastline ways are
intersected by latitude to identify the Gulf on the east; they are never individually
closed with artificial diagonals across the land.

The mask spans east −8608 to +2656 m and north −6272 to +4992 m: **11.264 km square**.
High quality uses 2816² source cells at 4 m; low quality averages 2×2 cells before upload,
giving 1408² at 8 m. Two RGBA weight layers share one vertically packed atlas, keeping
the complete terrain pipeline within WebGPU's standard 16 sampled-texture limit. With
mipmaps the atlas is approximately **80.7 MiB high / 20.2 MiB low**, allocated lazily.
Switching quality retains both variants for reuse. It adds one texture binding to roads.

Cover colours blend with the satellite in linear light: mapped land retains 28% of its
imagery colour and additional imagery luminance variation; water retains 18%. Unmapped
grassland keeps its observed colour with filtered tussock-scale variation. Olive marsh,
green-brown scrub, pale flats and sand have different roughness and mottling. Wet margins
come from a coarser water-mask mip. The long beach receives wet sand and two irregular
surf bands from a signed distance to the saved coastline. That distance is encoded in
two linear bytes at 1/128 m precision, avoiding metre-scale surf steps under magnification.
Near the coast the shader decodes four texels before bilinear interpolation, avoiding
UNORM hardware that rounds the filtered high byte separately. Distant views use the mip chain.

`boca-chica-imagery.png` and `boca-chica-plan.png` use the same camera and geographic UVs
with the cover disabled/enabled. The coastline and major bays agree at the available
imagery's broad scale. The bundled `earth-day` image is dated October 2004 and roughly
500 m/pixel at the equator; it cannot verify present-day small channels, beach width,
industrial footprints or metre-level alignment. No newer imagery or network data was used.

## Drawing and filtering

Water vertices are moved only in the shader toward the existing mean-sea frame, including
the Earth's curvature. The terrain geometry arrays, height sampling and physics remain
unchanged. Sheltered water uses a smooth normal with small, filtered ripples and the existing
sky reflection. The Gulf uses the existing wind spectrum and foam. With waves disabled,
the drawn sea level and calm reflections remain available. OSM land suppresses the older
imagery's blue-colour ocean classifier within the mask. Road coverage suppresses water
reflection and keeps pavement matte.

All surfaces pass through the existing GGX sunlight, sun shadows, sky lighting and aerial
perspective. No natural surface emits light. Cover weights use trilinear mipmaps; high
quality also uses anisotropic filtering. Procedural octaves fade to their mean before
becoming subpixel, including the broad octave at ascent distances. Road paint and surf
bands integrate coverage over the pixel footprint. High-quality ground bump detail uses
metre-scale derivatives; low quality omits it and fine grain. The mask fades over its outer
128 m. There are no new `pow()` calls or negative-base powers.

## Verification and captures

Build, from this worktree only:

```sh
cd /unreal/allan/albert-codex/build/linux
/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2
/usr/bin/ctest -R visual --output-on-failure
```

Reproduce captures from the repository root:

```sh
ALBERT_GPU=software ./build/linux/tests/visual_test land-captures build/codex/land-check
```

The fixture produces eleven 1280×960 direct GPU readbacks (`.ppm`); matching `.png` copies
and a contact sheet are local viewing artifacts. It uses the real `addTower(request,true)`
scenery and roads, the saved visual pack, the engine atmosphere, sun shadows and FFT sea,
without advancing a flight. Its ground is a georeferenced ENU plane. This exercises the
same material and water vertex treatment, but is **not a full application's DEM capture**.
The finite plane's far edge is visible in wide views.

All paths below are under `/unreal/allan/albert-codex/build/codex/land-check/`:

| Capture | View |
| --- | --- |
| `contact-sheet.png` | All eleven views. |
| `boca-chica-plan.png` | Land cover, western bays, tidal-flat islands and Gulf from 7 km. |
| `boca-chica-imagery.png` | Same view with natural cover and road material disabled. |
| `boca-chica-ascent.png` | Coastal region at 6.5 km. |
| `boca-chica-ascent-low.png` | Same camera, low quality. |
| `pad-coast-marsh.png` | Pads, channels, flats, beach and Gulf from 1.15 km. |
| `tidal-channels.png` | Winding water and sand bands beside the site from 250 m. |
| `western-lagoons.png` | Westward view across the flats and water. |
| `pad-eye-beach.png` | 2.2 m eye height beside the site, grazing grassland. |
| `gulf-surf.png` | Beach/surf and the pads in the distance from 60 m. |
| `gulf-surf-low.png` | Same coastal camera at low quality. |
| `coast-waves-disabled.png` | Same oblique coastal view with FFT waves off. |
| `osm-mask.png` | North-up diagnostic of the generated OSM classes. |

`build.log`, `ctest.log`, `captures.log`, `generation.log` and `invariants.txt` record the
build tail, test results, adapter/validation, generation and unchanged-file hashes.
The required three-target build completed successfully. Both visual tests passed (39.51 s
total), including actual software WebGPU rendering, with no skip. The capture adapter is
llvmpipe (LLVM 21.1.8, 256 bits), with zero validation errors across all eleven captures. The invariant audit checks 34 protected/terrain source files
byte-for-byte against HEAD.
The geographic GPU checks cover a lagoon, tidal flat, saltmarsh, scrub island and beach;
they also compare high/low lagoon coverage and check that inland water is dark without sun.

## Limits

- This is drawn land cover, with material-scale clumps and roughness. It adds no grass
  meshes, dune silhouettes, reeds, erosion, beach excavation, new industrial buildings,
  Starfactory or high bays. Those western buildings need separate scenery data/work.
- Water uses one visual mean-sea datum, without tides, bathymetry, surveyed lagoon levels,
  hydrological connectivity, planar object reflections or wave run-up simulation. Surf
  and fine lagoon ripples are procedural/static patterns; the existing Gulf spectrum moves.
- Vertex flattening follows the existing terrain tessellation. Channels narrower than its
  vertex spacing receive water colour and flat shading but may not get a fully level
  silhouette. Coarse LOD crossings and DEM shoreline contact need a full application audit.
- The static OSM epoch and old imagery can disagree with current coast, vegetation and
  construction. Stream widths without OSM tags are assumptions: 6 m streams, 10 m canals,
  3 m ditches and 20 m river centre lines (the river's polygons also remain).
- Like roads, this runs with Earth terrain and the first visible road-bearing scenery site.
  The built-in layer activates only near this Starbase origin. Multiple distinct sites need
  the existing road system's future multi-site extension.
- Software WebGPU validates rendering, geography and high/low views. Hardware frame cost,
  long-run temporal stability during an actual ascent/landing and full live-DEM views are
  not established by these offscreen captures. Mip/derivative filtering addresses the
  known distant aliasing sources; this is not a hardware performance guarantee.

No commits were made.
