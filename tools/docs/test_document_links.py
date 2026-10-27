#!/usr/bin/env python3
"""Exercise language-independent contract summaries and translated section links."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


ROOT = Path(__file__).resolve().parents[2]
LINKS = module("links", ROOT / "tools/docs/check_links.py")
I18N = module("localization", ROOT / "tools/docs/localization.py")
CONTRACTS = module("contracts", ROOT / "tools/contracts/contracts.py")
PORTAL = module("portal", ROOT / "tools/docs/portal.py")


class DocumentLinkTests(unittest.TestCase):
    def test_translated_duplicate_headings_have_distinct_destinations(self):
        en = "# Guide\n## Recovery\n## Recovery\n## Recovery-1\n"
        ru = "# Руководство\n## Восстановление\n## Восстановление\n## Восстановление-1\n"
        mapping = I18N.section_map(en, ru)
        self.assertEqual(mapping["recovery-1-1"], "восстановление-1-1")
        self.assertEqual(set(mapping), LINKS.page_anchors(en))
        self.assertEqual(set(mapping.values()), LINKS.page_anchors(ru))

    def test_missing_section_cannot_map_to_an_unrelated_heading(self):
        with self.assertRaises(ValueError):
            I18N.section_map("# Guide\n## Recovery\n", "# Руководство\n")

    def test_portal_uses_the_same_collision_suffixes_as_repository_links(self):
        source = "# Guide\n## Recovery\n```sh\n# Example\n```\n## Recovery\n## Recovery-1\n"
        result = PORTAL.heading_ids(source)
        self.assertIn("## Recovery {#recovery-1}", result)
        self.assertIn("## Recovery-1 {#recovery-1-1}", result)
        self.assertIn("```sh\n# Example\n```", result)

    def test_encoded_links_and_explicit_anchors(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory) / "source.md", Path(directory) / "target.md"
            source.write_text("# Links\n[RU](target.md#%D0%BE%D0%B1%D0%B7%D0%BE%D1%80)\n[ID](target.md#stable-id)\n")
            target.write_text('# Обзор\n<a id="stable-id"></a>\n')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(LINKS.main(["links", str(source)]), 0)

    def test_contract_summary_does_not_depend_on_heading_language(self):
        table = "\n<!-- kafka-topic-summary -->\n| `psim.audit.v1` | `tenant_id` |\n"
        for title in ("# Topic inventory", "## Сводная таблица"):
            self.assertEqual(CONTRACTS.topic_summary(title + table), {"psim.audit.v1": "tenant_id"})

    def test_ambiguous_or_missing_summary_is_rejected(self):
        for text in ("# Inventory", "<!-- kafka-topic-summary -->\n<!-- kafka-topic-summary -->",
                     "<!-- kafka-topic-summary -->\n| `psim.audit.v1` | `tenant_id` |\n| `psim.audit.v1` | `other` |\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                CONTRACTS.topic_summary(text)


if __name__ == "__main__":
    unittest.main()
