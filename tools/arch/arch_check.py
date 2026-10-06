#!/usr/bin/env python3
"""Architecture fitness functions of the C++ code base (step 1.2, ADR-001, ADR-033).

  deps <root> <build-dir>  FF-01: module dependency directions in the CMake target graph (File API
                           code model of a configured build) and in #include directives
  purity <root>            FF-02: no system clocks, randomness, I/O, threads or exceptions in libs/domain
  sql <root>               FF-07: SQL text is never composed at run time (SR-13)

Module rules: architecture.yaml next to this script. Exit code 1 when a rule is violated.
"""

import fnmatch
import glob
import json
import os
import re
import sys

import yaml

RULES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "architecture.yaml")
CXX_EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".ipp")
SCAN_ROOTS = ("libs", "services", "connectors", "tools", "contracts")
SKIP_DIRS = {"build", "third_party", ".cache", ".git", "node_modules"}
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

# C++23 standard library headers, including the C compatibility headers <cxxx>.
STD_HEADERS = set("""
algorithm any array atomic barrier bit bitset charconv chrono codecvt compare complex concepts
condition_variable coroutine deque exception execution expected filesystem flat_map flat_set format
forward_list fstream functional future generator initializer_list iomanip ios iosfwd iostream istream
iterator latch limits list locale map mdspan memory memory_resource mutex new numbers numeric optional
ostream print queue random ranges ratio regex scoped_allocator semaphore set shared_mutex
source_location span spanstream sstream stack stacktrace stdexcept stdfloat stop_token streambuf
string string_view strstream syncstream system_error thread tuple type_traits typeindex typeinfo
unordered_map unordered_set utility valarray variant vector version
cassert cctype cerrno cfenv cfloat cinttypes climits clocale cmath csetjmp csignal cstdarg cstddef
cstdint cstdio cstdlib cstring ctime cuchar cwchar cwctype
""".split())

# FF-02. The domain core is deterministic: time, randomness and I/O come through ports
# (crosscutting.md section 12), errors are values (std::expected), execution is single-threaded.
PURITY_HEADERS = {
    "ctime": "system time; use the Clock port",
    "random": "randomness; use the Random or IdGenerator port",
    "cstdio": "I/O", "print": "I/O", "iostream": "I/O", "fstream": "I/O", "filesystem": "I/O",
    "syncstream": "I/O", "csignal": "process control", "csetjmp": "non-local jumps",
    "thread": "threads", "mutex": "threads", "shared_mutex": "threads", "future": "threads",
    "condition_variable": "threads", "stop_token": "threads", "semaphore": "threads",
    "latch": "threads", "barrier": "threads",
}
PURITY_PATTERNS = [
    (re.compile(r"\b(system_clock|steady_clock|high_resolution_clock|utc_clock|tai_clock|gps_clock|file_clock)\b"),
     "system clock; use the Clock port"),
    (re.compile(r"\bstd::(time|clock)\s*\(|(?<![\w:])::(time|clock)\s*\(|"
                r"\b(clock_gettime|gettimeofday|localtime|gmtime|timespec_get)\s*\("),
     "system time call; use the Clock port"),
    (re.compile(r"\b(random_device|mt19937(_64)?|minstd_rand0?|default_random_engine|ranlux\w*|knuth_b)\b|"
                r"(?<![\w.>:])s?rand\s*\(|\bstd::s?rand\b|\bgetrandom\s*\("),
     "randomness; use the Random or IdGenerator port"),
    (re.compile(r"\bstd::(cout|cerr|clog|cin|wcout|wcerr|print|println|printf|puts|fopen|getenv|system)\b|"
                r"(?<![\w.>:])(printf|fprintf|puts|fopen|popen|getenv)\s*\("),
     "I/O; the domain core does no input or output"),
    (re.compile(r"\b(throw|try|catch)\b"), "exceptions are not a domain error mechanism; return std::expected"),
    (re.compile(r"\bstd::(thread|jthread|async|mutex|this_thread)\b"), "threads; the domain core is single-threaded"),
]

