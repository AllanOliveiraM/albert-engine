#!/usr/bin/env python3
"""Check that a GitHub release carries every container of an asset catalogue.

  gh release view assets-<id> --json assets > release.json
  python3 scripts/asset-release.py CATALOG release.json
Prints what is missing or has the wrong size; exits 1 unless the release is
complete. A light package must never point at an incomplete asset release.
"""
import json,sys

def missing(catalog,release):
    have={asset['name']:asset['size'] for asset in release.get('assets',[])}
    need=[(c['file'],c['bytes']) for c in catalog['containers']]+[('catalog.json',None)]
    return [name for name,size in need if name not in have or (size is not None and have[name]!=size)]

if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit(__doc__)
    with open(sys.argv[1],encoding='utf-8') as stream:catalog=json.load(stream)
    with open(sys.argv[2],encoding='utf-8') as stream:release=json.load(stream)
    absent=missing(catalog,release)
    for name in absent:print('missing or wrong size: '+name)
    print(f"{catalog['release']}: {'complete' if not absent else 'incomplete'}")
    sys.exit(1 if absent else 0)
