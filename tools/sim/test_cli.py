#!/usr/bin/env python3
"""Manual regression checks for the simulation CLI; invoked by task sim:test."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = str(pathlib.Path(sys.argv.pop(1)).resolve())


class SimulationCliTest(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([BINARY, *args], text=True, capture_output=True, timeout=30)

    def test_help_and_argument_validation(self):
        help_result = self.run_cli("--help")
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("--seed", help_result.stdout)
        for args in (("--seed",), ("--seed", "-1"), ("--seed", "oops"), ("--seed", ""),
                     ("--seed", "18446744073709551616"), ("--runs", "0"), ("--runs", "100001"),
                     ("--seed", "18446744073709551615", "--runs", "2"), ("--events", "0"),
                     ("--events", "10001"), ("--partitions", "0"), ("--partitions", "65"),
                     ("--max-handlers", "0"), ("--max-time-ms", "1000000001"),
                     ("--model-version", "2"), ("--unknown", "1")):
            with self.subTest(args=args):
                result = self.run_cli(*args)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_seed_range_including_uint64_max(self):
        result = self.run_cli("--seed", "18446744073709551615", "--runs", "1", "--events", "2",
                              "--partitions", "1", "--fault-decisions", "0", "--model-version", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS model=1 seed=18446744073709551615 runs=1", result.stdout)

    def test_saved_replay_is_identical(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = ("--seed", "42", "--runs", "1", "--save-trace", "--trace-dir", temporary)
            first = self.run_cli(*args)
            path = pathlib.Path(temporary) / "v1-seed-42.trace"
            self.assertEqual(first.returncode, 0, first.stderr)
            trace = path.read_text()
            replay = self.run_cli(*args)
            self.assertEqual(replay.returncode, 0, replay.stderr)
            self.assertEqual(first.stdout, replay.stdout)
            self.assertEqual(trace, path.read_text())
            self.assertIn("oracle ok", trace)

    def test_failure_trace_and_replay_keep_original_budget(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = ("--seed", "99", "--runs", "1", "--max-handlers", "2", "--max-time-ms", "0",
                    "--trace-dir", temporary)
            first = self.run_cli(*args)
            self.assertEqual(first.returncode, 1, first.stderr)
            self.assertIn("replay: task sim:replay SEED=99", first.stderr)
            self.assertIn("MAX_HANDLERS=2 MAX_TIME_MS=0", first.stderr)
            path = pathlib.Path(temporary) / "v1-seed-99.trace"
            trace = path.read_text()
            self.assertIn("max_handlers=2 max_time_ms=0", trace)
            second = self.run_cli(*args)
            self.assertEqual(second.returncode, 1, second.stderr)
            self.assertEqual(trace, path.read_text())
            self.assertIn("budget exhausted", trace)

    def test_trace_io_errors_fail_the_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            invalid = pathlib.Path(temporary) / "file"
            invalid.write_text("not a directory")
            result = self.run_cli("--runs", "1", "--save-trace", "--trace-dir", str(invalid))
            self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
