# Run the freshly built image in QEMU inside the container.
# Serial console output goes to this terminal (use -h / Ctrl-A X in
# serverless mode; with -it, Ctrl-A x quits QEMU).

param([switch]$Headless)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$image = "kernos-build:latest"

$native = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if ($native -and -not $Headless) {
    & $native.Source -m 128 -no-reboot `
        -display gtk,gl=off `
        -drive "format=raw,file=$repo/build/kernos.img,if=ide" `
        -device ich9-ahci,id=ahci `
        -drive "if=none,id=files,format=raw,file=$repo/build/ahcidisk.img" `
        -device ide-hd,drive=files,bus=ahci.0 `
        -serial stdio -monitor none
    exit $LASTEXITCODE
}

docker --context desktop-linux run --rm -it -v "${repo}:/os" -w /os $image `
    qemu-system-x86_64 `
    -m 128 -no-reboot `
    -drive format=raw,file=/os/build/kernos.img,if=ide `
    -device ich9-ahci,id=ahci `
    -drive if=none,id=files,format=raw,file=/os/build/ahcidisk.img `
    -device ide-hd,drive=files,bus=ahci.0 `
    -serial stdio -display none -monitor none
exit $LASTEXITCODE
