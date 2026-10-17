[CmdletBinding()]
param(
    [ValidateSet('setup', 'bootstrap', 'build', 'run', 'test', 'dist', 'clean', 'assets', 'assets-body', 'assets-moons',
                 'render-build', 'render-run', 'render-test', 'render-dist', 'render-clean')]
    [string]$Task = 'render-dist',
    [ValidateRange(1, 128)][int]$Jobs = 2,
    [ValidatePattern('^[a-z][a-z0-9-]{0,63}$')][string]$Body = 'mercury',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program falhou com codigo $LASTEXITCODE."
    }
}

$root = Split-Path $PSScriptRoot -Parent
$aliases = @{ build='render-build'; run='render-run'; test='render-test'; dist='render-dist'; clean='render-clean'; bootstrap='render-dist' }
if ($aliases.ContainsKey($Task)) { $Task = $aliases[$Task] }
if ($Task -eq 'render-clean') {
    $build = Join-Path $root 'build/windows'
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
    exit 0
}
if ($Task -eq 'assets' -or $Task -eq 'assets-body' -or $Task -eq 'assets-moons') {
    if ($Task -eq 'assets' -and (Test-Path (Join-Path $root 'assets/imagery/manifest.json'))) {
        Write-Host 'Biblioteca versionada: assets/imagery/ (reunida automaticamente no build)'
        exit 0
    }
    Push-Location $root
    try {
        Invoke-Checked -Program 'docker' -Arguments @('build', '-t', 'albert-assets-builder', '-f', 'scripts/Dockerfile.assets', 'scripts')
        if ($Task -eq 'assets-body') {
            Invoke-Checked -Program 'docker' -Arguments @('run', '--rm', '--init', '--entrypoint', 'python3', '-v', "${root}:/work", '-w', '/work', 'albert-assets-builder', 'scripts/prepare-body-assets.py', $Body)
        } elseif ($Task -eq 'assets-moons') {
            Invoke-Checked -Program 'docker' -Arguments @('run', '--rm', '--init', '--entrypoint', 'python3', '-v', "${root}:/work", '-w', '/work', 'albert-assets-builder', 'scripts/prepare-moon-assets.py')
        } else {
            Invoke-Checked -Program 'docker' -Arguments @('run', '--rm', '--init', '-v', "${root}:/work", '-w', '/work', 'albert-assets-builder')
        }
    } finally { Pop-Location }
    exit 0
}
$tools = Join-Path $root '.tools'
New-Item -ItemType Directory -Force -Path $tools | Out-Null

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if ($cmake) {
    $versionLine = & $cmake --version | Select-Object -First 1
    if ($versionLine -notmatch '(\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.25.0') {
        $cmake = $null
    }
}
if (-not $cmake) {
    $cmake = Join-Path $tools 'cmake-3.31.8-windows-x86_64\bin\cmake.exe'
    if (-not (Test-Path $cmake)) {
        $archive = Join-Path $tools 'cmake.zip'
        Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-windows-x86_64.zip' -OutFile $archive
        if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '81aa9964dbabd71fe02e7ec50472fd3ad56138c49944515ece9001efbff8d719') {
            Remove-Item $archive
            throw 'Checksum do CMake invalido.'
        }
        Expand-Archive -Path $archive -DestinationPath $tools -Force
        Remove-Item $archive
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = if (Test-Path $vswhere) {
    & $vswhere -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
} else { $null }
if (-not $visualStudio) {
    $installer = Join-Path $tools 'vs_BuildTools.exe'
    Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $installer
    $signature = Get-AuthenticodeSignature $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw 'Assinatura do instalador Microsoft invalida.'
    }
    # Only the build machine needs this compiler/SDK; distributed binaries use /MT.
    $process = Start-Process -FilePath $installer -Verb RunAs -Wait -PassThru -ArgumentList @(
        '--passive', '--wait', '--norestart', '--add',
        'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended')
    if ($process.ExitCode -eq 3010) {
        throw 'Build Tools instalado; reinicie o Windows e execute este script novamente.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Instalacao do Build Tools falhou: $($process.ExitCode)."
    }
}

if ($Task -eq 'setup') {
    if (Test-Path (Join-Path $root '.git')) {
        Invoke-Checked -Program 'git' -Arguments @('-C', $root, 'lfs', 'install', '--local')
        Invoke-Checked -Program 'git' -Arguments @('-C', $root, 'lfs', 'pull')
    }
    Write-Host 'Ferramentas prontas. Execute .\make.cmd build, run, test ou dist.'
    exit 0
}
if (($Task -eq 'render-run' -or $Task -eq 'render-dist' -or $Run) -and -not (Test-Path (Join-Path $root 'assets/imagery/manifest.json'))) {
    Invoke-Checked -Program 'docker' -Arguments @('build', '-t', 'albert-assets-builder', '-f', (Join-Path $root 'scripts/Dockerfile.assets'), (Join-Path $root 'scripts'))
    Invoke-Checked -Program 'docker' -Arguments @('run', '--rm', '--init', '-v', "${root}:/work", '-w', '/work', 'albert-assets-builder')
}
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
    $buildLock = $null
    $announcedWait = $false
    while ($null -eq $buildLock) {
        try {
            $buildLock = [System.IO.File]::Open((Join-Path $root 'build/.windows-build.lock'),
                [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        } catch [System.IO.IOException] {
            if (-not $announcedWait) { Write-Host 'Outra compilacao esta em andamento; aguardando sua conclusao...'; $announcedWait = $true }
            Start-Sleep -Seconds 1
        }
    }
    try {
        Invoke-Checked -Program $cmake -Arguments @('--preset', 'windows')
        $buildArguments = @('--build', '--preset', 'windows', '--parallel', "$Jobs")
        if ($Task -eq 'render-run') { $buildArguments += @('--target', 'render') }
        Invoke-Checked -Program $cmake -Arguments $buildArguments
    } finally { $buildLock.Dispose() }
    if ($Task -eq 'render-test' -or $Task -eq 'render-dist') {
        $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
        Invoke-Checked -Program $ctest -Arguments @('--preset', 'windows')
    }
    if ($Task -eq 'render-dist') {
        $dist = Join-Path $root 'dist/windows'
        Invoke-Checked -Program $cmake -Arguments @('-E', 'rm', '-rf', $dist)
        Invoke-Checked -Program $cmake -Arguments @('--install', 'build/windows', '--config', 'Release',
            '--prefix', $dist, '--component', 'Runtime', '--strip')
        Write-Host 'Distribuicao pronta em dist/windows/AlbertEngine.exe. Zipe a pasta inteira.'
    }
    if ($Task -eq 'render-run' -or $Run) {
        Invoke-Checked -Program (Join-Path $root 'build/windows/bin/Release/AlbertEngine.exe') -Arguments @()
    }
} finally {
    Pop-Location
}
