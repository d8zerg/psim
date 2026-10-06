# PSIM C++ toolchain image (ADR-032): clang 21 + libc++ 21, CMake, Ninja, Conan 2, ccache, buf, Task.
# Based on Debian 12 - the oldest target glibc (2.36); artifacts run on Debian 12, Astra Linux 1.8
# and newer distributions and are verified there by `task cpp:runtime-test`.
# Built by: task toolchain:build (the base image digest is pinned in the root Taskfile)
ARG BASE=debian:bookworm
ARG BUF_IMAGE=bufbuild/buf:1.73.0

FROM ${BUF_IMAGE} AS buf

FROM ${BASE}
ARG LLVM_VERSION=21
ARG CMAKE_VERSION=4.4.4
ARG NINJA_VERSION=1.13.2
ARG CONAN_VERSION=2.33.0
ARG TASK_VERSION=3.54.0

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg git make patch xz-utils bzip2 unzip file \
      python3 python3-venv python3-pip pkg-config perl ccache \
 && rm -rf /var/lib/apt/lists/*

# LLVM from apt.llvm.org (bookworm repository; Astra Linux 1.8 is binary compatible with Debian 12).
RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-${LLVM_VERSION} main" \
      > /etc/apt/sources.list.d/llvm.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      clang-${LLVM_VERSION} clang-tidy-${LLVM_VERSION} clang-format-${LLVM_VERSION} clang-tools-${LLVM_VERSION} \
      lld-${LLVM_VERSION} llvm-${LLVM_VERSION} libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev \
      libclang-rt-${LLVM_VERSION}-dev \
 && rm -rf /var/lib/apt/lists/* \
 && for t in clang clang++ clang-tidy clang-format clang-apply-replacements run-clang-tidy ld.lld \
             llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-strip llvm-cov llvm-profdata llvm-symbolizer; do \
      if [ -e /usr/bin/$t-${LLVM_VERSION} ]; then ln -sf /usr/bin/$t-${LLVM_VERSION} /usr/local/bin/$t; \
      elif [ -e /usr/lib/llvm-${LLVM_VERSION}/bin/$t ]; then ln -sf /usr/lib/llvm-${LLVM_VERSION}/bin/$t /usr/local/bin/$t; fi; \
    done

# CMake, Ninja, Conan in a virtual environment with pinned versions.
RUN python3 -m venv /opt/psim-venv \
 && /opt/psim-venv/bin/pip install --no-cache-dir \
      cmake==${CMAKE_VERSION} ninja==${NINJA_VERSION} conan==${CONAN_VERSION} pyyaml
ENV PATH=/opt/psim-venv/bin:${PATH}

# buf for contract code generation inside the toolchain.
COPY --from=buf /usr/local/bin/buf /usr/local/bin/buf

# Task, verified against the release checksum file.
RUN cd /tmp \
 && curl -fsSLO https://github.com/go-task/task/releases/download/v${TASK_VERSION}/task_linux_amd64.tar.gz \
 && curl -fsSLO https://github.com/go-task/task/releases/download/v${TASK_VERSION}/task_checksums.txt \
 && grep ' task_linux_amd64.tar.gz$' task_checksums.txt | sha256sum -c - \
 && tar -xzf task_linux_amd64.tar.gz -C /usr/local/bin task \
 && rm -f task_linux_amd64.tar.gz task_checksums.txt

# Non-root user for the dev container; `docker run -u` with the host UID works as well.
ARG USER_UID=1000
RUN useradd --create-home --uid ${USER_UID} --shell /bin/bash dev \
 && mkdir -p /cache/conan /cache/ccache /cache/home && chown -R dev /cache

ENV CC=clang CXX=clang++ PSIM_IN_TOOLCHAIN=1 \
    CONAN_HOME=/cache/conan CCACHE_DIR=/cache/ccache CCACHE_BASEDIR=/repo CCACHE_COMPILERCHECK=content
WORKDIR /repo
