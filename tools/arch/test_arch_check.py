"""Tests of the architecture fitness functions: each rule must catch its violation.

Run: task arch:selftest (python3 -m unittest discover -s tools/arch).
"""

import os
import tempfile
import textwrap
import unittest

import arch_check

RULES = arch_check.load_rules()


class Tree:
    """Temporary repository tree with C++ sources."""

    def __init__(self, files):
        self._dir = tempfile.TemporaryDirectory()
        self.root = self._dir.name
        for rel, text in files.items():
            path = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(textwrap.dedent(text))

    def __enter__(self):
        return self.root

    def __exit__(self, *exc):
        self._dir.cleanup()


def target(directory, deps=(), sources=("src/a.cpp",), includes=()):
    return {"dir": directory, "deps": list(deps), "sources": [f"{directory}/{s}" for s in sources],
            "includes": list(includes)}


class TargetGraph(unittest.TestCase):
    def check(self, targets):
        return arch_check.check_targets(targets, RULES, "/repo")

    def test_allowed_graph_passes(self):
        targets = {
            "common": target("libs/domain/common"),
            "incident": target("libs/domain/incident", ["common"], includes=["/repo/libs/domain/incident/include"]),
            "incident_test": target("libs/domain/incident", ["incident", "sim"], sources=["tests/a_test.cpp"]),
            "core": target("libs/platform/core"),
            "kafka": target("libs/platform/kafka", ["core", "contracts"]),
            "contracts": target("contracts"),
            "sim": target("libs/sim", ["core", "common"]),
            "svc": target("services/incident-service", ["incident", "kafka", "contracts", "common"]),
            "psimctl": target("tools/psimctl", ["core", "contracts"]),
        }
        self.assertEqual(self.check(targets), [])

    def test_domain_on_infrastructure_fails(self):
        targets = {"incident": target("libs/domain/incident", ["kafka"]), "kafka": target("libs/platform/kafka")}
        self.assertIn("domain may not depend on platform", self.check(targets)[0])

    def test_domain_on_other_context_fails(self):
        targets = {"incident": target("libs/domain/incident", ["catalog"]), "catalog": target("libs/domain/catalog")}
        self.assertIn("domain may not depend on domain", self.check(targets)[0])

    def test_domain_with_external_include_dir_fails(self):
        targets = {"incident": target("libs/domain/incident", includes=["/cache/conan/boost/include"])}
        self.assertIn("external include directory", self.check(targets)[0])

    def test_domain_test_may_use_sim_but_library_may_not(self):
        targets = {"incident": target("libs/domain/incident", ["sim"]), "sim": target("libs/sim")}
        self.assertIn("domain may not depend on sim", self.check(targets)[0])

    def test_service_on_service_fails(self):
        targets = {"a": target("services/incident-service", ["b"]), "b": target("services/audit-service")}
        self.assertIn("service may not depend on service", self.check(targets)[0])

    def test_service_on_foreign_domain_fails(self):
        targets = {"svc": target("services/incident-service", ["catalog"]), "catalog": target("libs/domain/catalog")}
        self.assertIn("not catalog", self.check(targets)[0])

    def test_unknown_service_fails(self):
        targets = {"svc": target("services/new-service", ["incident"]), "incident": target("libs/domain/incident")}
        self.assertIn("not listed", self.check(targets)[0])

    def test_platform_on_service_fails(self):
        targets = {"core": target("libs/platform/core", ["svc"]), "svc": target("services/audit-service")}
        self.assertIn("platform may not depend on service", self.check(targets)[0])

    def test_target_outside_modules_fails(self):
        self.assertIn("outside every module", self.check({"x": target("misc")})[0])


class Includes(unittest.TestCase):
    def check(self, files):
        with Tree(files) as root:
            return arch_check.check_includes(root, RULES)

    def test_allowed_includes_pass(self):
        problems = self.check({
            "libs/domain/incident/src/a.cpp": """
                #include "psim/domain/incident/incident.hpp"
                #include "psim/domain/common/ids.hpp"
                #include <expected>
                #include <cstdint>
            """,
            "libs/domain/incident/tests/a_test.cpp": '#include <gtest/gtest.h>\n#include "psim/sim/clock.hpp"\n',
            "services/incident-service/src/main.cpp": """
                #include "psim/domain/incident/incident.hpp"
                #include "psim/platform/runtime.hpp"
                #include "psim/incident/v1/events.pb.h"
                #include <boost/asio.hpp>
            """,
        })
        self.assertEqual(problems, [])

    def test_domain_third_party_header_fails(self):
        problems = self.check({"libs/domain/incident/src/a.cpp": "#include <boost/asio.hpp>\n"})
        self.assertIn("not a standard library header", problems[0])

    def test_domain_platform_header_fails(self):
        problems = self.check({"libs/domain/incident/src/a.cpp": '#include "psim/platform/kafka.hpp"\n'})
        self.assertIn("domain may not depend on platform", problems[0])

    def test_domain_contract_header_fails(self):
        problems = self.check({"libs/domain/incident/a.hpp": '#include "psim/incident/v1/events.pb.h"\n'})
        self.assertIn("domain may not depend on contracts", problems[0])

    def test_domain_other_context_header_fails(self):
        problems = self.check({"libs/domain/incident/a.hpp": '#include "psim/domain/catalog/site.hpp"\n'})
        self.assertIn("domain may not depend on domain", problems[0])

    def test_service_foreign_domain_header_fails(self):
        problems = self.check({"services/audit-service/a.cpp": '#include "psim/domain/incident/incident.hpp"\n'})
        self.assertIn("not incident", problems[0])


