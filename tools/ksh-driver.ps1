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
    [int]$MonitorPort = 4445,
    [string]$Container = "",
    [string]$SerialLog = "build\serial.log",
    [int]$MaxWaitBootSec = 150,
    [int]$ChildTid = 4,             # M6.5 commit 2: the spawned shell's
                                    # tid (single-slot namespace, lowest
                                    # free after A/B; calibrated 2026-11-06)
    # Milestone cues that MUST appear in the serial log before the
    # run may count as success (M6.1 lesson: the round-trip cues used
    # to be satisfiable by ANY userland - a pre-M6.1 u1 exited 0 and
    # every cue fired, so the distinguishing evidence was never
    # required. A cue the gate does not REQUIRE is not evidence; it
    # is decoration. All require-cues land before the exit cue.)
    [string[]]$RequireCues = @(),
    # Exact occurrence counts a cue must end the run with (M6.5 commit
    # 2: transitions are countable facts - exactly TWO re-home lines,
    # exactly FOUR claims, exactly TWO exit ledgers).
    [hashtable]$CountCues = @{},
    # The typed line (M5.10 probe freedom; default = the M5.5 script).
    [string]$Run = "run u1",
    # M6.5 commit 2: the typed two-owner session (see the block below
    # for the phase script).
    [switch]$FocusSession
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
    try { $c.Connect("127.0.0.1", $MonitorPort); $connected = $true }
    catch { Start-Sleep -Seconds 2; $c = New-Object System.Net.Sockets.TcpClient }
}
if (-not $connected) { Write-Host "monitor port $MonitorPort unreachable"; exit 1 }
$stream = $c.GetStream()

# --- M6.5 commit 2: the pointer's other two inputs (motion, buttons)
#     reach QEMU's QMP socket. The tools run INSIDE the container
#     (QMP binds the container's loopback); the driver shells out to
#     docker exec, keeping the harness host-side and self-contained.
function Send-Mouse([int]$moves, [int]$dx, [int]$dy, [int]$delay) {
    docker exec $Container python3 /os/tools/qmp-mouse.py $Port $moves $dx $dy $delay 2>&1 |
        ForEach-Object { Write-Host ("  " + $_) }
}
function Send-Click {
    docker exec $Container python3 /os/tools/qmp-click.py $Port 2>&1 |
        ForEach-Object { Write-Host ("  " + $_) }
}
# The pointer's position is EXACT, calibrated against QEMU 7.2's wire
# (cp. tools/qmp-mouse.py SIGN CONVENTION): QMP rel-y is negated by
# QEMU into the PS/2 wire delta, so the tool passes y through negated
# and callers speak screen truth (dy>0 = down). Deltas >127 ride in
# <=127-sized packets with no loss (450 -> 4 packets), pulls are FIFO
# ordered, and the queued press is hit-tested at pull time - so the
# SUM, not the batch, is what the press sees. One click = one move
# (<=4 packets = 8 events) + press (2 button packets = 6 events)
# = 14 events <= the 16-event loop-head drain window: one turn
# swallows a whole click. Park spot (16,736) = row 46; screen rows:
# cursor row = cy/16, parent's top band rows 0..23, child's ksh band
# rows 24..47. 736 -> 286 (row 17, parent's band), 286 -> 736 (row 46,
# the child's band), 736 -> 286 again for P9's backdrop (top vacant by
# then - no band at all).
function Click-Top      { Send-Mouse 1 0 -450 5 ; sleep 1; Send-Click }   # 736 -> 286: row 17 = parent's band
function Click-Bottom   { Send-Mouse 1 0 450 5  ; sleep 1; Send-Click }   # 286 -> 736: row 46 = child's band
function Click-NegTop   { Send-Mouse 1 0 -450 5 ; sleep 1; Send-Click }   # 736 -> 286: row 17, top vacant at P9 = backdrop

$ok = $true

