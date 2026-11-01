# Build KernOS. Requires Docker Desktop running (docker info succeeds).
#
#   scripts\build.ps1          full image (build + QEMU container image)
#   scripts\build.ps1 -Quick   toolchain only (skips QEMU install)

param(
    [switch]$Quick
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$image = "kernos-build:latest"

docker info 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker daemon is not running. Start Docker Desktop, then retry."
}

if (-not $Quick) {
    docker build -t $image $repo
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

docker run --rm -v "${repo}:/os" -w /os "gcc:13" make image
exit $LASTEXITCODE
