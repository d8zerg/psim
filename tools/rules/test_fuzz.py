"""The fuzzing gate requires elapsed time and a successful native process."""

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import fuzz


class FuzzGate(unittest.TestCase):
    def run_gate(self, code, elapsed, seconds=600, timeout=False):
        with tempfile.TemporaryDirectory() as directory, contextlib.chdir(directory):
            binary = Path("fuzzer")
            binary.write_bytes(b"test fixture")
            with (patch.object(sys, "argv", ["fuzz.py", str(binary), "--seconds", str(seconds)]),
                  patch.object(fuzz.time, "monotonic", side_effect=[0, elapsed]),
                  patch.object(fuzz.subprocess, "run") as run,
                  contextlib.redirect_stdout(io.StringIO()),
                  contextlib.redirect_stderr(io.StringIO())):
                run.return_value = subprocess.CompletedProcess([], code)
                if timeout:
                    run.side_effect = subprocess.TimeoutExpired("fuzzer", 720)
                with self.assertRaises(SystemExit) as exit_status:
                    fuzz.main()
                report = Path("build/rules/fuzz/report.json")
                return exit_status.exception.code, json.loads(report.read_text()) if report.exists() else None, run.called

    def test_rejects_a_short_requested_duration_before_execution(self):
        code, report, called = self.run_gate(0, 601, seconds=10)
        self.assertEqual(code, 2)
        self.assertIsNone(report)
        self.assertFalse(called)

    def test_successful_but_short_process_cannot_pass(self):
        code, report, _ = self.run_gate(0, 599.9)
        self.assertEqual(code, 1)
        self.assertFalse(report["passed"])
        self.assertFalse(report["gate_passed"])

    def test_crash_after_the_required_duration_cannot_pass(self):
        code, report, _ = self.run_gate(-11, 601)
        self.assertEqual(code, 1)
        self.assertEqual(report["exit_code"], -11)
        self.assertFalse(report["gate_passed"])

    def test_timeout_cannot_pass(self):
        code, report, _ = self.run_gate(0, 720, timeout=True)
        self.assertEqual(code, 1)
        self.assertEqual(report["exit_code"], 124)
        self.assertFalse(report["gate_passed"])

    def test_success_requires_time_and_exit_status(self):
        code, report, _ = self.run_gate(0, 601)
        self.assertEqual(code, 0)
        self.assertTrue(report["gate_passed"])
        self.assertEqual(report["elapsed_seconds"], 601)
        self.assertEqual(len(report["binary_sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
