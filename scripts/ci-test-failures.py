#!/usr/bin/env python3
"""Turn the output of failed CTest tests into GitHub annotations.

Annotations are readable without logging in, unlike raw logs: one per failed
test (at most 10), with the last lines of its output.
  python scripts/ci-test-failures.py build/windows
"""
import pathlib,re,sys,xml.etree.ElementTree as ET

def junit_failures(build):
    path=pathlib.Path(build)/'test-results.xml'
    if not path.exists():return {}
    try:root=ET.parse(path).getroot()
    except ET.ParseError:return {}
    found={}
    for case in root.iter('testcase'):
        failure=case.find('failure')
        if failure is None:failure=case.find('error')
        if failure is not None:
            reason=failure.get('message') or failure.get('type') or 'failed'
            found[case.get('name','unknown')]=f"CTest: {reason}; duration {case.get('time','unknown')} s"
    return found

def failures(build):
    temporary=pathlib.Path(build)/'Testing'/'Temporary'
    failed=temporary/'LastTestsFailed.log'
    details=junit_failures(build)
    names=[line.split(':',1)[1].strip() for line in failed.read_text(errors='replace').splitlines() if ':' in line] if failed.exists() else list(details)
    path=temporary/'LastTest.log';log=path.read_text(encoding='utf-8',errors='replace') if path.exists() else ''
    found=[]
    for name in names:
        match=re.search(r'\d+/\d+ Test: '+re.escape(name)+r'\n.*?Output:\n-+\n(.*?)<end of output>',log,re.S)
        output=match.group(1) if match else '(no output captured)'
        found.append((name,output,details.get(name)))
    return found

def escape(text):return text.replace('%','%25').replace('\r','').replace('\n','%0A')

if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit(__doc__)
    found=failures(sys.argv[1])
    print(f'{len(found)} failed test(s)')
    for name,output,detail in found[:10]:
        tail='\n'.join(output.strip().splitlines()[-40:])[-3500:]
        if detail:tail=detail+'\n'+tail
        print(f'::error title=Test {name} failed::{escape(tail)}')
