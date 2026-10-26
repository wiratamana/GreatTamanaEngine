<#
  Profiler A/B run. Drives a running editor over HTTP and writes one markdown table.
  Needs no module. Timing runs have cluster stats off; count runs have them on.
  Only JSON routes are used: no image request and no capture ever runs here.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [string]$Feature = 'GpuDrivenScene',
    [int]$WarmupFrames = 120,
    [int]$RecordFrames = 300,
    [int]$TimeoutSeconds = 120,
    [string]$OutFile = 'profiler_ab.md'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$NoSource = 'NO HTTP SOURCE'

function Get-Json([string]$Path) {
    Invoke-RestMethod -Uri ($BaseUrl + $Path) -Method Get -TimeoutSec 15
}

function Get-Percentile([double[]]$Values, [double]$Percent) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = $Values | Sort-Object
    $index = [Math]::Min($sorted.Count - 1, [Math]::Max(0, [int][Math]::Ceiling($Percent / 100.0 * $sorted.Count) - 1))
    return [double]$sorted[$index]
}

# A metric object reads as a number only when its status says it was measured.
function Format-Metric($Metric) {
    if ($null -eq $Metric) { return $NoSource }
    if ($Metric.status -eq 'Present' -or $Metric.status -eq 'Partial') {
        return ('{0:N2} {1}' -f [double]$Metric.value, $Metric.unit)
    }
    return ('{0} ({1})' -f $Metric.status, $Metric.reason)
}

function Get-Field($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Wait-ForFrames([int]$Count) {
    $start = (Get-Json '/profiler').frame_index
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        if (((Get-Json '/profiler').frame_index - $start) -ge $Count) { return }
    }
    throw "Timed out waiting for $Count frames. Is the editor rendering and is capture on?"
}

# Gathers distinct frames from /profiler/frames until $Count are known.
function Read-Frames([int]$Count) {
    $seen = @{}
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($seen.Count -lt $Count -and (Get-Date) -lt $deadline) {
        $reply = Get-Json '/profiler/frames?count=120'
        foreach ($frame in $reply.frames) { $seen[[string]$frame.frame_index] = $frame }
        Start-Sleep -Milliseconds 500
    }
    if ($seen.Count -lt $Count) { throw "Only $($seen.Count) of $Count frames were read." }
    return $seen.Values | Sort-Object { [uint64]$_.frame_index } | Select-Object -Last $Count
}

function Set-CollectStats([bool]$On) {
    $value = if ($On) { 'true' } else { 'false' }
    Get-Json "/feature_debug/set?name=$Feature&collectStats=$value" | Out-Null
}

function Get-Stat([object]$Frames, [string]$Property) {
    $values = @($Frames | ForEach-Object {
        $metric = Get-Field $_ $Property
        if ($null -ne $metric -and $metric.status -eq 'Present') { [double]$metric.value }
    })
    return [pscustomobject]@{
        Median = Get-Percentile $values 50
        P95    = Get-Percentile $values 95
        Count  = $values.Count
    }
}

function Format-Stat($Stat) {
    if ($Stat.Count -eq 0) { return 'Absent (no measured frame)' }
    return ('median {0:N2} ms, p95 {1:N2} ms ({2} frames)' -f $Stat.Median, $Stat.P95, $Stat.Count)
}

function Measure-Run([string]$Name, [bool]$CollectStats) {
    Write-Host "Run '$Name': cluster stats $(if ($CollectStats) { 'on' } else { 'off' })"
    Set-CollectStats $CollectStats
    Wait-ForFrames $WarmupFrames
    $frames = @(Read-Frames $RecordFrames)
    $overview = Get-Json '/profiler'
    $spikes = Get-Json '/profiler/spikes'
    $graph = Get-Json '/render_graph'
    $state = $null
    try { $state = (Get-Json "/feature_debug/state?name=$Feature").fields } catch { $state = $null }
    return [pscustomobject]@{
        Name = $Name; CollectStats = $CollectStats; Frames = $frames
        Overview = $overview; Spikes = $spikes; Graph = $graph; State = $state
    }
}

function Add-Row([System.Collections.Generic.List[string]]$Rows, [string]$Id, [string]$Label, [string]$Source, [string]$Value) {
    $Rows.Add("| $Id | $Label | ``$Source`` | $Value |")
}

