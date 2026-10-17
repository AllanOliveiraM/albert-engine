#!/usr/bin/env python3
"""Turn a failed build log into GitHub annotations and a job summary.

Unique compiler/linker/CMake errors only (warnings are far too many in the
dependencies). Annotations are readable without logging in, unlike raw logs.
  python scripts/ci-errors.py build-windows.log
"""
import os,re,sys

COMPILE=re.compile(r'^\s*(?:\d+>)?(?P<file>[^\s(][^(]*?)\((?P<line>\d+)(?:,(?P<column>\d+))?\)\s*:\s*(?:fatal )?error (?P<code>[A-Z]+\d+)\s*:\s*(?P<message>.*?)(?:\s+\[[^\]]+\])?\s*$')
LINK=re.compile(r'^\s*(?:\d+>)?(?P<file>[^:]*?)\s*:\s*(?:fatal )?error (?P<code>LNK\d+)\s*:\s*(?P<message>.*?)(?:\s+\[[^\]]+\])?\s*$')
GCC=re.compile(r'^(?P<file>[^\s:][^:]*):(?P<line>\d+):(?:\d+:)?\s*(?:fatal )?error:\s*(?P<message>.*)$')
SCRIPT=re.compile(r'^\s*(?P<file>\S+\.ps1)\s*:\s*(?P<message>.+)$')
CMAKE=re.compile(r'^\s*CMake Error(?: at (?P<file>[^:]+):(?P<line>\d+))?.*$')

def errors(lines):
    seen,found=set(),[]
    for raw in lines:
        match=COMPILE.match(raw) or LINK.match(raw)
        if match:
            item=match.groupdict();item.setdefault('line',None)
        elif GCC.match(raw):
            item=dict(GCC.match(raw).groupdict(),code='error')
        elif SCRIPT.match(raw):
            item=dict(SCRIPT.match(raw).groupdict(),code='PowerShell',line=None)
        elif CMAKE.match(raw):
            item=dict(CMAKE.match(raw).groupdict(),code='CMake',message=raw.strip())
        else:continue
        key=(item.get('file'),item.get('line'),item['code'],item['message'][:120])
        if key not in seen:seen.add(key);found.append(item)
    return found

def relative(path):
    path=(path or '').replace('\\','/');workspace=os.environ.get('GITHUB_WORKSPACE','').replace('\\','/').rstrip('/')
    return path[len(workspace)+1:] if workspace and path.lower().startswith(workspace.lower()+'/') else path

def main(log):
    with open(log,encoding='utf-8',errors='replace') as stream:found=errors(stream)
    print(f'{len(found)} unique build error(s)')
    for item in found[:10]:
        location=f"file={relative(item.get('file'))}"+(f",line={item['line']}" if item.get('line') else '')
        print(f"::error {location},title={item['code']}::{item['message'][:400]}")
    summary=os.environ.get('GITHUB_STEP_SUMMARY')
    if summary and found:
        with open(summary,'a',encoding='utf-8') as out:
            out.write(f'### {len(found)} unique build error(s)\n\n| Code | Location | Message |\n| --- | --- | --- |\n')
            for item in found[:50]:
                where=relative(item.get('file'))+(f":{item['line']}" if item.get('line') else '')
                out.write(f"| {item['code']} | `{where}` | {item['message'][:200].replace('|','/')} |\n")
    return found

if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit(__doc__)
    main(sys.argv[1])
