"""Tests of the service generator: a service created from the template is renamed and registered.

Run: task arch:selftest (python3 -m unittest discover -s tools/service-template).
"""

import os
import shutil
import tempfile
import unittest

import new_service

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


class Generator(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        shutil.copytree(os.path.join(ROOT, "tools/service-template"), os.path.join(self.root, "tools/service-template"))
        os.makedirs(os.path.join(self.root, "tools/arch"))
        shutil.copy(os.path.join(ROOT, "tools/arch/architecture.yaml"), os.path.join(self.root, "tools/arch"))
        os.makedirs(os.path.join(self.root, "services"))
        with open(os.path.join(self.root, "services/CMakeLists.txt"), "w", encoding="utf-8") as f:
            f.write("# services\n")
        with open(os.path.join(self.root, "Taskfile.yml"), "w", encoding="utf-8") as f:
            f.write("vars:\n  IMAGES: psimctl\n")

    def tearDown(self):
        shutil.rmtree(self.root)

    def read(self, rel):
        with open(os.path.join(self.root, rel), encoding="utf-8") as f:
            return f.read()

    def test_creates_a_renamed_packaged_service(self):
        new_service.create(self.root, "incident-service", ["incident"], "PSIM Incident Service")

        cmake = self.read("services/incident-service/CMakeLists.txt")
        self.assertIn("psim_add_service(incident-service", cmake)
        self.assertIn('SUMMARY "PSIM Incident Service")', cmake)
        self.assertNotIn("NO_PACKAGE", cmake)
        code = "\n".join(line for line in cmake.splitlines() if not line.startswith("#"))
        self.assertNotIn("template", code)  # only the provenance comment names the template
        worker = self.read("services/incident-service/src/worker.cpp")
        self.assertIn("namespace psim::incident_service", worker)
        self.assertIn("psim_incident_service_messages_processed_total", worker)
        self.assertIn('"psim.incident-service"', worker)

    def test_registers_the_service(self):
        new_service.create(self.root, "incident-service", ["incident"], "PSIM Incident Service")

        self.assertIn("add_subdirectory(incident-service)", self.read("services/CMakeLists.txt"))
        self.assertTrue(self.read("tools/arch/architecture.yaml").rstrip().endswith("incident-service: [incident]") or
                        "incident-service: [incident]" in self.read("tools/arch/architecture.yaml"))
        self.assertIn("IMAGES: psimctl psim-incident-service", self.read("Taskfile.yml"))
        self.assertIn('ENTRYPOINT ["/usr/bin/psim-incident-service"]',
                      self.read("deploy/images/psim-incident-service/Dockerfile"))

    def test_rejects_bad_names_existing_services_and_unknown_contexts(self):
        with self.assertRaises(SystemExit):
            new_service.create(self.root, "Incident_Service", ["incident"], "x")
        with self.assertRaises(SystemExit):
            new_service.create(self.root, "billing", ["billing"], "x")
        new_service.create(self.root, "audit-service", ["audit"], "x")
        with self.assertRaises(SystemExit):
            new_service.create(self.root, "audit-service", ["audit"], "x")


if __name__ == "__main__":
    unittest.main()
