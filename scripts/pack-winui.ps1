[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$NativeDirectory,
    [string]$BuildRoot='C:\dev\XmaxSDK-dotnet',
    [string]$DotNet='dotnet',
    [string]$DependencyNoticesDirectory=''
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$NativeDirectory=[IO.Path]::GetFullPath($NativeDirectory)
if (!(Test-Path (Join-Path $NativeDirectory 'XmaxSDK.dll'))) { throw 'Missing native SDK.' }
$version=(Get-Content "$repo/VERSION" -Raw).Trim()
$output=Join-Path $BuildRoot 'packages'
$properties=@('-p:Platform=x64',"-p:XmaxDotNetBuildRoot=$($BuildRoot.Replace('\','/'))/","-p:XmaxNativeDir=$NativeDirectory")
if (!$DependencyNoticesDirectory) {
    $candidate=Join-Path (Split-Path $NativeDirectory) 'licenses'
    if (Test-Path $candidate) { $DependencyNoticesDirectory=$candidate }
}
if ((Test-Path (Join-Path $NativeDirectory 'agora_rtc_sdk.dll')) -and !$DependencyNoticesDirectory) {
    throw 'Agora dependency notices are required. Use an extracted SDK package or provide -DependencyNoticesDirectory.'
}
if ($DependencyNoticesDirectory) {
    $DependencyNoticesDirectory=[IO.Path]::GetFullPath($DependencyNoticesDirectory)
    if (!(Test-Path $DependencyNoticesDirectory -PathType Container)) { throw 'Dependency notices directory does not exist.' }
    $properties+="-p:XmaxNativeNoticesDir=$DependencyNoticesDirectory"
}
function Invoke-Checked([string[]]$Arguments) { & $DotNet @Arguments; if ($LASTEXITCODE -ne 0) { throw 'WinUI packaging command failed.' } }
New-Item -ItemType Directory -Force $output | Out-Null
Invoke-Checked (@('pack',"$repo/Sources/XmaxSDK.Net",'-c','Release','-o',$output)+$properties)
Invoke-Checked (@('pack',"$repo/Sources/XmaxSDK.WinUI",'-c','Release','-o',$output)+$properties)
& "$PSScriptRoot/verify-winui-package.ps1" -BuildRoot $BuildRoot -DotNet $DotNet
if (!$?) { throw 'Independent WinUI NuGet consumer failed.' }
$publish=Join-Path $BuildRoot 'publish/XLabWinUI'
Invoke-Checked (@('publish',"$repo/Examples/XLabWinUI",'-c','Release','-r','win-x64','--self-contained','false','-o',$publish)+$properties)
Copy-Item "$repo/LICENSE","$repo/THIRD_PARTY.md" $publish -Force
Copy-Item "$repo/Materials/winui.md" "$publish/README.md" -Force
if ($DependencyNoticesDirectory) {
    $notices=Join-Path $publish 'licenses/native'; New-Item -ItemType Directory -Force $notices | Out-Null
    Copy-Item "$DependencyNoticesDirectory/*" $notices -Recurse -Force
}
# Preserve licenses for the redistributed Windows App SDK and managed runtime dependencies.
$assets=Get-Content (Join-Path $BuildRoot 'obj/XLabWinUI/project.assets.json') -Raw | ConvertFrom-Json
$packageRoot=$assets.packageFolders.PSObject.Properties.Name | Select-Object -First 1
foreach ($entry in $assets.libraries.PSObject.Properties) {
    if ($entry.Value.type -ne 'package' -or $entry.Name -match 'BuildTools|SDK.NET.Ref|CsWinRT') { continue }
    $package=Join-Path $packageRoot $entry.Value.path
    $licenses=Get-ChildItem $package -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '(?i)license|notice' }
    if ($licenses) {
        $destination=Join-Path $publish ('licenses/managed/'+$entry.Name.Replace('/','-'))
        New-Item -ItemType Directory -Force $destination | Out-Null
        $licenses | Copy-Item -Destination $destination -Force
    }
}
Compress-Archive -Path "$publish/*" -DestinationPath "$output/XLabWinUI-$version-win-x64.zip" -Force
Get-ChildItem $output -File | Where-Object { $_.Name -ne 'SHA256SUMS' } | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.Name
} | Set-Content "$output/SHA256SUMS" -Encoding UTF8
Write-Output "WinUI package and Demo archive created: $output"
