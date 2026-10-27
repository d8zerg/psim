"""Tests of the push pipeline scope: affected targets follow reverse dependencies, nothing else.

Run: task arch:selftest (python3 -m unittest discover -s tools/ci).
"""

import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "arch"))
import arch_check  # noqa: E402
import scope  # noqa: E402

RULES = arch_check.load_rules()


def target(directory, deps=()):
    return {"dir": directory, "deps": list(deps), "sources": [], "includes": []}


TARGETS = {
    "common": target("libs/domain/common"),
    "incident": target("libs/domain/incident", ["common"]),
    "incident_test": target("libs/domain/incident", ["incident"]),
    "catalog": target("libs/domain/catalog", ["common"]),
    "core": target("libs/platform/core"),
    "core_test": target("libs/platform/core", ["core"]),
    "svc": target("services/incident-service", ["incident", "core"]),
    "psimctl": target("tools/psimctl", ["core"]),
}


class AffectedTargets(unittest.TestCase):
    def affected(self, *changed):
        return scope.affected_targets(TARGETS, list(changed), RULES)

    def test_leaf_module_change_selects_its_targets_and_dependents(self):
        self.assertEqual(self.affected("libs/domain/incident/src/incident.cpp"),
                         {"incident", "incident_test", "svc"})

    def test_shared_module_change_selects_every_dependent(self):
        self.assertEqual(self.affected("libs/domain/common/include/psim/domain/common/ids.hpp"),
                         {"common", "incident", "incident_test", "catalog", "svc"})

    def test_platform_change_reaches_services_and_tools(self):
        self.assertEqual(self.affected("libs/platform/core/src/build_info.cpp"),
                         {"core", "core_test", "svc", "psimctl"})

    def test_unrelated_change_selects_nothing(self):
        self.assertEqual(self.affected("docs/product/release-readiness.md", "README.md"), set())

    def test_service_change_does_not_touch_its_dependencies(self):
        self.assertEqual(self.affected("services/incident-service/src/main.cpp"), {"svc"})


class Patterns(unittest.TestCase):
    def test_translated_assets_and_reference_tools_run_documentation_checks(self):
        for path in ("docs/ru/engineering/connector-sdk.md", "tools/contracts/reference.py",
                     "tools/docs/locales/proto.yaml", "deploy/observability/runbooks/PsimServiceUnavailableRu.html",
                     "deploy/observability/ru/dashboards/overview.json"):
            with (self.subTest(path=path), patch.object(scope, "git", return_value="base"),
                  patch.object(scope.arch_check, "load_codemodel", return_value=(None, TARGETS)),
                  patch.object(scope, "changed_files", return_value=[path])):
                self.assertTrue(scope.compute(".", "unused", False)["docs"])

    def test_full_scope_patterns(self):
        config = scope.yaml.safe_load(open(scope.CONFIG, encoding="utf-8"))
        for path in ("CMakeLists.txt", "cmake/PsimCompiler.cmake", "conan.lock", "tools/toolchain/Dockerfile"):
            self.assertTrue(scope.matches(path, config["full"]), path)
        for path in ("libs/platform/core/src/build_info.cpp", "docs/product/release-readiness.md", "contracts/proto/x.proto"):
            self.assertFalse(scope.matches(path, config["full"]), path)

    def test_rule_fuzzing_follows_contract_dependencies(self):
        targets = {
            "contracts": target("contracts"),
            "psim_correlation_rules": target("services/correlation-engine", ["contracts"]),
        }
        with (patch.object(scope, "git", return_value="base"),
              patch.object(scope.arch_check, "load_codemodel", return_value=(None, targets)),
              patch.object(scope, "changed_files", return_value=["contracts/proto/psim/common/v1/types.proto"])):
            self.assertTrue(scope.compute(".", "unused", False)["fuzz_rules"])
        with (patch.object(scope, "git", return_value="base"),
              patch.object(scope.arch_check, "load_codemodel", return_value=(None, targets)),
              patch.object(scope, "changed_files", return_value=["docs/product/release-readiness.md"])):
            self.assertFalse(scope.compute(".", "unused", False)["fuzz_rules"])


if __name__ == "__main__":
    unittest.main()
