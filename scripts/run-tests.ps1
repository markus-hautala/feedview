# Builds FeedView and the Companion module, runs every test, and reports which of your
# requests (tests/REQUIREMENTS.md) pass. Easiest: double-click run-tests.cmd.
#
#   powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1           # everything
#   powershell -ExecutionPolicy Bypass -File scripts/run-tests.ps1 -Quick    # no desktop tests
#
# The full run takes over the screen and the mouse for about two minutes (FeedView goes
# fullscreen, the mouse moves by itself): don't touch them meanwhile. It passes only if every
# request passes. -Quick leaves out the tests that need the desktop, so it can't check every
# request; it fails only if something it ran failed.
#
# A running FeedView from this build folder locks FeedView.exe; close it first.
# The Companion module's and the web page's tests need Node.js; the module's packages are
# installed on the first run, and its importable package (companion/feedview/feedview-<version>.tgz)
# is rebuilt every time. The report is written to build/requirements-report.md.
param([switch]$Quick, [string]$BuildDir = "build")

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$running = $null
if (Test-Path $BuildDir) {
    $buildFull = (Resolve-Path $BuildDir).Path
    $running = Get-Process FeedView -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($buildFull) }
}
if ($running) {
    Write-Host "FeedView is running from $BuildDir (pid $($running.Id -join ', ')). Close it so it can be rebuilt."
    exit 1
}
if (-not $Quick) {
    Write-Host "Running all tests. FeedView will go fullscreen and the mouse will move by itself for about two minutes: please don't touch the mouse or keyboard."
}
$node = Get-Command node -ErrorAction SilentlyContinue

# ---- Companion module: packages (first run) and the importable package
$module = 'companion/feedview'
if ($node) {
    Push-Location $module
    try {
        if (-not (Test-Path 'node_modules/@companion-module/base')) {
            npm ci --no-audit --no-fund
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        npm run --silent package
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "Node.js not found: the Companion module and the web page can't be tested."
}

# ---- FeedView (configure every time: it picks up the Node tests once they can run)
if (-not (Test-Path "$BuildDir/CMakeCache.txt")) {
    cmake -S . -B $BuildDir -A x64
} else {
    cmake -S . -B $BuildDir | Out-Null
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ---- Tests, then the report of your requests
$results = Join-Path (Resolve-Path $BuildDir).Path 'test-results.xml'
if (Test-Path $results) { Remove-Item $results }
$ctestArgs = @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure', '--output-junit', 'test-results.xml')
if ($Quick) { $ctestArgs += @('-LE', 'desktop') }
ctest @ctestArgs
$testsCode = $LASTEXITCODE

$reportCode = 0
if ($node) {
    $reportArgs = @('scripts/requirements-report.mjs', $results)
    if (-not $Quick) { $reportArgs += '--strict' }
    node @reportArgs
    $reportCode = $LASTEXITCODE
} elseif (-not $Quick) {
    Write-Host "Node.js not found: no report of your requests, and not every request could be tested."
    $reportCode = 1
}
if ($testsCode -ne 0) { exit $testsCode }
exit $reportCode
