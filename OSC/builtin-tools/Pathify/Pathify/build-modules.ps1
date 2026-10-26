param(
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$rustupBin = Join-Path $env:USERPROFILE '.cargo\bin'
if ((Test-Path -LiteralPath (Join-Path $rustupBin 'cargo.exe')) -and
    (Test-Path -LiteralPath (Join-Path $rustupBin 'rustc.exe'))) {
    $env:PATH = "$rustupBin;$env:PATH"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'target\exe-build\modules'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$mingwCandidates = @('C:\msys64\mingw64\bin\gcc.exe', 'C:\msys64\ucrt64\bin\gcc.exe')
if (-not [string]::IsNullOrWhiteSpace($env:MSYS2_ROOT)) {
    $mingwCandidates = @(
        (Join-Path $env:MSYS2_ROOT 'mingw64\bin\gcc.exe'),
        (Join-Path $env:MSYS2_ROOT 'ucrt64\bin\gcc.exe')
    ) + $mingwCandidates
}
$gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
if (-not $gcc) {
    $gccPath = $mingwCandidates | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Select-Object -First 1
    if ($gccPath) {
        $env:PATH = "$(Split-Path -Parent $gccPath);$env:PATH"
        $gcc = Get-Command gcc.exe -ErrorAction SilentlyContinue
    }
}
if (-not $gcc) {
    throw 'Building the Go module DLL requires an x64 MinGW-w64 gcc.exe for cgo. Install MSYS2 MinGW-w64 and make gcc.exe available, then retry.'
}
if (-not (Get-Command go.exe -ErrorAction SilentlyContinue)) {
    throw 'Go is required to build the Go example module.'
}
if (-not (Get-Command cargo.exe -ErrorAction SilentlyContinue)) {
    throw 'Cargo is required to build the Rust example module.'
}

$env:CGO_ENABLED = '1'
$env:GOOS = 'windows'
$env:GOARCH = 'amd64'
$env:CC = 'gcc'

$goSource = Join-Path $projectRoot 'modules\examples\hello-go'
$rustManifest = Join-Path $projectRoot 'modules\examples\hello-rust\Cargo.toml'
$goOutput = Join-Path $OutputDirectory 'hello-go'
$rustOutput = Join-Path $OutputDirectory 'hello-rust'
New-Item -ItemType Directory -Force -Path $goOutput, $rustOutput | Out-Null
$rustCompilerInfo = & rustc.exe -vV
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the active Rust toolchain target.'
}
$rustHost = ($rustCompilerInfo | Where-Object { $_ -match '^host: ' } | Select-Object -First 1) -replace '^host: ', ''
if ([string]::IsNullOrWhiteSpace($rustHost)) {
    throw 'Could not determine the active Rust toolchain target.'
}
$rustTargetLib = & rustc.exe --print target-libdir --target $rustHost
if ($LASTEXITCODE -ne 0 -or
    -not (Get-ChildItem -LiteralPath $rustTargetLib -Filter 'libstd-*.rlib' -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    throw "The Rust standard library for $rustHost is not installed. Install it with 'rustup target add $rustHost'."
}

Push-Location $goSource
try {
    & go.exe build -buildmode=c-shared -trimpath -o (Join-Path $goOutput 'module.dll') .
    if ($LASTEXITCODE -ne 0) {
        throw "Go module build failed (exit code $LASTEXITCODE)."
    }
}
finally {
    Pop-Location
}

$env:CARGO_TARGET_DIR = Join-Path $OutputDirectory 'rust-target'
& cargo.exe build --release --offline --target $rustHost --manifest-path $rustManifest
if ($LASTEXITCODE -ne 0) {
    throw "Rust module build failed (exit code $LASTEXITCODE)."
}
$rustDll = Join-Path $env:CARGO_TARGET_DIR "$rustHost\release\hello_rust.dll"
if (-not (Test-Path -LiteralPath $rustDll -PathType Leaf)) {
    throw "Rust module DLL was not produced: $rustDll"
}
Copy-Item -LiteralPath $rustDll -Destination (Join-Path $rustOutput 'module.dll') -Force

Copy-Item -LiteralPath (Join-Path $goSource 'module.json') -Destination $goOutput -Force
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $rustManifest) 'module.json') -Destination $rustOutput -Force
Write-Host "Built Go and Rust Pathify modules into $OutputDirectory"
