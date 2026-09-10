param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot 'build'
& cmake -S $PSScriptRoot -B $build -A Win32
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& (Join-Path $PSScriptRoot 'tests/test_version.ps1') -DllPath (Join-Path $build "$Configuration/hqcdd.dll")
& ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
