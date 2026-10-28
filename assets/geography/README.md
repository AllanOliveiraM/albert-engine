# Boca Chica natural cover

`boca-chica-natural.json` is the supplied Overpass extract, unmodified. Its OSM base timestamp
is 2026-07-24T11:04:51Z. © OpenStreetMap contributors, under the
[Open Database License 1.0](https://opendatacommons.org/licenses/odbl/1-0/).
Attribution: [OpenStreetMap](https://www.openstreetmap.org/copyright).

The derived mask in `src/render/starbase_cover.hpp` carries the same attribution and data
licence. Reproduce it offline with `python3 scripts/starbase_cover.py` (Pillow and numpy).
The generated PNG encodes classifications and distances, not satellite artwork. The
source JSON is retained so polygon IDs, tags, geometry and multipolygon holes remain available.

`starbase-build-site.json` is the supplied production-site Overpass extract, also
unmodified, with OSM base timestamp 2026-10-28T02:01:30Z. It contains 451 building
features, 235 roads and six storage tanks. © OpenStreetMap contributors, under the
same [Open Database License 1.0](https://opendatacommons.org/licenses/odbl/1-0/);
[OpenStreetMap attribution](https://www.openstreetmap.org/copyright).
`examples/starbase_production.hpp` is its derived, attributed scenery dataset.
Regenerate offline with `python3 scripts/starbase_production.py > examples/starbase_production.hpp`
(Python standard library only). Polygon rings, holes, OSM IDs and tagged heights
are retained; inferred heights and architectural details are documented in
[`docs/STARBASE_PRODUCTION.md`](../../docs/STARBASE_PRODUCTION.md).
