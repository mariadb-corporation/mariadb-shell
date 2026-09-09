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
    zip unzip tar pkgconfig patchelf \
    && dnf clean all

# Ensure 'ninja' command is available if the distro defaults to 'ninja-build'
RUN command -v ninja || ln -s /usr/bin/ninja-build /usr/bin/ninja

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

WORKDIR /workspace/src

ENTRYPOINT ["/usr/local/bin/entrypoint-sandbox-server.sh"]
