#!/usr/bin/env python3
"""Offline production-site scenery, with OSM rings (including courtyards).

python3 scripts/starbase_production.py > examples/starbase_production.hpp
Source: assets/geography/starbase-build-site.json, ODbL, © OpenStreetMap contributors.
"""
import hashlib
import json
import math
from pathlib import Path
from osm_enu import enu

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/geography/starbase-build-site.json'
LATITUDE, LONGITUDE = 25.996128, -97.154517


def rings(members, role):
    pending = [m['geometry'][:] for m in members if m['role'] == role and 'geometry' in m]
    result = []
    while pending:
        ring = pending.pop(0)
        while ring[0] != ring[-1]:
            for i, other in enumerate(pending):
                if ring[-1] == other[0]:
                    ring += other[1:]
                elif ring[-1] == other[-1]:
                    ring += other[-2::-1]
                else:
                    continue
                pending.pop(i)
                break
            else:
                raise ValueError('Unclosed OSM multipolygon')
        result.append(ring[:-1])
    return result


def inside(p, ring):
    x, y = p
    hit = False
    for a, b in zip(ring, ring[1:] + ring[:1]):
        if (a[1] > y) != (b[1] > y) and x < (b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:
            hit = not hit
    return hit


def number(tags, key, default):
    try:
        return float(tags[key].split()[0])
    except (KeyError, ValueError, IndexError):
        return default


def main():
    data = json.loads(SOURCE.read_bytes())
    # Match the pad's origin exactly; the coordinates are never hand shifted.
    rows, points, outlines, roads = [], [], [], []
    for e in data['elements']:
        t = e.get('tags', {})
        project = lambda ring: [enu(p, LATITUDE, LONGITUDE) for p in ring]
        if 'building' in t or t.get('man_made') == 'storage_tank':
            if e['type'] == 'relation':
                outer = [project(r) for r in rings(e['members'], 'outer')]
                holes = [project(r) for r in rings(e['members'], 'inner')]
            else:
                g = e['geometry']
                if g[0] != g[-1]:
                    raise ValueError(f"Unclosed building {e['id']}")
                outer, holes = [project(g[:-1])], []
            for contour in outer:
                # Use the longest footprint edge for landmark facade orientation. Actual
                # walls/roofs always use every polygon vertex, never this detail frame.
                a, b = max(zip(contour, contour[1:]+contour[:1]), key=lambda ab: math.dist(*ab))
                heading = math.atan2(b[0]-a[0], b[1]-a[1]) % math.pi
                sn, cs = math.sin(heading), math.cos(heading)
                u = [x*cs-y*sn for x, y in contour]
                v = [x*sn+y*cs for x, y in contour]
                mx, my = (min(u)+max(u))/2, (min(v)+max(v))/2
                w, length = max(u)-min(u), max(v)-min(v)
                name, kind = t.get('name', ''), t.get('building', '')
                style = 7 if t.get('man_made') == 'storage_tank' else 5 if name == 'S20' else 6 if name == 'B12' else 8 if name == 'HLS Nosecone' else 4 if name in ('Mega Bay', 'Mega Bay 2', 'Gigabay') else 3 if name == 'Starfactory' else 2 if kind in ('house', 'static_caravan') else 1 if kind in ('office', 'apartments') or t.get('amenity') in ('townhall', 'clinic') else 0
                default = 3.5 if style == 2 else 8 if style == 1 else 10 if w*length > 400 or kind in ('industrial', 'warehouse', 'construction') else 4
                default = number(t, 'building:levels', default/3.3)*3.3
                if t.get('parking') == 'multi-storey':
                    default = 20
                if style == 7:
                    default = min(16, max(6, w))
                if style == 8:
                    default = 18
                h = number(t, 'height', default)
                first = len(outlines)
                for ring in [contour]+[r for r in holes if inside(r[0], contour)]:
                    outlines.append((len(points), len(ring)))
                    points.extend(ring)
                rows.append((e['id'], first, len(outlines)-first, mx*cs+my*sn, -mx*sn+my*cs, w, length, math.degrees(heading), h, style, name or kind))
        elif 'highway' in t and e['type'] == 'way':
            p = project(e['geometry'])
            p = [q for i, q in enumerate(p) if not i or math.dist(q, p[i-1]) > .01]
            if len(p) < 2:
                continue
            highway = t['highway'] in ('secondary', 'tertiary') and t.get('lanes', '2') == '2'
            width = 10.5 if highway else 6 if t.get('junction') == 'roundabout' else 7 if t['highway'] == 'service' else 6 if t['highway'] == 'residential' else 3.5 if t['highway'] == 'track' else 1.5
            surface = t.get('surface', 'asphalt' if highway or t['highway'] == 'residential' else 'concrete' if t['highway'] == 'service' else 'sand')
            roads.append((e['id'], p, number(t, 'width', width), {'asphalt': 0, 'concrete': 1, 'compacted': 2}.get(surface, 3), highway))
    print('#pragma once\n// Generated by scripts/starbase_production.py; ODbL, © OpenStreetMap contributors.')
    print('// Source SHA-256: '+hashlib.sha256(SOURCE.read_bytes()).hexdigest())
    print('namespace albert::example::starship {\nstruct ProductionPoint {double x,y;};\ninline constexpr ProductionPoint productionPoints[]{')
    for x, y in points:
        print(f'    {{{x:.3f},{y:.3f}}},')
    print('};\nstruct ProductionRing {unsigned first,count;};\ninline constexpr ProductionRing productionRings[]{')
    for first, count in outlines:
        print(f'    {{{first},{count}}},')
    print('};\nstruct ProductionBuilding {unsigned long long id;unsigned first,count;double x,y,width,length,heading,height;int style;const char* name;};\ninline constexpr ProductionBuilding productionBuildings[]{')
    for ident, first, count, x, y, w, length, hd, h, style, name in rows:
        print(f'    {{{ident},{first},{count},{x:.3f},{y:.3f},{w:.3f},{length:.3f},{hd:.3f},{h:.3f},{style},{json.dumps(name)}}},')
    print('};\ninline constexpr ProductionPoint productionRoadPoints[]{')
    for _, p, _, _, _ in roads:
        for x, y in p:
            print(f'    {{{x:.3f},{y:.3f}}},')
    print('};\nstruct ProductionRoad {unsigned long long id;unsigned first,count;double width;int surface;bool highway;};\ninline constexpr ProductionRoad productionRoads[]{')
    first = 0
    for ident, p, w, surface, highway in roads:
        print(f'    {{{ident},{first},{len(p)},{w:g},{surface},{str(highway).lower()}}},')
        first += len(p)
    print('};\n}')


if __name__ == '__main__':
    main()
