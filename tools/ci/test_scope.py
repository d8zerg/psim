"""Tests of the push pipeline scope: affected targets follow reverse dependencies, nothing else.

Run: task arch:selftest (python3 -m unittest discover -s tools/ci).
"""

import os
import sys
import unittest

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
        self.assertEqual(self.affected("docs/progress.md", "README.md"), set())

    def test_service_change_does_not_touch_its_dependencies(self):
        self.assertEqual(self.affected("services/incident-service/src/main.cpp"), {"svc"})


class Patterns(unittest.TestCase):
    def test_full_scope_patterns(self):
        config = scope.yaml.safe_load(open(scope.CONFIG, encoding="utf-8"))
        for path in ("CMakeLists.txt", "cmake/PsimCompiler.cmake", "conan.lock", "tools/toolchain/Dockerfile"):
            self.assertTrue(scope.matches(path, config["full"]), path)
        for path in ("libs/platform/core/src/build_info.cpp", "docs/progress.md", "contracts/proto/x.proto"):
            self.assertFalse(scope.matches(path, config["full"]), path)


if __name__ == "__main__":
    unittest.main()
