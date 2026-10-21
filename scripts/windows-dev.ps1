[CmdletBinding()]
param(
    [ValidateSet('configure', 'build', 'test', 'open', 'run')]
    [string]$Action = 'build',
    [string]$BuildRoot = 'C:\dev\XmaxSDK-build',
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$AgoraRoot = $env:XMAX_AGORA_ROOT,
    [ValidatePattern('^([D-Zd-z]:)?$')]
    [string]$SourceDrive = '',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [ValidateRange(1, 64)]
    [int]$Parallel = 4
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$sourceRoot = Split-Path -Parent $PSScriptRoot
$disks = @(Get-CimInstance Win32_LogicalDisk)
if ($sourceRoot.StartsWith('\\')) {
    # Qt custom build commands use cmd.exe, whose working directory cannot be UNC.
    if (-not $SourceDrive) {
        $existing = $disks | Where-Object {
            $_.ProviderName -and $_.ProviderName.TrimEnd('\') -ieq $sourceRoot.TrimEnd('\')
        } | Select-Object -First 1
        if ($existing) {
            $SourceDrive = $existing.DeviceID
        } else {
            $SourceDrive = @('S:', 'R:', 'Q:', 'P:', 'O:', 'N:', 'M:', 'L:') |
                Where-Object { $_ -notin $disks.DeviceID } | Select-Object -First 1
        }
        if (-not $SourceDrive) { throw 'No free source drive. Pass -SourceDrive with an available drive.' }
    }
    $disk = $disks | Where-Object { $_.DeviceID -ieq $SourceDrive }
    if ($disk -and ((-not $disk.ProviderName) -or $disk.ProviderName.TrimEnd('\') -ine $sourceRoot.TrimEnd('\'))) {
        throw "$SourceDrive is already in use. Choose a free drive with -SourceDrive."
    }
    if (-not $disk) {
        & net.exe use $SourceDrive $sourceRoot /persistent:yes
        if ($LASTEXITCODE -ne 0) { throw 'Failed to map the shared source directory.' }
    }
    $sourceRoot = "$SourceDrive\"
}
if (-not [IO.Path]::IsPathRooted($BuildRoot)) { throw 'BuildRoot must be an absolute Windows path.' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if ($BuildRoot.StartsWith('\\') -or $BuildRoot.StartsWith('//')) {
    throw 'BuildRoot must be on a local Windows disk, separate from the shared source.'
}
$buildDrive = $disks | Where-Object { $_.DeviceID -ieq [IO.Path]::GetPathRoot($BuildRoot).Substring(0, 2) }
if (-not $buildDrive -or $buildDrive.DriveType -notin @(2, 3)) {
    throw 'BuildRoot must use a local Windows drive, not a mapped network drive.'
}
if (-not $QtRoot) { $QtRoot = 'C:\Qt\6.8.3\msvc2022_64' }
if (-not $AgoraRoot) { $AgoraRoot = 'C:\dev\XmaxSDK-deps\agora\sdk' }
if (-not (Test-Path "$QtRoot\bin\windeployqt.exe")) {
    throw 'Qt MSVC x64 was not found. Set QT_ROOT_DIR or pass -QtRoot.'
}
if (-not (Test-Path "$AgoraRoot\high_level_api\include\IAgoraRtcEngine.h")) {
    throw 'Agora Windows SDK was not found. Set XMAX_AGORA_ROOT or pass -AgoraRoot.'
}
if (Test-Path 'C:\dev\XmaxTools\Scripts\cmake.exe') {
    $env:PATH = 'C:\dev\XmaxTools\Scripts;' + $env:PATH
}
$env:PATH = "$QtRoot\bin;" + $env:PATH
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$executable = Join-Path $BuildRoot "Examples\XLab\$Configuration\XLab.exe"

# Keep command execution and error propagation identical for every entry point.
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

# Generate the Visual Studio solution without placing cache files in the source tree.
function Configure-Project {
    Invoke-Checked $cmake @(
        '-S', $sourceRoot, '-B', $BuildRoot,
        '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DBUILD_TESTING=ON', '-DXMAX_BUILD_EXAMPLE=ON', '-DXMAX_WITH_AGORA=ON',
        "-DXMAX_AGORA_ROOT=$AgoraRoot", "-DCMAKE_PREFIX_PATH=$QtRoot"
    )
}

# Deploy the same Qt runtime and test platform used by the full Windows verification.
function Build-Project {
    $running = Get-Process XLab -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $executable }
    if ($running) { throw 'Close the running XLab Demo before rebuilding its DLLs.' }
    Configure-Project
    Invoke-Checked $cmake @('--build', $BuildRoot, '--config', $Configuration, '--parallel', "$Parallel")
    $deploymentMode = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
    Invoke-Checked "$QtRoot\bin\windeployqt.exe" @(
        $deploymentMode, '--verbose', '0', '--qmldir', "$sourceRoot\Examples\XLab\qml", $executable
    )
    # Offscreen is required by CTest, but windeployqt normally selects qwindows only.
    $plugin = if ($Configuration -eq 'Debug') { 'qoffscreend.dll' } else { 'qoffscreen.dll' }
    Copy-Item "$QtRoot\plugins\platforms\$plugin" (Join-Path (Split-Path $executable) "platforms\$plugin") -Force
}

switch ($Action) {
    'configure' { Configure-Project }
    'build' { Build-Project }
    'test' {
        Build-Project
        $env:QT_PLUGIN_PATH = "$QtRoot\plugins"
        Invoke-Checked $ctest @('--test-dir', $BuildRoot, '-C', $Configuration, '--output-on-failure')
    }
    'open' {
        Configure-Project
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $visualStudio = & $vswhere -latest -version '[17.0,18.0)' -products '*' -property productPath
        if (-not $visualStudio) { throw 'Visual Studio 2022 was not found.' }
        Start-Process $visualStudio -ArgumentList ('"' + (Join-Path $BuildRoot 'XmaxWindows.sln') + '"')
    }
    'run' {
        Build-Project
        Start-Process $executable -WorkingDirectory $BuildRoot
    }
}
