#!/usr/bin/env python3
"""Derive the light portable folder from a complete one.

Same program and files, without assets/visuals.pak, plus assets/catalog.json:
the program downloads the containers listed there from the matching release.
  python3 scripts/light-dist.py dist/linux dist/linux-light
"""
import argparse,importlib.util,json,pathlib,shutil,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]

def light(full,output):
 full=pathlib.Path(full).resolve();output=pathlib.Path(output).resolve();pack=full/'assets/visuals.pak'
 if not pack.is_file():raise ValueError('Complete distribution expected: assets/visuals.pak missing')
 if output.exists() or full in output.parents or output==full:raise ValueError('Output must be a new folder outside the distribution')
 spec=importlib.util.spec_from_file_location('split',ROOT/'scripts/split-asset-pack.py');split=importlib.util.module_from_spec(spec);spec.loader.exec_module(split)
 with tempfile.TemporaryDirectory() as work:catalog=split.split(pack,work,catalog_only=True)
 shutil.copytree(full,output,symlinks=True,ignore=lambda directory,names:['visuals.pak'] if pathlib.Path(directory).resolve()==full/'assets' else [])
 (output/'assets/catalog.json').write_text(json.dumps(catalog,indent=1)+'\n')
 return catalog

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 parser.add_argument('full',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
 catalog=light(args.full,args.output);print(f"{args.output}: downloads {catalog['total_bytes']/1e9:.2f} GB from release {catalog['release']}")
