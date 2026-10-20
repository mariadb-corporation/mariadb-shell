# mariadb-lsp: the SQL language server in the shell's packages

Part of [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md).

`mariadb-lsp`, the MariaDB SQL language server, comes from the mariadb-corporation/lsp-server repository, the `src/lsp-server` submodule here. The shell builds it and installs it next to `mariadb-shell`; lsp-server makes no releases of its own. mariadb-shell-plugins' `mcp_plugin` runs it for its `sql` tools (`sql.check`, `sql.outline`, `sql.format`) and looks for it next to `mariadb-shell` (after its `lspPath` setting, before the PATH).

## Design

- **The pin is the gitlink** of `src/lsp-server` (`git ls-tree HEAD src/lsp-server`); `.gitmodules` only names the path and URL. Code and tests are maintained in lsp-server, whose CI covers the shell's platforms and triplets; a shell pull request moves the pin.
- **Built as a separate CMake build** by `cmake/lsp_server.cmake` (`ExternalProject`), not `add_subdirectory`: lsp-server bootstraps vcpkg before `project()` with its own manifest, keeps `-Werror` off MariaDB's extracted lexer, and has cmake modules named like the shell's. It gets the shell's compiler, compiler launchers, vcpkg toolchain and triplet, bison, macOS deployment target, and MariaDB server tree, with `-DWITH_TESTS=0 -DMDBLS_CHECK_SERVER_PIN=OFF` (without a server tree, `-DWITH_MARIADB_CLIENT=OFF`). It builds only the `mariadb-lsp` target, `BUILD_ALWAYS`, with `LSP_SERVER_BUILD_JOBS` jobs (default half the cores, as it runs beside the shell's build).
- **Bundled with `install_bundled_binaries`** into `INSTALL_BINDIR`: the RPATH of the other bundled binaries (`$ORIGIN/../lib/mariadb-shell`, `@loader_path/../lib/mariadb-shell`), so it loads the package's OpenSSL and zlib; on Windows the DLLs are next to it in `bin`. `CPACK_STRIP_FILES` and `scripts/verify_package.sh` name it.
- **`WITH_LSP=AUTO|ON|OFF`**: `AUTO` (default) builds it when the submodule is checked out, so a fork's pull request or a clone without submodules builds without it; release builds (`shell-release.yml`, the Rocky container through `docker/entrypoint.sh`) pass `ON`, so a missing submodule fails them. `LSP_SERVER_SOURCE_DIR` builds another lsp-server checkout. The native `.deb` and `.rpm` builds pass `OFF`: their file lists (`packaging/debian/CMakeLists.txt`'s `.install`, the spec's `%files`) name every installed file, and their build against system libraries is not prepared for it (see Next steps).
- **Guards:** configure warns when the submodule is not at the pinned commit; it fails when `src/lsp-server/vcpkg.json` differs from `vcpkg.json` in `builtin-baseline` or the `openssl` and `zlib` overrides, since `mariadb-lsp` links the ones the package bundles. The `lsp-server-pin` job of `shell-ci.yml` fails a pull request that moves the pin back (GitHub's compare API says `behind` or `diverged`), unless its title has `[lsp-server-downgrade]`.
- **Fetching the submodule in CI:** the workflows that build (`shell-ci.yml`, `shell-release.yml`, `rocky-linux.yml`) fetch it in a step of their own with `DEVTOOLS_TOKEN`, which must be able to read lsp-server; `rocky-linux.yml` does it on the host, before the container mounts the tree.

## Gotchas

- A build directory configured before the submodule moved fails ("does not match the source used to generate cache"): delete its `lsp-server` and `lsp_server-prefix` subdirectories.
- After a pull or branch switch that moves the pin, run `git submodule update` (or set `submodule.recurse true`), or the build uses the old commit; configure warns.
- Changes are kept in step with lsp-server and mariadb-shell-plugins: a lsp-server change that affects the build (options, vcpkg manifest, bison or compiler minimums, linked libraries) or the CLI contract `mcp_plugin` relies on lands with the matching change here and there.

## Next steps

1. **When lsp-server becomes public:**
   - Replace the "Fetch the lsp-server submodule" steps with `submodules: true` on the `actions/checkout` steps, and drop `DEVTOOLS_TOKEN` from the `lsp-server-pin` job.
   - Ship `mariadb-lsp` in the native `.deb` and `.rpm` packages: pass `-DWITH_LSP=ON` in `packaging/debian/rules.in` and `packaging/rpm/mariadb-shell.spec.in`; add `usr/bin/mariadb-lsp` to the `.install` file written by `packaging/debian/CMakeLists.txt` and `%{_bindir}/mariadb-lsp` to `%files`; add build dependencies for RapidJSON headers and bison 3.7.4 or newer; decide whether those builds get a MariaDB server tree for its client library or build it without live connections (`WITH_MARIADB_CLIENT=OFF`, the fallback without a tree); build and test on Ubuntu, Fedora and Rocky.
2. Not yet built on Windows; the release workflows have not run with the submodule.
