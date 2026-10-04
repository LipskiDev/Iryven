[CmdletBinding()]
param(
    [string]$OutputPath,
    [switch]$SkipBuild,
    [switch]$IncludeSymbols
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot "dist\Sandbox.exe"
}

if ($env:OS -ne "Windows_NT") {
    throw "Sandbox can only be exported as a Windows .exe from Windows."
}

$repositoryRoot = $PSScriptRoot
$solutionPath = Join-Path $repositoryRoot "Iryven.sln"
$binaryDirectory = Join-Path $repositoryRoot "bin\Release-windows-x86_64\Sandbox"
$sandboxBinary = Join-Path $binaryDirectory "Sandbox.exe"
$scratchRoot = Join-Path $repositoryRoot ".codex_tmp"
$workDirectory = Join-Path $scratchRoot ("sandbox-export-" + [guid]::NewGuid().ToString("N"))
$payloadRoot = Join-Path $workDirectory "payload"
$payloadArchive = Join-Path $workDirectory "payload.zip"
$launcherPath = Join-Path $workDirectory "launch.cmd"
$sedPath = Join-Path $workDirectory "sandbox-export.sed"

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $candidate = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }

    $command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw "MSBuild was not found. Install Visual Studio with the Desktop development with C++ workload."
}

function Find-VisualStudioRoot {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    return (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
}

function Copy-MSVC-Runtime([string]$Destination) {
    $visualStudioRoot = Find-VisualStudioRoot
    if (-not $visualStudioRoot) {
        throw "Visual Studio could not be located, so the MSVC runtime cannot be bundled."
    }

    $redistRoot = Join-Path $visualStudioRoot "VC\Redist\MSVC"
    $redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]"0.0" } } -Descending |
        Select-Object -First 1
    if (-not $redistVersion) { throw "No MSVC redistributable was found under '$redistRoot'." }

    $runtimeDirectory = Get-ChildItem -LiteralPath (Join-Path $redistVersion.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" |
        Select-Object -First 1
    if (-not $runtimeDirectory) { throw "No x64 MSVC runtime was found in '$($redistVersion.FullName)'." }

    Get-ChildItem -LiteralPath $runtimeDirectory.FullName -File -Filter "*.dll" | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination
    }
}

New-Item -ItemType Directory -Path $workDirectory, $payloadRoot -Force | Out-Null

try {
    if (-not $SkipBuild) {
        if (-not $env:VULKAN_SDK) {
            throw "VULKAN_SDK must be set before building Sandbox."
        }
        $msbuild = Find-MSBuild
        Write-Host "Building Sandbox (Release, x64)..."
        & $msbuild $solutionPath /t:Sandbox /p:Configuration=Release /p:Platform=x64 /m
        if ($LASTEXITCODE -ne 0) { throw "The Sandbox build failed with exit code $LASTEXITCODE." }
    }

    if (-not (Test-Path -LiteralPath $sandboxBinary)) {
        throw "Sandbox.exe was not found at '$sandboxBinary'. Build it first or omit -SkipBuild."
    }

    Write-Host "Collecting the executable and runtime assets..."
    Copy-Item -LiteralPath $sandboxBinary -Destination $payloadRoot
    if ($IncludeSymbols) {
        $symbols = Join-Path $binaryDirectory "Sandbox.pdb"
        if (Test-Path -LiteralPath $symbols) { Copy-Item -LiteralPath $symbols -Destination $payloadRoot }
    }

    $shaderSource = Join-Path $repositoryRoot "assets\shaders\internal"
    $shaderDestination = Join-Path $payloadRoot "assets\shaders\internal"
    New-Item -ItemType Directory -Path $shaderDestination -Force | Out-Null
    Get-ChildItem -LiteralPath $shaderSource -File -Filter "*.spv" | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $shaderDestination
    }
    if (-not (Get-ChildItem -LiteralPath $shaderDestination -File -Filter "*.spv")) {
        throw "No compiled SPIR-V shaders were found in '$shaderSource'."
    }

    $modelSource = Join-Path $repositoryRoot "assets\models\sponza"
    $modelDestination = Join-Path $payloadRoot "assets\models\sponza"
    Copy-Item -LiteralPath $modelSource -Destination $modelDestination -Recurse
    # .iryasset is an optional local import cache; Sandbox loads Sponza.gltf.
    Get-ChildItem -LiteralPath $modelDestination -File -Filter "*.iryasset" | Remove-Item -Force

    Copy-MSVC-Runtime -Destination $payloadRoot

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $payloadRoot,
        $payloadArchive,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)

    $payloadHash = (Get-FileHash -LiteralPath $payloadArchive -Algorithm SHA256).Hash.Substring(0, 16).ToLowerInvariant()
    $launcher = @"
@echo off
setlocal
set "APPDIR=%LOCALAPPDATA%\Iryven\Sandbox\$payloadHash"
if not exist "%APPDIR%\Sandbox.exe" (
  if exist "%APPDIR%" rmdir /s /q "%APPDIR%"
  mkdir "%APPDIR%" || exit /b 1
  powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%~dp0payload.zip' -DestinationPath '%APPDIR%' -Force" || exit /b 1
)
pushd "%APPDIR%" || exit /b 1
Sandbox.exe
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
"@
    Set-Content -LiteralPath $launcherPath -Value $launcher -Encoding Ascii

    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputPath)
    $outputDirectory = Split-Path -Parent $resolvedOutput
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    if (Test-Path -LiteralPath $resolvedOutput) { Remove-Item -LiteralPath $resolvedOutput -Force }

    $escapedWorkDirectory = $workDirectory.TrimEnd("\") + "\"
    $sed = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$resolvedOutput
FriendlyName=Iryven Sandbox
AppLaunched=cmd.exe /d /c launch.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[SourceFiles]
SourceFiles0=$escapedWorkDirectory
[SourceFiles0]
%FILE0%=
%FILE1%=
[Strings]
FILE0="payload.zip"
FILE1="launch.cmd"
"@
    Set-Content -LiteralPath $sedPath -Value $sed -Encoding Ascii

    Write-Host "Creating the single-file package..."
    $iexpress = Start-Process -FilePath "$env:SystemRoot\System32\iexpress.exe" `
        -ArgumentList @("/N", "/Q", $sedPath) -Wait -PassThru
    if ($iexpress.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $resolvedOutput)) {
        throw "IExpress failed to create '$resolvedOutput'."
    }

    $sizeMiB = [math]::Round((Get-Item -LiteralPath $resolvedOutput).Length / 1MB, 1)
    Write-Host "Exported '$resolvedOutput' ($sizeMiB MiB)."
}
finally {
    if (Test-Path -LiteralPath $workDirectory) {
        Remove-Item -LiteralPath $workDirectory -Recurse -Force
    }
}
