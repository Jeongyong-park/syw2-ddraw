$ErrorActionPreference = 'Stop'
$recordPath = Join-Path $PSScriptRoot 'hqcdd-install.json'
if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) {
    $repoOutput = Join-Path $PSScriptRoot 'output'
    $installations = @()
    if ((Test-Path -LiteralPath (Join-Path $PSScriptRoot 'prepare.py')) -and
        (Test-Path -LiteralPath $repoOutput -PathType Container)) {
        $installations = @(Get-ChildItem -LiteralPath $repoOutput -Directory -Filter 'syw2-graphics-*' |
            Where-Object { (Test-Path -LiteralPath (Join-Path $_.FullName 'hqcdd-install.json')) -and
                           (Test-Path -LiteralPath (Join-Path $_.FullName 'launch.ps1')) })
    }
    if ($installations.Count -gt 0) {
        Write-Host 'Run a prepared installation launcher (choose the version you want):'
        foreach ($installation in $installations) {
            $preparedLauncher = (Join-Path $installation.FullName 'launch.ps1').Replace("'", "''")
            Write-Host "  & '$preparedLauncher'"
        }
    }
    throw 'This folder has no prepared installation. Run prepare.py with your game EXE, then run launch.ps1 from its output folder. Do not copy hqcdd-install.json into the source folder.'
}
$record = Get-Content -LiteralPath $recordPath -Raw -Encoding UTF8 | ConvertFrom-Json
$exeEntries = @($record.files.PSObject.Properties | Where-Object { $_.Name.EndsWith('.exe') })
if ($exeEntries.Count -ne 1) { throw 'Expected one generated game EXE in hqcdd-install.json' }
foreach ($entry in $record.files.PSObject.Properties) {
    if ([IO.Path]::GetFileName($entry.Name) -ne $entry.Name) { throw 'Invalid installation file name' }
    $file = Join-Path $PSScriptRoot $entry.Name
    $stream = [IO.File]::OpenRead($file)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '') }
    finally { $sha256.Dispose(); $stream.Dispose() }
    if ($actualHash -ne $entry.Value) {
        throw "File changed since preparation: $file"
    }
}
$gameDirectory = Split-Path -Parent $record.source
if (-not (Test-Path -LiteralPath $gameDirectory -PathType Container)) {
    throw "Original game folder is missing: $gameDirectory"
}
$exePath = Join-Path $PSScriptRoot $exeEntries[0].Name
Start-Process -FilePath $exePath -WorkingDirectory $gameDirectory
