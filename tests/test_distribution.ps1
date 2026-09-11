$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
$zip = Join-Path $repoRoot "output/syw2-ddraw-v$version.zip"
# Use a disposable Unicode/space path; do not modify an installed game.
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("HQCDD 한글 경로 " + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    Expand-Archive -LiteralPath $zip -DestinationPath $testRoot
    Copy-Item -LiteralPath (Join-Path $repoRoot 'build/Release/loader_test.exe') -Destination (Join-Path $testRoot 'loader_test.exe')
    & (Join-Path $PSScriptRoot 'test_version.ps1') -DllPath (Join-Path $testRoot 'ddraw.dll')
    $loader = Join-Path $testRoot 'loader_test.exe'
    $dll = Join-Path $testRoot 'ddraw.dll'
    $alias = Join-Path $testRoot 'ddraw-alias.dll'
    New-Item -ItemType HardLink -Path $alias -Target $dll | Out-Null
    & $loader --same-file $dll $alias
    if ($LASTEXITCODE -ne 0) { throw 'File identity rejected a hard-link alias' }
    $copy = Join-Path $testRoot 'ddraw-copy.dll'
    Copy-Item -LiteralPath $dll -Destination $copy
    & $loader --same-file $dll $copy
    if ($LASTEXITCODE -ne 3) { throw 'File identity accepted a separate DLL copy' }
    $missing = Join-Path $testRoot 'missing.dll'
    & $loader --same-file $dll $missing
    if ($LASTEXITCODE -ne 3) { throw 'File identity accepted a missing file' }
    & $loader --same-file $missing $dll
    if ($LASTEXITCODE -ne 3) { throw 'File identity accepted a missing expected file' }
    Write-Host 'PASS: file identity accepts aliases and rejects copies or missing files'
    & $loader
    if ($LASTEXITCODE -ne 0) { throw "DDRAW import smoke test failed: $LASTEXITCODE" }
    Write-Host 'PASS: extracted end-user ZIP loads without prepare.py or a game EXE patch'
    $asiRoot = Join-Path $testRoot 'ASI distribution'
    Expand-Archive -LiteralPath (Join-Path $repoRoot "output/syw2-ddraw-v$version-asi.zip") -DestinationPath $asiRoot
    $asi = Join-Path $asiRoot 'plugins/hqcdd.asi'
    if (Test-Path -LiteralPath (Join-Path $asiRoot 'ddraw.dll')) { throw 'ASI ZIP replaces the loader' }
    & (Join-Path $PSScriptRoot 'test_version.ps1') -DllPath $asi
    Copy-Item -LiteralPath (Join-Path $repoRoot 'build/Release/asi_import_test.exe') -Destination $asiRoot
    & (Join-Path $asiRoot 'asi_import_test.exe') $asi
    if ($LASTEXITCODE -ne 0) { throw "Extracted ASI import test failed: $LASTEXITCODE" }
    Write-Host 'PASS: extracted ASI ZIP connects real DDRAW imports from a Unicode/space path'
} finally {
    # Only delete the exact temporary directory created by this invocation.
    $resolvedTest = [IO.Path]::GetFullPath($testRoot)
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedTest.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -or
        -not ([IO.Path]::GetFileName($resolvedTest)).StartsWith('HQCDD 한글 경로 ')) {
        throw 'Unexpected temporary test path; cleanup refused'
    }
    Remove-Item -LiteralPath $resolvedTest -Recurse -Force
}
