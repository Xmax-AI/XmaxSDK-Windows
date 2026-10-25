[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$NativeDirectory,
    [string]$BuildRoot='C:\dev\XmaxSDK-dotnet',
    [string]$DotNet='dotnet',
    [switch]$Camera,
    [switch]$Live,
    [switch]$Package
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if ($BuildRoot.StartsWith('\\')) { throw 'Use a local Windows BuildRoot.' }
$env:DOTNET_ROOT=Split-Path (Get-Command $DotNet -ErrorAction Stop).Source
$properties=@('-p:Platform=x64',"-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/","-p:XmaxNativeDir=$NativeDirectory")
& $DotNet build "$repo/Examples/XLabWinUI" -c Release @properties
if ($LASTEXITCODE -ne 0) { throw 'WinUI build failed.' }
& $DotNet run --project "$repo/Tests/XLabRecordingTests" -c Release @properties -- --native "--artifacts=$BuildRoot/recording-artifacts"
if ($LASTEXITCODE -ne 0) { throw 'Recording encoder and ownership verification failed.' }
$app=Join-Path $BuildRoot 'bin/XLabWinUI/x64/Release/net10.0-windows10.0.26100.0/win-x64/XLabWinUI.exe'
$previousOutput=$env:XMAX_SMOKE_OUTPUT
$previousCredential=$env:XMAX_SMOKE_CREDENTIAL_ID
try {
    $env:XMAX_SMOKE_OUTPUT=Join-Path $BuildRoot 'winui-screenshots'
    New-Item -ItemType Directory -Force $env:XMAX_SMOKE_OUTPUT | Out-Null
    function Invoke-Smoke([string]$Mode) {
        $report=Join-Path $env:XMAX_SMOKE_OUTPUT ($Mode.TrimStart('-')+'.json')
        Remove-Item $report -ErrorAction SilentlyContinue
        $process=Start-Process $app -ArgumentList $Mode -PassThru
        if (!$process.WaitForExit(150000)) { Stop-Process -Id $process.Id -Force; throw "$Mode timed out." }
        if (!(Test-Path $report)) { throw "$Mode exited with code $($process.ExitCode) before writing a report; inspect $env:XMAX_SMOKE_OUTPUT." }
        if ($process.ExitCode -ne 0) { throw "$Mode exited with code $($process.ExitCode)." }
        $result=Get-Content $report -Raw | ConvertFrom-Json
        if (!$result.passed) { throw "$Mode failed: $($result.error)" }
        Write-Output "PASS WinUI $Mode"
    }
    Invoke-Smoke '--smoke-test-recording'
    Invoke-Smoke '--smoke-test-frames'
    Invoke-Smoke '--smoke-test-ui'
    Invoke-Smoke '--smoke-test-key-gate'
    $env:XMAX_SMOKE_CREDENTIAL_ID=[Guid]::NewGuid().ToString('N')
    foreach ($phase in @('write','read','clear','empty')) { Invoke-Smoke "--smoke-test-key-memory-$phase" }
    if ($Camera -or $Live) { Invoke-Smoke '--smoke-test-camera' }
    if ($Live) {
        if (!$env:XMAX_API_KEY) { throw 'Set XMAX_API_KEY for explicit real-service verification; the test does not save it.' }
        Invoke-Smoke '--smoke-test-live'
    }
    if ($Package) { & "$PSScriptRoot/pack-winui.ps1" -NativeDirectory $NativeDirectory -BuildRoot $BuildRoot -DotNet $DotNet; if (!$?) { throw 'WinUI packaging failed.' } }
}
finally { $env:XMAX_SMOKE_OUTPUT=$previousOutput; $env:XMAX_SMOKE_CREDENTIAL_ID=$previousCredential }
Write-Output 'WinUI component and Demo verification passed.'
