# Build + test without make (Windows PowerShell). Usage: powershell -ExecutionPolicy Bypass -File scripts/build.ps1
# Requires g++ (C++20) on PATH. Output: build\ps\wallet.exe and build\ps\wallet_tests.exe
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$out = "build/ps"
New-Item -ItemType Directory -Force $out | Out-Null

$flags = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Iinclude", "-static")
$lib   = Get-ChildItem src -Recurse -Filter *.cpp | Where-Object { $_.Name -ne "main.cpp" } | ForEach-Object { $_.FullName }
$tests = Get-ChildItem tests -Filter *.cpp | ForEach-Object { $_.FullName }

Write-Host "Building wallet.exe ..."
& g++ @flags $lib "src/main.cpp" -o "$out/wallet.exe"
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "Building wallet_tests.exe ..."
& g++ @flags $lib $tests -o "$out/wallet_tests.exe"
if ($LASTEXITCODE -ne 0) { throw "test build failed" }

Write-Host "Running tests ..."
& "$out/wallet_tests.exe"
exit $LASTEXITCODE
