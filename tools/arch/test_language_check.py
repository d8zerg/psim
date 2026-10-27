"""Prove that product Python and a C++ launcher delegating to Python fail the language check."""
from pathlib import Path
import tempfile
import unittest

from language_check import check


class Language(unittest.TestCase):
    def test_product_runtime_and_auxiliary_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in ('tools/deploy/render.py', 'services/example/tests/fixture.py'):
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_text('pass\n')
            self.assertEqual(check(root), [])
            path = root / 'services/example/src/main.py'; path.parent.mkdir(); path.write_text('pass\n')
            self.assertEqual(len(check(root)), 1)
            native = root / 'tools/psimctl'; native.mkdir(parents=True)
            (native / 'CMakeLists.txt').write_text('add_executable(psimctl main.cpp)\ninstall(TARGETS psimctl)\n')
            (native / 'operations.py').write_text('pass\n')
            self.assertEqual(len(check(root)), 2)

    def test_runtime_entrypoint_and_cpp_dispatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'deploy/infrastructure/example/Dockerfile'; path.parent.mkdir(parents=True)
            path.write_text('ENTRYPOINT ["python3", "/agent.py"]\n')
            source = root / 'tools/psimctl/src/main.cpp'; source.parent.mkdir(parents=True)
            source.write_text('execute("/usr/bin/python3", arguments);\n')
            self.assertEqual(len(check(root)), 2)
