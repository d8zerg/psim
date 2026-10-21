"""Verify explicit license selection without weakening the dependency policy."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import sbom

ROOT = Path(__file__).resolve().parents[2]


class LicenseChoiceTest(unittest.TestCase):
    def evaluate(self, name, licenses):
        with tempfile.TemporaryDirectory() as directory:
            graph = Path(directory) / 'graph.json'
            graph.write_text(json.dumps({'graph': {'nodes': {
                '0': {'dependencies': {'1': {'direct': True}}},
                '1': {'name': name, 'version': '10.10.1', 'context': 'host', 'license': licenses},
            }}}))
            document = sbom.app_sbom(graph, '14', '0.1.0', 'test')
            output = Path(directory) / 'sbom.json'
            sbom.save(document, output)
            with contextlib.redirect_stdout(io.StringIO()):
                status = sbom.check_licenses(ROOT / 'tools/release/licenses.yaml', [output])
            library = next(item for item in document['components'] if item['name'] == name)
            return status, library

    def test_selects_the_permissive_alternative_and_records_the_choice(self):
        status, library = self.evaluate('rocksdb', ['GPL-2.0-only', 'Apache-2.0'])
        self.assertEqual(status, 0)
        self.assertEqual(library['licenses'], [{'license': {'id': 'Apache-2.0'}}])
        self.assertIn({'name': 'psim:upstream-licenses', 'value': 'GPL-2.0-only OR Apache-2.0'}, library['properties'])

    def test_changed_terms_and_other_packages_still_fail_the_policy(self):
        self.assertEqual(self.evaluate('rocksdb', ['GPL-2.0-only'])[0], 1)
        self.assertEqual(self.evaluate('another-library', ['GPL-2.0-only', 'Apache-2.0'])[0], 1)

    def test_conjunction_requires_every_license_to_be_allowed(self):
        self.assertEqual(self.evaluate('another-library', ['IJG', 'BSD-3-Clause', 'Zlib'])[0], 0)
        self.assertEqual(self.evaluate('another-library', ['MIT', 'GPL-2.0-only'])[0], 1)
        self.assertEqual(self.evaluate('another-library', 'MIT OR GPL-2.0-only')[0], 1)


if __name__ == '__main__':
    unittest.main()