# FF-07. SQL keywords are written in upper case (coding standard), so upper-case literals are SQL.
SQL_STATEMENT = re.compile(
    r"^\s*(SELECT|INSERT\s+INTO|UPDATE\s+[\w.\"]+\s+SET|DELETE\s+FROM|WITH(\s+RECURSIVE)?\s+\w+\s+AS|MERGE\s+INTO|"
    r"(CREATE|ALTER|DROP|TRUNCATE)\s+(TABLE|INDEX|VIEW|SCHEMA|DATABASE|MATERIALIZED|FUNCTION|PARTITION)|"
    r"COPY\s+[\w.]+\s+(FROM|TO))\b")
SQL_FRAGMENT = re.compile(r"^\s*(WHERE|AND|OR|FROM|VALUES|ORDER\s+BY|GROUP\s+BY|LIMIT|OFFSET|JOIN|SET|IN)\b")
SQL_ALLOW = re.compile(r"psim-arch:\s*allow-sql\s*\(\s*\S[^)]*\)")
FORMAT_CALL = re.compile(r"\b(v?format(_to(_n)?)?|StrCat|StrFormat|StrAppend|s?n?printf|append)\s*\(")


def load_rules(path=RULES_FILE):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def source_files(root):
    for top in SCAN_ROOTS:
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, top)):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
            for name in sorted(filenames):
                if name.endswith(CXX_EXTENSIONS):
                    yield os.path.relpath(os.path.join(dirpath, name), root).replace(os.sep, "/")


def is_test_path(rel):
    """Tests and benchmarks: may use test frameworks and the simulation bench."""
    return not {"tests", "benchmarks"}.isdisjoint(rel.split("/"))


def classify(rel, rules):
    """Return (kind, module directory) of a path relative to the root, or (None, None)."""
    parts = rel.split("/")
    for module in rules["modules"]:
        depth = len(module["pattern"].split("/"))
        if len(parts) >= depth and fnmatch.fnmatchcase("/".join(parts[:depth]), module["pattern"]):
            return module["kind"], "/".join(parts[:depth])
    return None, None


def lex(text):
    """Blank comments and replace string literals with "#<index>" placeholders.

    Returns (code, strings): line breaks are preserved, strings is a list of (line, value).
    """
    out, strings = [], []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
        elif c == '"':
            prefix = re.search(r"[A-Za-z0-9_]*$", text[max(0, i - 3):i]).group()
            line = text.count("\n", 0, i) + 1
            if prefix.endswith("R") and prefix in ("R", "u8R", "uR", "UR", "LR"):
                open_paren = text.index("(", i)
                delimiter = text[i + 1:open_paren]
                end = text.index(")" + delimiter + '"', open_paren)
                value, i = text[open_paren + 1:end], end + len(delimiter) + 2
            else:
                j = i + 1
                while j < n and text[j] not in '"\n':
                    j += 2 if text[j] == "\\" else 1
                value, i = text[i + 1:j], j + 1
            out.append(f'"#{len(strings)}"' + "\n" * value.count("\n"))
            strings.append((line, value))
        elif c == "'" and i > 0 and text[i - 1].isalnum() and not re.search(r"\b(u8|u|U|L)$", text[max(0, i - 2):i]):
            out.append(c)  # digit separator: 1'000'000
            i += 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] not in "'\n":
                j += 2 if text[j] == "\\" else 1
            out.append("' '")
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out), strings


def line_of(code, pos):
    return code.count("\n", 0, pos) + 1


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


# --- FF-01: dependency directions --------------------------------------------------------------

