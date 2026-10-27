"""Keep owned product runtime code in C++; Python is reserved for auxiliary tooling."""
from pathlib import Path
import re
import sys


def check(root):
    root = Path(root)
    problems = []
    native_tools = [str(path.parent.relative_to(root)) for path in (root / 'tools').glob('*/CMakeLists.txt')
                    if re.search(r'^\s*(?:install|psim_package)\s*\(', path.read_text(), re.M)]
    for directory in ('services', 'libs', 'connectors', *native_tools):
        for path in (root / directory).rglob('*.py'):
            relative = path.relative_to(root)
            if 'tests' not in relative.parts and 'third_party' not in relative.parts:
                problems.append(f'{relative}: product runtime must use C++')
    for path in (root / 'deploy').rglob('Dockerfile'):
        for line in path.read_text().splitlines():
            if re.match(r'\s*(ENTRYPOINT|CMD)\b', line) and re.search(r'\bpython\d*\b|\.py\b', line):
                problems.append(f'{path.relative_to(root)}: owned runtime entrypoint must use C++')
    for directory in ('services', 'libs', 'connectors', 'tools'):
        for path in (root / directory).rglob('*.cpp'):
            relative = path.relative_to(root)
            if 'tests' in relative.parts or 'third_party' in relative.parts:
                continue
            text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
            if re.search(r'"[^"\n]*(?:/python\d*|\bpython3?\b|\.py(?:["\s]))', text):
                problems.append(f'{relative}: C++ runtime must not dispatch product operations to Python')
    return problems


if __name__ == '__main__':
    problems = check(sys.argv[1])
    print('\n'.join(problems) if problems else 'arch:language: product runtime uses C++')
    raise SystemExit(bool(problems))
