<#
  Hi-Z demo before/after table. Opens the demo scene (POST /open_preset_scene) and measures
  profile x wall (off, on) x mode (NoCull, Frustum, HiZ) in alternating rounds with Measure-CullConfig.
  Table: span, primary, secondary, GpuDrivenScene passes, clusters tested and drawn, FPS (uncapped only).
  A value that was not measured prints '-', never 0. Profiles: geometry_only (fixed-cost features off), full, both.
  Checks per profile: wall on  -> Hi-Z GpuDrivenScene primary ms <= WallRatio (0.25) x Frustum;
                      wall off -> Hi-Z GpuDrivenScene primary ms <= Frustum + HizBudgetMs (3.0).
  Hi-Z runs a second full cull pass over all work items; only an indirect-dispatch cull would remove that cost.
  Exit code: 0 = all checks pass, 1 = a check failed, 2 = precondition failed or editor unreachable,
  3 = a check could not be evaluated (invalid rows, Hi-Z not applied, missing data).
  Start the editor with --no-vsync and the Game tab visible. Cleanup closes the demo through
  POST /close_preset_scene, which restores the features and settings. -OutFile writes <name>.md and <name>.json.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [int]$Count = 50000,
    [double]$Spacing = 2.0,
    [Alias('Profile')][ValidateSet('geometry_only', 'full', 'both')][string]$ProfileChoice = 'both',
    [int]$Samples = 20,
    [int]$Repeats = 3,
    [string]$WallName = 'HizDemo Wall',
    [double]$MinValidPercent = 80.0,
    [double]$HizBudgetMs = 3.0,
    [double]$WallRatio = 0.25,
    [string]$OutFile = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $ScriptDir 'lib.ps1')
Initialize-CullLib -BaseUrl $BaseUrl

$script:Report = New-Object System.Collections.Generic.List[string]
$script:Rows = New-Object System.Collections.Generic.List[object]
$script:Opened = $false
$script:Prefixes = @()
$script:WallHome = $null
$script:PresentPolicy = 'unknown'
$script:WallFar = @(0.0, 50000.0, 0.0)

function Out-Line {
    param([string]$Text)
    Write-Host $Text
    $script:Report.Add($Text)
}

# Wall on = original position; wall off = far above the field, outside every frustum.
function Set-DemoWall {
    param([bool]$On)
    if ($On) { Set-CullTrs -Name $WallName -Position $script:WallHome } else { Set-CullTrs -Name $WallName -Position $script:WallFar }
}

# One measurement plus the profiler verdict kind (FPS is invalid when present limited).
function Measure-DemoRun {
    param([string]$Label)
    $m = Measure-CullConfig -Label $Label -Samples $Samples
    $p = Invoke-CullGet '/profiler'
    $kind = [string](Get-Field (Get-Field $p 'verdict') 'kind' '')
    return ($m | Add-Member -NotePropertyName VerdictKind -NotePropertyValue $kind -PassThru)
}

