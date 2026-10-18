#!/usr/bin/env python3
"""Turn the output of failed CTest tests into GitHub annotations.

Annotations are readable without logging in, unlike raw logs: one per failed
test (at most 10), with the last lines of its output.
  python scripts/ci-test-failures.py build/windows
"""
import pathlib,re,sys

def failures(build):
    temporary=pathlib.Path(build)/'Testing'/'Temporary'
    failed=temporary/'LastTestsFailed.log'
    if not failed.exists():return []
    names=[line.split(':',1)[1].strip() for line in failed.read_text(errors='replace').splitlines() if ':' in line]
    log=(temporary/'LastTest.log').read_text(encoding='utf-8',errors='replace')
    found=[]
    for name in names:
        match=re.search(r'\d+/\d+ Test: '+re.escape(name)+r'\n.*?Output:\n-+\n(.*?)<end of output>',log,re.S)
        found.append((name,match.group(1) if match else '(no output captured)'))
    return found

def escape(text):return text.replace('%','%25').replace('\r','').replace('\n','%0A')

if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit(__doc__)
    found=failures(sys.argv[1])
    print(f'{len(found)} failed test(s)')
    for name,output in found[:10]:
        tail='\n'.join(output.strip().splitlines()[-40:])[-3500:]
        print(f'::error title=Test {name} failed::{escape(tail)}')