def edge_problem(src_kind, src_module, dst_kind, dst_module, is_test, rules):
    """Return a reason why src may not depend on dst, or None when allowed."""
    if dst_module == src_module or (dst_module is None and dst_kind == src_kind):
        return None
    allowed = rules["allowed"].get(src_kind, [])
    if dst_kind not in allowed and not (is_test and dst_kind in rules.get("test_extra", [])):
        return f"{src_kind} may not depend on {dst_kind}"
    if src_kind == "service" and dst_kind == "domain" and dst_module:
        service, context = src_module.split("/")[-1], dst_module.split("/")[-1]
        contexts = rules["services"].get(service)
        if contexts is None:
            return f"service {service} is not listed in architecture.yaml"
        if context not in contexts:
            return f"service {service} may use domain contexts {contexts}, not {context}"
    return None


def check_targets(targets, rules, root):
    """Check the CMake target graph. targets: name -> {dir, deps, sources, includes}."""
    problems = []
    for name, t in sorted(targets.items()):
        kind, module = classify(t["dir"], rules)
        if kind is None:
            problems.append(f"target {name} ({t['dir']}): outside every module of architecture.yaml")
            continue
        is_test = bool(t["sources"]) and all(is_test_path(s) for s in t["sources"])
        for dep in t["deps"]:
            if dep not in targets:
                continue
            dst_kind, dst_module = classify(targets[dep]["dir"], rules)
            reason = edge_problem(kind, module, dst_kind, dst_module, is_test, rules)
            if reason:
                problems.append(f"target {name} ({module}) -> {dep} ({dst_module}): {reason}")
        if kind in ("domain", "domain-common") and not is_test:
            for inc in t["includes"]:
                if os.path.commonpath([root, os.path.abspath(inc)]) != root:
                    problems.append(f"target {name} ({module}): external include directory {inc}; "
                                    "the domain core uses only the standard library")
    return problems


def include_target(path, rules):
    """Return (kind, module) of an included project header, or (None, None) for others."""
    for rule in rules["includes"]:
        if ("prefix" in rule and path.startswith(rule["prefix"])) or \
           ("regex" in rule and re.match(rule["regex"], path)):
            module = None
            if rule["kind"] == "domain":
                module = "libs/domain/" + path[len(rule["prefix"]):].split("/")[0]
            elif rule["kind"] == "domain-common":
                module = "libs/domain/common"
            return rule["kind"], module
    return None, None


