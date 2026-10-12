# MariaDB Shell runtime image, made for running the MCP server in multi-tenant
# mode (see docker/README.md).
#
# Nothing is compiled here: the image unpacks the released Linux tarball of
# VERSION for the platform being built. Those are built on Rocky 9 against
# glibc 2.34 (rocky-linux.yml) and bundle Python and every library they need,
# so building for a foreign architecture under QEMU only runs curl and tar.
#
#   docker buildx build --platform linux/amd64,linux/arm64 \
#     --build-arg VERSION=26.10.1 -f docker/mariadb-shell.Dockerfile docker
#
# RELEASE_TAG names the GitHub release when its tag is not v<VERSION>.

ARG BASE_IMAGE=debian:13-slim

FROM ${BASE_IMAGE} AS fetch

ARG VERSION
ARG RELEASE_TAG
ARG TARGETARCH

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

# The glibc floor is part of the file name, so it is looked up in SHA256SUMS
# rather than spelled out here, where it would silently go stale.
RUN set -eu; \
    : "${VERSION:?pass --build-arg VERSION=<release, e.g. 26.10.1>}"; \
    case "$TARGETARCH" in \
      amd64) arch=x86-64bit ;; \
      arm64) arch=arm-64bit ;; \
      *) echo "unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac; \
    base="https://github.com/mariadb-corporation/mariadb-shell/releases/download/${RELEASE_TAG:-v${VERSION}}"; \
    cd /tmp; \
    curl -fsSL --retry 3 -o SHA256SUMS "$base/SHA256SUMS"; \
    grep -E "  mariadb-shell-${VERSION}-linux-glibc[0-9.]+-${arch}\.tar\.gz$" SHA256SUMS > tarball.sha256; \
    [ "$(wc -l < tarball.sha256)" -eq 1 ] || { echo "no single Linux $arch tarball in SHA256SUMS" >&2; exit 1; }; \
    name=$(awk '{print $2}' tarball.sha256); \
    curl -fsSL --retry 3 -o "$name" "$base/$name"; \
    sha256sum -c tarball.sha256; \
    mkdir -p /opt/mariadb-shell; \
    tar -xzf "$name" -C /opt/mariadb-shell --strip-components=1


FROM ${BASE_IMAGE}

ARG VERSION

LABEL org.opencontainers.image.title="MariaDB Shell" \
      org.opencontainers.image.description="MariaDB Shell with the MariaDB MCP server" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.source="https://github.com/mariadb-corporation/mariadb-shell" \
      org.opencontainers.image.vendor="MariaDB plc" \
      org.opencontainers.image.licenses="GPL-2.0-only"

# - locales: the shell sets LC_ALL=en_US.UTF-8 at startup (src/mysqlsh/main.cc)
#   and warns on every run when the locale does not exist.
# - ca-certificates: the bundled OpenSSL looks in /etc/ssl/certs, which is how
#   the server reaches Keycloak and login servers over TLS.
# - tini: the shell, as PID 1, would not get the default signal handlers, so a
#   `podman stop` would wait for the timeout and then kill it.
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates locales tini \
    && rm -rf /var/lib/apt/lists/* \
    && sed -i 's/^# *\(en_US.UTF-8 UTF-8\)/\1/' /etc/locale.gen \
    && locale-gen \
    && useradd --uid 10001 --user-group --create-home --home-dir /home/mcp \
               --shell /usr/sbin/nologin mcp \
    && chgrp 0 /home/mcp && chmod g=u /home/mcp

COPY --from=fetch /opt/mariadb-shell /opt/mariadb-shell
COPY mcp-entrypoint.sh /usr/local/bin/mcp-entrypoint.sh

ENV PATH=/opt/mariadb-shell/bin:$PATH \
    LANG=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8 \
    HOME=/home/mcp

# The volume is the whole home directory, not just ~/.mariadb-shell: the
# login-path credential store keeps the users' API keys and connection
# passwords in ~/.mylogin.cnf. HOME is set, and the directory belongs to group
# 0 as well, for a container started under another uid (docker run --user,
# OpenShift's random uid in group 0), which has no passwd entry to take HOME
# from.
USER mcp
WORKDIR /home/mcp
VOLUME /home/mcp
EXPOSE 8080

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/mcp-entrypoint.sh"]
CMD ["start-server"]
