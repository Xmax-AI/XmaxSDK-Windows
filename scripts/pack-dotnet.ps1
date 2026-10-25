[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$NativeDirectory,
    [string]$BuildRoot = '',
    [string]$DotNet = 'dotnet',
    [string]$DependencyNoticesDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$BuildRoot) { $BuildRoot = Join-Path $repo '.build/dotnet' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$NativeDirectory = [IO.Path]::GetFullPath($NativeDirectory)
if (!(Test-Path (Join-Path $NativeDirectory 'XmaxSDK.dll'))) { throw 'Missing native SDK.' }
$version = (Get-Content "$repo/VERSION" -Raw).Trim()
$output = Join-Path $BuildRoot 'packages'
$properties = @("-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/", "-p:XmaxNativeDir=$NativeDirectory")
if (!$DependencyNoticesDirectory) {
    $candidate = Join-Path (Split-Path $NativeDirectory) 'licenses'
    if (Test-Path $candidate) { $DependencyNoticesDirectory = $candidate }
}
if ((Test-Path (Join-Path $NativeDirectory 'agora_rtc_sdk.dll')) -and !$DependencyNoticesDirectory) {
    throw 'Agora dependency notices are required. Use an extracted SDK package or provide -DependencyNoticesDirectory.'
}
if ($DependencyNoticesDirectory) {
    $DependencyNoticesDirectory = [IO.Path]::GetFullPath($DependencyNoticesDirectory)
    if (!(Test-Path $DependencyNoticesDirectory -PathType Container)) { throw 'Dependency notices directory does not exist.' }
    $properties += "-p:XmaxNativeNoticesDir=$DependencyNoticesDirectory"
}
function Invoke-Checked([string[]]$Arguments) {
    & $DotNet @Arguments
    if ($LASTEXITCODE -ne 0) { throw "dotnet failed with exit code $LASTEXITCODE." }
}
New-Item -ItemType Directory -Force $output | Out-Null
Invoke-Checked (@('pack', "$repo/Sources/XmaxSDK.Net", '-c', 'Release', '-o', $output) + $properties)
Invoke-Checked (@('pack', "$repo/Sources/XmaxSDK.Wpf", '-c', 'Release', '-o', $output) + $properties)

# Consume the package outside the source tree, with no XmaxNativeDir or project reference.
$consumer = Join-Path $BuildRoot 'package-consumer'
New-Item -ItemType Directory -Force $consumer | Out-Null
@'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><RuntimeIdentifier>win-x64</RuntimeIdentifier><PlatformTarget>x64</PlatformTarget></PropertyGroup></Project>
'@ | Set-Content "$consumer/consumer.csproj" -Encoding UTF8
@'
using Xmax.SDK;
using var client = new XmaxClient();
if (!client.GetCapabilities().HasFlag(XmaxCapabilities.Storage)) throw new System.Exception("Invalid package capabilities");
if (XmaxClient.GetModelInfo().SupportedFormats.Count != 2) throw new System.Exception("Invalid model ABI");
System.Console.WriteLine("Independent NuGet consumer passed: " + XmaxRuntime.Version);
'@ | Set-Content "$consumer/Program.cs" -Encoding UTF8
Invoke-Checked @('add', "$consumer/consumer.csproj", 'package', 'Xmax.SDK', '--version', $version, '--source', $output)
$previousNative = $env:XMAX_NATIVE_DIR
$previousPackages = $env:NUGET_PACKAGES
try {
    $env:NUGET_PACKAGES = Join-Path $BuildRoot ('package-cache/' + [Guid]::NewGuid().ToString('N'))
    $env:XMAX_NATIVE_DIR = $null
    Invoke-Checked @('restore', "$consumer/consumer.csproj", '--force', '--source', $output)
    Invoke-Checked @('run', '--project', "$consumer/consumer.csproj", '-c', 'Release', '--no-restore')
}
finally { $env:XMAX_NATIVE_DIR = $previousNative; $env:NUGET_PACKAGES = $previousPackages }

$publish = Join-Path $BuildRoot 'publish/XLabWpf'
Invoke-Checked (@('publish', "$repo/Examples/XLabWpf", '-c', 'Release', '-r', 'win-x64', '--self-contained', 'false', '-o', $publish) + $properties)
Copy-Item "$repo/LICENSE", "$repo/THIRD_PARTY.md" $publish -Force
Copy-Item "$repo/Materials/wpf.md" "$publish/README.md" -Force
if ($DependencyNoticesDirectory) {
    $notices = Join-Path $publish 'licenses/native'
    New-Item -ItemType Directory -Force $notices | Out-Null
    Copy-Item "$DependencyNoticesDirectory/*" $notices -Recurse -Force
}
Compress-Archive -Path "$publish/*" -DestinationPath "$output/XLabWpf-$version-win-x64.zip" -Force
Get-ChildItem $output -File | Where-Object { $_.Name -ne 'SHA256SUMS' } | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
} | Set-Content "$output/SHA256SUMS" -Encoding UTF8
Write-Output "Verified managed packages: $output"
