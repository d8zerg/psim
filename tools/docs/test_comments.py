#!/usr/bin/env python3
"""Check source comment detection without rejecting localized application strings."""

from pathlib import Path
import importlib.util
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("check_comments", Path(__file__).with_name("check_comments.py"))
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


class CommentTests(unittest.TestCase):
    def scan(self, name, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / name
            path.write_text(source)
            return [number for number, comment in CHECKER.comments(path) if CHECKER.CYRILLIC.search(comment)]

    def test_localized_strings_are_not_comments(self):
        fixtures = {
            "example.cpp": 'auto text = "// Текст"; auto raw = R"tag(/* Текст */)tag"; // English\n',
            "example.py": 'text = "# Текст"\n# English\n',
            "example.ts": 'const text = `// Текст`; // English\n',
            "example.yaml": 'text: "# Текст"\nmessage: |\n  # Текст\n# English\n',
            "example.sql": "SELECT '-- Текст', '/* Текст */'; -- English\n",
            "example.html": '<p>Текст</p><!-- English -->\n',
        }
        for name, source in fixtures.items():
            with self.subTest(name=name):
                self.assertEqual(self.scan(name, source), [])

    def test_cyrillic_comments_are_rejected(self):
        for name, source in {
            "example.cpp": 'auto text = "Russian"; /* Комментарий */\n',
            "example.py": 'text = "Russian" # Комментарий\n',
            "example.ts": 'const text = `Russian`; // Комментарий\n',
            "example.yaml": 'text: "Russian" # Комментарий\n',
            "example.sql": "SELECT 'Russian'; -- Комментарий\n",
            "example.html": '<p>Russian</p><!-- Комментарий -->\n',
            "example.css": 'p { color: red; } /* Комментарий */\n',
        }.items():
            with self.subTest(name=name):
                self.assertEqual(self.scan(name, source), [1])

    def test_yaml_alias_cycle_does_not_recurse(self):
        self.assertEqual(self.scan("cycle.yaml", "value: &cycle [*cycle]\n# Комментарий\n"), [2])

    def test_yaml_template_comments_are_checked(self):
        self.assertEqual(self.scan("template.yaml", "{{- $value := dict -}}\n# Комментарий\n"), [2])


if __name__ == "__main__":
    unittest.main()
