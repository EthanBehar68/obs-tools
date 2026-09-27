[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    # Copy the plugins into C:\ProgramData\obs-studio\plugins (close OBS first)
    [switch] $Install,
    # Produce release\<plugin>-<version>-windows-x64.zip for each plugin
    [switch] $Package,
    [switch] $SkipTests,
    # Folder holding the portable toolchain. Defaults to ..\..\tools next to the repo.
    [string] $ToolsDir
)

# Configures, builds, tests and optionally installs/packages every plugin with the portable toolchain.

$ErrorActionPreference = 'Stop'

$RepoDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ToolsDir) { $ToolsDir = Join-Path $PSScriptRoot '..\..\tools' }
$ToolsDir = (Resolve-Path $ToolsDir).Path
$env:PATH = "$ToolsDir\cmake\bin;$ToolsDir\git\cmd;$env:PATH"

function Invoke-Checked {
    param([string] $Exe, [string[]] $Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

Push-Location $RepoDir
try {
    # CMake caches absolute paths. If the drive letter or folder changed since the last configure
    # (e.g. the drive was moved to another PC), start from a fresh build tree.
    $Cache = Join-Path $RepoDir 'build_x64\CMakeCache.txt'
    if (Test-Path $Cache) {
        $HomeLine = Select-String -Path $Cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
        $CachedHome = if ($HomeLine) { $HomeLine.Matches[0].Groups[1].Value } else { '' }
        $CompilerLine = Select-String -Path $Cache -Pattern '^CMAKE_GENERATOR_INSTANCE:\w+=(.*)$' | Select-Object -First 1
        $CachedCompiler = if ($CompilerLine) { $CompilerLine.Matches[0].Groups[1].Value } else { '' }
        $Moved = $CachedHome -and ((Resolve-Path $CachedHome -ErrorAction SilentlyContinue).Path -ne $RepoDir)
        $CompilerGone = $CachedCompiler -and -not (Test-Path $CachedCompiler)
        if ($Moved -or $CompilerGone) {
            Write-Host 'Build tree was configured on another machine or path; reconfiguring from scratch'
            Remove-Item -Recurse -Force (Join-Path $RepoDir 'build_x64')
            Get-ChildItem (Join-Path $RepoDir '.deps') -Directory -Filter 'obs-studio-*' -ErrorAction SilentlyContinue |
                ForEach-Object { Remove-Item -Recurse -Force (Join-Path $_.FullName 'build_x64') -ErrorAction SilentlyContinue }
        }
    }

    Write-Host '== Configure'
    Invoke-Checked cmake @('--preset', 'windows-x64')

    Write-Host "== Build ($Configuration)"
    Invoke-Checked cmake @('--build', 'build_x64', '--config', $Configuration, '--parallel', '--', '/consoleLoggerParameters:Summary', '/noLogo')

    if (-not $SkipTests) {
        Write-Host '== Test'
        Invoke-Checked ctest @('--test-dir', 'build_x64', '-C', $Configuration, '--output-on-failure')
    }

    if ($Install) {
        if (Get-Process obs64 -ErrorAction SilentlyContinue) {
            throw 'OBS is running. Close OBS before installing.'
        }
        Write-Host '== Install'
        Invoke-Checked cmake @('--install', 'build_x64', '--config', $Configuration)
    }

    if ($Package) {
        Write-Host '== Package'
        $Staging = Join-Path $RepoDir "release\$Configuration"
        if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
        Invoke-Checked cmake @('--install', 'build_x64', '--config', $Configuration, '--prefix', $Staging)
        foreach ($SpecFile in Get-ChildItem (Join-Path $RepoDir 'plugins\*\plugin.json')) {
            $Spec = Get-Content $SpecFile.FullName -Raw | ConvertFrom-Json
            $Zip = Join-Path $RepoDir "release\$($Spec.name)-$($Spec.version)-windows-x64.zip"
            if (Test-Path $Zip) { Remove-Item -Force $Zip }
            Compress-Archive -Path (Join-Path $Staging $Spec.name) -DestinationPath $Zip
            Write-Host "Created $Zip"
        }
    }
} finally {
    Pop-Location
}
