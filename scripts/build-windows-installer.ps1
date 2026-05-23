param(
    [switch] $SkipBuild,
    [string] $Configuration = "Release",
    [string] $AppVersion = "0.0.1",
    [string] $InnoSetupCompiler = ""
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $root "build"
$releaseDir = Join-Path $buildDir $Configuration
$installerDir = Join-Path $root "installer"
$stageRoot = Join-Path $buildDir "installer"
$packageDir = Join-Path $stageRoot "package"
$outputDir = Join-Path $stageRoot "output"
$issFile = Join-Path $installerDir "TraySampleApp.iss"

function Remove-StagingDirectory {
    param([string] $Path)

    $resolvedRoot = [System.IO.Path]::GetFullPath($root)
    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    if (-not $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the repository: $resolvedPath"
    }
    if (Test-Path $resolvedPath) {
        Remove-Item -LiteralPath $resolvedPath -Recurse -Force
    }
}

function Find-InnoSetupCompiler {
    param([string] $ExplicitPath)

    if ($ExplicitPath) {
        return $ExplicitPath
    }

    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $fromPath = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    throw "Inno Setup compiler (ISCC.exe) was not found. Install Inno Setup 6 or pass -InnoSetupCompiler."
}

Push-Location $root
try {
    if (-not $SkipBuild) {
        if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
            throw "Required command 'cmake' was not found in PATH. Install Visual Studio Build Tools with C++ and CMake."
        }

        & cmake -S $root -B $buildDir -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed."
        }

        & cmake --build $buildDir --config $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed."
        }
    }

    $appExe = Join-Path $releaseDir "TraySampleApp.exe"
    $serviceExe = Join-Path $releaseDir "TraySampleService.exe"

    foreach ($required in @($appExe, $serviceExe)) {
        if (-not (Test-Path $required)) {
            throw "Required build artifact not found: $required"
        }
    }

    Remove-StagingDirectory $stageRoot
    New-Item -ItemType Directory -Force -Path $packageDir, $outputDir | Out-Null

    Copy-Item -Path $appExe -Destination $packageDir -Force
    Copy-Item -Path $serviceExe -Destination $packageDir -Force

    $iscc = Find-InnoSetupCompiler -ExplicitPath $InnoSetupCompiler
    & $iscc "/DAppVersion=$AppVersion" $issFile
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup compilation failed."
    }

    Write-Host "Installer artifacts:"
    Get-ChildItem -Path $outputDir -Filter "*.exe" | ForEach-Object { Write-Host " - $($_.FullName)" }
}
finally {
    Pop-Location
}
