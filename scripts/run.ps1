# Run the freshly built image directly in native Windows QEMU.

param([switch]$Headless)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$native = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
if (!(Test-Path -LiteralPath $native)) {
    throw "Native QEMU is required at $native"
}
$display = if ($Headless) {'none'} else {'gtk,gl=off'}
& $native -m 128 -no-reboot -display $display `
    -drive "format=raw,file=$repo/build/kernos.img,if=ide" `
    -device ich9-ahci,id=ahci `
    -drive "if=none,id=files,format=raw,file=$repo/build/ahcidisk.img" `
    -device ide-hd,drive=files,bus=ahci.0 `
    -serial stdio -monitor none
exit $LASTEXITCODE
