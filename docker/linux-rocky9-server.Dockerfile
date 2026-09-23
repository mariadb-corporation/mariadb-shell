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
# openssl-devel is required for SSL support: without it, cmake's SSL library
# detection falls through to GnuTLS and fails there instead ("Could NOT find
# GnuTLS"), since gnutls-devel isn't installed either (nor wanted -- this
# build targets OpenSSL).
RUN dnf install -y \
    gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils \
    gcc-toolset-14-annobin-annocheck gcc-toolset-14-annobin-plugin-gcc \
    git cmake ninja-build bison perl-core ccache \
    ncurses-devel libaio-devel libxml2-devel openssl-devel \
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

# The entrypoint is baked into the image rather than resolved through the
# bind-mounted source tree (as docker/entrypoint.sh is for the shell build):
# the source bind-mounted at /workspace/src here is the MariaDB/server repo,
# not this one, so it does not carry this script.
COPY docker/entrypoint-sandbox-server.sh /usr/local/bin/entrypoint-sandbox-server.sh
RUN chmod +x /usr/local/bin/entrypoint-sandbox-server.sh

# Baked in for the same reason, and used by the entrypoint's packaging step.
COPY scripts/prune_sandbox_server.sh /usr/local/bin/prune_sandbox_server.sh
RUN chmod +x /usr/local/bin/prune_sandbox_server.sh

WORKDIR /workspace/src

ENTRYPOINT ["/usr/local/bin/entrypoint-sandbox-server.sh"]
