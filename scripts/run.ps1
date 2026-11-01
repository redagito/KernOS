# Run the freshly built image in QEMU inside the container.
# Serial console output goes to this terminal (use -h / Ctrl-A X in
# serverless mode; with -it, Ctrl-A x quits QEMU).

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$image = "kernos-build:latest"

docker run --rm -it -v "${repo}:/os" -w /os $image `
    qemu-system-x86_64 `
    -drive format=raw,file=/os/build/kernos.img,if=ide `
    -serial stdio -display none -monitor none
exit $LASTEXITCODE