# Polls GET /render_graph until no fixed-cost feature reads on (the list lags the open by a few frames).
# Returns the names still on after the timeout.
function Wait-FixedCostFeaturesOff {
    param([int]$TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ($true) {
        $states = Get-FeatureStates
        $on = @(@($states.Keys) | Where-Object { (Test-GeometryOnlyFeature $_ $script:Prefixes) -and $states[$_].G })
        if (($on.Count -eq 0) -or ((Get-Date) -ge $deadline)) { return $on }
        Start-Sleep -Milliseconds 200
    }
}

# Closes the demo through the engine, which restores the features and settings it changed.
function Close-DemoScene {
    try {
        $reply = Invoke-CullPost '/close_preset_scene' @{} 60
        if (-not [bool](Get-Field $reply 'success' $false)) {
            Write-Host ('WARN  close_preset_scene: ' + [string](Get-Field $reply 'message' (Get-Field $reply 'error' 'no message')))
        } else {
            Write-Host ('Closed: ' + [string](Get-Field $reply 'message' ''))
        }
    } catch { Write-Host ('WARN  close_preset_scene: ' + $_) }
}

# Opens the demo scene for one profile and checks the preconditions. Returns $false when they fail.
function Open-DemoScene {
    param([string]$ProfileName)
    Out-Line ''
    Out-Line ('=== profile ' + $ProfileName + ' ===')
    $body = [ordered]@{ preset = 'hiz_demo'; count = $Count; spacing = $Spacing; profile = $ProfileName }
    $reply = Invoke-CullPost '/open_preset_scene' $body 300
    if (-not [bool](Get-Field $reply 'success' $false)) {
        Out-Line ('FAIL  open_preset_scene: ' + [string](Get-Field $reply 'error' (Get-Field $reply 'message' 'no message')))
        return $false
    }
    $script:Opened = $true
    Out-Line ('Opened: ' + [string](Get-Field $reply 'message' ''))
    Out-Line ('instances={0} clamped={1} clustersPerInstance={2} workItems={3}' -f (Get-Field $reply 'instanceCount' '-'),
        (Get-Field $reply 'countClamped' '-'), (Get-Field $reply 'clustersPerInstance' '-'), (Get-Field $reply 'workItems' '-'))

    if ($ProfileName -eq 'geometry_only') {
        foreach ($n in @(Wait-FixedCostFeaturesOff 10)) { Out-Line ('WARN  fixed-cost feature is still on: ' + $n) }
    }
    Show-SplitViews
    Start-Sleep -Seconds 3
    Wait-CullFrames 10

    $wall = Get-CullTrs $WallName
    if ($null -eq $wall) { Out-Line ("FAIL  entity '" + $WallName + "' not found."); return $false }
    $script:WallHome = $wall.Position
    Set-GpuDrivenCfg @{ enabled = 'true'; collectStats = 'true'; cone = 'false'; debugView = 'Off'; mode = 'Frustum' } | Out-Null
    Wait-CullFrames 10

    $pre = Test-CullPreconditions
    foreach ($w in $pre.Warnings) { Out-Line ('WARN  ' + $w) }
    if (-not $pre.Ok) {
        foreach ($f in $pre.Failures) { Out-Line ('FAIL  ' + $f) }
        return $false
    }
    $script:PresentPolicy = [string](Get-Field (Get-Field (Invoke-CullGet '/profiler') 'device_state') 'present_policy' 'unknown')
    return $true
}

# Measures every wall state and mode of the open scene in alternating rounds and stores the table rows.
function Measure-DemoProfile {
    param([string]$ProfileName)
    $modes = @('NoCull', 'Frustum', 'HiZ')
    foreach ($wallOn in @($false, $true)) {
        $wallLabel = if ($wallOn) { 'on' } else { 'off' }
        Set-DemoWall $wallOn
        $res = @{}
        foreach ($m in $modes) { $res[$m] = New-Object System.Collections.Generic.List[object] }
        for ($rep = 1; $rep -le $Repeats; $rep++) {
            $order = [string[]]$modes.Clone()
            if ($rep % 2 -eq 0) { [array]::Reverse($order) }
            foreach ($m in $order) {
                Set-GpuDrivenCfg @{ mode = $m } | Out-Null
                Write-Host ('  ' + $ProfileName + ' wall ' + $wallLabel + ' round ' + $rep + '/' + $Repeats + ' ' + $m)
                $res[$m].Add((Measure-DemoRun ($ProfileName + ' wall ' + $wallLabel + ' ' + $m)))
            }
        }
        foreach ($m in $modes) {
            $script:Rows.Add((New-HizDemoRow $ProfileName $wallLabel $m $res[$m].ToArray() $script:PresentPolicy))
        }
    }
}

$exitCode = 0
try {
    try { $null = Invoke-CullGet '/profiler' } catch { Write-Host ('Editor not reachable at ' + $BaseUrl + ': ' + $_); exit 2 }
    try { $script:Prefixes = @(Get-GeometryOnlyPrefixes) } catch { Write-Host ('Fixed-cost feature list unavailable: ' + $_); exit 2 }

    Show-SplitViews
    Start-Sleep -Seconds 2

    $profiles = if ($ProfileChoice -eq 'both') { @('geometry_only', 'full') } else { @($ProfileChoice) }
    foreach ($p in $profiles) {
        if (-not (Open-DemoScene $p)) { $exitCode = 2; break }
        Measure-DemoProfile $p
    }

    if ($exitCode -eq 0) {
        $rows = $script:Rows.ToArray()
        Out-Line ''
        Out-Line ("Present policy: $($script:PresentPolicy). Samples per run: $Samples, rounds: $Repeats, count: $Count, stats on (adds 3 passes per part).")
        Out-Line 'Medians over rounds. GpuDrivenScene columns are per-pass sums (basis A); span, primary, secondary are basis B.'
        Out-Line ''
        foreach ($l in @(Format-HizDemoTable $rows)) { Out-Line $l }
        Out-Line ''
        $v = Get-HizDemoVerdicts $rows $HizBudgetMs $WallRatio $MinValidPercent
        foreach ($l in @($v.Lines)) { Out-Line $l }
        Out-Line ''
        foreach ($c in @($v.Checks)) { Out-Line ('[{0}] {1}  {2}  ({3})' -f $c.Status, $c.Id, $c.Text, $c.Detail) }
        $exitCode = Get-HizDemoExitCode $v.Checks
        Out-Line ''
        Out-Line ("Summary: exit code $exitCode (0 pass, 1 check failed, 3 not evaluable).")

        if ($OutFile) {
            $base = [System.IO.Path]::ChangeExtension($OutFile, $null)
            if ($base.EndsWith('.')) { $base = $base.Substring(0, $base.Length - 1) }
            $json = [ordered]@{
                count = $Count; spacing = $Spacing; samples = $Samples; repeats = $Repeats
                presentPolicy = $script:PresentPolicy
                rows = @(ConvertTo-HizDemoJsonRows $rows)
                verdicts = @($v.Lines)
                checks = @($v.Checks)
                exitCode = $exitCode
            }
            Set-Content -Path ($base + '.md') -Value $script:Report -Encoding ASCII
            Set-Content -Path ($base + '.json') -Value (ConvertTo-Json -InputObject $json -Depth 6) -Encoding ASCII
        }
    } else {
        Out-Line 'STOP  precondition failed; no table was produced.'
    }
} catch {
    Write-Host ('Run aborted: ' + $_)
    $exitCode = 2
} finally {
    if ($script:Opened) {
        Write-Host 'Cleanup: closing the Hi-Z demo.'
        Close-DemoScene
    }
}
exit $exitCode
