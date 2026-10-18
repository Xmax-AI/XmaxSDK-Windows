param([string]$Destination = "$PSScriptRoot/../.build/vendor/agora")
$ErrorActionPreference = "Stop"
$url = "https://download.agora.io/sdk/release/Agora_Native_SDK_for_Windows_v4.6.2_FULL.zip"
$sha256 = "44d0caabc2b5232b0416df63845d88e45de45681b9d32d99617dbd41ba8e6523"
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$archive = Join-Path $Destination "agora-windows-4.6.2.zip"
if (-not (Test-Path $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive }
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sha256) {
    throw "Agora SDK SHA-256 mismatch. Remove the archive and download again."
}
Expand-Archive -Path $archive -DestinationPath $Destination -Force
$sdk = Join-Path $Destination "Agora_Native_SDK_for_Windows_FULL/sdk"
if (-not (Test-Path "$sdk/high_level_api/include/IAgoraRtcEngine.h")) { throw "Agora SDK include folder was not found." }
Write-Output (Resolve-Path $sdk).Path
