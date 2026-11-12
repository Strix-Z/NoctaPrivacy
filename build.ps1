# NoctaPrivacy Windows x64 local / GitHub Actions build helper.
# Requires MSVC 2022, CMake, Git and Windows SDK.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw 'CMake not found. Install Visual Studio 2022 with C++ and CMake.'
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  throw 'Git not found.'
}

# This tag is verified in upstream Spout2, unlike the unverified 2.007.018 in v0.1.
$spoutVersion = '2.007.017'
$spoutDir = Join-Path $PSScriptRoot 'external/Spout2'
if (-not (Test-Path (Join-Path $spoutDir 'CMakeLists.txt'))) {
  New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot 'external') | Out-Null
  & git clone --depth 1 --branch $spoutVersion https://github.com/leadedge/Spout2.git $spoutDir
  if ($LASTEXITCODE -ne 0) { throw "Git clone failed for Spout2 $spoutVersion" }
}

& cmake -S . -B build -A x64 -G 'Visual Studio 17 2022'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build build --config Release --target NoctaPrivacy --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed. Check the MSVC output above.' }

$exe = Join-Path $PSScriptRoot 'build/Release/NoctaPrivacy.exe'
if (-not (Test-Path $exe)) { throw "Build reported success but missing executable: $exe" }
Write-Host "EXE ready: $exe" -ForegroundColor Green
