#!/usr/bin/env python3
"""Public CI annotations must retain crash/timeout status even without stdout."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPORTER=pathlib.Path(__file__).resolve().parents[1]/'scripts'/'ci-test-failures.py'

class FailureReport(unittest.TestCase):
    def report(self,xml=None,output=None):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            if xml is not None:(root/'test-results.xml').write_text(xml,encoding='utf-8')
            if output is not None:
                temporary=root/'Testing'/'Temporary';temporary.mkdir(parents=True)
                # Write exact CRLF bytes: text mode on Windows would translate
                # these explicit CRLFs again, producing CRCRLF and blank lines.
                (temporary/'LastTestsFailed.log').write_bytes(b'13:visual_sun\r\n')
                (temporary/'LastTest.log').write_bytes(('13/56 Test: visual_sun\r\nOutput:\r\n----------\r\n'+output+'\r\n<end of output>\r\n').encode('utf-8'))
            return subprocess.run([sys.executable,str(REPORTER),str(root)],check=True,capture_output=True,text=True).stdout

    def test_crash_without_stdout_or_legacy_log(self):
        report=self.report('<testsuite><testcase name="visual_sun" time="2.5"><failure message="SEGFAULT"/></testcase></testsuite>')
        self.assertIn('1 failed test(s)',report)
        self.assertIn('CTest: SEGFAULT; duration 2.5 s',report)
        self.assertIn('(no output captured)',report)

    def test_status_survives_long_output_tail_and_windows_newlines(self):
        output='\r\n'.join(['earlier output']*100+['Solar render: sun-horizon','progress 50%'])
        report=self.report('<testsuite><testcase name="visual_sun" time="600"><failure message="Timeout"/></testcase></testsuite>',output)
        self.assertIn('CTest: Timeout; duration 600 s%0A',report)
        self.assertIn('Solar render: sun-horizon',report)
        self.assertIn('progress 50%25',report)
        self.assertNotIn('\r',report)

    def test_missing_or_incomplete_junit_retains_legacy_failure(self):
        for xml in (None,'<testsuite>'):
            with self.subTest(xml=xml):
                report=self.report(xml,'WebGPU device lost')
                self.assertIn('Test visual_sun failed',report)
                self.assertIn('WebGPU device lost',report)

    def test_passes_and_skips_are_not_failures(self):
        report=self.report('<testsuite><testcase name="pass"/><testcase name="no_adapter"><skipped/></testcase></testsuite>')
        self.assertIn('0 failed test(s)',report)
        self.assertNotIn('::error',report)

if __name__=='__main__':unittest.main()
