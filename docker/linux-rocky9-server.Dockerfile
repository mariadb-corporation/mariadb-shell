FROM rockylinux:9

# Install EPEL & CRB
RUN dnf install -y epel-release dnf-plugins-core && \
    crb enable && \
    dnf update -y

# MariaDB sandbox server build dependencies.
#
# WITH_PCRE=bundled and the excluded plugins/features (RocksDB, Mroonga,
# Connect, OQGraph, mariabackup, wsrep, embedded server) keep this list short:
# no pcre-devel, no boost-devel (extra/boost ships bundled in the source
# tree), no judy/snappy/lz4-devel.
#
# No openssl-devel: the server builds against the OpenSSL built below, which
# the package ships. Leaving the system one out means a build that failed to
# pick ours up fails at configure time (cmake's SSL detection falls through to
# GnuTLS, "Could NOT find GnuTLS") instead of silently linking Rocky's.
RUN dnf install -y \
    gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils \
    gcc-toolset-14-annobin-annocheck gcc-toolset-14-annobin-plugin-gcc \
    git cmake ninja-build bison perl-core ccache \
    ncurses-devel libaio-devel libxml2-devel \
    libxcrypt-static cpio \
    zip unzip tar pkgconfig patchelf \
    && dnf clean all

# Ensure 'ninja' command is available if the distro defaults to 'ninja-build'
RUN command -v ninja || ln -s /usr/bin/ninja-build /usr/bin/ninja

# Static libaio.a, so the sandbox binary doesn't depend on libaio.so at
# runtime (it's more portable that way: libaio isn't installed by default on
# every distro/version, and the .so it links carries a distro-specific
# SONAME). libaio-devel only ships libaio.so + headers here -- no .a -- so
# build the archive from libaio's own upstream Makefile, which does produce
# one, using the same source RPM as the installed libaio-devel to keep them
# in lockstep. libxcrypt-static (installed above) gives us the matching
# /usr/lib64/libcrypt.a for the same reason; see entrypoint-sandbox-server.sh
# for how both are actually linked in.
RUN dnf download --source -y libaio --destdir /tmp/libaio-src && \
    cd /tmp/libaio-src && \
    rpm2cpio libaio-*.src.rpm | cpio -idmv && \
    tar xzf libaio-*.tar.gz && \
    make -C libaio-*/src libaio.a && \
    mkdir -p /opt/static-libs && \
    cp libaio-*/src/libaio.a /opt/static-libs/libaio.a && \
    cd / && rm -rf /tmp/libaio-src

# Bake Toolset 14 Environment
ENV PATH="/opt/rh/gcc-toolset-14/root/usr/bin:${PATH}" \
    LD_LIBRARY_PATH="/opt/rh/gcc-toolset-14/root/usr/lib64:${LD_LIBRARY_PATH}" \
    CC="/opt/rh/gcc-toolset-14/root/usr/bin/gcc" \
    CXX="/opt/rh/gcc-toolset-14/root/usr/bin/g++"

# OpenSSL for the server, built here and shipped in the package's lib/.
#
# The packages run on every glibc >= 2.34 distro, and those do not agree on
# OpenSSL: Fedora 45 moved to OpenSSL 4 (libssl.so.4) and has libssl.so.3 only
# in a compat package, and minimal images such as debian:12 have no libssl at
# all. So the server cannot link the system OpenSSL, any more than libaio or
# libcrypt (above). It is built shared, not static: with a static OpenSSL the
# server's CMake would have to build auth_mysql_sha2 and auth_parsec into
# mariadbd (as it does for bundled wolfSSL) to keep a second copy out of the
# process, while shared, the plugins stay .so files and reuse the libssl.so.3
# mariadbd already loaded. entrypoint-sandbox-server.sh copies the libraries
# into the package and points every binary at them with an RPATH.
#
# --openssldir names a directory that exists on no user's machine, so OpenSSL
# reads no host openssl.cnf and runs on its built-in defaults everywhere. With
# Rocky's own libcrypto it would read /etc/pki/tls/openssl.cnf, which on
# Fedora 45 is written for OpenSSL 4 and on RHEL pulls in crypto-policies.
# The server needs no OpenSSL config file: its TLS settings are its own
# options. no-module: no provider .so files, the default provider is built in.
#
# The version is pinned, so a security fix in OpenSSL means bumping it here
# and rebuilding the sandbox servers. The SHA-256 is the one published with the
# release, checked against an independent download.
ARG OPENSSL_VERSION=3.5.9
ARG OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
RUN mkdir -p /tmp/openssl-src && cd /tmp/openssl-src && \
    curl -fsSL -o openssl.tar.gz \
      "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" && \
    echo "${OPENSSL_SHA256}  openssl.tar.gz" | sha256sum -c - && \
    tar xzf openssl.tar.gz && cd "openssl-${OPENSSL_VERSION}" && \
    ./Configure --prefix=/opt/sandbox-openssl --libdir=lib \
      --openssldir=/opt/mariadb-sandbox-openssl-no-config \
      shared no-module no-tests no-docs no-apps && \
    make -j"$(nproc)" && make install_sw && \
    cd / && rm -rf /tmp/openssl-src

# The entrypoint is baked into the image rather than resolved through the
# bind-mounted source tree (as docker/entrypoint.sh is for the shell build):
# the source bind-mounted at /workspace/src here is the MariaDB/server repo,
# not this one, so it does not carry this script.
COPY docker/entrypoint-sandbox-server.sh /usr/local/bin/entrypoint-sandbox-server.sh
RUN chmod +x /usr/local/bin/entrypoint-sandbox-server.sh

# Baked in for the same reason, and used by the entrypoint's packaging step.
COPY scripts/prune_sandbox_server.sh /usr/local/bin/prune_sandbox_server.sh
RUN chmod +x /usr/local/bin/prune_sandbox_server.sh

# Same again for the self-containment check the entrypoint runs last.
COPY scripts/check_sandbox_server_deps.sh /usr/local/bin/check_sandbox_server_deps.sh
RUN chmod +x /usr/local/bin/check_sandbox_server_deps.sh

WORKDIR /workspace/src

ENTRYPOINT ["/usr/local/bin/entrypoint-sandbox-server.sh"]
