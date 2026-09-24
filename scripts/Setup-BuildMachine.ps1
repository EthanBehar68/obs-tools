[CmdletBinding()]
param(
    # Folder holding the portable toolchain (cmake, git, vs2022-buildtools). Defaults to ..\..\tools next to the repo.
    [string] $ToolsDir
)

# Prepares a Windows PC to build obs-unified-chat using only the toolchain copied on the drive.
# Installs Visual Studio 2022 Build Tools from the offline layout (needs admin, no internet),
# then marks the repo as a safe git directory (FAT32/exFAT drives have no file ownership).

$ErrorActionPreference = 'Stop'

if (-not $ToolsDir) { $ToolsDir = Join-Path $PSScriptRoot '..\..\tools' }
$ToolsDir = (Resolve-Path $ToolsDir).Path
$RepoDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$CMake = Join-Path $ToolsDir 'cmake\bin\cmake.exe'
$Git = Join-Path $ToolsDir 'git\cmd\git.exe'
$Layout = Join-Path $ToolsDir 'vs2022-buildtools\layout'

foreach ($Path in $CMake, $Git, (Join-Path $Layout 'vs_BuildTools.exe')) {
    if (-not (Test-Path $Path)) {
        throw "Missing $Path. Copy the whole OBS-Dev folder, including tools\, to this PC."
    }
}

$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$HasMsvc = $false
if (Test-Path $VsWhere) {
    $Found = & $VsWhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $HasMsvc = [bool] $Found
}

if ($HasMsvc) {
    Write-Host "Visual Studio 2022 C++ build tools already installed: $Found"
} else {
    $IsAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $IsAdmin) {
        throw 'Visual Studio 2022 Build Tools are not installed. Re-run this script from an elevated (Run as administrator) PowerShell.'
    }
    Write-Host 'Installing Visual Studio 2022 Build Tools from the offline layout (this takes several minutes)...'
    $Installer = Start-Process -FilePath (Join-Path $Layout 'vs_BuildTools.exe') -ArgumentList '--noWeb', '--passive', '--wait', '--norestart' -Wait -PassThru
    # 3010 = success, reboot required
    if ($Installer.ExitCode -ne 0 -and $Installer.ExitCode -ne 3010) {
        throw "Visual Studio Build Tools installer failed with exit code $($Installer.ExitCode)"
    }
    if ($Installer.ExitCode -eq 3010) {
        Write-Warning 'Build Tools installed. Windows asks for a restart before building.'
    }
}

$SafeDirs = @(& $Git config --global --get-all safe.directory)
$RepoGitPath = $RepoDir -replace '\\', '/'
if ($SafeDirs -notcontains $RepoGitPath) {
    & $Git config --global --add safe.directory $RepoGitPath
    Write-Host "Marked $RepoGitPath as a safe git directory"
}

Write-Host ''
Write-Host "CMake: $(& $CMake --version | Select-Object -First 1)"
Write-Host "Git:   $(& $Git --version)"
Write-Host ''
Write-Host 'Build machine ready. Next: .\scripts\Build.ps1 -Install'
