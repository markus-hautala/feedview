# Makes a FeedView release package you can share: dist\FeedView-<version>-windows-x64.zip.
# Easiest: double-click make-release.cmd.
#
# The zip holds only what users need - nothing from the tests or the source:
#   FeedView\FeedView.exe                     the app
#   FeedView\Processing.NDI.Lib.x64.dll       the NDI runtime (so users install nothing)
#   FeedView\NDI-Processing.NDI.Lib.Licenses.txt, THIRD_PARTY_NOTICES.md   licences
#   FeedView\README.txt                       first-start notes, keys, web remote, Companion
#   FeedView\Companion\feedview-<version>.tgz the Bitfocus Companion module (import it in Companion)
# -WithTestSender adds feedview-test-sender.exe (a test-pattern NDI source; off by default).
#
# First it runs all the tests (scripts\run-tests.ps1, ~2 minutes, takes over the screen) and
# makes no package if they fail. -SkipTests skips that (not for a real release); -NoBuild uses
# the existing build as it is (the tests use that).
#
# The NDI runtime comes from ndi-runtime\ (scripts\fetch-ndi-runtime.ps1 -OutDir ndi-runtime)
# or from the NDI Runtime / NDI Tools installed on this computer. Redistributing it is governed
# by the NDI SDK licence: https://ndi.link/ndisdk_license
param([switch]$SkipTests, [switch]$NoBuild, [switch]$WithTestSender, [string]$OutDir = 'dist', [string]$BuildDir = 'build')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-Location (Split-Path -Parent $PSScriptRoot)
$root = (Get-Location).Path

$version = ([regex]'project\(feedview VERSION ([0-9.]+)').Match((Get-Content CMakeLists.txt -Raw)).Groups[1].Value
if (-not $version) { throw 'No version in CMakeLists.txt' }
$name = "FeedView-$version-windows-x64"
Write-Host "FeedView $version release package"

# ---- Tests, build
if (-not $SkipTests) {
    Write-Host 'Running all tests first (about two minutes; FeedView goes fullscreen and the mouse moves by itself - please don''t touch them).'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\run-tests.ps1') -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'The tests failed: no release package. See build\requirements-report.md.'
        exit 1
    }
} elseif (-not $NoBuild) {
    if (-not (Test-Path "$BuildDir/CMakeCache.txt")) { cmake -S . -B $BuildDir -A x64; if ($LASTEXITCODE) { exit 1 } }
    cmake --build $BuildDir --config Release --parallel --target feedview feedview_test_sender
    if ($LASTEXITCODE) { exit 1 }
    if ((Get-Command node -ErrorAction SilentlyContinue) -and (Test-Path 'companion/feedview/node_modules/@companion-module/base')) {
        Push-Location companion/feedview
        try { npm run --silent package; if ($LASTEXITCODE) { exit 1 } } finally { Pop-Location }
    }
}

# ---- What goes in
$exe = Join-Path $BuildDir 'Release\FeedView.exe'
if (-not (Test-Path $exe)) { throw "$exe not found: build FeedView first (or leave out -NoBuild)" }
$ndiDir = $null
foreach ($d in @('ndi-runtime', $env:NDI_RUNTIME_DIR_V6)) {
    if ($d -and (Test-Path (Join-Path $d 'Processing.NDI.Lib.x64.dll'))) { $ndiDir = $d; break }
}
if (-not $ndiDir) {
    throw 'No NDI runtime to bundle: install NDI Tools, or run scripts\fetch-ndi-runtime.ps1 -OutDir ndi-runtime (as administrator)'
}
$tgz = Join-Path 'companion/feedview' "feedview-$version.tgz"
if (-not (Test-Path $tgz)) {
    $tgz = Get-ChildItem 'companion/feedview' -Filter 'feedview-*.tgz' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}

