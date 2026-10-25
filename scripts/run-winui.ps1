[CmdletBinding()]
param(
    [string]$NativeDirectory = 'C:\dev\XmaxSDK-build\Sources\XmaxSDK\RelWithDebInfo',
    [string]$BuildRoot = 'C:\dev\XmaxSDK-dotnet',
    [string]$DotNet = '',
    [switch]$Preview,
    [switch]$PreviewSession
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$DotNet) { $command = Get-Command dotnet -ErrorAction SilentlyContinue; $DotNet = if ($command) { $command.Source } else { 'C:\dev\dotnet\dotnet.exe' } }
if (!(Test-Path $DotNet)) { throw 'Install .NET 10 SDK or pass -DotNet.' }
if (!(Test-Path "$NativeDirectory/XmaxSDK.dll")) { throw 'Pass -NativeDirectory with the native SDK and matching dependencies.' }
$env:DOTNET_ROOT = Split-Path $DotNet
& $DotNet build "$repo/Examples/XLabWinUI" -c Release '-p:Platform=x64' "-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/" "-p:XmaxNativeDir=$NativeDirectory"
if ($LASTEXITCODE -ne 0) { throw 'WinUI build failed.' }
$app = Join-Path $BuildRoot 'bin/XLabWinUI/x64/Release/net10.0-windows10.0.26100.0/win-x64/XLabWinUI.exe'
$arguments = @()
if ($PreviewSession) { $arguments += '--preview-session' }
elseif ($Preview) { $arguments += '--preview' }
$start = @{ FilePath=$app; WorkingDirectory=(Split-Path $app); PassThru=$true }
if ($arguments.Count) { $start.ArgumentList=$arguments }
$process = Start-Process @start
Write-Output "WinUI 3 XLab started (PID $($process.Id))."