def check_includes(root, rules):
    problems = []
    for rel in source_files(root):
        kind, module = classify(rel, rules)
        if kind is None:
            continue
        is_test = is_test_path(rel)
        for no, line in enumerate(read(root, rel).splitlines(), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            bracket, path = m.groups()
            if bracket == "<":
                if kind in ("domain", "domain-common") and not is_test and path not in STD_HEADERS:
                    problems.append(f"{rel}:{no}: <{path}> is not a standard library header; "
                                    "the domain core uses only the standard library")
                continue
            dst_kind, dst_module = include_target(path, rules)
            if dst_kind:
                reason = edge_problem(kind, module, dst_kind, dst_module, is_test, rules)
                if reason:
                    problems.append(f'{rel}:{no}: "{path}": {reason}')
    return problems


def load_codemodel(build_dir):
    """Read targets from the CMake File API reply of a configured build directory."""
    reply = os.path.join(build_dir, ".cmake", "api", "v1", "reply")
    indexes = sorted(glob.glob(os.path.join(reply, "index-*.json")))
    if not indexes:
        raise SystemExit(f"arch: no CMake File API reply in {reply}; configure the build first (task cpp:configure)")

    def load(name):
        with open(os.path.join(reply, name), encoding="utf-8") as f:
            return json.load(f)

    codemodel = load(load(os.path.basename(indexes[-1]))["reply"]["codemodel-v2"]["jsonFile"])
    root = codemodel["paths"]["source"]
    by_id, targets = {}, {}
    for ref in codemodel["configurations"][0]["targets"]:
        t = load(ref["jsonFile"])
        by_id[t["id"]] = t["name"]
        targets[t["name"]] = {
            "dir": t["paths"]["source"],
            "deps": [d["id"] for d in t.get("dependencies", [])],
            "sources": [s["path"] for s in t.get("sources", []) if not s.get("isGenerated")],
            "includes": [i["path"] for g in t.get("compileGroups", []) for i in g.get("includes", [])],
        }
    for t in targets.values():
        t["deps"] = [by_id[d] for d in t["deps"] if d in by_id]
    return root, targets


# --- FF-02: purity of the domain core ----------------------------------------------------------

def check_purity(root, rules):
    problems = []
    for rel in source_files(root):
        kind, _ = classify(rel, rules)
        if kind not in ("domain", "domain-common") or is_test_path(rel):
            continue
        text = read(root, rel)
        for no, line in enumerate(text.splitlines(), 1):
            m = INCLUDE.match(line)
            if m and m.group(1) == "<" and m.group(2) in PURITY_HEADERS:
                problems.append(f"{rel}:{no}: <{m.group(2)}>: {PURITY_HEADERS[m.group(2)]}")
        code, _ = lex(text)
        for pattern, reason in PURITY_PATTERNS:
            for m in pattern.finditer(code):
                problems.append(f"{rel}:{line_of(code, m.start())}: {m.group().strip()}: {reason}")
    return problems


# --- FF-07: no SQL composed at run time --------------------------------------------------------

def sql_problem(code, start, end, variable_uses):
    """Return how the SQL literal at code[start:end] is composed at run time, or None."""
    # Also through a wrapping constructor: std::string("SELECT ...") + id, x + std::string{"WHERE ..."}.
    if re.match(r"\s*[)}]?\s*(\+(?![+=])|<<)", code[end:]) or \
       re.search(r"(\+|<<|\+=)\s*([\w:]+\s*[({]\s*)?$", code[:start]):
        return "concatenated with a run-time value"
    statement = code[max(code.rfind(";", 0, start), code.rfind("{", 0, start), code.rfind("}", 0, start)) + 1:start]
    if FORMAT_CALL.search(statement):
        return "passed to a run-time formatting call"
    m = re.search(r"\b(\w+)\s*(=|\{|\()\s*$", statement)
    if m and m.group(1) not in ("return",) and variable_uses(m.group(1)):
        return f"stored in {m.group(1)}, which is then modified or formatted"
    return None


def check_sql(root, rules):
    problems = []
    for rel in source_files(root):
        text = read(root, rel)
        lines = text.splitlines()
        code, strings = lex(text)

        def variable_uses(name):
            v = re.escape(name)
            return re.search(rf"\b{v}\s*(\+=|\+(?!\+))|[^+]\+\s*{v}\b|\b{v}\s*\.\s*(append|insert|replace)\s*\(|"
                             rf"\bv?format(_to)?\s*\(\s*{v}\b", code)

        for m in re.finditer(r'"#(\d+)"', code):
            line, value = strings[int(m.group(1))]
            if not (SQL_STATEMENT.match(value) or SQL_FRAGMENT.match(value)):
                continue
            reason = sql_problem(code, m.start(), m.end(), variable_uses)
            if not reason:
                continue
            context = lines[max(0, line - 2):line]
            if any(SQL_ALLOW.search(c) for c in context):
                continue
            problems.append(f"{rel}:{line}: SQL text {reason}; pass values as query parameters (SR-13)")
    return problems


def report(name, problems, scope):
    for p in problems:
        print(p)
    print(f"arch:{name}: {scope}, {len(problems)} problems")
    return 1 if problems else 0


def main(argv):
    if len(argv) < 3 or argv[1] not in ("deps", "purity", "sql"):
        print(__doc__, file=sys.stderr)
        return 2
    command, root = argv[1], os.path.abspath(argv[2])
    rules = load_rules()
    if command == "deps":
        if len(argv) != 4:
            print(__doc__, file=sys.stderr)
            return 2
        model_root, targets = load_codemodel(argv[3])
        problems = check_targets(targets, rules, model_root) + check_includes(root, rules)
        return report("deps", problems, f"{len(targets)} targets")
    if command == "purity":
        return report("domain-purity", check_purity(root, rules), "libs/domain")
    return report("sql", check_sql(root, rules), f"{sum(1 for _ in source_files(root))} files")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
