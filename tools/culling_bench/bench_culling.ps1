<#
  Culling benchmark. Drives a running editor over HTTP; nothing is saved to disk by the editor.
  Tables: T1 geometry only | T2 full pipeline | T3 Hi-Z wall | T7 camera looks away | T4 empty floor | T5 heavy scene (-IncludeHeavy, also T7h).
  Each table runs its configs in -Repeats alternating A/B rounds with -Samples samples per run.
  Exit code: 0 = all checks pass, 1 = a check failed, 2 = precondition failed or editor unreachable,
  3 = a check could not be evaluated (invalid rows or missing data).
  Compare milliseconds, never FPS. Start the editor with --no-vsync.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [string]$Terrain = '',
    [switch]$SkipLoad,
    [string]$CameraEntity = 'Camera',
    [double[]]$TerrainCamera = @(0, 900, 0),
    [int]$Samples = 20,
    [int]$Repeats = 3,
    [switch]$IncludeHeavy,
    [int]$GridCount = 100000,
    [double]$GridSpacing = 2.0,
    [double]$GridCameraZ = -300.0,
    [double]$MinValidPercent = 80.0,
    [double]$HizBudgetMs = 0.4,
    [double]$BaselineSpanMs = 0.0,
    [string]$OutFile = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Terrain) { $Terrain = Join-Path $ScriptDir '..\..\build\Project\terrain_clustered.gta' }
. (Join-Path $ScriptDir 'lib.ps1')
Initialize-CullLib -BaseUrl $BaseUrl

$script:Report = New-Object System.Collections.Generic.List[string]
$script:Checks = New-Object System.Collections.Generic.List[object]
$script:Created = New-Object System.Collections.Generic.List[string]
$script:RowOk = @{}
$script:Saved = $null
$script:ShadowPattern = '^Shadow'
$script:Prefixes = @()
$script:MdRows = New-Object System.Collections.Generic.List[string]

function Out-Line {
    param([string]$Text)
    Write-Host $Text
    $script:Report.Add($Text)
}

# One measurement configuration. Post/Shadow: 'saved' keeps the editor state, 'off' disables.
# Secondary: 'saved' keeps the per-view flags, 'all' enables every enabled feature in the secondary view.
function New-Cfg {
    param([string]$Name, [string]$Mode, [string]$Post = 'saved', [string]$Shadow = 'saved',
        [string]$Secondary = 'saved', [bool]$Stats = $false)
    return [pscustomobject]@{ Name = $Name; Mode = $Mode; Post = $Post; Shadow = $Shadow; Secondary = $Secondary; Stats = $Stats }
}

function Set-BenchConfig {
    param($Cfg)
    $stats = if ($Cfg.Stats) { 'true' } else { 'false' }
    Set-GpuDrivenCfg @{ mode = $Cfg.Mode; collectStats = $stats } | Out-Null
    foreach ($name in $script:Saved.Keys) {
        $t = $script:Saved[$name]
        $g = $t.G
        if ($name -match $script:ShadowPattern -and $Cfg.Shadow -eq 'off') { $g = $false }
        if ((Test-GeometryOnlyFeature $name $script:Prefixes) -and ($name -notmatch $script:ShadowPattern) -and $Cfg.Post -eq 'off') { $g = $false }
        $s = if ($Cfg.Secondary -eq 'all') { $true } else { $t.S }
        Set-FeatureTriple $name $g $t.P $s
    }
}

