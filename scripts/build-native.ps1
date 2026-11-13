# Build on Windows with LLVM + Visual Studio's host C libraries; no Docker.
# Intermediate files are isolated from the GNU toolchain under build/native.
param([switch]$TestOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo
$llvm = 'C:\Program Files\LLVM\bin'
$out = 'build/native'
$obj = "$out/obj"
$temporary = Join-Path $repo 'build\tmp'
New-Item -ItemType Directory -Force $obj, $temporary | Out-Null
$savedTemp, $savedTmp = $env:TEMP, $env:TMP
$env:TEMP = $temporary
$env:TMP = $temporary
function Invoke-Tool($name, [string[]]$arguments) {
    & "$llvm/$name.exe" @arguments
    if ($LASTEXITCODE) { throw "$name failed ($LASTEXITCODE)." }
}
try {
    $hostFlags = @('-O2','-fno-builtin','-D_CRT_SECURE_NO_WARNINGS','-fuse-ld=lld','-Iinclude','-Ikernel','-Itests')
    foreach ($tool in @('mkfont','mkimage','mkfat32','fsinstall')) {
        $sources = @("tools/$tool.c")
        if ($tool -eq 'fsinstall') { $sources += @('tests/blk_host.c','kernel/fat32.c') }
        Invoke-Tool clang ($hostFlags + $sources + @('-o',"$out/$tool.exe"))
    }
    & "$out/mkfont.exe" --out build/font.psf2
    if ($LASTEXITCODE) { throw 'Font generation failed.' }
    $common = @('-target','x86_64-none-elf','-std=c11','-O2','-ffreestanding','-fno-builtin',
        '-fno-stack-protector','-fno-pie','-fno-pic','-mno-red-zone',
        '-fno-asynchronous-unwind-tables','-Wall','-Wextra','-Iinclude','-Ikernel')
    $integer = @('-mno-mmx','-mno-sse','-mno-sse2','-msoft-float')
    $makefile = Get-Content -LiteralPath Makefile -Raw
    $block = [regex]::Match($makefile,'(?s)KERNEL_OBJ := (.*?)BOOT_OBJ').Groups[1].Value
    $names = [regex]::Matches($block,'\$\(OBJ\)/([a-zA-Z0-9_]+)\.o') | ForEach-Object {$_.Groups[1].Value}
    $objects = @()
    foreach ($name in $names) {
        $source = if ($name -eq 'start64') {'kernel/arch/x86_64/start.s'}
            elseif ($name -eq 'input_core') {'kernel/input.c'}
            elseif ($name -in @('font','swtch','isr')) {"kernel/arch/x86_64/$name.s"}
            elseif (Test-Path -LiteralPath "kernel/$name.c") {"kernel/$name.c"}
            else {"kernel/arch/x86_64/$name.c"}
        $flags = if ($source.EndsWith('.s')) { @('-target','x86_64-none-elf') } else { $common + $integer }
        Invoke-Tool clang ($flags + @('-c',$source,'-o',"$obj/$name.o"))
        $objects += "$obj/$name.o"
    }
    Invoke-Tool ld.lld (@('-nostdlib','-T','kernel/arch/x86_64/kernel.ld','-o',"$out/kernel.elf") + $objects)
    Invoke-Tool llvm-objcopy @('-O','binary',"$out/kernel.elf","$out/kernel.bin")
    foreach ($stage in @('mbr','stage2')) {
        Invoke-Tool clang @('-target','i386-none-elf','-c',"boot/$stage.s",'-o',"$obj/$stage.o")
        $entry = if ($stage -eq 'stage2') {'stage2_entry'} else {'_stage1'}
        Invoke-Tool ld.lld @('-m','elf_i386','-e',$entry,'-T',"boot/$stage.ld",'-o',"$out/$stage.elf","$obj/$stage.o")
        Invoke-Tool llvm-objcopy @('-O','binary',"$out/$stage.elf","$out/$stage.bin")
    }
    $libObjects = @()
    foreach ($file in @('crt0','format','stdio','string','stdlib','unistd')) {
        Invoke-Tool clang ($common + $integer + @('-Ilibc/include','-c',"libc/src/$file.c",'-o',"$obj/lc_$file.o"))
        $libObjects += "$obj/lc_$file.o"
    }
    Invoke-Tool clang ($common + $integer + @('-Ilibc/include','-c','vendor/tcc-0.9.27/lib/va_list.c','-o',"$obj/tcc_va.o"))
    $libObjects += "$obj/tcc_va.o"
    # Recreate the archive so removed or renamed members cannot linger.
    if (Test-Path -LiteralPath "$out/libc.a") { Remove-Item -LiteralPath "$out/libc.a" }
    Invoke-Tool llvm-ar (@('rcs',"$out/libc.a") + $libObjects)
    foreach ($app in @('u1','shell','edit')) {
        Invoke-Tool clang ($common + $integer + @('-Ilibc/include','-c',"kernel/user/$app.c",'-o',"$obj/$app.o"))
        Invoke-Tool ld.lld @('-s','-nostdlib','-T','kernel/user/prog.ld','-o',"$out/$app.elf","$obj/$app.o","$out/libc.a")
    }
    $compilerFlags = @('-target','x86_64-none-elf','-std=c11','-O2','-fno-builtin','-ffreestanding',
        '-fno-pie','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mno-red-zone',
        '-Ikcc/include','-Ilibc/include','-Iinclude/user','-Iinclude','-Ikernel','-Ivendor/tcc-0.9.27')
    $compilerObjects = @("$obj/lc_crt0.o")
    foreach ($item in @(@('main','kcc/main.c'),@('arena','kcc/arena.c'),@('os_compat','kcc/os_compat.c'),
        @('libc_extra','kcc/libc_extra.c'),@('setjmp','kcc/setjmp.S'),@('elf','kernel/elf64.c'),
        @('libtcc','vendor/tcc-0.9.27/libtcc.c'))) {
        Invoke-Tool clang ($compilerFlags + @('-w','-c',$item[1],'-o',"$obj/kcc_$($item[0]).o"))
        $compilerObjects += "$obj/kcc_$($item[0]).o"
    }
    Invoke-Tool ld.lld (@('-s','-nostdlib','-T','kernel/user/prog.ld','-o',"$out/kcc.elf") + $compilerObjects + @("$out/libc.a"))
    & "$out/mkimage.exe" "$out/kernos.img" "$out/mbr.bin" "$out/stage2.bin" "$out/kernel.bin"
    if ($LASTEXITCODE) { throw 'Boot image creation failed.' }
    $disk = if ($TestOnly) {"$out/files.img"} else {'build/ahcidisk.img'}
    if ($TestOnly -and (Test-Path -LiteralPath 'build/ahcidisk.img')) {
        Copy-Item -LiteralPath build/ahcidisk.img -Destination $disk -Force
    }
    if (!(Test-Path -LiteralPath $disk)) {
        & "$out/mkfat32.exe" $disk "$out/u1.elf" "$out/shell.elf"
        if ($LASTEXITCODE) { throw 'Filesystem creation failed.' }
    }
    & "$out/fsinstall.exe" $disk U1.ELF "$out/u1.elf" SHELL.ELF "$out/shell.elf" EDIT.ELF "$out/edit.elf" KCC.ELF "$out/kcc.elf" LIBC.A "$out/libc.a" CRT0.OBJ "$obj/lc_crt0.o" STDIO.H libc/include/stdio.h STDLIB.H libc/include/stdlib.h STRING.H libc/include/string.h UNISTD.H libc/include/unistd.h STDDEF.H include/user/stddef.h STDINT.H include/user/stdint.h STDBOOL.H include/user/stdbool.h STDARG.H vendor/tcc-0.9.27/include/stdarg.h
    if ($LASTEXITCODE) { throw 'File installation failed. Stop the VM before rebuilding.' }
    if (!$TestOnly) {
        Copy-Item -LiteralPath "$out/kernos.img" -Destination build/kernos.img -Force
        & (Join-Path $PSScriptRoot 'build-launcher.ps1')
    }
    Write-Output "Native build ready: $out/kernos.img; filesystem: $disk"
} finally {
    $env:TEMP, $env:TMP = $savedTemp, $savedTmp
}
