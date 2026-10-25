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
if (!$DotNet) {
    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    $DotNet = if ($command) { $command.Source } else { 'C:\dev\dotnet\dotnet.exe' }
}
if (!(Test-Path $DotNet)) { throw 'Install the .NET 10 SDK or pass -DotNet with its full path.' }
if (!(Test-Path "$NativeDirectory/XmaxSDK.dll")) { throw 'Pass -NativeDirectory with the native SDK and its dependencies.' }
$env:DOTNET_ROOT = Split-Path $DotNet
& $DotNet build "$repo/Examples/XLabWpf" -c Release "-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/" "-p:XmaxNativeDir=$NativeDirectory"
if ($LASTEXITCODE -ne 0) { throw 'WPF build failed.' }
$app = Join-Path $BuildRoot 'bin/XLabWpf/Release/net10.0-windows/win-x64/XLabWpf.dll'
$appArguments = @(('"' + $app + '"'))
if ($PreviewSession) { $appArguments += '--preview-session' }
elseif ($Preview) { $appArguments += '--preview' }
$process = Start-Process $DotNet -ArgumentList $appArguments -WorkingDirectory (Split-Path $app) -PassThru
Write-Output "WPF XLab started (PID $($process.Id))."