$stageRoot = Join-Path $OutDir $name
$stage = Join-Path $stageRoot 'FeedView'
if (Test-Path $stageRoot) { Remove-Item $stageRoot -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $ndiDir 'Processing.NDI.Lib.x64.dll') $stage
Get-ChildItem $ndiDir -File | Where-Object { $_.Name -match 'licen' } | ForEach-Object {
    $n = if ($_.Name -like 'NDI-*') { $_.Name } else { "NDI-$($_.Name)" }
    Copy-Item $_.FullName (Join-Path $stage $n)
}
Copy-Item THIRD_PARTY_NOTICES.md $stage
$readme = Get-Content packaging\README-dist.txt -Raw
if ($WithTestSender) {
    Copy-Item (Join-Path $BuildDir 'Release\feedview-test-sender.exe') $stage
} else {
    # The test-pattern sender isn't in this package: leave out its section.
    $readme = [regex]::Replace($readme, '(?ms)^Test source\r?\n.*?(\r?\n\r?\n)', '')
}
Set-Content (Join-Path $stage 'README.txt') ($readme -replace "`r?`n", "`r`n") -NoNewline -Encoding ascii
if ($tgz) {
    New-Item -ItemType Directory -Force (Join-Path $stage 'Companion') | Out-Null
    Copy-Item $tgz (Join-Path $stage 'Companion')
} else {
    Write-Host 'Note: no Companion module package found (npm run package in companion\feedview); left out.'
}

$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

# ---- Check the package: only the files users need, and FeedView starts with the bundled runtime
$check = Join-Path $env:TEMP "feedview-release-check-$PID"
if (Test-Path $check) { Remove-Item $check -Recurse -Force }
Expand-Archive $zip -DestinationPath $check
$files = Get-ChildItem $check -Recurse -File | ForEach-Object { $_.FullName.Substring($check.Length + 1).Replace('\', '/') }
$allowed = @('^FeedView/FeedView\.exe$', '^FeedView/Processing\.NDI\.Lib\.x64\.dll$', '^FeedView/NDI-[^/]*licen[^/]*\.txt$',
    '^FeedView/THIRD_PARTY_NOTICES\.md$', '^FeedView/README\.txt$', '^FeedView/Companion/feedview-[0-9.]+\.tgz$')
if ($WithTestSender) { $allowed += '^FeedView/feedview-test-sender\.exe$' }
$extra = $files | Where-Object { $f = $_; -not ($allowed | Where-Object { $f -match $_ }) }
$problems = @()
if ($extra) { $problems += "files that don't belong in a release: $($extra -join ', ')" }
foreach ($need in 'FeedView/FeedView.exe', 'FeedView/Processing.NDI.Lib.x64.dll', 'FeedView/README.txt', 'FeedView/THIRD_PARTY_NOTICES.md') {
    if ($files -notcontains $need) { $problems += "missing $need" }
}
# Run it as on a computer without NDI installed: only the bundled NDI runtime can be found
# (no NDI_RUNTIME_DIR_*, nothing from NDI on PATH).
$saved = @{ V6 = $env:NDI_RUNTIME_DIR_V6; V5 = $env:NDI_RUNTIME_DIR_V5; LIB = $env:FEEDVIEW_NDI_LIBRARY; PATH = $env:PATH }
$env:NDI_RUNTIME_DIR_V6 = $null; $env:NDI_RUNTIME_DIR_V5 = $null; $env:FEEDVIEW_NDI_LIBRARY = $null
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
try {
    $packaged = Join-Path $check 'FeedView\FeedView.exe'
    $v = (& $packaged --version | Out-String).Trim()
    if ($v -ne "FeedView $version") { $problems += "FeedView --version says '$v'" }
    & $packaged --list-sources=1 | Out-Null
    if ($LASTEXITCODE -ne 0) { $problems += "the packaged FeedView can't load its bundled NDI runtime (exit $LASTEXITCODE)" }
} finally {
    $env:NDI_RUNTIME_DIR_V6 = $saved.V6; $env:NDI_RUNTIME_DIR_V5 = $saved.V5; $env:FEEDVIEW_NDI_LIBRARY = $saved.LIB
    $env:PATH = $saved.PATH
}
Remove-Item $check -Recurse -Force
Remove-Item $stageRoot -Recurse -Force

if ($problems) {
    Remove-Item $zip -Force
    Write-Host "The package isn't right, so it was removed:"
    $problems | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
$size = '{0:N1} MB' -f ((Get-Item $zip).Length / 1MB)
Write-Host ''
Write-Host "Release package: $((Resolve-Path $zip).Path)  ($size)"
$files | ForEach-Object { Write-Host "  $_" }
Write-Host 'Checked: only these files, and the packaged FeedView starts with its own NDI runtime.'
