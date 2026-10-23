"""Prove scope and authentication drift is rejected."""
from pathlib import Path
import tempfile
import unittest

from authz_check import check


class AuthorizationFitness(unittest.TestCase):
    def fixture(self, source, test=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "services" / "example" / ("tests" if test else "adapters") / "boundary.cpp"
            path.parent.mkdir(parents=True)
            path.write_text(source)
            return check(root)

    def test_scope_membership_and_local_parsing_are_rejected(self):
        for source in ('p.scopes.contains("*")', 'p.scopes.empty()', 'scopes.find(site)', 'scope.starts_with("site:")', "'site:' || resource.site"):
            with self.subTest(source=source):
                self.assertTrue(self.fixture(source))

    def test_verification_cannot_omit_online_configuration(self):
        self.assertTrue(self.fixture('JwksCache::create(options);'))
        self.assertFalse(self.fixture('stream::authentication_options(context); JwksCache::create(options);'))

    def test_shared_policy_and_test_fixtures_are_allowed(self):
        self.assertFalse(self.fixture('authz::scoped(p, resource); authz::scope_filter(p);'))
        self.assertFalse(self.fixture('p.scopes.contains("*")', test=True))
