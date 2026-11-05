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
  Concurrency checks for install.ps1: several installers running at once into
  one prefix, installs while a version is running, installers killed mid-unpack
  or while holding the install mutex.

.DESCRIPTION
  scripts\test_install_concurrent.ps1 -Mirror <dir> [-Work <dir>] [-Exe <powershell>]

  -Mirror holds one release's SHA256SUMS and this machine's Windows package, so
  the many installs here read it locally instead of downloading it each time:

    gh release download v26.11.0 -D <dir> -p SHA256SUMS -p '*-windows-arm-64bit.tar.gz'

  The installer under test is a copy of the repository's install.ps1 that copies
  its assets from -Mirror instead of downloading them; nothing else in it
  changes. (PowerShell 7's Invoke-WebRequest cannot read file:// URLs.) Every
  install goes into -Work (default: a new temporary folder, removed at the end),
  never the default prefix. -Exe is the PowerShell that runs the installers;
  the default is the one running this script. Exits 1 if any check fails.

  Each scenario's folders are removed as soon as it is checked: an unpacked
  shell takes a few hundred MB, and a small test VM's disk holds few at once.
#>
param(
    [Parameter(Mandatory)] [string] $Mirror,
    [string] $Work,
    [string] $Exe = (Get-Process -Id $PID).Path
)
$ErrorActionPreference = 'Continue'

$Mirror = (Resolve-Path -LiteralPath $Mirror).Path
$pkg = Get-ChildItem -LiteralPath $Mirror -Filter 'mariadb-shell-*-windows-*.tar.gz' | Select-Object -First 1
if (-not $pkg -or -not (Test-Path -LiteralPath (Join-Path $Mirror 'SHA256SUMS'))) {
    [Console]::Error.WriteLine("$Mirror needs SHA256SUMS and a mariadb-shell-*-windows-*.tar.gz package"); exit 2
}
if ($pkg.Name -notmatch '^mariadb-shell-([0-9][0-9.]*)-') {
    [Console]::Error.WriteLine("cannot read the version from $($pkg.Name)"); exit 2
}
$Version = $Matches[1]
$Tag = "v$Version"

$removeWork = -not $Work
if (-not $Work) { $Work = Join-Path ([IO.Path]::GetTempPath()) ('install-concurrent-' + [Guid]::NewGuid().ToString('N')) }
New-Item -ItemType Directory -Force $Work | Out-Null
$Work = (Resolve-Path -LiteralPath $Work).Path

# The test copy: assets copied from the mirror instead of downloaded.
$Installer = Join-Path $Work 'install.ps1'
$src = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '..\install.ps1')
$urlFrom = '"https://github.com/$Repo/releases/download/$Tag"'
$getFrom = 'Invoke-WebRequest -Uri "$Base/$Name" -OutFile $OutFile -UseBasicParsing'
if (-not $src.Contains($urlFrom) -or -not $src.Contains($getFrom)) {
    [Console]::Error.WriteLine("could not point the test copy of install.ps1 at the mirror"); exit 2
}
$src = $src.Replace($urlFrom, "'$Mirror'").Replace($getFrom, 'Copy-Item -LiteralPath (Join-Path $Base $Name) -Destination $OutFile')
Set-Content -LiteralPath $Installer -Value $src -Encoding UTF8

