"""Conan 2 dependencies of the PSIM C++ code base (ADR-032).

Dependencies are built from source with the project profile (clang 21, system libstdc++, C++23) and always
in Release; the project itself is built in any configuration (CMakePresets.json). Versions are
pinned here and, with recipe revisions, in conan.lock. Steps add their dependencies here.
"""

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class PsimConan(ConanFile):
    name = "psim"
    settings = "os", "arch", "compiler", "build_type"
    default_options = {
        "*:shared": False,
        # Service runtime (ADR-039): Asio and Beast are header-only.
        "boost/*:header_only": True,
        # Traces leave over OTLP gRPC only (gRPC is already a dependency); no libcurl.
        "opentelemetry-cpp/*:with_otlp_grpc": True,
        "opentelemetry-cpp/*:with_otlp_http": False,
        "opentelemetry-cpp/*:with_zipkin": False,
        # Metrics are served by the runtime's own /metrics endpoint: only the prometheus-cpp core.
        "prometheus-cpp/*:with_pull": False,
        "prometheus-cpp/*:with_push": False,
        "prometheus-cpp/*:with_compression": False,
        # Kafka layer (ADR-041): zstd compression, TLS and SCRAM over OpenSSL;
        # no Cyrus SASL (GSSAPI) and no libcurl (OIDC token fetch).
        "librdkafka/*:zstd": True,
        "librdkafka/*:ssl": True,
        "librdkafka/*:sasl": False,
        "librdkafka/*:curl": False,
        "clickhouse-cpp/*:with_openssl": True,
        "rocksdb/*:with_zstd": True,
        "aws-sdk-cpp/*:s3": True,
        "aws-sdk-cpp/*:access-management": False,
        "aws-sdk-cpp/*:identity-management": False,
        "aws-sdk-cpp/*:queues": False,
        "aws-sdk-cpp/*:s3-encryption": False,
        "aws-sdk-cpp/*:text-to-speech": False,
        "aws-sdk-cpp/*:transfer": False,
        "aws-sdk-cpp/*:monitoring": False,
    }

    PROTOBUF = "protobuf/6.33.5"
    GRPC = "grpc/1.84.0"

    def requirements(self):
        self.requires(self.PROTOBUF)
        self.requires(self.GRPC)
        self.requires("openssl/3.5.9")
        # Service runtime (ADR-039).
        self.requires("boost/1.90.0")
        self.requires("opentelemetry-cpp/1.26.0")
        self.requires("prometheus-cpp/1.3.0")
        self.requires("yaml-cpp/0.9.0")
        self.requires("nlohmann_json/3.11.3")  # the version json-schema-validator requires
        self.requires("json-schema-validator/2.4.0")
        # Kafka layer (ADR-041): KIP-848 consumer protocol and transactions.
        self.requires("librdkafka/2.14.2")
        # PostgreSQL layer (ADR-014): static libpq with TLS.
        self.requires("libpq/18.6")
        # ClickHouse layer (ADR-045): native protocol with TLS.
        self.requires("clickhouse-cpp/2.6.1")
        self.requires("aws-sdk-cpp/1.11.692")
        self.requires("libpng/1.6.58")
        self.requires("libjpeg-turbo/3.2.0")
        self.requires("rocksdb/10.10.1")

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
