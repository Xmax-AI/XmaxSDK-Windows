[CmdletBinding()]
param([string]$BuildRoot='C:\dev\XmaxSDK-dotnet',[string]$DotNet='dotnet')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$version=(Get-Content "$repo/VERSION" -Raw).Trim()
$packages=Join-Path $BuildRoot 'packages'
$consumer=Join-Path $BuildRoot 'winui-package-consumer'
New-Item -ItemType Directory -Force $consumer | Out-Null
@"
<?xml version="1.0" encoding="utf-8"?>
<configuration><packageSources><clear/><add key="local-xmax" value="$packages"/><add key="nuget.org" value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>
"@ | Set-Content "$consumer/NuGet.Config" -Encoding UTF8
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>
<OutputType>WinExe</OutputType><TargetFramework>net10.0-windows10.0.26100.0</TargetFramework>
<SupportedOSPlatformVersion>10.0.19041.0</SupportedOSPlatformVersion><UseWinUI>true</UseWinUI>
<WindowsPackageType>None</WindowsPackageType><WindowsAppSDKSelfContained>true</WindowsAppSDKSelfContained>
<PlatformTarget>x64</PlatformTarget><Platforms>x64</Platforms><RuntimeIdentifier>win-x64</RuntimeIdentifier>
<Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings>
</PropertyGroup><ItemGroup>
<PackageReference Include="Xmax.SDK.WinUI" Version="$version"/>
<PackageReference Include="Microsoft.Windows.SDK.BuildTools" Version="10.0.28000.2705" PrivateAssets="all"/>
</ItemGroup></Project>
"@ | Set-Content "$consumer/consumer.csproj" -Encoding UTF8
@'
<Application x:Class="WinUiConsumer.App" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"><Application.Resources><ResourceDictionary><ResourceDictionary.MergedDictionaries><XamlControlsResources xmlns="using:Microsoft.UI.Xaml.Controls"/></ResourceDictionary.MergedDictionaries></ResourceDictionary></Application.Resources></Application>
'@ | Set-Content "$consumer/App.xaml" -Encoding UTF8
@'
<Window x:Class="WinUiConsumer.MainWindow" xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:sdk="using:Xmax.SDK.WinUI"><sdk:XmaxRealtimeVideoView x:Name="Video" Width="160" Height="80"/></Window>
'@ | Set-Content "$consumer/MainWindow.xaml" -Encoding UTF8
@'
using Microsoft.UI.Xaml;
using Xmax.SDK;
namespace WinUiConsumer;
public sealed partial class MainWindow : Window { public MainWindow() => InitializeComponent(); public Xmax.SDK.WinUI.XmaxRealtimeVideoView View => Video; }
public partial class App : Application
{
    private MainWindow? window;
    public App() => InitializeComponent();
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        bool passed=false; string? error=null;
        try
        {
            window=new(); window.Activate(); await Task.Delay(200);
            using var client=new XmaxClient();
            await using var session=client.CreateRealtime();
            using var sink=await session.BindExternalAsync(new(16,8));
            window.View.VideoSource=session.LocalVideo;
            byte[] pixels=new byte[64*8]; for(int i=0;i<pixels.Length;i+=4) { pixels[i+2]=255; pixels[i+3]=255; }
            sink.PushBgra(pixels,64,1000);
            var deadline=DateTime.UtcNow.AddSeconds(10);
            while(window.View.PresentedFrameCount==0 && DateTime.UtcNow<deadline) await Task.Delay(20);
            if(window.View.VideoWidth!=16 || window.View.VideoHeight!=8 || window.View.PresentedFrameCount==0) throw new Exception("Packaged XAML control did not present native video.");
            window.View.VideoSource=null; passed=true;
        }
        catch(Exception failure) { error=failure.ToString(); }
        finally
        {
            await System.IO.File.WriteAllTextAsync(Environment.GetEnvironmentVariable("XMAX_WINUI_CONSUMER_RESULT")!,System.Text.Json.JsonSerializer.Serialize(new {passed,error}));
            window?.Close(); Exit();
        }
    }
}
'@ | Set-Content "$consumer/App.xaml.cs" -Encoding UTF8
$previousNative=$env:XMAX_NATIVE_DIR
$previousPackages=$env:NUGET_PACKAGES
$previousResult=$env:XMAX_WINUI_CONSUMER_RESULT
try {
    # Isolate our changing development packages from every existing NuGet cache.
    $env:NUGET_PACKAGES=Join-Path $BuildRoot ('winui-package-cache/'+[Guid]::NewGuid().ToString('N'))
    $env:XMAX_NATIVE_DIR=$null
    $env:XMAX_WINUI_CONSUMER_RESULT=Join-Path $consumer 'result.json'
    Remove-Item $env:XMAX_WINUI_CONSUMER_RESULT -ErrorAction SilentlyContinue
    & $DotNet restore "$consumer/consumer.csproj" --force --configfile "$consumer/NuGet.Config"
    if ($LASTEXITCODE -ne 0) { throw 'WinUI NuGet consumer restore failed.' }
    & $DotNet build "$consumer/consumer.csproj" -c Release --no-restore '-p:Platform=x64'
    if ($LASTEXITCODE -ne 0) { throw 'WinUI NuGet/XAML consumer build failed.' }
    $env:DOTNET_ROOT=Split-Path (Get-Command $DotNet).Source
    $process=Start-Process "$consumer/bin/x64/Release/net10.0-windows10.0.26100.0/win-x64/consumer.exe" -PassThru
    if (!$process.WaitForExit(45000)) { Stop-Process -Id $process.Id -Force; throw 'WinUI NuGet consumer timed out.' }
    if (!(Test-Path $env:XMAX_WINUI_CONSUMER_RESULT) -or !(Get-Content $env:XMAX_WINUI_CONSUMER_RESULT -Raw | ConvertFrom-Json).passed) { throw 'Independent WinUI NuGet/XAML consumer failed.' }
    Write-Output 'Independent WinUI NuGet/XAML consumer presented native video successfully.'
}
finally { $env:XMAX_NATIVE_DIR=$previousNative; $env:NUGET_PACKAGES=$previousPackages; $env:XMAX_WINUI_CONSUMER_RESULT=$previousResult }
