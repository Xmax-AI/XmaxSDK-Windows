[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$NativeDirectory,
    [string]$BuildRoot = '',
    [string]$DotNet = 'dotnet',
    [string]$CMake = 'cmake',
    [string]$DependencyNoticesDirectory = '',
    [switch]$Camera,
    [switch]$Live,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildRoot) { $BuildRoot = Join-Path $repo '.build/dotnet' }
if ($BuildRoot.StartsWith('\\')) { throw 'Use a local Windows BuildRoot, e.g. C:\dev\XmaxSDK-dotnet.' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$NativeDirectory = [IO.Path]::GetFullPath($NativeDirectory)
if (!(Test-Path (Join-Path $NativeDirectory 'XmaxSDK.dll'))) { throw 'NativeDirectory must contain XmaxSDK.dll and matching dependencies.' }
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_ROOT = Split-Path (Get-Command $DotNet -ErrorAction Stop).Source
$properties = @("-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/", "-p:XmaxNativeDir=$NativeDirectory")
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

# Build an independent C probe using the public header, not managed hard-coded structure sizes.
$probeBuild = Join-Path $BuildRoot 'abi-native'
Invoke-Checked $CMake @('-S', $repo, '-B', $probeBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DBUILD_TESTING=ON', '-DXMAX_BUILD_EXAMPLE=OFF', '-DXMAX_WITH_AGORA=OFF')
Invoke-Checked $CMake @('--build', $probeBuild, '--config', 'Release', '--target', 'xmax_dotnet_abi_probe')
$layout = Join-Path $BuildRoot 'native-layout.txt'
& (Join-Path $probeBuild 'Release/xmax_dotnet_abi_probe.exe') | Set-Content $layout -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'Native ABI probe failed.' }
Invoke-Checked $DotNet (@('run', '--project', "$repo/Tests/XmaxSDKNetTests", '-c', 'Release') + $properties + @('--', $layout))
Invoke-Checked $DotNet (@('run', '--project', "$repo/Tests/XmaxSDKWpfTests", '-c', 'Release') + $properties)
Invoke-Checked $DotNet (@('build', "$repo/Examples/XLabWpf", '-c', 'Release') + $properties)
$app = Join-Path $BuildRoot 'bin/XLabWpf/Release/net10.0-windows/win-x64/XLabWpf.dll'
$env:XMAX_SMOKE_OUTPUT = Join-Path $BuildRoot 'screenshots'

function Invoke-Smoke([string]$Mode) {
    # Explicit process timeout also covers a hung native close, not only the managed await.
    $process = Start-Process -FilePath (Get-Command $DotNet).Source -ArgumentList @(('"' + $app + '"'), $Mode) -PassThru
    if (!$process.WaitForExit(150000)) { Stop-Process -Id $process.Id -Force; throw "$Mode timed out." }
    if ($process.ExitCode -ne 0) { throw "$Mode failed; inspect $env:XMAX_SMOKE_OUTPUT." }
    $report = Join-Path $env:XMAX_SMOKE_OUTPUT ($Mode.TrimStart('-') + '.json')
    if (!(Test-Path $report) -or !(Get-Content $report -Raw | ConvertFrom-Json).passed) { throw "$Mode did not produce a passing report." }
    Get-Content $report
}
Invoke-Smoke '--smoke-test'
Invoke-Smoke '--smoke-test-ui'
Invoke-Smoke '--smoke-test-key-gate'
$previousCredentialId = $env:XMAX_SMOKE_CREDENTIAL_ID
try {
    $env:XMAX_SMOKE_CREDENTIAL_ID = [Guid]::NewGuid().ToString('N')
    foreach ($phase in @('write', 'read', 'clear', 'empty')) { Invoke-Smoke "--smoke-test-key-memory-$phase" }
}
finally { $env:XMAX_SMOKE_CREDENTIAL_ID = $previousCredentialId }
if ($Camera -or $Live) { Invoke-Smoke '--smoke-test-camera' }
if ($Live) {
    if (!$env:XMAX_API_KEY) { throw 'Set XMAX_API_KEY for explicit real-service validation; it is never printed or saved.' }
    Invoke-Smoke '--smoke-test-live'
}
if ($Package) {
    & "$PSScriptRoot/pack-dotnet.ps1" -NativeDirectory $NativeDirectory -BuildRoot $BuildRoot -DotNet $DotNet -DependencyNoticesDirectory $DependencyNoticesDirectory
    if (!$?) { throw '.NET packaging failed.' }
}
Write-Output 'C# SDK and WPF verification passed.'
