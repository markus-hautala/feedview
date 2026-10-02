# Downloads the official NDI(R) runtime for Windows and copies
# Processing.NDI.Lib.x64.dll (plus its licence text) into -OutDir so it can be bundled.
#
# Uses the redistributable runtime installer referenced by the NDI SDK headers
# (NDILIB_REDIST_URL). Needs admin rights (it runs the installer silently), which
# GitHub-hosted runners have. Override the URL with $env:NDI_RUNTIME_WIN_URL.
# By downloading you accept the NDI SDK licence: https://ndi.link/ndisdk_license
param([Parameter(Mandatory = $true)][string]$OutDir)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$url = if ($env:NDI_RUNTIME_WIN_URL) { $env:NDI_RUNTIME_WIN_URL } else { 'https://ndi.link/NDIRedistV6' }
$tmp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$exe = Join-Path $tmp 'ndi-runtime-installer.exe'

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Invoke-WebRequest -Uri $url -OutFile $exe

$p = Start-Process -FilePath $exe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-' -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "NDI runtime installer failed with exit code $($p.ExitCode)" }

$dll = Get-ChildItem -Path "$env:ProgramFiles\NDI" -Recurse -Filter 'Processing.NDI.Lib.x64.dll' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dll) { throw 'Processing.NDI.Lib.x64.dll not found after installing the runtime' }

Copy-Item $dll.FullName -Destination $OutDir
Get-ChildItem -Path $dll.DirectoryName -File | Where-Object { $_.Name -match 'licen' } |
    ForEach-Object { Copy-Item $_.FullName -Destination (Join-Path $OutDir ("NDI-" + $_.Name)) }

Write-Host "NDI runtime ready in ${OutDir}:"
Get-ChildItem $OutDir | Format-Table Name, Length