$script:fail = 0
function Pass($m) { Write-Host "PASS: $m" }
function Fail($m) { Write-Host "FAIL: $m"; $script:fail = 1 }
function Drop([string[]] $Dirs) {
    foreach ($x in $Dirs) { if (Test-Path -LiteralPath $x) { cmd /c "rmdir /s /q `"$x`"" 2>$null } }
}
function Get-InstallerTemp {
    @(Get-ChildItem ([IO.Path]::GetTempPath()) -Directory -Filter 'mariadb-shell-install-*' -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName)
}
function Get-InstallArgs([string] $Dir) {
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$Installer`"",
    '-Tag', $Tag, '-Prefix', "`"$Dir\prefix`"", '-BinDir', "`"$Dir\bin`""
}

# Starts n installers together into <dir>\prefix and returns them once all ended.
function Start-Installs([string] $Dir, [int] $N) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $procs = 1..$N | ForEach-Object {
        Start-Process -FilePath $Exe -PassThru -NoNewWindow `
            -RedirectStandardOutput "$Dir\out.$_" -RedirectStandardError "$Dir\err.$_" `
            -ArgumentList (Get-InstallArgs $Dir)
    }
    # PowerShell 5.1 reports no ExitCode unless the handle was read while it ran.
    $procs | ForEach-Object { $null = $_.Handle }
    $procs | ForEach-Object { $_.WaitForExit(); $_ }
}

# All exited 0, nothing but version folders under the prefix, and a shim that
# runs the installed version.
function Test-Result([string] $Dir, $Procs, [string] $Label) {
    $codes = @($Procs | ForEach-Object { $_.ExitCode })
    if (@($codes | Where-Object { $_ -ne 0 }).Count -eq 0) { Pass "${Label}: all $($codes.Count) exit 0" }
    else { Fail "${Label}: exit codes $($codes -join ',')"; Get-Content "$Dir\err.*" | Select-Object -Last 6 }
    $left = @(Get-ChildItem -Force "$Dir\prefix" | Where-Object { $_.Name -like '.*' })
    if ($left.Count -eq 0) { Pass "${Label}: no .unpack-* or .remove-* left" }
    else { Fail "${Label}: left behind $($left.Name -join ' ')" }
    $v = & cmd /c "`"$Dir\bin\mariadb-shell.cmd`" --version" 2>$null | Select-Object -First 1
    if ($v -match "Ver $([regex]::Escape($Version))") { Pass "${Label}: shim runs $Version" }
    else { Fail "${Label}: shim gave '$v'" }
}

function Start-Sleeper([string] $ShellExe) {
    Start-Process -FilePath $ShellExe -ArgumentList '--py', '-e', 'import time; time.sleep(120)' -PassThru -WindowStyle Hidden
}

Write-Host "Testing $Installer with $Tag from $Mirror, run by $Exe"
$tempBefore = Get-InstallerTemp
$sleepers = @()
try {
    foreach ($n in 1, 2, 3) {
        $d = "$Work\fresh$n"
        Test-Result $d (Start-Installs $d $n) "fresh x$n"
        if ($n -ne 1) { Drop $d }   # fresh1 is reused below
    }

    # Three upgrades together over two older versions: the higher one is kept.
    $d = "$Work\upgrade"
    New-Item -ItemType Directory -Force "$d\prefix\0.0.2\bin", "$d\prefix\0.0.1\bin" | Out-Null
    Test-Result $d (Start-Installs $d 3) "upgrade x3"
    if ((Test-Path "$d\prefix\0.0.2") -and -not (Test-Path "$d\prefix\0.0.1")) { Pass "upgrade: kept 0.0.2, pruned 0.0.1" }
    else { Fail "upgrade: prune left $((Get-ChildItem $d\prefix).Name -join ' ')" }
    Drop $d

    # A rerun while that version's mariadb-shell.exe runs keeps the folder.
    $d = "$Work\fresh1"
    $sleepers += Start-Sleeper "$d\prefix\$Version\bin\mariadb-shell.exe"
    Start-Sleep 2
    Test-Result $d (Start-Installs $d 1) "rerun while $Version runs"
    if (Select-String -Quiet -Pattern '==> Keeping' "$d\out.1") { Pass "rerun kept the folder" } else { Fail "rerun did not keep the folder" }

    # An install while a superseded version runs: the prune leaves that one
    # whole with a warning, removes the other, and the install succeeds.
    $d = "$Work\inuse"
    New-Item -ItemType Directory -Force "$d\prefix\0.0.1\bin", "$d\prefix\0.0.4\bin" | Out-Null
    Copy-Item "$Work\fresh1\prefix\$Version" "$d\prefix\0.0.3" -Recurse
    $sleepers += Start-Sleeper "$d\prefix\0.0.3\bin\mariadb-shell.exe"
    Start-Sleep 2
    Test-Result $d (Start-Installs $d 1) "install while a superseded version runs"
    if (Select-String -Quiet -Pattern 'warning: could not remove' "$d\err.1") { Pass "prune warned about the running 0.0.3" }
    else { Fail "no prune warning"; Get-Content "$d\err.1" }
    if ((Test-Path "$d\prefix\0.0.3\bin\mariadb-shell.exe") -and (Test-Path "$d\prefix\0.0.4") -and
        -not (Test-Path "$d\prefix\0.0.1")) { Pass "running 0.0.3 left whole, 0.0.4 kept, 0.0.1 pruned" }
    else { Fail "prune left $((Get-ChildItem $d\prefix).Name -join ' ')" }
    $sleepers | Stop-Process -Force -ErrorAction SilentlyContinue
    $sleepers = @()
    Start-Sleep 1
    Drop "$Work\fresh1", $d

    # Killed mid-unpack (the whole tree, tar included, as a closed terminal
    # would): the next run is not blocked and removes the stale folder.
    $d = "$Work\killed"
    New-Item -ItemType Directory -Force $d | Out-Null
    $k = Start-Process -FilePath $Exe -PassThru -NoNewWindow -RedirectStandardOutput "$d\out.k" -RedirectStandardError "$d\err.k" `
        -ArgumentList (Get-InstallArgs $d)
    $null = $k.Handle
    $deadline = (Get-Date).AddSeconds(120)
    while (-not (Get-ChildItem -Force "$d\prefix" -Filter '.unpack-*' -ErrorAction SilentlyContinue) -and
           -not $k.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (Get-ChildItem -Force "$d\prefix" -Filter '.unpack-*' -ErrorAction SilentlyContinue) {
        Start-Sleep -Milliseconds 500
        & taskkill /F /T /PID $k.Id | Out-Null
        $k.WaitForExit()
        Test-Result $d (Start-Installs $d 1) "after a kill mid-unpack"
    } else {
        & taskkill /F /T /PID $k.Id 2>$null | Out-Null
        Fail "the installer never started unpacking"; Get-Content "$d\out.k", "$d\err.k"
    }
    Drop $d

    # A process takes the install mutex and is killed while it waits: the
    # installer waits for it, then gets the mutex as abandoned. The name is
    # worked out the way install.ps1 does it.
    $d = "$Work\abandoned"
    $prefixFull = "$d\prefix"
    $sha = [Security.Cryptography.SHA256]::Create()
    $hash = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($prefixFull.ToLowerInvariant())) |
        Select-Object -First 8 | ForEach-Object { $_.ToString('x2') })
    $sha.Dispose()
    $holderCmd = "`$m = [System.Threading.Mutex]::new(`$false, 'Global\mariadb-shell-install-$hash'); `$null = `$m.WaitOne(); Start-Sleep 300"
    $holder = Start-Process -FilePath $Exe -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile', '-EncodedCommand',
        ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($holderCmd)))
    Start-Sleep 3
    $killer = Start-Job -ScriptBlock { param($id) Start-Sleep 8; Stop-Process -Id $id -Force } -ArgumentList $holder.Id
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Test-Result $d (Start-Installs $d 1) "after the mutex holder was killed"
    if (Select-String -Quiet -Pattern 'waiting for another install' "$d\err.1") { Pass "waited for the holder ($([int]$sw.Elapsed.TotalSeconds)s)" }
    else { Fail "did not wait: the holder's mutex was not seen" }
    $killer | Remove-Job -Force
    Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue
    Drop $d
} finally {
    $sleepers | Stop-Process -Force -ErrorAction SilentlyContinue
    # The killed installer's finally never ran: its download folder, package
    # included, is still in TEMP.
    Get-InstallerTemp | Where-Object { $tempBefore -notcontains $_ } | ForEach-Object { Drop $_ }
    if ($removeWork) { Drop $Work }
}

exit $script:fail
