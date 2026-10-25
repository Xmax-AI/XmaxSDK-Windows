[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [Parameter(Mandatory=$true)][string]$DestinationPath
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$source=[IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
$destination=[IO.Path]::GetFullPath($DestinationPath)
$temporary=$destination+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
try {
    $archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in [IO.Directory]::GetFiles($source,'*',[IO.SearchOption]::AllDirectories)) {
            # ZIP entry names use forward slashes, including on Windows PowerShell 5.1.
            $relative=$file.Substring($source.Length).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file,$relative,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
    finally { $archive.Dispose() }
    Move-Item $temporary $destination -Force
}
finally { if (Test-Path $temporary) { Remove-Item $temporary -Force } }
