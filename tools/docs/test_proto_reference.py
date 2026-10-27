#!/usr/bin/env python3
"""Exercise translation failures and preservation of generated contract structure."""

import importlib.util
from pathlib import Path
import unittest


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


ROOT = Path(__file__).resolve().parents[2]
PROTO = module("proto_reference", ROOT / "tools/docs/proto_reference.py")
REFERENCE = module("reference", ROOT / "tools/contracts/reference.py")


class ReferenceTest(unittest.TestCase):
    def test_preserves_wire_names_links_and_escaped_placeholders(self):
        source = ('### Event\n<a name="Event"></a>\n'
                  '| Field | Type | Label | Description |\n| --- | --- | --- | --- |\n'
                  '| event_id | [string](#string) | repeated | Key &lt;tenant_id&gt;. |\n')
        phrases = {"Field": "Поле", "Type": "Тип", "Label": "Метка", "Description": "Описание",
                   "Key <tenant_id>": "Ключ <tenant_id>", "Scalar Value Types": "Скалярные типы"}
        result = PROTO.render(source, phrases)
        self.assertIn('### Event\n<a name="Event"></a>', result)
        self.assertIn('| event_id | [string](#string) | repeated | Ключ &lt;tenant_id&gt;. |', result)
        self.assertNotIn('Ключ <tenant_id>', result)

    def test_new_description_blocks_generation(self):
        with self.assertRaisesRegex(ValueError, "Untranslated Protobuf description"):
            PROTO.render('| event_id | string | | Changed semantic requirement. |\n', {})

    def test_navigation_normalization_is_idempotent(self):
        source = '# Protocol Documentation\n<a name="top"></a>\n\nText\u2014detail.  \n'
        first = REFERENCE.normalize(source)
        self.assertEqual(first, REFERENCE.normalize(first))
        self.assertEqual(first.count('<!-- language-links -->'), 1)
        self.assertIn('Text-detail.\n', first)


if __name__ == "__main__":
    unittest.main()
