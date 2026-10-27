#!/usr/bin/env python3
"""Reject incomplete or stale translations and divergent executable examples."""

import contextlib
import copy
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

import yaml

SPEC = importlib.util.spec_from_file_location("localization", Path(__file__).with_name("localization.py"))
I18N = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(I18N)


class LocalizationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.en = "# Installation\n\n## Check\n\nRun `task deploy:flow` for S1.\n\n```sh\ntask deploy:flow\n```\n"
        self.ru = "# Установка\n\n## Проверка\n\nЗапустите `task deploy:flow` для S1.\n\n```sh\ntask deploy:flow\n```\n"
        (self.root / "en.md").write_text(self.en)
        (self.root / "ru.md").write_text(self.ru)
        self.manifest = {"default_language": "en", "languages": ["en", "ru"], "pages": [
            {"id": "install", "en": "en.md", "ru": "ru.md", "en_sha256": I18N.digest(self.en), "ru_sha256": I18N.digest(self.ru)}]}

    def check(self, expected):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = I18N.check(self.root, self.manifest, require_inventory=False)
        self.assertEqual(result, expected, output.getvalue())

    def test_complete_pair(self):
        self.check(False)
        self.assertEqual([level for level, _ in I18N.headings(self.en)], [1, 2])

    def test_missing_language_is_rejected(self):
        (self.root / "ru.md").unlink()
        self.check(True)

    def test_source_change_requires_translation_review(self):
        (self.root / "en.md").write_text(self.en + "A new prerequisite.\n")
        self.check(True)

    def test_review_rejects_divergent_command_without_updating_manifest(self):
        directory = self.root / "docs"
        directory.mkdir()
        path = directory / "localization.yaml"
        original = yaml.safe_dump(self.manifest)
        path.write_text(original)
        (self.root / "ru.md").write_text(self.ru.replace("task deploy:flow", "task deploy:vm:destroy"))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(I18N.review(self.root, ["en.md"], require_inventory=False))
        self.assertEqual(path.read_text(), original)

    def test_review_rejects_unknown_selection(self):
        directory = self.root / "docs"
        directory.mkdir()
        path = directory / "localization.yaml"
        original = yaml.safe_dump(self.manifest)
        path.write_text(original)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(I18N.review(self.root, ["unknown.md"], require_inventory=False))
        self.assertEqual(path.read_text(), original)

    def test_updated_hash_does_not_hide_changed_command(self):
        changed = self.ru.replace("task deploy:flow", "task deploy:vm:destroy")
        (self.root / "ru.md").write_text(changed)
        self.manifest["pages"][0]["ru_sha256"] = I18N.digest(changed)
        self.check(True)

    def test_updated_hash_does_not_hide_missing_section(self):
        changed = self.ru.replace("## Проверка\n", "")
        (self.root / "ru.md").write_text(changed)
        self.manifest["pages"][0]["ru_sha256"] = I18N.digest(changed)
        self.check(True)

    def test_default_language_and_duplicate_pairs(self):
        self.manifest["default_language"] = "ru"
        self.manifest["pages"].append(copy.deepcopy(self.manifest["pages"][0]))
        self.check(True)

    def test_english_page_cannot_silently_use_russian(self):
        (self.root / "en.md").write_text(self.ru)
        self.manifest["pages"][0]["en_sha256"] = I18N.digest(self.ru)
        self.check(True)

    def test_review_hash_does_not_hide_unexpected_language(self):
        changed = self.en + "Each request重新 selects its source.\n"
        (self.root / "en.md").write_text(changed)
        self.manifest["pages"][0]["en_sha256"] = I18N.digest(changed)
        self.check(True)

    def test_embedded_diagram_requires_english_labels(self):
        diagram = '\n```plantuml\n@startuml\nrectangle "Сервис" as S\n@enduml\n```\n'
        for language, source in (("en", self.en), ("ru", self.ru)):
            changed = source + diagram
            (self.root / (language + ".md")).write_text(changed)
            self.manifest["pages"][0][language + "_sha256"] = I18N.digest(changed)
        self.check(True)

    def test_artifact_preserves_query_and_translates_only_ui(self):
        directory = self.root / "tools/docs/locales"
        directory.mkdir(parents=True)
        document = {"title": "Event rate", "targets": [{"expr": "rate(psim_events_total[1m])"}],
                    "fieldConfig": {"defaults": {"noValue": "No data"}}}
        definition = {"kind": "dashboard", "strings": {
            "/title": {"source": "Event rate", "ru": "Частота событий"},
            "/fieldConfig/defaults/noValue": {"source": "No data", "ru": "Нет данных"}}}
        (directory / "artifacts.yaml").write_text(yaml.safe_dump({"artifacts": {"board.json": definition}}))
        result = I18N.localized_artifact(self.root, "board.json", document, "ru")
        self.assertEqual(result["title"], "Частота событий")
        self.assertEqual(result["targets"], document["targets"])
        self.assertEqual(document["title"], "Event rate")
        document["title"] = "Incident rate"
        with self.assertRaisesRegex(ValueError, "stale"):
            I18N.localized_artifact(self.root, "board.json", document, "ru")

    def test_artifact_new_field_requires_translation(self):
        directory = self.root / "tools/docs/locales"
        directory.mkdir(parents=True)
        (directory / "artifacts.yaml").write_text(yaml.safe_dump({"artifacts": {
            "api.yaml": {"kind": "openapi", "strings": {}}}}))
        with self.assertRaisesRegex(ValueError, "incomplete"):
            I18N.localized_artifact(self.root, "api.yaml", {"info": {"description": "New behavior"}}, "ru")

    def test_placeholder_cannot_be_replaced_with_prose(self):
        directory = self.root / "tools/docs/locales"
        directory.mkdir(parents=True)
        (directory / "artifacts.yaml").write_text(yaml.safe_dump({"artifacts": {
            "board.json": {"kind": "dashboard", "strings": {
                "/noValue": {"source": "-", "ru": "Unexpected translation instructions"}}}}}))
        with self.assertRaisesRegex(ValueError, "placeholder"):
            I18N.localized_artifact(self.root, "board.json", {"noValue": "-"}, "ru")

    def test_html_translation_preserves_commands(self):
        directory = self.root / "deploy/observability/runbooks"
        directory.mkdir(parents=True)
        pair = {"en": "deploy/observability/runbooks/Test.html", "ru": "deploy/observability/runbooks/TestRu.html"}
        for language, heading in (("en", "Diagnostics"), ("ru", "Диагностика")):
            text = f'<html lang="{language}"><a data-language-switch>EN/RU</a><h2 id="diagnostics">{heading}</h2><code>psimctl diagnose</code></html>'
            (self.root / pair[language]).write_text(text)
            pair[language + "_sha256"] = I18N.digest(text)
        manifest = {"assets": [pair]}
        self.assertEqual(I18N.check_assets(self.root, manifest), [])
        changed = (self.root / pair["ru"]).read_text().replace("psimctl diagnose", "psimctl backup create")
        (self.root / pair["ru"]).write_text(changed)
        pair["ru_sha256"] = I18N.digest(changed)
        self.assertTrue(any("executable examples differ" in error for error in I18N.check_assets(self.root, manifest)))


if __name__ == "__main__":
    unittest.main()
