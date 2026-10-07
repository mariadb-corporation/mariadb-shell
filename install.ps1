#!/usr/bin/env pwsh
# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA

<#
.SYNOPSIS
  MariaDB Shell installer for Windows. The counterpart of install.sh.

.DESCRIPTION
  Detects the CPU architecture, picks the matching package from a release,
  verifies its checksum and unpacks it. Like install.sh it is data-driven: the
  package list is read from the release's SHA256SUMS rather than hardcoded, so
  this script does not go stale as versions come and go.

  Piped into a shell, parameters cannot be passed the usual way, so every one of
  them also has an environment variable:

    irm https://github.com/mariadb-corporation/mariadb-shell/raw/main/install.ps1 | iex

    $env:MARIADB_SHELL_PRERELEASE = 1
    irm https://github.com/mariadb-corporation/mariadb-shell/raw/main/install.ps1 | iex

  Or, to pass parameters properly, create a scriptblock rather than piping:

    & ([scriptblock]::Create((irm https://github.com/mariadb-corporation/mariadb-shell/raw/main/install.ps1))) -PreRelease

.PARAMETER PreRelease
  Install the newest release even if it is a prerelease. Without it, prereleases
  are skipped, exactly as /releases/latest/ skips them.
  Environment: MARIADB_SHELL_PRERELEASE

.PARAMETER Tag
  Install this release tag rather than the newest. Wins over -PreRelease, since
  it already names the release to install.
  Environment: MARIADB_SHELL_TAG

.PARAMETER Repo
  The owner/repo to install from. It has to be public.
  Environment: MARIADB_SHELL_REPO

.PARAMETER AddToPath
  Add the shim directory to the user PATH. Off by default: editing a user's
  environment is not something a piped installer should do uninvited.
  Environment: MARIADB_SHELL_ADDTOPATH
#>
[CmdletBinding()]
param(
    [switch] $PreRelease,
    [switch] $AddToPath,
    [string] $Tag,
    [string] $Repo,
    [string] $Prefix,
    [string] $BinDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 draws a progress bar for every Invoke-WebRequest, which
# costs far more than the download itself on a 60 MB package. It also negotiates
# TLS 1.0 by default on older setups, which github.com no longer accepts.
$ProgressPreference = 'SilentlyContinue'
try {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
    # PowerShell 7 on .NET Core manages this itself and the type may be absent.
}

function Info([string] $Message) { Write-Host "==> $Message" }

function Die([string] $Message) {
    # Written whole rather than through Write-Error, whose wrapping and
    # "at line:1 char:1" framing turn a formatted block into noise.
    [Console]::Error.WriteLine("install.ps1: $Message")
    exit 1
}

# --------------------------------------------------------------------------
# Settings. Every parameter falls back to an environment variable, because the
# documented one-liner pipes this script into iex, where arguments cannot reach.
# --------------------------------------------------------------------------
# The default is a scriptblock, not a value: computing it eagerly would evaluate
# a LOCALAPPDATA-derived path even when -Prefix was passed, and fail on any
# machine where that variable is unset.
function Fallback([string] $Value, [string] $EnvName, [scriptblock] $Default) {
    if ($Value) { return $Value }
    $fromEnv = [Environment]::GetEnvironmentVariable($EnvName)
    if ($fromEnv) { return $fromEnv }
    if ($Default) { return (& $Default) }
    return ''
}

$Repo   = Fallback $Repo   'MARIADB_SHELL_REPO'   { 'mariadb-corporation/mariadb-shell' }
$Tag    = Fallback $Tag    'MARIADB_SHELL_TAG'    $null
$Prefix = Fallback $Prefix 'MARIADB_SHELL_PREFIX' {
    if (-not $env:LOCALAPPDATA) {
        Die "LOCALAPPDATA is not set, so there is no default install location.
  Pass -Prefix, or set MARIADB_SHELL_PREFIX."
    }
    Join-Path $env:LOCALAPPDATA 'Programs\mariadb-shell'
}
$BinDir = Fallback $BinDir 'MARIADB_SHELL_BINDIR' { Join-Path $Prefix 'bin' }

if (-not $PreRelease) { $PreRelease = [bool] (Fallback '' 'MARIADB_SHELL_PRERELEASE' $null) }
if (-not $AddToPath)  { $AddToPath  = [bool] (Fallback '' 'MARIADB_SHELL_ADDTOPATH' $null) }

if ($PreRelease) {
    $PreReleaseHint = @'
  Prereleases are already enabled. Name a release outright if the one you want
  is not the newest:

      $env:MARIADB_SHELL_TAG = '<tag>'
'@
} else {
    $PreReleaseHint = @'
  A prerelease is never 'latest'. Reach the newest one with -PreRelease, or name
  a release outright:

      $env:MARIADB_SHELL_TAG = '<tag>'
'@
}

# --------------------------------------------------------------------------
# Platform
# --------------------------------------------------------------------------
# PROCESSOR_ARCHITECTURE reports the architecture of the *process*, so a 32-bit
# PowerShell on 64-bit Windows would call itself x86; ARCHITEW6432 is the
# machine's real answer in exactly that case.
$rawArch = $env:PROCESSOR_ARCHITEW6432
if (-not $rawArch) { $rawArch = $env:PROCESSOR_ARCHITECTURE }

switch ($rawArch) {
    'AMD64' { $Arch = 'x86-64bit' }
    'ARM64' { $Arch = 'arm-64bit' }
    'x86'   { Die "32-bit Windows is not supported; no 32-bit package is built." }
    default { Die "unrecognised processor architecture '$rawArch'." }
}

Info "Detected: windows, $Arch"

# tar has shipped with Windows since 10 1803. Checked up front rather than after
# a 60 MB download, which is the wrong moment to discover it is missing.
if (-not (Get-Command tar -ErrorAction SilentlyContinue)) {
    Die "tar was not found. It ships with Windows 10 1803 and later; on an older
  build, unpack the .tar.gz from the release manually."
}

# --------------------------------------------------------------------------
# Release resolution
# --------------------------------------------------------------------------
# A pinned tag and the latest stable release are both reachable by a plain
# download URL: /releases/latest/download/ is a permanent URL GitHub redirects to
# the newest non-prerelease release, so nothing here needs to know the version
# and this script never goes stale. Only the newest prerelease has to be looked
# up first, because it is invisible to /releases/latest: the releases are listed
# through the GitHub API and the newest one taken.
$ResolvePreRelease = $false
if ($Tag) {
    $Base        = "https://github.com/$Repo/releases/download/$Tag"
    $ReleaseDesc = $Tag
} elseif ($PreRelease) {
    $ResolvePreRelease = $true
    $Base        = ''   # known once the release is resolved
    $ReleaseDesc = 'newest, prereleases included'
} else {
    $Base        = "https://github.com/$Repo/releases/latest/download"
    $ReleaseDesc = 'latest'
}

# Every release is public, so once the tag is known the assets are downloaded by
# name like on the other paths -- the GitHub API is only asked for the listing.
# Its anonymous requests are limited to 60 an hour per address, so the API is
# never asked for anything the plain download URLs can provide.
#
# Returns what it resolved rather than assigning across scopes. A $script:
# qualifier would look right and work when this file is run directly, yet bind
# to the caller's scope under [scriptblock]::Create() -- which is exactly how the
# documented one-liner runs it.
function Resolve-PreRelease {
    try {
        $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=20" `
            -Headers @{ 'Accept' = 'application/vnd.github+json' } -UseBasicParsing
    } catch {
        [Console]::Error.WriteLine("  $($_.Exception.Message)`n")
        Die "could not list the releases of $Repo.

  The GitHub API limits anonymous requests to 60 an hour per address, so a
  shared address may have used them up. Naming a release avoids the API:

      `$env:MARIADB_SHELL_TAG = '<tag>'
"
    }

    $release = @($releases) | Where-Object { -not $_.draft } | Select-Object -First 1
    if (-not $release) {
        Die "could not find a published release in $Repo -- is every release a draft?"
    }
    return $release.tag_name
}

function Get-Asset([string] $Name, [string] $OutFile) {
    Invoke-WebRequest -Uri "$Base/$Name" -OutFile $OutFile -UseBasicParsing
}

if ($ResolvePreRelease) {
    $ReleaseDesc = Resolve-PreRelease
    $Base        = "https://github.com/$Repo/releases/download/$ReleaseDesc"
}

$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("mariadb-shell-install-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

try {
    # ----------------------------------------------------------------------
    # SHA256SUMS is the manifest. Reading the asset list from the same file that
    # carries the checksums means there is no separate index to drift out of
    # sync with the real assets.
    # ----------------------------------------------------------------------
    Info "Fetching package list from the $ReleaseDesc release"
    $sumsFile = Join-Path $Tmp 'SHA256SUMS'
    try {
        Get-Asset 'SHA256SUMS' $sumsFile
    } catch {
        [Console]::Error.WriteLine("  $($_.Exception.Message)`n")
        # A release that was just found in the listing exists; one that was
        # named or implied may not.
        if ($ResolvePreRelease) {
            Die "could not download SHA256SUMS from release $ReleaseDesc of $Repo.

  The release exists but has no SHA256SUMS file. A release that is still being
  published does not have its files yet; try again in a few minutes."
        }
        Die "could not download SHA256SUMS from the $ReleaseDesc release of $Repo.

  That release may not exist.

$PreReleaseHint
"
    }

    # ----------------------------------------------------------------------
    # Select. Windows packages carry no platform version in their names -- there
    # is no glibc equivalent to range-match against -- so the architecture alone
    # identifies the right one.
    # ----------------------------------------------------------------------
    $suffix   = "-windows-$Arch.tar.gz"
    $manifest = Get-Content $sumsFile | Where-Object { $_.Trim() }
    $entries  = foreach ($line in $manifest) {
        $parts = $line -split '\s+', 2
        if ($parts.Count -eq 2) {
            [pscustomobject]@{ Sum = $parts[0].Trim(); Name = $parts[1].Trim() }
        }
    }

    $best = $entries | Where-Object { $_.Name.EndsWith($suffix) } | Select-Object -First 1
    if (-not $best) {
        [Console]::Error.WriteLine("install.ps1: no package for windows / $Arch in the $ReleaseDesc release.")
        [Console]::Error.WriteLine("Available packages:")
        foreach ($e in $entries) { [Console]::Error.WriteLine("  $($e.Name)") }
        exit 1
    }

    Info "Selected $($best.Name)"

    # ----------------------------------------------------------------------
    # Download and verify
    # ----------------------------------------------------------------------
    Info "Downloading"
    $pkg = Join-Path $Tmp $best.Name
    try {
        Get-Asset $best.Name $pkg
    } catch {
        Die "could not download $($best.Name) from release $ReleaseDesc of $Repo.

  $($_.Exception.Message)"
    }

    Info "Verifying checksum"
    $actual = (Get-FileHash -Path $pkg -Algorithm SHA256).Hash.ToLowerInvariant()
    $expected = $best.Sum.ToLowerInvariant()
    if ($actual -ne $expected) {
        Die "checksum mismatch for $($best.Name)
    expected: $expected
    actual:   $actual"
    }

    # ----------------------------------------------------------------------
    # Unpack. The tarball holds a single top-level
    # mariadb-shell-<ver>-windows-<arch> directory, renamed to just the version:
    # the platform is a property of the machine that unpacked it, not something
    # worth repeating in every path a user types.
    # ----------------------------------------------------------------------
    Info "Unpacking into $Prefix"
    New-Item -ItemType Directory -Path $Prefix -Force | Out-Null

    $topDir = (& tar -tzf $pkg | Select-Object -First 1) -split '/' | Select-Object -First 1
    if (-not $topDir) { Die "unexpected tarball layout in $($best.Name)" }

    & tar -xzf $pkg -C $Prefix
    if ($LASTEXITCODE -ne 0) { Die "tar failed to unpack $($best.Name)" }

    $unpacked = Join-Path $Prefix $topDir
    if (-not (Test-Path $unpacked)) { Die "expected $unpacked after unpacking" }

    # Falls back to the directory the tarball actually carried, so an
    # unrecognised name costs the tidy layout rather than the install.
    $Version = $topDir
    if ($topDir -match '^mariadb-shell-([0-9][0-9.]*)-') { $Version = $Matches[1] }

    $target = Join-Path $Prefix $Version
    if ($topDir -ne $Version) {
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Move-Item -Path $unpacked -Destination $target
    }

    $shellExe = Join-Path (Join-Path $target 'bin') 'mariadb-shell.exe'
    if (-not (Test-Path $shellExe)) { Die "no mariadb-shell.exe at $shellExe after unpacking" }

    # ----------------------------------------------------------------------
    # Shims. A symlink on Windows needs privileges this script cannot assume, so
    # the stable names are .cmd forwarders instead -- the same device the build
    # itself uses for msh. They are written relative to their own location
    # (%~dp0), so moving the whole prefix does not break them.
    #
    # Both point at the exe rather than one chaining through the other: nesting
    # .cmd files needs `call` and re-quotes every argument on the way through.
    # ----------------------------------------------------------------------
    New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    $relative = "..\$Version\bin\mariadb-shell.exe"
    $shim = "@echo off`r`n`"%~dp0$relative`" %*`r`n"
    foreach ($name in @('mariadb-shell.cmd', 'msh.cmd')) {
        Set-Content -Path (Join-Path $BinDir $name) -Value $shim -NoNewline -Encoding ascii
    }

    $reported = $Version
    try {
        $reported = (& $shellExe --version 2>$null | Select-Object -First 1)
        if (-not $reported) { $reported = $Version }
    } catch {
        # A package for another architecture, or a machine mid-upgrade: the
        # install is still sound, so this is a nicety, not a failure.
    }

    Info "Installed $reported"
    Info "Commands: $BinDir\mariadb-shell.cmd"
    Info "          $BinDir\msh.cmd"

    # ----------------------------------------------------------------------
    # Prune. Only the version just installed and the highest of the rest
    # survive: one way back is worth keeping, a museum is not. Deliberately
    # narrow about what it will delete -- a name has to be purely digits and
    # dots to be considered ours, so anything else under the prefix, including
    # directories left by an older layout, is left alone rather than guessed at.
    # ----------------------------------------------------------------------
    function ConvertTo-SortableVersion([string] $Name) {
        try { return [version] $Name } catch { return $null }
    }

    $versionDirs = Get-ChildItem -Path $Prefix -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^[0-9][0-9.]*$' -and $_.Name -ne $Version -and
                       $null -ne (ConvertTo-SortableVersion $_.Name) }

    $keepOther = $versionDirs |
        Sort-Object -Property @{ Expression = { ConvertTo-SortableVersion $_.Name } } |
        Select-Object -Last 1

    foreach ($d in $versionDirs) {
        if ($keepOther -and $d.Name -eq $keepOther.Name) { continue }
        Info "Removing superseded version $($d.Name)"
        Remove-Item -Recurse -Force $d.FullName
    }
    if ($keepOther) { Info "Kept previous version $($keepOther.Name)" }

    # ----------------------------------------------------------------------
    # PATH. Edited only when asked: rewriting a user's environment from a piped
    # installer is not this script's call to make, which is the same line
    # install.sh draws at a user's shell rc.
    # ----------------------------------------------------------------------
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $onPath = $userPath -and (($userPath -split ';') -contains $BinDir)

    if ($AddToPath) {
        if ($onPath) {
            Info "$BinDir is already on your PATH"
        } else {
            $updated = if ($userPath) { "$userPath;$BinDir" } else { $BinDir }
            [Environment]::SetEnvironmentVariable('Path', $updated, 'User')
            Info "Added $BinDir to your user PATH (open a new terminal to pick it up)"
        }
    } elseif (-not $onPath) {
        Write-Host ""
        Write-Host "$BinDir is not on your PATH. Add it with:"
        Write-Host ""
        Write-Host "    [Environment]::SetEnvironmentVariable('Path', ([Environment]::GetEnvironmentVariable('Path','User') + ';$BinDir'), 'User')"
        Write-Host ""
        Write-Host "or re-run this installer with -AddToPath."
        Write-Host ""
    }
} finally {
    Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}
