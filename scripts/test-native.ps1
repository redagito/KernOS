# Host regression suites over the same C modules used in the kernel.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
$clang = 'C:\Program Files\LLVM\bin\clang.exe'
$out = 'build/native'
$savedTemp, $savedTmp = $env:TEMP, $env:TMP
$env:TEMP = Join-Path (Get-Location) 'build\tmp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $out, $env:TEMP | Out-Null
$flags = @('-std=c11','-O2','-fno-builtin','-D_CRT_SECURE_NO_WARNINGS','-fuse-ld=lld',
    '-Iinclude','-Ikernel','-Ikernel/arch/x86_64','-Itests')
function Compile-Test($name, [string[]]$sources, [string[]]$extra = @()) {
    & $clang @flags @extra @sources -o "$out/test_$name.exe"
    if ($LASTEXITCODE) { throw "Compile failed: $name" }
}
try {
    Compile-Test fat32 @('tests/test_fat32.c','tests/blk_host.c','kernel/fat32.c')
    Compile-Test elf64 @('tests/test_elf64.c','kernel/elf64.c')
    Compile-Test format @('tests/test_format.c','libc/src/format.c') @('-Ilibc/include','-Dsnprintf=kernos_snprintf','-Dvsnprintf=kernos_vsnprintf')
    Compile-Test font @('tests/test_font.c')
    Compile-Test input @('tests/test_input.c','kernel/input.c')
    Compile-Test utf8 @('tests/test_utf8.c','kernel/utf8.c')
    Compile-Test pmm @('tests/test_pmm.c','kernel/arch/x86_64/pmm.c')
    Compile-Test editor @('tests/test_editor.c')
    Compile-Test terminal @('tests/test_terminal.c','kernel/arch/x86_64/terminal.c','kernel/utf8.c')
    Compile-Test mouse @('tests/test_mouse.c','kernel/arch/x86_64/cursor.c')
    Compile-Test desktop @('tests/test_desktop.c','kernel/input.c','kernel/utf8.c')
    & $clang -std=c11 -O2 -fno-builtin -Dmalloc=kcc_malloc -Dfree=kcc_free -Drealloc=kcc_realloc -Dcalloc=kcc_calloc -c kcc/arena.c -o "$out/kcc_arena_host.obj"
    if ($LASTEXITCODE) { throw 'Compiler arena test compile failed.' }
    Compile-Test kcc_arena @('tests/test_kcc_arena.c',"$out/kcc_arena_host.obj")
    & "$out/mkfat32.exe" "$out/testdisk.img" "$out/u1.elf" "$out/shell.elf"
    if ($LASTEXITCODE) { throw 'Test fixture creation failed.' }
    $runArgs = @{
        fat32=@("$out/testdisk.img","$out/u1.elf","$out/shell.elf")
        elf64=@("$out/u1.elf")
        font=@('build/font.psf2')
    }
    foreach ($name in @('fat32','elf64','format','font','input','utf8','pmm','editor','terminal','kcc_arena','mouse','desktop')) {
        [string[]]$testArgs = @()
        if ($runArgs.ContainsKey($name)) { $testArgs = @($runArgs[$name]) }
        & "$out/test_$name.exe" @testArgs
        if ($LASTEXITCODE) { throw "Test failed: $name" }
    }
} finally { $env:TEMP, $env:TMP = $savedTemp, $savedTmp }