class Purity(unittest.TestCase):
    def check(self, text, rel="libs/domain/incident/src/a.cpp"):
        with Tree({rel: text}) as root:
            return arch_check.check_purity(root, RULES)

    def test_pure_code_passes(self):
        self.assertEqual(self.check("""
            #include <chrono>
            #include <expected>
            // throw, std::chrono::system_clock and rand() in comments are fine
            std::expected<int, Error> f(Timestamp time, const Clock& clock) noexcept {
              const char* text = "try catch throw system_clock";
              return entry.time() + clock.now() + 1'000'000;
            }
        """), [])

    def test_violations_fail(self):
        cases = {
            "auto t = std::chrono::system_clock::now();": "system clock",
            "auto t = std::chrono::steady_clock::now();": "system clock",
            "auto t = std::time(nullptr);": "system time",
            "int r = rand();": "randomness",
            "std::mt19937 gen;": "randomness",
            "std::cout << x;": "I/O",
            "std::println(\"{}\", x);": "I/O",
            "throw Error{};": "exceptions",
            "try { f(); } catch (...) {}": "exceptions",
            "std::thread t;": "threads",
        }
        for code, reason in cases.items():
            with self.subTest(code=code):
                problems = self.check(f"void f() {{ {code} }}\n")
                self.assertTrue(problems and reason in problems[0], problems)

    def test_forbidden_headers_fail(self):
        for header in ("random", "iostream", "thread", "ctime", "filesystem"):
            with self.subTest(header=header):
                self.assertIn(f"<{header}>", self.check(f"#include <{header}>\n")[0])

    def test_domain_tests_are_not_checked(self):
        self.assertEqual(self.check("#include <random>\nthrow 1;\n", "libs/domain/incident/tests/a_test.cpp"), [])

    def test_platform_is_not_checked(self):
        self.assertEqual(self.check("auto t = std::chrono::system_clock::now();\n", "libs/platform/core/a.cpp"), [])


class Sql(unittest.TestCase):
    def check(self, text):
        with Tree({"services/incident-service/src/repo.cpp": text}) as root:
            return arch_check.check_sql(root, RULES)

    def test_parameterized_queries_pass(self):
        self.assertEqual(self.check("""
            constexpr std::string_view kSelect = "SELECT id FROM incident.incidents WHERE tenant_id = $1";
            constexpr auto kInsert = R"sql(
              INSERT INTO incident.incidents (id, tenant_id) VALUES ($1, $2)
            )sql";
            auto r = co_await db.query(kSelect, tenant_id);
            auto q = db.query("SELECT 1 " "FROM t");
            log.info("loaded " + std::to_string(n) + " incidents");
        """), [])

    def test_composed_sql_fails(self):
        cases = [
            'auto q = "SELECT * FROM t WHERE id = " + id;',
            'auto q = std::string("DELETE FROM t WHERE id = ") + id;',
            'auto q = prefix + "WHERE name = \'" + name + "\'";',
            'auto q = std::format("SELECT * FROM {} WHERE id = {}", table, id);',
            'std::string q = "UPDATE t SET a = 1 WHERE id = "; q += id;',
            'std::string q = "SELECT a FROM t"; q.append(filter);',
            'out << "INSERT INTO t VALUES (" << value << ")";',
            'auto q = R"(SELECT * FROM t WHERE name = )" + name;',
        ]
        for code in cases:
            with self.subTest(code=code):
                self.assertTrue(self.check(code + "\n"), code)

    def test_justified_suppression_passes(self):
        self.assertEqual(self.check("""
            // psim-arch: allow-sql(partition name is generated from a validated date, not user input)
            auto q = std::format("CREATE TABLE audit.records_{} PARTITION OF audit.records", day);
        """), [])

    def test_suppression_without_reason_fails(self):
        self.assertTrue(self.check("""
            // psim-arch: allow-sql()
            auto q = std::format("CREATE TABLE audit.records_{} PARTITION OF audit.records", day);
        """))


class Lexer(unittest.TestCase):
    def test_comments_strings_and_digit_separators(self):
        code, strings = arch_check.lex('int a = 1\'000; // throw\nauto s = "x\\"y"; /* try\n */ char c = \'"\';\n')
        self.assertNotIn("throw", code)
        self.assertNotIn("try", code)
        self.assertEqual(strings, [(2, 'x\\"y')])
        self.assertEqual(code.count("\n"), 3)


if __name__ == "__main__":
    unittest.main()
