#!/usr/bin/env python3
"""Regression checks for independent quality and security registries."""

import contextlib
import copy
import importlib.util
import io
from pathlib import Path
import shutil
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


QUALITY = module("verification", "tools/quality/verification.py")
SECURITY = module("threat_model", "tools/security/threat_model.py")


class RegistryTests(unittest.TestCase):
    def setUp(self):
        self.quality = yaml.safe_load((ROOT / "docs/quality/verification.yaml").read_text())
        self.security = yaml.safe_load((ROOT / "docs/security/threat-model.yaml").read_text())

    def check_result(self, checker, root, registry, expected):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = checker(root, registry)
        self.assertEqual(result, expected, output.getvalue())

    def test_registries_without_development_materials(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ("docs/quality/requirements.yaml", "docs/product/acceptance-spec.md",
                             "docs/security/threat-model.md"):
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, target)
            for requirement in self.security["requirements"]:
                for component in requirement["components"]:
                    (root / component).mkdir(parents=True, exist_ok=True)
            self.check_result(QUALITY.verify, root, self.quality, 0)
            self.check_result(SECURITY.verify, root, self.security, 0)
            self.assertIn("G-19", QUALITY.render(root, self.quality))
            self.assertIn("SR-38", SECURITY.render(self.security))

    def test_missing_quality_requirement_is_rejected(self):
        self.quality["goals"].pop()
        self.check_result(QUALITY.verify, ROOT, self.quality, 1)

    def test_duplicate_goal_is_rejected(self):
        self.quality["goals"].append(copy.deepcopy(self.quality["goals"][0]))
        self.check_result(QUALITY.verify, ROOT, self.quality, 1)

    def test_unknown_scenario_is_rejected(self):
        self.quality["scenarios"].append({"id": "S999", "methods": ["TS-01"]})
        self.check_result(QUALITY.verify, ROOT, self.quality, 1)

    def test_slo_requires_automated_verification(self):
        self.quality["goals"][0]["methods"] = ["M-01"]
        self.check_result(QUALITY.verify, ROOT, self.quality, 1)

    def test_security_component_must_exist_inside_repository(self):
        for component in ("services/nonexistent", "..", str(ROOT)):
            with self.subTest(component=component):
                self.security["requirements"][0]["components"] = [component]
                self.check_result(SECURITY.verify, ROOT, self.security, 1)

    def test_high_risk_requires_strong_verification(self):
        for requirement in self.security["requirements"]:
            requirement["verify"] = "review"
        self.check_result(SECURITY.verify, ROOT, self.security, 1)


if __name__ == "__main__":
    unittest.main()
