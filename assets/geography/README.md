# Boca Chica natural cover

`boca-chica-natural.json` is the supplied Overpass extract, unmodified. Its OSM base timestamp
is 2026-07-24T11:04:51Z. © OpenStreetMap contributors, under the
[Open Database License 1.0](https://opendatacommons.org/licenses/odbl/1-0/).
Attribution: [OpenStreetMap](https://www.openstreetmap.org/copyright).

The derived mask in `src/render/starbase_cover.hpp` carries the same attribution and data
licence. Reproduce it offline with `python3 scripts/starbase_cover.py` (Pillow and numpy).
The generated PNG encodes classifications and distances, not satellite artwork. The
source JSON is retained so polygon IDs, tags, geometry and multipolygon holes remain available.
