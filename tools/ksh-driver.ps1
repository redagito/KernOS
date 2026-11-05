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
#   2. type  $Run (default "run u1")    -> spawn/wait/exit round-trip
#   3. wait  "exited code="             -> child collected by WAIT
#   4. type  "exit"                     -> shell exits; main joins it
#   5. wait  "gate5: shell exited code=0"
# Exit code 0 = the whole typed round-trip was proven on real IRQs.
# M6.1 hardening: callers pass -RequireCues for the milestone's OWN
# evidence lines ("input: gate ok" / "input: idle - ..."); every one
# must exist in serial.log before the run can exit 0 - the round-trip
# cues alone are satisfiable by a userland that never gained the
# observatory phase (and the M6.1 gate run caught exactly that lie).
# M5.10: -Run lets a probe choose the typed line (default unchanged),
# e.g. "run u1.elf" exercises the gate's WITH-suffix spawn path; any
# refusal leaves the script's "exited code=" cue waiting forever, so
# refusal probes run against a freshly booted QEMU WITHOUT this
# driver and read the "run refused" line off serial.log directly.
#
# Discipline learned on the first run: every cue-report is Write-Host
# (console stream), NEVER Write-Output - the wait function's return
# value feeds a boolean AND, and pipeline output would have turned the
# "TIMEOUT..." text itself into a truthy "success".

param(
    [int]$Port = 4444,
    [string]$SerialLog = "build\serial.log",
    [int]$MaxWaitBootSec = 150,
    # Milestone cues that MUST appear in the serial log before the
    # run may count as success (M6.1 lesson: the round-trip cues used
    # to be satisfiable by ANY userland - a pre-M6.1 u1 exited 0 and
    # every cue fired, so the distinguishing evidence was never
    # required. A cue the gate does not REQUIRE is not evidence; it
    # is decoration. All require-cues land before the exit cue.)
    [string[]]$RequireCues = @(),
    # The typed line (M5.10 probe freedom; default = the M5.5 script).
    [string]$Run = "run u1"
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
        $key = if ($ch -eq ' ') { 'spc' }
               elseif ($ch -eq '.') { 'dot' }   # QEMU names the period
               else { [string]$ch }
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
if ($w1) { Type-Line $Run }
$ok = $ok -and $w1

$w2 = Wait-Serial "exited code=" $MaxWaitBootSec
if ($w2) { Type-Line "exit" }
$ok = $ok -and $w2

$w3 = Wait-Serial "gate5: shell exited code=0" (2 * $MaxWaitBootSec)
$ok = $ok -and $w3

foreach ($cue in $RequireCues) {
    $ok = $ok -and (Wait-Serial $cue (2 * $MaxWaitBootSec))
}

$c.Close()
if ($ok) { Write-Host "GATE55-POSITIVE: ok"; exit 0 }
Write-Host "GATE55-POSITIVE: FAILED"
exit 1
