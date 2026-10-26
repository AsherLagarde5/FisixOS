param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $projectRoot

try {
    if ($env:OS -ne 'Windows_NT') {
        throw 'The Pathify installer can only be built on Windows.'
    }

    $rustupBin = Join-Path $env:USERPROFILE '.cargo\bin'
    if ((Test-Path -LiteralPath (Join-Path $rustupBin 'cargo.exe')) -and
        (Test-Path -LiteralPath (Join-Path $rustupBin 'rustc.exe'))) {
        $env:PATH = "$rustupBin;$env:PATH"
    }
    if (-not $env:CARGO_TARGET_DIR) {
        $env:CARGO_TARGET_DIR = Join-Path $projectRoot 'target\exe-build'
    }
    if (-not (Get-Command gcc.exe -ErrorAction SilentlyContinue)) {
        $gccCandidates = @('C:\msys64\mingw64\bin\gcc.exe', 'C:\msys64\ucrt64\bin\gcc.exe')
        if (-not [string]::IsNullOrWhiteSpace($env:MSYS2_ROOT)) {
            $gccCandidates = @(
                (Join-Path $env:MSYS2_ROOT 'mingw64\bin\gcc.exe'),
                (Join-Path $env:MSYS2_ROOT 'ucrt64\bin\gcc.exe')
            ) + $gccCandidates
        }
        $gccPath = $gccCandidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | Select-Object -First 1
        if ($gccPath) {
            $env:PATH = "$(Split-Path -Parent $gccPath);$env:PATH"
        }
    }

    $metadataJson = & cargo metadata --no-deps --format-version 1
    if ($LASTEXITCODE -ne 0) {
        throw 'cargo metadata failed.'
    }
    $metadata = $metadataJson | ConvertFrom-Json
    $package = $metadata.packages | Where-Object { $_.name -eq 'pathify' } | Select-Object -First 1
    if (-not $package) {
        throw 'Could not read Pathify package metadata.'
    }

    & cargo build --release --offline -j 1
    if ($LASTEXITCODE -ne 0) {
        throw 'cargo build --release failed.'
    }
    if (-not (Get-Command gcc.exe -ErrorAction SilentlyContinue)) {
        throw 'The bundled Go module requires an x64 MinGW-w64 GCC compiler. Install MSYS2 MinGW-w64 and make gcc.exe available.'
    }

    $binaryPath = Join-Path $metadata.target_directory 'release\pathify.exe'
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "Release executable was not produced: $binaryPath"
    }

    $compilerCandidates = @(
        (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'),
        (Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe')
    )
    $compiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $compiler) {
        throw 'The .NET Framework C# compiler (csc.exe) is required to build the standalone installer.'
    }

    $sourcePath = Join-Path $projectRoot 'installer\PathifySetup.cs'
    $guiSourcePath = Join-Path $projectRoot 'installer\PathifyGui.cs'
    $manifestPath = Join-Path $projectRoot 'installer\PathifySetup.manifest'
    $uninstallerPath = Join-Path $projectRoot 'Uninstall-Pathify.ps1'
    $moduleBuildScript = Join-Path $projectRoot 'build-modules.ps1'
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Installer source was not found: $sourcePath"
    }
    if (-not (Test-Path -LiteralPath $guiSourcePath -PathType Leaf)) {
        throw "Pathify GUI source was not found: $guiSourcePath"
    }
    if (-not (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
        throw "Uninstaller was not found: $uninstallerPath"
    }
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Installer manifest was not found: $manifestPath"
    }
    if (-not (Test-Path -LiteralPath $moduleBuildScript -PathType Leaf)) {
        throw "Native module build script was not found: $moduleBuildScript"
    }

    $outputDirectory = Join-Path $metadata.target_directory 'installer'
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $modulePayloadDirectory = Join-Path $outputDirectory 'module-payload'
    & $moduleBuildScript -OutputDirectory $modulePayloadDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Native example module build failed (exit code $LASTEXITCODE)."
    }
    $goModuleManifest = Join-Path $modulePayloadDirectory 'hello-go\module.json'
    $goModuleDll = Join-Path $modulePayloadDirectory 'hello-go\module.dll'
    $rustModuleManifest = Join-Path $modulePayloadDirectory 'hello-rust\module.json'
    $rustModuleDll = Join-Path $modulePayloadDirectory 'hello-rust\module.dll'
    foreach ($moduleFile in @($goModuleManifest, $goModuleDll, $rustModuleManifest, $rustModuleDll)) {
        if (-not (Test-Path -LiteralPath $moduleFile -PathType Leaf)) {
            throw "Expected bundled module file was not produced: $moduleFile"
        }
    }
    $guiPath = Join-Path $outputDirectory 'PathifyGui.exe'
    $guiBuildPath = Join-Path $outputDirectory 'PathifyGui.building.exe'
    if (Test-Path -LiteralPath $guiBuildPath) {
        Remove-Item -LiteralPath $guiBuildPath -Force
    }
    & $compiler /nologo /target:winexe /optimize+ `
        "/out:$guiBuildPath" `
        /reference:System.dll `
        /reference:System.Drawing.dll `
        /reference:System.Windows.Forms.dll `
        $guiSourcePath
    if ($LASTEXITCODE -ne 0) {
        throw "The Pathify GUI failed to compile (exit code $LASTEXITCODE)."
    }
    if (-not (Test-Path -LiteralPath $guiBuildPath -PathType Leaf)) {
        throw "The Pathify GUI executable was not produced: $guiBuildPath"
    }
    Move-Item -LiteralPath $guiBuildPath -Destination $guiPath -Force

    $installerPath = Join-Path $outputDirectory "Pathify-$($package.version)-windows-setup.exe"
    $buildPath = Join-Path $outputDirectory "Pathify-$($package.version)-windows-setup.building.exe"
    if (Test-Path -LiteralPath $buildPath) {
        Remove-Item -LiteralPath $buildPath -Force
    }
    & $compiler /nologo /target:winexe /optimize+ `
        "/out:$buildPath" `
        "/win32manifest:$manifestPath" `
        /reference:System.dll `
        /reference:System.Drawing.dll `
        /reference:System.Windows.Forms.dll `
        "/resource:$binaryPath,Pathify.exe" `
        "/resource:$guiPath,PathifyGui.exe" `
        "/resource:$goModuleManifest,Module.hello-go.module.json" `
        "/resource:$goModuleDll,Module.hello-go.module.dll" `
        "/resource:$rustModuleManifest,Module.hello-rust.module.json" `
        "/resource:$rustModuleDll,Module.hello-rust.module.dll" `
        "/resource:$uninstallerPath,Uninstall-Pathify.ps1" `
        $sourcePath
    if ($LASTEXITCODE -ne 0) {
        throw "The custom setup application failed to compile (exit code $LASTEXITCODE)."
    }
    if (-not (Test-Path -LiteralPath $buildPath -PathType Leaf)) {
        throw "The setup executable was not produced: $buildPath"
    }
    try {
        Move-Item -LiteralPath $buildPath -Destination $installerPath -Force
    }
    catch [System.IO.IOException] {
        $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $installerPath = Join-Path $outputDirectory "Pathify-$($package.version)-windows-setup-$timestamp.exe"
        Move-Item -LiteralPath $buildPath -Destination $installerPath
    }

    Write-Host "Standalone installer created: $installerPath"
    Write-Host 'The Pathify CLI, C# GUI, Go and Rust example modules, and uninstaller are embedded in this setup application.'
}
finally {
    Pop-Location
}
