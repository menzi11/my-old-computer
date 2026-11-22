# Builds the native Windows version with the MSVC toolchain that ships with
# Visual Studio (Build Tools are enough).  No CMake, no package manager.
#
#   .\build.ps1            release build
#   .\build.ps1 -Debug     unoptimised build
#   .\build.ps1 -Clean     remove build\ first
param(
    [switch]$Debug,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir = Join-Path $root 'src'
$outDir = Join-Path $root 'build'

if ($Clean -and (Test-Path $outDir)) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) not found. Install the Desktop development with C++ workload.'
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'MSVC C++ tools not found. Install "Desktop development with C++" in Visual Studio.'
}
$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $devCmd)) { throw "VsDevCmd.bat not found under $vsPath" }

$sources = (Get-ChildItem $srcDir -Filter *.cpp | ForEach-Object { '"' + $_.FullName + '"' }) -join ' '
$defines = '/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00'
$flags = if ($Debug) { '/Od /Zi /MDd /RTC1' } else { '/O2 /GS- /MD' }
$libs = 'user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib oleaut32.lib windowscodecs.lib shlwapi.lib msimg32.lib comdlg32.lib advapi32.lib'
$link = "/SUBSYSTEM:WINDOWS /OUT:`"$outDir\MyOldComputer.exe`""

$cmd = '"{0}" -arch=amd64 -host_arch=amd64 >nul && rc /nologo /I "{3}" /fo "{4}\app.res" "{3}\app.rc" && cl /nologo /std:c++17 /EHsc /W3 /utf-8 {1} {2} /Fo"{4}\\" {5} /link {6} {7} "{4}\app.res"' -f `
    $devCmd, $defines, $flags, $srcDir, $outDir, $sources, $link, $libs

Write-Host "Building My Old Computer (Windows)..." -ForegroundColor Cyan
& cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$exe = Join-Path $outDir 'MyOldComputer.exe'
Write-Host ("Built {0} ({1:N0} bytes)" -f $exe, (Get-Item $exe).Length) -ForegroundColor Green
