# [REQ-21] The release package (scripts\make-release.ps1) has only what users need - no test
# programs, test files or source - and the FeedView in it runs with its bundled NDI runtime.
# Uses the existing build (no tests, no rebuild); packages into a temporary folder.
#   powershell -File tests/release_package_test.ps1 [-BuildDir build]
# Prints "RESULT PASS|FAIL ..." lines like the other tests (tests/check.h).
param([string]$BuildDir = 'build')
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root
$failed = 0
function Result([bool]$ok, [string]$name, [string]$detail = '') {
    if ($detail) { Write-Host "  $detail" }
    Write-Host "RESULT $(if ($ok) { 'PASS' } else { 'FAIL' }) $name"
    if (-not $ok) { $script:failed++ }
}
$out = Join-Path $env:TEMP "feedview-release-test-$PID"
$version = ([regex]'project\(feedview VERSION ([0-9.]+)').Match((Get-Content CMakeLists.txt -Raw)).Groups[1].Value
$zip = Join-Path $out "FeedView-$version-windows-x64.zip"
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Package([string[]]$extra) {
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    $log = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'scripts\make-release.ps1') `
        -SkipTests -NoBuild -OutDir $out -BuildDir $BuildDir @extra 2>&1 | Out-String
    return @{ code = $LASTEXITCODE; log = $log }
}
function Entries() {
    $z = [IO.Compression.ZipFile]::OpenRead($zip)
    try { return @($z.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName.Replace('\', '/') }) } finally { $z.Dispose() }
}
function ReadmeText() {
    $z = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $e = $z.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq 'FeedView/README.txt' }
        $r = New-Object IO.StreamReader($e.Open()); try { return $r.ReadToEnd() } finally { $r.Dispose() }
    } finally { $z.Dispose() }
}

Write-Host '- [REQ-21] make-release makes a zip to share'
$p = Package @()
Write-Host ($p.log.Trim() -split "`r?`n" | Select-Object -Last 12 | ForEach-Object { "  | $_" } | Out-String).TrimEnd()
Result ($p.code -eq 0 -and (Test-Path $zip)) '[REQ-21] make-release makes a zip to share, and checks that FeedView in it starts with its own NDI runtime' "exit $($p.code)"

if (Test-Path $zip) {
    $entries = Entries
    Write-Host '- [REQ-21] the zip has only what users need'
    $testish = @($entries | Where-Object { $_ -match '(?i)test|\.pdb$|\.lib$|\.cpp$|\.h$|\.mjs$|\.js$|cmake|\.ps1$|\.cmd$|node_modules' })
    Result ($testish.Count -eq 0) '[REQ-21] no test programs, test files, scripts or source in the zip' $(if ($testish) { "found: $($testish -join ', ')" } else { "$($entries.Count) files" })
    $need = 'FeedView/FeedView.exe', 'FeedView/Processing.NDI.Lib.x64.dll', 'FeedView/README.txt', 'FeedView/THIRD_PARTY_NOTICES.md'
    $missing = @($need | Where-Object { $entries -notcontains $_ })
    Result ($missing.Count -eq 0) '[REQ-21] the app, its NDI runtime, the README and the licences are in it' $(if ($missing) { "missing: $($missing -join ', ')" } else { $entries -join ', ' })
    $hasModule = Test-Path 'companion/feedview/node_modules/@companion-module/base'
    if ($hasModule) {
        Result (@($entries | Where-Object { $_ -match '^FeedView/Companion/feedview-[0-9.]+\.tgz$' }).Count -eq 1) '[REQ-21] the Companion module package is in it'
    }
    $readme = ReadmeText
    Result (-not ($readme -match 'feedview-test-sender')) "[REQ-21] its README doesn't talk about the test sender, which isn't in it"
    Result ((Get-Item $zip).Length -lt 100MB) '[REQ-21] small enough to share' ('{0:N1} MB' -f ((Get-Item $zip).Length / 1MB))

    Write-Host '- [REQ-21] -WithTestSender adds the test-pattern sender (and its README section)'
    $p2 = Package @('-WithTestSender')
    $entries2 = if (Test-Path $zip) { Entries } else { @() }
    $ok2 = $p2.code -eq 0 -and ($entries2 -contains 'FeedView/feedview-test-sender.exe') -and ((ReadmeText) -match 'Test source')
    Result $ok2 '[REQ-21] -WithTestSender adds the test-pattern sender (and its README section)' "exit $($p2.code)"
}
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
Write-Host "$(if ($failed) { "$failed failed" } else { 'all passed' })"
exit $(if ($failed) { 1 } else { 0 })
