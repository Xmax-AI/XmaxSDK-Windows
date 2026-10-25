param([string]$Destination = "$PSScriptRoot/../.build/vendor/agora")
$ErrorActionPreference = "Stop"
$locked = Get-Content "$PSScriptRoot/../Configurations/Release/dependencies.json" -Raw | ConvertFrom-Json
$url = $locked.agora.url
$sha256 = $locked.agora.sha256
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$archive = Join-Path $Destination "agora-windows-$($locked.agora.version).zip"
if (-not (Test-Path $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive }
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) {
    throw "Agora SDK SHA-256 mismatch. Remove the archive and download again."
}
Expand-Archive -Path $archive -DestinationPath $Destination -Force
$sdk = Join-Path $Destination "Agora_Native_SDK_for_Windows_FULL/sdk"
if (-not (Test-Path "$sdk/high_level_api/include/IAgoraRtcEngine.h")) { throw "Agora SDK include folder was not found." }
Write-Output (Resolve-Path $sdk).Path
