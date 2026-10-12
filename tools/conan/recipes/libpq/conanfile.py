"""Pinned PostgreSQL client recipe until Conan Center provides libpq 18 (ADR-014)."""

import os

from conan import ConanFile
from conan.tools.files import copy, get
from conan.tools.gnu import Autotools, AutotoolsDeps, AutotoolsToolchain
from conan.tools.layout import basic_layout


class LibpqConan(ConanFile):
    name = "libpq"
    version = "18.6"
    license = "PostgreSQL"
    homepage = "https://www.postgresql.org/docs/18/libpq.html"
    description = "PostgreSQL client library with TLS and nonblocking pipeline support"
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"

    def layout(self):
        basic_layout(self, src_folder="src")

    def requirements(self):
        self.requires("openssl/3.5.9")

    def build_requirements(self):
        self.tool_requires("bison/3.8.2")
        self.tool_requires("flex/2.6.4")

    def source(self):
        get(self, url="https://ftp.postgresql.org/pub/source/v18.6/postgresql-18.6.tar.bz2",
            sha256="555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f", strip_root=True)

    def generate(self):
        tc = AutotoolsToolchain(self)
        tc.configure_args += ["--with-ssl=openssl", "--without-readline", "--without-icu", "--without-zlib",
                              "--without-ldap", "--without-gssapi", "--disable-nls"]
        tc.extra_cflags += ["-fPIC"]
        tc.generate()
        AutotoolsDeps(self).generate()

    def build(self):
        autotools = Autotools(self)
        autotools.configure()
        autotools.make(target="submake-libpgport", args=["-C", "src/interfaces/libpq"])
        autotools.make(target="libpq.a", args=["-C", "src/interfaces/libpq"])

    def package(self):
        copy(self, "COPYRIGHT", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "libpq.a", os.path.join(self.build_folder, "src/interfaces/libpq"),
             os.path.join(self.package_folder, "lib"), keep_path=False)
        for header in ("libpq-fe.h", "libpq-events.h"):
            copy(self, header, os.path.join(self.source_folder, "src/interfaces/libpq"),
                 os.path.join(self.package_folder, "include"), keep_path=False)
        for folder in ("src/common", "src/port"):
            copy(self, "*_shlib.a", os.path.join(self.build_folder, folder), os.path.join(self.package_folder, "lib"),
                 keep_path=False)
        for header in ("postgres_ext.h", "pg_config_ext.h"):
            for folder in (self.source_folder, self.build_folder):
                copy(self, header, os.path.join(folder, "src/include"), os.path.join(self.package_folder, "include"),
                     keep_path=False)
        # Ship the static client and headers only; runtime dependencies are OpenSSL and libc.
        for name in os.listdir(os.path.join(self.package_folder, "lib")):
            if ".so" in name:
                os.remove(os.path.join(self.package_folder, "lib", name))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "PostgreSQL")
        self.cpp_info.set_property("cmake_target_name", "PostgreSQL::PostgreSQL")
        self.cpp_info.libs = ["pq", "pgcommon_shlib", "pgport_shlib"]
        self.cpp_info.system_libs = ["pthread", "m", "dl"]
