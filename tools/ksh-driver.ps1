# tools/ksh-driver.ps1 - M5.5 boot-gate typing harness (host side).
#
# KernOS grew a real input channel: the keyboard IRQ ring feeds the
# READ gate, and the shell reads it. That means the gate itself must
# TYPE - QEMU is started with `-monitor tcp`, and this driver sends
# HMP "sendkey" lines over that TCP socket, cued by what the boot run
# has ALREADY committed to serial.log (cue = evidence, typed keys
# follow only after the previous phase's named output exists).
#
# Script of the positive M5.5 gate:
#   1. wait  "ksh: kernos shell ready"  -> shell parked in READ
#   2. type  "run u1"                   -> spawn/wait/exit round-trip
#   3. wait  "exited code="             -> child collected by WAIT
#   4. type  "exit"                     -> shell exits; main joins it
#   5. wait  "gate5: shell exited code=0"
# Exit code 0 = the whole typed round-trip was proven on real IRQs.
#
# Discipline learned on the first run: every cue-report is Write-Host
# (console stream), NEVER Write-Output - the wait function's return
# value feeds a boolean AND, and pipeline output would have turned the
# "TIMEOUT..." text itself into a truthy "success".

param(
    [int]$Port = 4444,
    [string]$SerialLog = "build\serial.log",
    [int]$MaxWaitBootSec = 150
)

$ErrorActionPreference = "Stop"

function Wait-Serial([string]$needle, [int]$sec) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $SerialLog) -and
            (Select-String -Path $SerialLog -Pattern ([regex]::Escape($needle)) -Quiet)) {
            Write-Host ("  cue ok: " + $needle)
            return $true
        }
        Start-Sleep -Milliseconds 400
    }
    Write-Host ("  TIMEOUT wait for: " + $needle)
    return $false
}

# Write to the monitor, then drain its response for ~150 ms and print
# anything (errors like unknown key names must be visible, not lost).
function Send-Monitor([string]$line) {
    $b = [Text.Encoding]::ASCII.GetBytes($line + "`n")
    $stream.Write($b, 0, $b.Length)
    Start-Sleep -Milliseconds 150
    while ($stream.DataAvailable) {
        $buf = New-Object byte[] 4096
        $n = $stream.Read($buf, 0, $buf.Length)
        if ($n -gt 0) {
            $txt = [Text.Encoding]::ASCII.GetString($buf, 0, $n).Trim()
            if ($txt) {
                $flat = ($txt -replace "[`r`n?x1b?]+", " | ")
                Write-Host ("  monitor: " + $flat)
            }
        }
    sleep -Milliseconds 30
    }
}

function Type-Line([string]$text) {
    foreach ($ch in $text.ToCharArray()) {
        $key = if ($ch -eq ' ') { 'spc' } else { [string]$ch }
        Send-Monitor ("sendkey " + $key)
    }
    Send-Monitor "sendkey ret"
}

# --- connect (with retry: the port publish can lag the container) ---
$c = New-Object System.Net.Sockets.TcpClient
$connected = $false
for ($i = 0; $i -lt 60 -and -not $connected; ++$i) {
    try { $c.Connect("127.0.0.1", $Port); $connected = $true }
    catch { Start-Sleep -Seconds 2; $c = New-Object System.Net.Sockets.TcpClient }
}
if (-not $connected) { Write-Host "monitor port $Port unreachable"; exit 1 }
$stream = $c.GetStream()

$ok = $true
$w1 = Wait-Serial "ksh: kernos shell ready" $MaxWaitBootSec
if ($w1) { Type-Line "run u1" }
$ok = $ok -and $w1

$w2 = Wait-Serial "exited code=" $MaxWaitBootSec
if ($w2) { Type-Line "exit" }
$ok = $ok -and $w2

$w3 = Wait-Serial "gate5: shell exited code=0" (2 * $MaxWaitBootSec)
$ok = $ok -and $w3

$c.Close()
if ($ok) { Write-Host "GATE55-POSITIVE: ok"; exit 0 }
Write-Host "GATE55-POSITIVE: FAILED"
exit 1
