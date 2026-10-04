param(
  [string]$GamePath = "D:\Games\Driver San Francisco"
)

$ErrorActionPreference = "Stop"

$dist = Join-Path $PSScriptRoot "dist"
$files = @(
  "winmm.dll",
  "dsf_enhanced.asi",
  "dsf_enhanced.ini"
)

$missingFiles = @($files | Where-Object {
  -not (Test-Path -LiteralPath (Join-Path $dist $_) -PathType Leaf)
})
if ($missingFiles.Count -ne 0) {
  powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1")
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
  }
}

if (-not (Test-Path -LiteralPath (Join-Path $GamePath "Driver.exe"))) {
  throw "Driver.exe was not found in: $GamePath"
}

foreach ($file in $files) {
  Copy-Item -LiteralPath (Join-Path $dist $file) -Destination (Join-Path $GamePath $file) -Force
}

Write-Host "Installed DSF Enhanced to $GamePath"
