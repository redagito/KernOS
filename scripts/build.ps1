# Build KernOS. Requires Docker Desktop running (docker info succeeds).
#
#   scripts\build.ps1          full image (build + QEMU container image)
#   scripts\build.ps1 -Quick   reuse the existing build container image

param(
    [switch]$Quick
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$image = "kernos-build:latest"

docker --context desktop-linux info 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker daemon is not running. Start Docker Desktop, then retry."
}

if (-not $Quick) {
    docker --context desktop-linux build -t $image $repo
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

docker --context desktop-linux run --rm -v "${repo}:/os" -w /os $image make image
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot 'build-launcher.ps1')
exit $LASTEXITCODE
