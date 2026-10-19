FROM rockylinux:9

# Install EPEL & CRB
RUN dnf install -y epel-release dnf-plugins-core && \
    crb enable && \
    dnf update -y

# Install dependencies including ninja-build
#
# Build tooling only. The -devel packages for libraries the package ships
# (libffi, xz, bzip2, openssl, sqlite, zlib) are deliberately absent: those come
# from vcpkg and are bundled, so the binaries stay loadable on a minimal install
# that has none of them. Adding distro headers for any of them here would be
# actively harmful -- the build would link the system copy and the dependency
# would leave the package. See cmake/bootstrap_python.cmake.
#
# ncurses-devel is the exception, and only because it cannot be avoided: it is
# pulled in transitively by the toolchain packages, so dropping it from this list
# does not remove it from the image. CPython would otherwise build _curses
# against /lib64/libncursesw.so.6; that module is disabled at the Python build
# instead, which does not depend on what the image happens to contain.
#
# krb5-devel is the other exception, and a deliberate one: the Kerberos client
# auth plugin (lib/mariadb/plugins/auth_gssapi_client.so) has to link the system
# Kerberos, so it reads the system's krb5.conf and the credential cache kinit
# writes. Only that plugin links it, and scripts/verify_package.sh allows it
# for that file alone. Without krb5-devel the Connector/C finds no krb5-config
# and the plugin isn't built.
RUN dnf install -y \
    gcc-c++ git cmake ninja-build perl-core bison zip unzip tar pkgconfig \
    ncurses-devel krb5-devel patchelf gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ \
    gcc-toolset-14-binutils gcc-toolset-14-annobin-annocheck \
    gcc-toolset-14-annobin-plugin-gcc \
    && dnf clean all

# Ensure 'ninja' command is available if the distro defaults to 'ninja-build'
RUN command -v ninja || ln -s /usr/bin/ninja-build /usr/bin/ninja

# Bake Toolset 14 Environment
ENV PATH="/opt/rh/gcc-toolset-14/root/usr/bin:${PATH}" \
    LD_LIBRARY_PATH="/opt/rh/gcc-toolset-14/root/usr/lib64:${LD_LIBRARY_PATH}" \
    CC="/opt/rh/gcc-toolset-14/root/usr/bin/gcc" \
    CXX="/opt/rh/gcc-toolset-14/root/usr/bin/g++"

WORKDIR /workspace/src

ENTRYPOINT ["/workspace/src/docker/entrypoint.sh"]