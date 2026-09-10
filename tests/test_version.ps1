param([string]$DllPath = (Join-Path $PSScriptRoot '../build/Release/hqcdd.dll'))
$ErrorActionPreference = 'Stop'
$expected = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim()
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $DllPath).Path)
$fields = @{
    CompanyName = 'Park Jeongyong'
    FileDescription = 'HQCDD - SYW2Plus DirectDraw 7 graphics wrapper'
    FileVersion = $expected
    ProductVersion = $expected
    ProductName = 'HQCDD for SYW2Plus'
    InternalName = 'hqcdd'
    OriginalFilename = 'hqcdd.dll'
    LegalCopyright = 'Copyright (c) 2026 Park Jeongyong. MIT License.'
}
foreach ($field in $fields.Keys) {
    if ($info.$field -cne $fields[$field]) { throw "DLL metadata mismatch: $field = $($info.$field)" }
}
$numeric = "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart)"
$product = "$($info.ProductMajorPart).$($info.ProductMinorPart).$($info.ProductBuildPart)"
if ($numeric -ne $expected -or $product -ne $expected -or $info.FilePrivatePart -ne 0 -or $info.ProductPrivatePart -ne 0) {
    throw 'DLL numeric version mismatch'
}
Write-Host "PASS: DLL version $expected; author $($info.CompanyName)"