function Write-BenchRow {
    param([string]$Table, [string]$Name, $Reps)
    $arr = $Reps.ToArray()
    $validSum = 0; $sampleSum = 0
    foreach ($m in $arr) { $validSum += $m.Valid; $sampleSum += $m.Samples }
    $validPct = if ($sampleSum -gt 0) { 100.0 * $validSum / $sampleSum } else { 0.0 }
    $ok = ($validPct -ge $MinValidPercent)
    $script:RowOk[$Table + '|' + $Name] = $ok
    $span = Get-Median @($arr | ForEach-Object { $_.Span })
    $pri = Get-Median @($arr | ForEach-Object { $_.Primary })
    $sec = Get-Median @($arr | ForEach-Object { $_.Secondary })
    $cpu = Get-Median @($arr | ForEach-Object { $_.Cpu })
    $pp = Get-Median @($arr | ForEach-Object { $_.PassPrimary })
    $ps = Get-Median @($arr | ForEach-Object { $_.PassSecondary })
    $bar = Get-Median @($arr | ForEach-Object { $_.BarrierPrimary })
    $work = Get-Median @($arr | ForEach-Object { $_.WorkPrimary })
    $vis = Get-Median @($arr | ForEach-Object { $_.Visible })
    Out-Line ('{0,-18} span={1,7} primary={2,7} secondary={3,7} cpu={4,7} | passes P={5,7} S={6,7} (P barrier {7}, work {8}) | visible={9} | valid {10}/{11} ({12}%){13}' -f `
        $Name, (Format-Num $span), (Format-Num $pri), (Format-Num $sec), (Format-Num $cpu), (Format-Num $pp), (Format-Num $ps),
        (Format-Num $bar), (Format-Num $work), (Format-Num $vis 'N0'), $validSum, $sampleSum, (Format-Num $validPct 'N0'),
        $(if ($ok) { '' } else { '  ROW FAIL (< ' + $MinValidPercent + '% valid)' }))
    foreach ($view in @('primary', 'secondary')) {
        $acc = @{}
        foreach ($m in $arr) {
            foreach ($k in $m.FeatureMs.Keys) {
                if ($k.StartsWith($view + '|')) {
                    if (-not $acc.ContainsKey($k)) { $acc[$k] = New-Object System.Collections.Generic.List[double] }
                    $acc[$k].Add([double]$m.FeatureMs[$k])
                }
            }
        }
        $parts = @($acc.Keys | ForEach-Object { [pscustomobject]@{ F = $_.Substring($view.Length + 1); Ms = (Get-Mean $acc[$_].ToArray()) } } |
            Sort-Object Ms -Descending | Select-Object -First 7 | ForEach-Object { $_.F + ' ' + (Format-Num $_.Ms) })
        if ($parts.Count -gt 0) { Out-Line ('    ' + $view + ' by feature: ' + ($parts -join ' | ')) }
    }
    $script:MdRows.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8}% |' -f $Table, $Name, (Format-Num $span), (Format-Num $pri),
        (Format-Num $sec), (Format-Num $pp), (Format-Num $ps), (Format-Num $vis 'N0'), (Format-Num $validPct 'N0')))
}

# Runs every config once per round. Odd rounds go forward, even rounds backward (alternating A/B pairs).
function Invoke-BenchTable {
    param([string]$Table, [object[]]$Configs)
    Out-Line ''
    Out-Line ('=== ' + $Table + ' ===')
    $res = @{}
    foreach ($c in $Configs) { $res[$c.Name] = New-Object System.Collections.Generic.List[object] }
    for ($rep = 1; $rep -le $Repeats; $rep++) {
        $order = [object[]]$Configs.Clone()
        if ($rep % 2 -eq 0) { [array]::Reverse($order) }
        foreach ($c in $order) {
            Set-BenchConfig $c
            Write-Host ('  ' + $Table + ' round ' + $rep + '/' + $Repeats + ' ' + $c.Name)
            $res[$c.Name].Add((Measure-CullConfig -Label ($Table + ' ' + $c.Name) -Samples $Samples))
        }
    }
    foreach ($c in $Configs) { Write-BenchRow $Table $c.Name $res[$c.Name] }
    return $res
}

function Test-RowsOk {
    param([string[]]$Keys)
    foreach ($k in $Keys) { if (-not $script:RowOk.ContainsKey($k) -or -not $script:RowOk[$k]) { return $false } }
    return $true
}

# Element-wise value over two runs of the same round: $Fn($a, $b) for rep i.
function Get-Pairwise {
    param($A, $B, [scriptblock]$Fn)
    $out = New-Object System.Collections.Generic.List[double]
    $n = [math]::Min($A.Count, $B.Count)
    for ($i = 0; $i -lt $n; $i++) { $out.Add([double](& $Fn $A[$i] $B[$i])) }
    return $out.ToArray()
}

# Excess <= 0 passes (value minus its limit). MsRule: no single round may miss by more than 2 ms.
# -Partial: the baseline is incomplete, so the numbers are printed but the status is PARTIAL, never PASS or FAIL.
function Add-Check {
    param([string]$Id, [string]$Text, [double[]]$Excess, [bool]$RowsOk, [bool]$MsRule, [string]$Note = '', [switch]$Partial)
    $status = 'INVALID'
    $detail = ''
    $vals = @($Excess | Where-Object { -not [double]::IsNaN($_) })
    if ($RowsOk -and $vals.Count -gt 0 -and $vals.Count -eq @($Excess).Count) {
        $med = Get-Median $vals
        $worst = [double](($vals | Measure-Object -Maximum).Maximum)
        $pass = ($med -le 0.0) -and ((-not $MsRule) -or ($worst -le 2.0))
        $status = if ($Partial) { 'PARTIAL' } elseif ($pass) { 'PASS' } else { 'FAIL' }
        $detail = ('median excess {0}, worst round {1}' -f (Format-Num $med 'N3'), (Format-Num $worst 'N3'))
    } else {
        $detail = if (-not $RowsOk) { 'a row has too few valid samples' } else { 'missing data' }
    }
    $script:Checks.Add([pscustomobject]@{ Id = $Id; Status = $status })
    Out-Line ('[{0}] {1,-4} {2}  ({3}){4}' -f $status, $Id, $Text, $detail, $(if ($Note) { ' ' + $Note } else { '' }))
}

function Remove-Created {
    for ($i = $script:Created.Count - 1; $i -ge 0; $i--) {
        $n = $script:Created[$i]
        if (-not (Remove-CullEntity $n)) { Write-Warning ("Could not delete entity '$n' (already gone?).") }
    }
    $script:Created.Clear()
}

# Camera rotations tried for 'nothing in view' (pitch, yaw, roll in degrees).
$script:AwayRotations = @(@(80.0, 0.0, 0.0), @(-80.0, 0.0, 0.0), @(0.0, 180.0, 0.0))

# True when the game view holds a valid, non-zero count of visible clusters (Frustum mode, stats on).
function Test-SceneInView {
    $st = Get-GpuDrivenState
    return ([bool](Get-Field $st 'game.visibleValid' $false) -and ([double](Get-Field $st 'game.visibleClusters' 0) -gt 0))
}

# First pose that skips a part and shows nothing, or $null. Tries the given position first, then raised
# positions (a camera inside the part box cannot skip it). Returns Position and Rotation.
function Find-AwayPose {
    param([double[]]$Position)
    $positions = @($Position,
        [double[]]@($Position[0], [math]::Max(4000.0, $Position[1] + 3000.0), $Position[2]),
        [double[]]@($Position[0], 20000.0, $Position[2]))
    foreach ($pos in $positions) {
        foreach ($rot in $script:AwayRotations) {
            Set-CullCamera $CameraEntity $pos $rot
            Wait-CullFrames 12
            $st = Get-GpuDrivenState
            $nothing = [bool](Get-Field $st 'game.visibleValid' $false) -and ([double](Get-Field $st 'game.visibleClusters' -1) -eq 0)
            if ($nothing -and ([double](Get-Field $st 'game.skippedParts' 0) -ge 1)) {
                return [pscustomobject]@{ Position = [double[]]$pos; Rotation = [double[]]$rot }
            }
        }
    }
    return $null
}

# Frustum runs with the camera facing the scene and looking away, in alternating rounds (stats on).
# Returns Facing and Away measurement lists, or $null when the poses cannot be established.
function Invoke-AwayTable {
    param([string]$Table, $Facing)
    Out-Line ''
    Out-Line ('=== ' + $Table + ' ===')
    Set-GpuDrivenCfg @{ mode = 'Frustum'; collectStats = 'true'; debugView = 'Off'; cone = 'false' } | Out-Null
    Set-CullCamera $CameraEntity $Facing.Position $Facing.Rotation
    Wait-CullFrames 12
    if (-not (Test-SceneInView)) {
        Out-Line 'INVALID  the facing pose shows no visible cluster; the away table is skipped.'
        return $null
    }
    $away = Find-AwayPose $Facing.Position
    if ($null -eq $away) {
        Set-CullCamera $CameraEntity $Facing.Position $Facing.Rotation
        Out-Line 'INVALID  no camera pose was found that skips the part; the away table is skipped.'
        return $null
    }
    Out-Line ('Away position: ' + ($away.Position -join ', ') + '  rotation (pitch, yaw, roll): ' + ($away.Rotation -join ', '))
    $poses = @{ facing = $Facing; away = $away }
    $res = @{ facing = New-Object System.Collections.Generic.List[object]; away = New-Object System.Collections.Generic.List[object] }
    for ($rep = 1; $rep -le $Repeats; $rep++) {
        $order = if ($rep % 2 -eq 0) { @('away', 'facing') } else { @('facing', 'away') }
        foreach ($n in $order) {
            Set-CullCamera $CameraEntity $poses[$n].Position $poses[$n].Rotation
            Write-Host ('  ' + $Table + ' round ' + $rep + '/' + $Repeats + ' ' + $n)
            $m = Measure-CullConfig -Label ($Table + ' ' + $n) -Samples $Samples
            $st = Get-GpuDrivenState
            $skipped = [double](Get-Field $st 'game.skippedParts' ([double]::NaN))
            $res[$n].Add(($m | Add-Member -NotePropertyName Skipped -NotePropertyValue $skipped -PassThru))
        }
    }
    Set-CullCamera $CameraEntity $Facing.Position $Facing.Rotation
    foreach ($n in @('facing', 'away')) {
        $arr = $res[$n].ToArray()
        $validSum = 0; $sampleSum = 0
        foreach ($m in $arr) { $validSum += $m.Valid; $sampleSum += $m.Samples }
        $validPct = if ($sampleSum -gt 0) { 100.0 * $validSum / $sampleSum } else { 0.0 }
        $script:RowOk[$Table + '|' + $n] = ($validPct -ge $MinValidPercent)
        # A part with no pass has no row in the graph: its GpuDrivenScene time is exactly 0 while the view is valid.
        $gds = Get-Median @($arr | ForEach-Object { $g = Get-FeatureMs $_ 'primary' 'GpuDrivenScene'; if ([double]::IsNaN($g)) { 0.0 } else { $g } })
        $tested = Get-Median @($arr | ForEach-Object { $_.Tested })
        $skip = Get-Median @($arr | ForEach-Object { $_.Skipped })
        Out-Line ('{0,-8} GpuDrivenScene primary={1,7} ms  clusters tested={2,9}  skippedParts={3,4}  valid {4}/{5} ({6}%)' -f $n,
            (Format-Num $gds 'N3'), (Format-Num $tested 'N0'), (Format-Num $skip 'N0'), $validSum, $sampleSum, (Format-Num $validPct 'N0'))
    }
    return [pscustomobject]@{ Facing = $res['facing']; Away = $res['away'] }
}

# Checks of an away table: the part is skipped, nothing is tested, no GpuDrivenScene pass runs; facing is not skipped.
function Add-AwayChecks {
    param($Res, [string]$Table, [string]$Id)
    if ($null -eq $Res) {
        Add-Check ($Id + '1') ($Table + ': away view skips the part') @([double]::NaN) $false $false 'no away pose'
        return
    }
    $rowsOk = Test-RowsOk @(($Table + '|facing'), ($Table + '|away'))
    $away = $Res.Away.ToArray()
    $facing = $Res.Facing.ToArray()
    $ex = @($away | ForEach-Object { 1.0 - $_.Skipped })
    Add-Check ($Id + '1') ($Table + ': away view skips >= 1 part (game.skippedParts)') $ex $rowsOk $false
    $ex = @($away | ForEach-Object { [double]$_.Tested })
    Add-Check ($Id + '2') ($Table + ': away view tests 0 clusters') $ex $rowsOk $false
    $ex = @($away | ForEach-Object { $g = Get-FeatureMs $_ 'primary' 'GpuDrivenScene'; if ([double]::IsNaN($g)) { 0.0 } else { $g } })
    Add-Check ($Id + '3') ($Table + ': away view has no GpuDrivenScene pass in the primary view (sum = 0 ms)') $ex $rowsOk $false
    $ex = @($facing | ForEach-Object { $_.Skipped })
    Add-Check ($Id + '4') ($Table + ': facing view skips no part') $ex $rowsOk $false
}

$exitCode = 0
$savedCfg = $null
$savedCam = $null
$viewSizeStart = 'unknown'
$terrainName = ''
try {
    try { $null = Invoke-CullGet '/profiler' } catch { Write-Host ('Editor not reachable at ' + $BaseUrl + ': ' + $_); exit 2 }
    try { $script:Prefixes = @(Get-GeometryOnlyPrefixes) } catch { Write-Host ('Fixed-cost feature list unavailable: ' + $_); exit 2 }

    Show-SplitViews
    Start-Sleep -Seconds 2
    $script:Saved = Get-FeatureStates
    $savedCfg = Save-GpuDrivenCfg
    $savedCam = Get-CullCameraPose $CameraEntity
    if ($null -eq $savedCam) { Write-Warning "Camera '$CameraEntity' pose not readable: it will not be restored." }
    $viewSizeStart = Get-CullViewSize

    if (-not $SkipLoad) {
        $terrainPath = [System.IO.Path]::GetFullPath($Terrain)
        if (-not (Test-Path -LiteralPath $terrainPath)) { throw "Terrain asset not found: $terrainPath (use -Terrain or -SkipLoad)" }
        $terrainName = Add-CullAsset $terrainPath
        $script:Created.Add($terrainName)
        $script:Created.Add((Add-CullSun 'BenchSun'))
    }
    Set-GpuDrivenCfg @{ collectStats = 'false'; debugView = 'Off'; cone = 'false' } | Out-Null
    Set-CullCamera $CameraEntity $TerrainCamera @(0, 0, 0)
    Start-Sleep -Seconds 2
    Wait-CullFrames 10

    $pre = Test-CullPreconditions
    foreach ($w in $pre.Warnings) { Out-Line ('WARN  ' + $w) }
    if (-not $pre.Ok) {
        foreach ($f in $pre.Failures) { Out-Line ('FAIL  ' + $f) }
        Out-Line 'STOP  preconditions failed; no measurement was taken.'
        $exitCode = 2
    } else {
        $pp = Get-Field (Get-Field (Invoke-CullGet '/profiler') 'device_state') 'present_policy' 'unknown'
        Out-Line ("Present policy: $pp. Absolute ms are orientation only (drift up to +-30% between runs); the checks use same-run ratios.")
        Out-Line ("Samples per run: $Samples, rounds: $Repeats, view size: $viewSizeStart")

        $t1 = Invoke-BenchTable 'T1 geometry-only' @(
            (New-Cfg 'NoCull' 'NoCull' 'off' 'off'), (New-Cfg 'Frustum' 'Frustum' 'off' 'off'), (New-Cfg 'HiZ' 'HiZ' 'off' 'off'))

        $t2 = Invoke-BenchTable 'T2 full-pipeline' @(
            (New-Cfg 'NoCull' 'NoCull'), (New-Cfg 'Frustum' 'Frustum'), (New-Cfg 'HiZ' 'HiZ'),
            (New-Cfg 'Frustum-noShadow' 'Frustum' 'saved' 'off'),
            (New-Cfg 'Frustum-allScopes' 'Frustum' 'saved' 'saved' 'all'))

        # T3: big thin wall in front of the camera.
        $wall = Add-CullPrimitive 'cube' 'BenchWall' @($TerrainCamera[0], $TerrainCamera[1], $TerrainCamera[2] + 5) @(4000, 4000, 0.2)
        $script:Created.Add($wall)
        $t3 = Invoke-BenchTable 'T3 hiz-wall' @(
            (New-Cfg 'Frustum' 'Frustum' 'saved' 'saved' 'saved' $true), (New-Cfg 'HiZ' 'HiZ' 'saved' 'saved' 'saved' $true))
        [void]$script:Created.Remove($wall)
        [void](Remove-CullEntity $wall)

        # T7: camera looks away, nothing in view.
        $away = Invoke-AwayTable 'T7 away' ([pscustomobject]@{ Position = [double[]]$TerrainCamera; Rotation = @(0.0, 0.0, 0.0) })

        # T4: empty scene, the fixed floor.
        $t4 = $null
        if (-not $SkipLoad) {
            if (-not (Remove-CullEntity $terrainName)) { Write-Warning "Could not delete '$terrainName'; T4 is not an empty scene." }
            [void]$script:Created.Remove($terrainName)
            $t4 = Invoke-BenchTable 'T4 empty-floor' @(
                (New-Cfg 'Frustum-full' 'Frustum'), (New-Cfg 'Frustum-postOff' 'Frustum' 'off' 'off'))
        } else {
            Out-Line ''
            Out-Line '=== T4 empty-floor === skipped (-SkipLoad: the scene is not owned by this script)'
        }

        # T5: heavy scene (needs -IncludeHeavy).
        $t5 = $null; $t5w = $null; $away5 = $null
        if ($IncludeHeavy) {
            Out-Line ''
            Out-Line ("Spawning instance grid: count=$GridCount spacing=$GridSpacing (camera far plane cannot be set over HTTP; the default 1000 is assumed)")
            Invoke-CullPost '/spawn_instance_grid' @{ count = $GridCount; spacing = $GridSpacing } | Out-Null
            Set-CullCamera $CameraEntity @(0, 0, $GridCameraZ) @(0, 0, 0)
            Start-Sleep -Seconds 2
            $t5 = Invoke-BenchTable 'T5 heavy-grid' @(
                (New-Cfg 'NoCull' 'NoCull' 'off' 'off'), (New-Cfg 'Frustum' 'Frustum' 'off' 'off'), (New-Cfg 'HiZ' 'HiZ' 'off' 'off'))
            $away5 = Invoke-AwayTable 'T7h away-grid' ([pscustomobject]@{ Position = @(0.0, 0.0, $GridCameraZ); Rotation = @(0.0, 0.0, 0.0) })
            $wall5 = Add-CullPrimitive 'cube' 'BenchWall5' @(0, 0, $GridCameraZ + 50) @(4000, 4000, 0.2)
            $script:Created.Add($wall5)
            $t5w = Invoke-BenchTable 'T5w heavy-grid-wall' @(
                (New-Cfg 'Frustum' 'Frustum' 'off' 'off' 'saved' $true), (New-Cfg 'HiZ' 'HiZ' 'off' 'off' 'saved' $true))
        }

        # ---- Definition-of-done checks (ratios of a same-run baseline; ms rules only where stated) ----
        Out-Line ''
        Out-Line '=== Checks ==='
        $ok1 = Test-RowsOk @('T1 geometry-only|NoCull', 'T1 geometry-only|Frustum', 'T1 geometry-only|HiZ')
        $ex = Get-Pairwise $t1['NoCull'] $t1['Frustum'] { param($a, $b)
            $base = $a.PassPrimary + $a.PassSecondary
            if ($base -le 0) { [double]::NaN } else { (($b.PassPrimary + $b.PassSecondary) / $base) - 0.15 } }
        Add-Check 'D1' 'T1: Frustum pass sum <= 15% of NoCull (basis A, all passes of both views)' $ex $ok1 $false
        $ex = Get-Pairwise $t1['NoCull'] $t1['Frustum'] { param($a, $b)
            $base = $a.PassPrimary + $a.PassSecondary
            if ($base -le 0) { [double]::NaN } else { (($b.PassPrimary + $b.PassSecondary) / $base) - (1.0 / 7.0) } }
        Add-Check 'V7' 'T1: NoCull >= 7x Frustum (regression guard)' $ex $ok1 $false
        $ex = Get-Pairwise $t1['Frustum'] $t1['HiZ'] { param($f, $h) $h.PassPrimary - ($f.PassPrimary + $HizBudgetMs) }
        Add-Check 'D3' ('T1: HiZ <= Frustum + ' + $HizBudgetMs + ' ms (basis A, primary view, pyramid passes included)') $ex $ok1 $true

        $ok2 = Test-RowsOk @('T2 full-pipeline|NoCull', 'T2 full-pipeline|Frustum')
        $ex = Get-Pairwise $t2['NoCull'] $t2['Frustum'] { param($a, $b) 3.0 - ($a.Span - $b.Span) }
        Add-Check 'D1b' 'T2: NoCull - Frustum >= 3 ms (basis B)' $ex $ok2 $true '(absolute ms: orientation only)'
        $ok5 = Test-RowsOk @('T2 full-pipeline|Frustum', 'T2 full-pipeline|Frustum-noShadow')
        $ex = Get-Pairwise $t2['Frustum'] $t2['Frustum-noShadow'] { param($on, $offr) ($on.Span - $offr.Span) - 6.0 }
        Add-Check 'D5' 'T2 Frustum: shadows on <= shadows off + 6 ms (basis B)' $ex $ok5 $true '(absolute ms: orientation only)'
        $ok6 = Test-RowsOk @('T2 full-pipeline|Frustum')
        $ex = Get-Pairwise $t2['Frustum'] $t2['Frustum'] { param($a, $b) if ($a.Span -le 0) { [double]::NaN } else { ($a.Secondary / $a.Span) - 0.35 } }
        Add-Check 'D6' 'T2 Frustum: secondary <= 35% of the span with defaults (basis B)' $ex $ok6 $false
        if ($BaselineSpanMs -gt 0) {
            $ex = @($t2['Frustum'].ToArray() | ForEach-Object { ($_.Span / $BaselineSpanMs) - 0.65 })
            Add-Check 'D7' ('T2 Frustum: span <= 0.65 x baseline ' + (Format-Num $BaselineSpanMs) + ' ms (stored baseline)') $ex $ok6 $false
        } else {
            $ok7 = Test-RowsOk @('T2 full-pipeline|Frustum', 'T2 full-pipeline|Frustum-allScopes')
            $ex = Get-Pairwise $t2['Frustum-allScopes'] $t2['Frustum'] { param($before, $after) if ($before.Span -le 0) { [double]::NaN } else { ($after.Span / $before.Span) - 0.65 } }
            Add-Check 'D7' 'T2 Frustum: span <= 0.65 x span with every feature on in both views' $ex $ok7 $false 'Not evaluable without -BaselineSpanMs: the baseline emulates the per-view switch only, not the old fixed shadow size.' -Partial
        }

        $ok3 = Test-RowsOk @('T3 hiz-wall|Frustum', 'T3 hiz-wall|HiZ')
        $applied = (@($t3['HiZ'].ToArray() | Where-Object { $_.Occlusion -ne 'Applied' }).Count -eq 0)
        $ex = Get-Pairwise $t3['Frustum'] $t3['HiZ'] { param($f, $h) if (-not ($f.Visible -gt 0)) { [double]::NaN } else { ($h.Visible / $f.Visible) - 0.05 } }
        Add-Check 'D2' 'T3 wall: clusters drawn in HiZ <= 5% of Frustum, pyramid active' $ex ($ok3 -and $applied) $false $(if ($applied) { '' } else { '(game.occlusion was not Applied in HiZ)' })

        if ($null -ne $t5w) {
            $ok5w = Test-RowsOk @('T5w heavy-grid-wall|Frustum', 'T5w heavy-grid-wall|HiZ')
            $applied5 = (@($t5w['HiZ'].ToArray() | Where-Object { $_.Occlusion -ne 'Applied' }).Count -eq 0)
            $ex = Get-Pairwise $t5w['Frustum'] $t5w['HiZ'] { param($f, $h) if (-not ($f.Visible -gt 0)) { [double]::NaN } else { ($h.Visible / $f.Visible) - 0.05 } }
            Add-Check 'D2b' 'T5w grid behind wall: clusters drawn in HiZ <= 5% of Frustum' $ex ($ok5w -and $applied5) $false $(if ($applied5) { '' } else { '(game.occlusion was not Applied in HiZ)' })
        }

        Add-AwayChecks $away 'T7 away' 'A'
        if ($IncludeHeavy) { Add-AwayChecks $away5 'T7h away-grid' 'Ah' }

        if ($null -ne $t4) {
            $floor = Get-Median @($t4['Frustum-postOff'].ToArray() | ForEach-Object { $_.Span })
            $full = Get-Median @($t4['Frustum-full'].ToArray() | ForEach-Object { $_.Span })
            Out-Line ("Floor (empty scene): full pipeline {0} ms, every post feature off {1} ms (orientation only)." -f (Format-Num $full), (Format-Num $floor))
        }

        $viewSizeEnd = Get-CullViewSize
        if ($viewSizeEnd -ne $viewSizeStart) { Out-Line ("WARN  view size changed during the run: $viewSizeStart -> $viewSizeEnd. Results are not comparable.") }

        $failed = @($script:Checks | Where-Object { $_.Status -eq 'FAIL' }).Count
        $invalid = @($script:Checks | Where-Object { $_.Status -eq 'INVALID' }).Count
        $partial = @($script:Checks | Where-Object { $_.Status -eq 'PARTIAL' }).Count
        Out-Line ''
        Out-Line ("Summary: {0} checks, {1} FAIL, {2} INVALID, {3} PARTIAL." -f $script:Checks.Count, $failed, $invalid, $partial)
        if ($failed -gt 0) { $exitCode = 1 } elseif (($invalid + $partial) -gt 0) { $exitCode = 3 }

        Out-Line ''
        Out-Line 'Result table (ms; medians over rounds; pass sums are basis A):'
        Out-Line '| table | config | span | primary | secondary | passes P | passes S | visible | valid |'
        Out-Line '| --- | --- | --- | --- | --- | --- | --- | --- | --- |'
        foreach ($r in $script:MdRows) { Out-Line $r }
    }
} finally {
    Write-Host 'Cleanup: removing created entities, restoring camera, settings and feature states.'
    try { Remove-Created } catch { Write-Warning ('Entity cleanup failed: ' + $_) }
    if ($IncludeHeavy) { Out-Line 'NOTE  the instance grid cannot be bulk-deleted over HTTP. Close the editor without saving.' }
    try { if ($null -ne $savedCam) { Set-CullCamera $CameraEntity $savedCam.Position $savedCam.Rotation } } catch { Write-Warning ('Camera restore failed: ' + $_) }
    try { if ($null -ne $savedCfg) { Set-GpuDrivenCfg $savedCfg | Out-Null } } catch { Write-Warning ('GpuDrivenScene restore failed: ' + $_) }
    try { if ($null -ne $script:Saved) { Restore-FeatureStates $script:Saved } } catch { Write-Warning ('Feature restore failed: ' + $_) }
    if ($OutFile) { Set-Content -Path $OutFile -Value $script:Report -Encoding ASCII }
}
exit $exitCode