if ($FocusSession) {
    # === M6.5 commit 2: the typed two-owner session =====================
    # Byte ledger - TOTALS BALANCE: 58 typed bytes = 29+29 served; no
    # byte lost, none doubled, under two owner changes (the failed
    # green run's serial PROVED both totals - clicks inert, but the
    # byte routing stood: the child, owner by claim, popped 1+12+5).
    #   parent(tid 3): "echo topline"(13) + "run shell"(10) + "\n"(1,
    #                phase P9's loop-turn, popped post-re-claim)
    #                + "exit"(5)                        -> served=29
    #   child (tid 4): "echo child"(11, its own first turn as owner)
    #                + "\n"(1, P5's loop-turn byte - popped BEFORE the
    #                  click applies, while still owner: the trigger
    #                  that un-blocks the owner's ring drain)
    #                + "echo parent"(12, typed AT owner=3-in-wait,
    #                  served only after the click re-homes to tid 4)
    #                + "exit"(5)                        -> served=29
    # The "echo parent" bytes are the bytes-follow-the-click proof:
    # typed while the parent owns and is blocked in wait(), nobody
    # pops; the child's parked-drain ("bytes wait for the next owner"
    # v0 semantics) serves them the moment the re-home lands and the
    # served= ledger at each exit is where it becomes visible
    # evidence. Each phase gates on the previous phase's kernel line
    # before typing/clicking again - evidence first, input after.
    $w = Wait-Serial "ksh: kernos shell ready" $MaxWaitBootSec
    $ok = $ok -and $w
    $w = Wait-Serial "focus: claim owner=3 band=24..48" $MaxWaitBootSec
    if (-not $w) { $c.Close(); Write-Host "GATE55-POSITIVE: FAILED"; exit 1 }

    Type-Line "echo topline"
    $ok = (Wait-Serial "topline" 30) -and $ok

    Type-Line "run shell"
    $ok = (Wait-Serial "focus: claim owner=3 band=0..24" 30) -and $ok
    $ok = (Wait-Serial "focus: claim owner=$ChildTid band=24..48" 30) -and $ok

    Type-Line "echo child"          # P4: the child's own first turn as
    $ok = (Wait-Serial "echo child" 30) -and $ok   # owner - the echoed
                                    # line = the child SERVED its bytes

    Click-Top                       # P5: queues the TOP click (row 0)
    Type-Line ""                    # + the owner's loop-turn byte: the
                                    # child pops \n, then its kbd-empty
                                    # drain applies the pointer and the
                                    # press re-homes to the parent
    $ok = (Wait-Serial "focus: rehome owner=3 band=0..24" 30) -and $ok

    Type-Line "echo parent"         # P6: typed AT owner=3 (in wait());
    Click-Bottom                    # P7: nobody pops - the child's
    $ok = (Wait-Serial "focus: rehome owner=$ChildTid band=24..48" 30) -and $ok
    $ok = (Wait-Serial "parent" 30) -and $ok       # parked-drain does the
                                    # pull, the press re-homes, and the
                                    # FIFO bytes follow the click

    Type-Line "exit"                # P8: the CHILD exits
    $ok = (Wait-Serial "focus: exit owner=$ChildTid served=29" 30) -and $ok

    Click-NegTop                    # the backdrop is not a program:
    Type-Line ""                    # loop-turn processes the click,
    Start-Sleep -Seconds 3          # and NO re-home line may appear
                                    # (counted below, no new wait)

    Type-Line "exit"                # the PARENT exits -> final ledger
    $ok = (Wait-Serial "focus: exit owner=3 served=29" 30) -and $ok
    $ok = (Wait-Serial "gate5: shell exited code=0" (2 * $MaxWaitBootSec)) -and $ok

    foreach ($cue in $RequireCues) {
        if (-not (Wait-Serial $cue (2 * $MaxWaitBootSec))) {
            Write-Host "  remaining unmatched require-cues:"
            $seen = @{}
            foreach ($c2 in $RequireCues) {
                if ($c2 -ne $cue -and -not $seen[$c2] -and
                    -not ((Test-Path $SerialLog) -and
                    (Select-String -Path $SerialLog -Pattern ([regex]::Escape($c2)) -Quiet))) {
                    Write-Host ("    - " + $c2); $seen[$c2] = $true
                }
            }
            $c.Close()
            Write-Host "GATE55-POSITIVE: FAILED"
            exit 1
        }
    }
    foreach ($k in $CountCues.Keys) {
        $raw = Get-Content $SerialLog -Raw
        $hops = [regex]::Matches($raw, [regex]::Escape($k)).Count
        if ($hops -ne $CountCues[$k]) {
            Write-Host ("  count cue FAILED: '" + $k + "' = " + $hops + " (want " + $CountCues[$k] + ")")
            $ok = $false
        } else {
            Write-Host ("  count cue ok: '" + $k + "' = " + $hops)
        }
    }
    $c.Close()
    if ($ok) { Write-Host "GATE55-POSITIVE: ok"; exit 0 }
    Write-Host "GATE55-POSITIVE: FAILED"
    exit 1
}

$w1 = Wait-Serial "ksh: kernos shell ready" $MaxWaitBootSec
if ($w1) { Type-Line $Run }
$ok = $ok -and $w1

$w2 = Wait-Serial "exited code=" $MaxWaitBootSec
if ($w2) { Type-Line "exit" }
$ok = $ok -and $w2

$w3 = Wait-Serial "gate5: shell exited code=0" (2 * $MaxWaitBootSec)
$ok = $ok -and $w3

# M6.4 lesson (measured): on a red, this loop used to burn 2*150s per
# missing require-cue, sequentially - one failing cue cost 5+ minutes
# of dead wait per debug cycle (measured twice today). The gate is
# ALREADY failed the moment one cue times out; nothing that follows
# can turn it green, so report the unmatched remainder and exit now.
foreach ($cue in $RequireCues) {
    if (-not (Wait-Serial $cue (2 * $MaxWaitBootSec))) {
        Write-Host "  remaining unmatched require-cues:"
        $seen = @{}
        foreach ($c2 in $RequireCues) {
            if ($c2 -ne $cue -and -not $seen[$c2] -and
                -not ((Test-Path $SerialLog) -and
                (Select-String -Path $SerialLog -Pattern ([regex]::Escape($c2)) -Quiet))) {
                Write-Host ("    - " + $c2); $seen[$c2] = $true
            }
        }
        $c.Close()
        Write-Host "GATE55-POSITIVE: FAILED"
        exit 1
    }
}

$c.Close()
if ($ok) { Write-Host "GATE55-POSITIVE: ok"; exit 0 }
Write-Host "GATE55-POSITIVE: FAILED"
exit 1
