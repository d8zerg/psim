"""Conan 2 dependencies of the PSIM C++ code base (ADR-032).

Dependencies are built from source with the project profile (clang 21, libc++, C++23) and always
in Release; the project itself is built in any configuration (CMakePresets.json). Versions are
pinned here and, with recipe revisions, in conan.lock. Steps add their dependencies here.
"""

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class PsimConan(ConanFile):
    name = "psim"
    settings = "os", "arch", "compiler", "build_type"
    default_options = {"*:shared": False}

    PROTOBUF = "protobuf/6.33.5"
    GRPC = "grpc/1.84.0"

    def requirements(self):
        self.requires(self.PROTOBUF)
        self.requires(self.GRPC)
        self.requires("openssl/3.5.9")

    def build_requirements(self):
        # Test-only libraries: linked into tests and benchmarks, never shipped, not in the SBOM.
        self.test_requires("gtest/1.18.0")
        self.test_requires("benchmark/1.9.5")
        # protoc and grpc_cpp_plugin run at build time.
        self.tool_requires(self.PROTOBUF)
        self.tool_requires(self.GRPC)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False  # the project has its own CMakePresets.json
        tc.generate()
        CMakeDeps(self).generate()