function Build-Table($Run) {
    $rows = [System.Collections.Generic.List[string]]::new()
    $o = $Run.Overview

    Add-Row $rows 'A1' 'frame time' '/profiler/frames cpu' (Format-Stat (Get-Stat $Run.Frames 'cpu'))
    Add-Row $rows 'A2' 'GPU span' '/profiler gpu.span' ((Format-Metric $o.gpu.span) + '; ' + (Format-Stat (Get-Stat $Run.Frames 'gpu_span')))
    Add-Row $rows 'A3' 'GPU primary view' '/profiler gpu.views.primary' (Format-Metric $o.gpu.views.primary)
    Add-Row $rows 'A3' 'GPU secondary view' '/profiler gpu.views.secondary' (Format-Metric $o.gpu.views.secondary)
    Add-Row $rows 'A4' 'GPU tail and startup' '-' $NoSource

    $wait = $o.scopes | Where-Object { $_.name -like 'GpuWait(Offscreen*' } | Select-Object -First 1
    $waitText = if ($null -ne $wait) { Format-Metric $wait.total } else { 'blocked on GPU ' + (Format-Metric $o.blocked.gpu) + ' (scope not in the top rows)' }
    Add-Row $rows 'A5' 'GpuWait(Offscreen)' '/profiler scopes, blocked.gpu' $waitText
    Add-Row $rows 'A6' 'verdict' '/profiler verdict' $o.verdict.text

    $passes = @($Run.Graph.offscreen_regime.passes | Where-Object { -not $_.is_culled -and $null -ne $_.gpu_timing_milliseconds })
    $top = $passes | Sort-Object { [double]$_.gpu_timing_milliseconds } -Descending | Select-Object -First 8
    $passText = if ($passes.Count -eq 0) { 'Absent (no measured pass)' } else {
        (($top | ForEach-Object { '{0}/{1} {2:N2} ms' -f $_.name, $_.view, [double]$_.gpu_timing_milliseconds }) -join '; ') + " (top 8 of $($passes.Count) measured)"
    }
    Add-Row $rows 'A7' 'per-pass GPU table' '/render_graph offscreen_regime.passes' $passText

    if ($Run.CollectStats -and $null -ne $Run.State) {
        $tested = Get-Field $Run.State 'scene.testedClusters'
        $visible = Get-Field $Run.State 'scene.visibleClusters'
        $visibleFrame = Get-Field $Run.State 'scene.visibleFrame'
        $text = "tested $tested, visible $visible (frame $visibleFrame)"
    } elseif ($Run.CollectStats) {
        $text = $NoSource
    } else {
        $text = 'not collected in a timing run'
    }
    Add-Row $rows 'A8' 'cluster counts' "/feature_debug/state?name=$Feature" $text

    $spikeText = if (@($Run.Spikes.spikes).Count -eq 0) { 'no spike frozen' } else {
        (($Run.Spikes.spikes | ForEach-Object { 'frame {0}: {1} ({2})' -f $_.frame_index, $_.classification, $_.cause }) -join '; ')
    }
    Add-Row $rows 'A9' 'spikes and causes' '/profiler/spikes' $spikeText
    Add-Row $rows 'A10' 'profiler overhead' '/profiler overhead' ((Format-Metric $o.overhead.estimated) + ', ' + (Format-Metric $o.overhead.share))
    return $rows
}

# --- run ---------------------------------------------------------------------

$null = Get-Json '/profiler'
$timing = Measure-Run 'timing' $false
$counts = Measure-Run 'counts' $true
Set-CollectStats $false

$out = [System.Collections.Generic.List[string]]::new()
$out.Add('# Profiler A/B run')
$out.Add('')
$out.Add("Date: $(Get-Date -Format s). Warm-up $WarmupFrames frames, record $RecordFrames frames. Rows without a source are profiler bugs.")
$out.Add('')
foreach ($run in @($timing, $counts)) {
    $out.Add("## Run: $($run.Name) (cluster stats $(if ($run.CollectStats) { 'on' } else { 'off' }))")
    $out.Add('')
    $out.Add('| Row | What | HTTP source | Value |')
    $out.Add('| --- | --- | --- | --- |')
    foreach ($row in (Build-Table $run)) { $out.Add($row) }
    $out.Add('')
}
$out.Add('## Stamps on/off')
$out.Add('')
$out.Add('No route switches the GPU stamps on and off, so the span difference is not measured.')
$out.Add('')
$out.Add('## Timeline')
$out.Add('')
$out.Add("GPU clock alignment: $(Get-Field (Get-Field $timing.Overview 'timeline') 'reason')")

Set-Content -Path $OutFile -Value $out -Encoding UTF8
Write-Host "Wrote $OutFile"
