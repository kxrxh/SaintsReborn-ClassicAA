#requires -Version 7.0
param([switch]$Test, [string]$GameRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$output = Join-Path $repo 'build\installer'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$version = [regex]::Match([IO.File]::ReadAllText("$repo\mod.ini"), '(?m)^version\s*=\s*(\d+\.\d+\.\d+)\s*$').Groups[1].Value
if (!$version) { throw 'Missing mod version.' }
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler = Join-Path $framework 'csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.8 is required to build the Windows installer.' }
$payload = Join-Path $output 'payload.zip'
if (Test-Path -LiteralPath $payload) { Remove-Item -LiteralPath $payload }
$files = @('Build.ps1','Install.ps1','Uninstall.ps1','main.cpp','mod.ini','README.md','LICENSE','THIRD_PARTY_NOTICES.md') | ForEach-Object { Get-Item -LiteralPath (Join-Path $repo $_) }
foreach ($directory in @('renderer-extension','scripts','third-party-licenses')) { $files += Get-ChildItem -LiteralPath (Join-Path $repo $directory) -File -Recurse }
$archive = [IO.Compression.ZipFile]::Open($payload, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($repo, $file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
[IO.File]::WriteAllText("$output\Version.cs", "[assembly: System.Reflection.AssemblyVersion(`"$version.0`")]`n[assembly: System.Reflection.AssemblyCompany(`"kxrxh`")]`n[assembly: System.Reflection.AssemblyProduct(`"Classic AA`")]")
$references = @('System.dll','System.Core.dll','System.Net.Http.dll','System.Web.Extensions.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','System.Xaml.dll') | ForEach-Object { "/reference:$framework\$_" }
foreach ($assembly in @('PresentationFramework','PresentationCore','WindowsBase')) { $references += "/reference:$framework\WPF\$assembly.dll" }
$common = @('/nologo','/optimize+','/platform:x64',"/resource:$PSScriptRoot\MainWindow.xaml,MainWindow.xaml","/resource:$payload,payload.zip", "$PSScriptRoot\Program.cs", "$output\Version.cs") + $references
$exe = Join-Path $output "SaintsReborn-ClassicAA-Setup-v$version.exe"
& $compiler /target:winexe "/out:$exe" @common
if ($LASTEXITCODE) { throw 'Windows installer build failed.' }
if ($Test) {
    & $compiler /target:exe /main:InstallerTests "/out:$output\installer-tests.exe" @common "$repo\tests\installer_test.cs"
    if ($LASTEXITCODE) { throw 'Installer tests did not compile.' }
    if ($GameRoot) { & "$output\installer-tests.exe" $GameRoot }
    else { & "$output\installer-tests.exe" }
    if ($LASTEXITCODE) { throw 'Installer tests failed.' }
}
Write-Output $exe
