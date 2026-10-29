# Helpers for the culling scripts. Dot-source it: . (Join-Path $PSScriptRoot 'lib.ps1')
# Needs a running editor with the HTTP server. Never name a function 'Set' (alias of Set-Variable).
Set-StrictMode -Version Latest

$global:CullBase = 'http://127.0.0.1:8080'
$global:CullTimeoutSec = 30
$global:CullFeatureCur = $null

function Initialize-CullLib {
    param([string]$BaseUrl = 'http://127.0.0.1:8080', [int]$TimeoutSec = 30)
    $global:CullBase = $BaseUrl.TrimEnd('/')
    $global:CullTimeoutSec = $TimeoutSec
    # A corporate proxy must never see loopback traffic.
    [System.Net.WebRequest]::DefaultWebProxy = $null
}

# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------

function Get-HttpErrorText($ErrorRecord) {
    $text = $ErrorRecord.Exception.Message
    if ($null -ne $ErrorRecord.ErrorDetails -and $ErrorRecord.ErrorDetails.Message) {
        $text += ' | body: ' + $ErrorRecord.ErrorDetails.Message
        return $text
    }
    $resp = $null
    if ($null -ne $ErrorRecord.Exception.PSObject.Properties['Response']) { $resp = $ErrorRecord.Exception.Response }
    if ($null -ne $resp) {
        try {
            $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
            $body = $reader.ReadToEnd()
            $reader.Close()
            if ($body) { $text += ' | body: ' + $body }
        } catch { }
    }
    return $text
}

# Invoke-WebRequest + ConvertFrom-Json: Invoke-RestMethod caps JSON at 2 MB on PowerShell 5.1.
function Invoke-CullGet {
    param([string]$Path, [int]$TimeoutSec = 0)
    if ($TimeoutSec -le 0) { $TimeoutSec = $global:CullTimeoutSec }
    try {
        $r = Invoke-WebRequest -Uri ($global:CullBase + $Path) -Method Get -UseBasicParsing -TimeoutSec $TimeoutSec
        return ($r.Content | ConvertFrom-Json)
    } catch {
        throw ('GET ' + $Path + ' failed: ' + (Get-HttpErrorText $_))
    }
}

function Invoke-CullPost {
    param([string]$Path, $Body, [int]$TimeoutSec = 0)
    if ($TimeoutSec -le 0) { $TimeoutSec = $global:CullTimeoutSec }
    $json = if ($Body -is [string]) { $Body } else { ConvertTo-Json -InputObject $Body -Compress -Depth 6 }
    try {
        $r = Invoke-WebRequest -Uri ($global:CullBase + $Path) -Method Post -Body $json -ContentType 'application/json' `
            -UseBasicParsing -TimeoutSec $TimeoutSec
        return ($r.Content | ConvertFrom-Json)
    } catch {
        throw ('POST ' + $Path + ' failed: ' + (Get-HttpErrorText $_))
    }
}

# Saves GET /get_game_view (raw PNG) to $Path. Returns $null on success, else the error text.
function Save-GameView {
    param([string]$Path, [int]$TimeoutSec = 30)
    try {
        Invoke-WebRequest -Uri ($global:CullBase + '/get_game_view') -Method Get -UseBasicParsing `
            -TimeoutSec $TimeoutSec -OutFile $Path | Out-Null
    } catch {
        return ('GET /get_game_view failed: ' + (Get-HttpErrorText $_))
    }
    $head = [System.IO.File]::ReadAllBytes($Path)
    if ($head.Length -lt 8 -or $head[0] -ne 0x89 -or $head[1] -ne 0x50 -or $head[2] -ne 0x4E -or $head[3] -ne 0x47) {
        return 'GET /get_game_view did not return a PNG'
    }
    return $null
}

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

# Strict-mode safe property read.
function Get-Field {
    param($Object, [string]$Name, $Default = $null)
    if ($null -eq $Object) { return $Default }
    $p = $Object.PSObject.Properties[$Name]
    if ($null -eq $p) { return $Default }
    return $p.Value
}

function Get-Median {
    param([double[]]$Values)
    $v = @($Values | Where-Object { -not [double]::IsNaN($_) } | Sort-Object)
    if ($v.Count -eq 0) { return [double]::NaN }
    $mid = [int][math]::Floor($v.Count / 2)
    if ($v.Count % 2 -eq 1) { return [double]$v[$mid] }
    return ([double]$v[$mid - 1] + [double]$v[$mid]) / 2.0
}

function Get-Mean {
    param([double[]]$Values)
    $v = @($Values | Where-Object { -not [double]::IsNaN($_) })
    if ($v.Count -eq 0) { return [double]::NaN }
    return [double](($v | Measure-Object -Average).Average)
}

function Format-Num {
    param([double]$Value, [string]$Format = 'N2')
    if ([double]::IsNaN($Value)) { return 'n/a' }
    return $Value.ToString($Format, [System.Globalization.CultureInfo]::InvariantCulture)
}

# A profiler metric reads as a number only when its status is Present; else NaN (never 0).
function Get-MetricValue {
    param($Metric)
    if ($null -eq $Metric) { return [double]::NaN }
    if ((Get-Field $Metric 'status') -ne 'Present') { return [double]::NaN }
    $v = Get-Field $Metric 'value'
    if ($null -eq $v) { return [double]::NaN }
    return [double]$v
}

function Get-FrameIndex {
    return [int64](Invoke-CullGet '/profiler').frame_index
}

function Wait-CullFrames {
    param([int]$Count, [int]$TimeoutSec = 20)
    $start = Get-FrameIndex
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 20
        if (((Get-FrameIndex) - $start) -ge $Count) { return }
    }
    throw ("Timed out waiting for $Count frames. Is the editor rendering?")
}

# ---------------------------------------------------------------------------
# Scene, camera, entities
# ---------------------------------------------------------------------------

# Euler degrees: x = pitch, y = yaw, z = roll.
function Set-CullTrs {
    param([string]$Name, [double[]]$Position = $null, [double[]]$RotationEuler = $null, [double[]]$Scale = $null)
    $body = [ordered]@{ name = $Name }
    if ($null -ne $Position) { $body['translation'] = [ordered]@{ x = $Position[0]; y = $Position[1]; z = $Position[2] } }
    if ($null -ne $RotationEuler) {
        $body['rotation_euler_degrees'] = [ordered]@{ x = $RotationEuler[0]; y = $RotationEuler[1]; z = $RotationEuler[2] }
    }
    if ($null -ne $Scale) { $body['scale'] = [ordered]@{ x = $Scale[0]; y = $Scale[1]; z = $Scale[2] } }
    Invoke-CullPost '/set_entity_trs' $body | Out-Null
}

function Set-CullCamera {
    param([string]$Name, [double[]]$Position, [double[]]$RotationEuler)
    Set-CullTrs -Name $Name -Position $Position -RotationEuler $RotationEuler
}

# Pose of a camera entity from GET /scene_camera/state, or $null when it is not listed.
function Get-CullCameraPose {
    param([string]$Name = 'Camera')
    try { $state = Invoke-CullGet '/scene_camera/state' } catch { return $null }
    foreach ($c in @(Get-Field $state 'cameras' @())) {
        if ((Get-Field $c 'name') -eq $Name) {
            $p = $c.position
            $r = $c.rotation_euler_degrees
            return [pscustomobject]@{ Position = @([double]$p.x, [double]$p.y, [double]$p.z); Rotation = @([double]$r.x, [double]$r.y, [double]$r.z) }
        }
    }
    return $null
}

# Width x height of the secondary view image, used to detect a resized view during a run.
function Get-CullViewSize {
    try {
        $state = Invoke-CullGet '/scene_camera/state'
        $img = Get-Field $state 'image'
        return ('{0}x{1}' -f (Get-Field $img 'width' 0), (Get-Field $img 'height' 0))
    } catch { return 'unknown' }
}

# Returns $true when the entity existed and was deleted.
function Remove-CullEntity {
    param([string]$Name)
    try { Invoke-CullPost '/delete_entity' @{ name = $Name } | Out-Null; return $true } catch { return $false }
}

# Creates a primitive and returns its resolved entity name (the engine may rename on a clash).
function Add-CullPrimitive {
    param([string]$Shape, [string]$Name, [double[]]$Position, [double[]]$Scale = $null)
    $pos = [ordered]@{ x = $Position[0]; y = $Position[1]; z = $Position[2] }
    $r = Invoke-CullPost '/instantiate_primitive' @{ shape = $Shape; name = $Name; world_position = $pos }
    $resolved = [string](Get-Field $r 'name' $Name)
    if ($null -ne $Scale) { Set-CullTrs -Name $resolved -Scale $Scale }
    return $resolved
}

# Creates a directional light and returns its resolved entity name.
function Add-CullSun {
    param([string]$Name, [double[]]$RotationEuler = @(50, 30, 0))
    $rot = [ordered]@{ x = $RotationEuler[0]; y = $RotationEuler[1]; z = $RotationEuler[2] }
    $r = Invoke-CullPost '/instantiate_light' @{ light_type = 'directional'; name = $Name; rotation_euler_degrees = $rot }
    return [string](Get-Field $r 'name' $Name)
}

# Instantiates a mesh asset and returns its resolved entity name.
function Add-CullAsset {
    param([string]$GtaPath)
    $r = Invoke-CullPost '/instantiate_asset' @{ gta_path = $GtaPath }
    return [string](Get-Field $r 'name' '')
}

# Game beside Scene: the Game view is not rendered while its tab is hidden.
function Show-SplitViews {
    Invoke-CullGet '/split_views?enabled=true' | Out-Null
}

# ---------------------------------------------------------------------------
# GpuDrivenScene settings and state
# ---------------------------------------------------------------------------

# Query text of a value. The route only accepts lowercase 'true'/'false' for booleans.
function ConvertTo-QueryValue {
    param($Value)
    if ($Value -is [bool]) { return $Value.ToString().ToLowerInvariant() }
    return [uri]::EscapeDataString([string]$Value)
}

# Writable keys: enabled, mode (NoCull|Frustum|HiZ), cone, collectStats, debugView (Off|ClusterId).
function Set-GpuDrivenCfg {
    param([hashtable]$Pairs)
    $q = (@($Pairs.GetEnumerator() | ForEach-Object { $_.Key + '=' + (ConvertTo-QueryValue $_.Value) }) -join '&')
    return (Invoke-CullGet ('/feature_debug/set?name=GpuDrivenScene&' + $q)).fields
}

# Flat dotted keys: 'game.rendered', 'game.visibleClusters', 'scene.*', 'mode', ...
function Get-GpuDrivenState {
    return (Invoke-CullGet '/feature_debug/state?name=GpuDrivenScene').fields
}

# Settings to restore after a run.
function Save-GpuDrivenCfg {
    $s = Get-GpuDrivenState
    return @{ enabled = $s.enabled; mode = $s.mode; cone = $s.cone; collectStats = $s.collectStats; debugView = $s.debugView }
}

# ---------------------------------------------------------------------------
# Render features: global enable plus per-view-scope enable
# ---------------------------------------------------------------------------

# Reads render_features[] (name, enabled, enabled_primary, enabled_secondary) and resets the local cache.
function Get-FeatureStates {
    $graph = Invoke-CullGet '/render_graph'
    $states = [ordered]@{}
    foreach ($f in @(Get-Field $graph 'render_features' @())) {
        $g = [bool](Get-Field $f 'enabled' $true)
        $states[[string]$f.name] = [pscustomobject]@{
            G = $g
            P = [bool](Get-Field $f 'enabled_primary' $true)
            S = [bool](Get-Field $f 'enabled_secondary' $true)
        }
    }
    $cache = [ordered]@{}
    foreach ($k in $states.Keys) { $cache[$k] = $states[$k].PSObject.Copy() }
    $global:CullFeatureCur = $cache
    return $states
}

function Send-FeatureEnabled {
    param([string]$Name, [bool]$Enabled, [string]$View = '')
    $q = '/render_graph/set_feature_enabled?name=' + [uri]::EscapeDataString($Name) + '&enabled=' + (ConvertTo-QueryValue $Enabled)
    if ($View) { $q += '&view=' + $View }
    Invoke-CullGet $q | Out-Null
}

# Moves one feature to (global, primary, secondary) with the fewest calls.
# A global call without view also sets both scope flags; a scope flag cannot be raised while the feature is off.
function Set-FeatureTriple {
    param([string]$Name, [bool]$G, [bool]$P, [bool]$S)
    if ($null -eq $global:CullFeatureCur) { throw 'Call Get-FeatureStates before Set-FeatureTriple.' }
    $cur = $global:CullFeatureCur[$Name]
    if ($null -eq $cur) { throw ("Unknown render feature '$Name'.") }
    if ($cur.G -ne $G) {
        Send-FeatureEnabled $Name $G
        $cur.G = $G; $cur.P = $G; $cur.S = $G
    }
    if (-not $G) { return }
    if ($cur.P -ne $P) { Send-FeatureEnabled $Name $P 'primary'; $cur.P = $P }
    if ($cur.S -ne $S) { Send-FeatureEnabled $Name $S 'secondary'; $cur.S = $S }
}

# Restores exactly the states returned by Get-FeatureStates.
function Restore-FeatureStates {
    param($Saved)
    foreach ($name in $Saved.Keys) {
        $t = $Saved[$name]
        try { Set-FeatureTriple $name $t.G $t.P $t.S } catch { Write-Warning ("Could not restore feature '$name': " + $_) }
    }
}

# ---------------------------------------------------------------------------
# Per-pass GPU time from GET /render_graph (offscreen regime)
# ---------------------------------------------------------------------------

# Sums gpu_timing_milliseconds per view and per owning feature. Only rows with status Present count.
# Returns: Folded (gpu_folded_passes), Rows (key 'view|feature' -> Ms, Barrier, Work, Present, Total), Views (view -> ms).
function Get-PassSums {
    param($Graph)
    $regime = Get-Field $Graph 'offscreen_regime'
    $folded = [int](Get-Field $regime 'gpu_folded_passes' 0)
    $rows = @{}
    $views = @{ primary = 0.0; secondary = 0.0; shared = 0.0 }
    $viewPresent = @{ primary = 0; secondary = 0; shared = 0 }
    foreach ($p in @(Get-Field $regime 'passes' @())) {
        if (Get-Field $p 'is_culled' $false) { continue }
        $view = [string](Get-Field $p 'view' 'shared')
        $feature = [string](Get-Field $p 'owning_feature_name' '')
        if (-not $feature) { $feature = '(builtin)' }
        $key = $view + '|' + $feature
        if (-not $rows.ContainsKey($key)) {
            $rows[$key] = [pscustomobject]@{ Ms = 0.0; Barrier = 0.0; Work = 0.0; Present = 0; Total = 0 }
        }
        $row = $rows[$key]
        $row.Total++
        if ((Get-Field $p 'gpu_timing_status') -ne 'Present') { continue }
        $ms = Get-Field $p 'gpu_timing_milliseconds'
        if ($null -eq $ms) { continue }
        $row.Present++
        $row.Ms += [double]$ms
        $b = Get-Field $p 'gpu_barrier_milliseconds'
        $w = Get-Field $p 'gpu_work_milliseconds'
        if ($null -ne $b) { $row.Barrier += [double]$b }
        if ($null -ne $w) { $row.Work += [double]$w }
        if ($views.ContainsKey($view)) { $views[$view] += [double]$ms; $viewPresent[$view]++ }
    }
    return [pscustomobject]@{ Folded = $folded; Rows = $rows; Views = $views; ViewPresent = $viewPresent }
}

# ---------------------------------------------------------------------------
# Measuring
# ---------------------------------------------------------------------------

# Takes $Samples profiler + render graph samples, $IntervalMs apart, after $SettleFrames frames.
# A sample is valid when the primary view and the span are Present and no GPU record is folded.
function Measure-CullConfig {
    param([string]$Label, [int]$Samples = 20, [int]$SettleFrames = 10, [int]$IntervalMs = 200)
    Wait-CullFrames $SettleFrames
    $span = New-Object System.Collections.Generic.List[double]
    $pri = New-Object System.Collections.Generic.List[double]
    $sec = New-Object System.Collections.Generic.List[double]
    $cpu = New-Object System.Collections.Generic.List[double]
    $passPri = New-Object System.Collections.Generic.List[double]
    $passSec = New-Object System.Collections.Generic.List[double]
    $barPri = New-Object System.Collections.Generic.List[double]
    $workPri = New-Object System.Collections.Generic.List[double]
    $keys = @{}
    $valid = 0
    $foldedMax = 0
    for ($i = 0; $i -lt $Samples; $i++) {
        $p = Invoke-CullGet '/profiler'
        $g = Invoke-CullGet '/render_graph'
        $gpu = Get-Field $p 'gpu'
        $views = Get-Field $gpu 'views'
        $s = Get-MetricValue (Get-Field $gpu 'span')
        $pv = Get-MetricValue (Get-Field $views 'primary')
        $sv = Get-MetricValue (Get-Field $views 'secondary')
        $sums = Get-PassSums $g
        if ($sums.Folded -gt $foldedMax) { $foldedMax = $sums.Folded }
        if (-not [double]::IsNaN($s) -and -not [double]::IsNaN($pv) -and $sums.Folded -eq 0) {
            $valid++
            $span.Add($s); $pri.Add($pv)
            if (-not [double]::IsNaN($sv)) { $sec.Add($sv) }
            $c = Get-MetricValue (Get-Field $p 'cpu_frame')
            if (-not [double]::IsNaN($c)) { $cpu.Add($c) }
            $passPri.Add([double]$sums.Views['primary'])
            $passSec.Add([double]$sums.Views['secondary'])
            $bp = 0.0; $wp = 0.0
            foreach ($k in $sums.Rows.Keys) {
                if (-not $keys.ContainsKey($k)) { $keys[$k] = New-Object System.Collections.Generic.List[double] }
                $keys[$k].Add([double]$sums.Rows[$k].Ms)
                if ($k.StartsWith('primary|')) { $bp += $sums.Rows[$k].Barrier; $wp += $sums.Rows[$k].Work }
            }
            $barPri.Add($bp); $workPri.Add($wp)
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
    $keyMed = @{}
    foreach ($k in $keys.Keys) { $keyMed[$k] = Get-Median ($keys[$k].ToArray()) }
    $st = Get-GpuDrivenState
    $visValid = [bool](Get-Field $st 'game.visibleValid' $false)
    $visible = if ($visValid) { [double](Get-Field $st 'game.visibleClusters' 0) } else { [double]::NaN }
    return [pscustomobject]@{
        Label = $Label
        Samples = $Samples
        Valid = $valid
        ValidPct = if ($Samples -gt 0) { 100.0 * $valid / $Samples } else { 0.0 }
        FoldedMax = $foldedMax
        Span = Get-Median $span.ToArray()
        Primary = Get-Median $pri.ToArray()
        Secondary = Get-Median $sec.ToArray()
        Cpu = Get-Median $cpu.ToArray()
        PassPrimary = Get-Median $passPri.ToArray()
        PassSecondary = Get-Median $passSec.ToArray()
        BarrierPrimary = Get-Median $barPri.ToArray()
        WorkPrimary = Get-Median $workPri.ToArray()
        FeatureMs = $keyMed
        Visible = $visible
        Tested = [double](Get-Field $st 'game.testedClusters' 0)
        Occlusion = [string](Get-Field $st 'game.occlusion' '')
        HizReason = [string](Get-Field $st 'game.hizReason' '')
        InertReason = [string](Get-Field $st 'game.inertReason' '')
    }
}

# ---------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------

# Checks the preconditions of every timing or equality run. Returns Ok, Failures[], Warnings[].
function Test-CullPreconditions {
    $fail = New-Object System.Collections.Generic.List[string]
    $warn = New-Object System.Collections.Generic.List[string]
    try {
        $st = Get-GpuDrivenState
        if (-not [bool](Get-Field $st 'supported' $false)) { $fail.Add('GpuDrivenScene: supported = false (device caps).') }
        if (-not [bool](Get-Field $st 'game.rendered' $false)) {
            $fail.Add('GpuDrivenScene: game.rendered = false. Show the Game tab (GET /split_views?enabled=true).')
        }
        $inert = [string](Get-Field $st 'game.inertReason' 'missing')
        if ($inert -ne 'none') { $fail.Add("GpuDrivenScene: game.inertReason = '$inert' (expected 'none').") }
        $claimed = [int64](Get-Field $st 'game.claimedEntities' 0)
        if ($claimed -lt 1) { $fail.Add("GpuDrivenScene: game.claimedEntities = $claimed (expected >= 1). Load a clustered mesh.") }
    } catch { $fail.Add('GpuDrivenScene state unreadable: ' + $_) }
    try {
        $g = Invoke-CullGet '/render_graph'
        $folded = [int](Get-Field (Get-Field $g 'offscreen_regime') 'gpu_folded_passes' 0)
        if ($folded -ne 0) { $fail.Add("render_graph: gpu_folded_passes = $folded (pipelined regime or record budget exceeded).") }
    } catch { $fail.Add('render_graph unreadable: ' + $_) }
    try {
        $fd = Invoke-CullGet '/frame_debugger/state'
        if ([bool](Get-Field $fd 'windowOpen' $false)) { $fail.Add('Frame Debugger window is open. Close it (capture suppresses the cluster path).') }
        if ([bool](Get-Field $fd 'enabled' $false)) { $warn.Add('Frame Debugger capture is enabled.') }
    } catch { $fail.Add('frame_debugger state unreadable: ' + $_) }
    try {
        $p = Invoke-CullGet '/profiler'
        $policy = [string](Get-Field (Get-Field $p 'device_state') 'present_policy' 'unknown')
        if ($policy -ne 'uncapped') { $warn.Add("present_policy = '$policy'. FPS is vsync-capped; start the editor with --no-vsync. Compare ms only.") }
        if ([bool](Get-Field $p 'display_paused' $false)) { $warn.Add('Profiler display is paused.') }
    } catch { $warn.Add('profiler unreadable: ' + $_) }
    return [pscustomobject]@{ Ok = ($fail.Count -eq 0); Failures = $fail.ToArray(); Warnings = $warn.ToArray() }
}

# ---------------------------------------------------------------------------
# Fixed-cost feature set (the engine owns the list)
# ---------------------------------------------------------------------------

# Name prefixes of the fixed-cost features, from GET /hiz_demo/geometry_only_features. Throws when empty.
function Get-GeometryOnlyPrefixes {
    $r = Invoke-CullGet '/hiz_demo/geometry_only_features'
    $p = @(@(Get-Field $r 'prefixes' @()) | ForEach-Object { [string]$_ })
    if ($p.Count -eq 0) { throw 'GET /hiz_demo/geometry_only_features returned no prefixes.' }
    return $p
}

function Test-GeometryOnlyFeature {
    param([string]$Name, [string[]]$Prefixes)
    foreach ($p in $Prefixes) {
        if ($Name.StartsWith($p, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# Entity transform read, feature pass time
# ---------------------------------------------------------------------------

# Current transform of an entity. A name-only POST /set_entity_trs changes nothing. Returns $null on failure.
function Get-CullTrs {
    param([string]$Name)
    try { $r = Invoke-CullPost '/set_entity_trs' @{ name = $Name } } catch { return $null }
    $t = Get-Field $r 'transform'
    if ($null -eq $t) { return $null }
    $p = $t.position
    $e = $t.rotation_euler_degrees
    $s = $t.scale
    return [pscustomobject]@{
        Position = @([double]$p.x, [double]$p.y, [double]$p.z)
        Rotation = @([double]$e.x, [double]$e.y, [double]$e.z)
        Scale = @([double]$s.x, [double]$s.y, [double]$s.z)
    }
}

# Median pass ms of one feature in one view ('primary' | 'secondary') of a Measure-CullConfig result.
# NaN when the feature had no timed pass in that view.
function Get-FeatureMs {
    param($Measurement, [string]$View, [string]$Feature)
    $key = $View + '|' + $Feature
    $fm = $Measurement.FeatureMs
    if ($fm.ContainsKey($key)) { return [double]$fm[$key] }
    return [double]::NaN
}

# ---------------------------------------------------------------------------
# Hi-Z demo table and verdicts (pure functions of measured rows)
# ---------------------------------------------------------------------------

# Number as text; a value that was not measured prints '-', never 0.
function Format-Cell {
    param([double]$Value, [string]$Format = 'N2')
    if ([double]::IsNaN($Value)) { return '-' }
    return $Value.ToString($Format, [System.Globalization.CultureInfo]::InvariantCulture)
}

# Signed change of $Value against $Base as '+1.23 ms (+4.5 %)'. '-' when either is unknown.
function Format-HizDelta {
    param([double]$Value, [double]$Base)
    if ([double]::IsNaN($Value) -or [double]::IsNaN($Base)) { return '-' }
    $d = $Value - $Base
    $sign = if ($d -lt 0) { '-' } else { '+' }
    $text = ('{0}{1} ms' -f $sign, (Format-Cell ([math]::Abs($d))))
    if ($Base -gt 0) { $text += (' ({0}{1} %)' -f $sign, (Format-Cell ([math]::Abs(100.0 * $d / $Base)) 'N1')) }
    return $text
}

# One table row from the repeated measurements ($Reps) of one profile, wall state and mode.
# Each measurement is a Measure-CullConfig result, optionally with a VerdictKind note property.
function New-HizDemoRow {
    param([string]$Profile, [string]$Wall, [string]$Mode, $Reps, [string]$PresentPolicy)
    $arr = @($Reps)
    $validSum = 0
    $sampleSum = 0
    foreach ($m in $arr) { $validSum += $m.Valid; $sampleSum += $m.Samples }
    $validPct = if ($sampleSum -gt 0) { 100.0 * $validSum / $sampleSum } else { 0.0 }
    $cpu = Get-Median @($arr | ForEach-Object { $_.Cpu })
    $limited = (@($arr | Where-Object { (Get-Field $_ 'VerdictKind' '') -eq 'present_limited' }).Count -gt 0)
    $fps = [double]::NaN
    $fpsNote = ''
    if ($PresentPolicy -ne 'uncapped') { $fpsNote = 'n/a: capped' }
    elseif ($limited) { $fpsNote = 'n/a: present limited' }
    elseif ([double]::IsNaN($cpu) -or $cpu -le 0) { $fpsNote = 'n/a: not measured' }
    else { $fps = 1000.0 / $cpu }
    $applied = ($Mode -ne 'HiZ') -or (@($arr | Where-Object { $_.Occlusion -ne 'Applied' }).Count -eq 0)
    return [pscustomobject]@{
        Profile = $Profile
        Wall = $Wall
        Mode = $Mode
        Span = Get-Median @($arr | ForEach-Object { $_.Span })
        Primary = Get-Median @($arr | ForEach-Object { $_.Primary })
        Secondary = Get-Median @($arr | ForEach-Object { $_.Secondary })
        GdsPrimary = Get-Median @($arr | ForEach-Object { Get-FeatureMs $_ 'primary' 'GpuDrivenScene' })
        GdsSecondary = Get-Median @($arr | ForEach-Object { Get-FeatureMs $_ 'secondary' 'GpuDrivenScene' })
        Tested = Get-Median @($arr | ForEach-Object { $_.Tested })
        Drawn = Get-Median @($arr | ForEach-Object { $_.Visible })
        Cpu = $cpu
        Fps = $fps
        FpsNote = $fpsNote
        Applied = $applied
        ValidPct = $validPct
    }
}

function Find-HizDemoRow {
    param($Rows, [string]$Profile, [string]$Wall, [string]$Mode)
    foreach ($r in $Rows) {
        if ($r.Profile -eq $Profile -and $r.Wall -eq $Wall -and $r.Mode -eq $Mode) { return $r }
    }
    return $null
}

# Markdown table lines, one row per measured configuration.
function Format-HizDemoTable {
    param($Rows)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('| profile | wall | mode | span ms | primary ms | secondary ms | GpuDrivenScene primary ms | GpuDrivenScene secondary ms | clusters tested | clusters drawn | FPS | samples valid % |')
    $lines.Add('| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |')
    foreach ($r in $Rows) {
        $fps = if (-not [double]::IsNaN($r.Fps)) { Format-Cell $r.Fps 'N0' } elseif ($r.FpsNote) { $r.FpsNote } else { '-' }
        $lines.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} | {11} |' -f `
            $r.Profile, $r.Wall, $r.Mode, (Format-Cell $r.Span), (Format-Cell $r.Primary), (Format-Cell $r.Secondary),
            (Format-Cell $r.GdsPrimary), (Format-Cell $r.GdsSecondary), (Format-Cell $r.Tested 'N0'), (Format-Cell $r.Drawn 'N0'),
            $fps, (Format-Cell $r.ValidPct 'N0')))
    }
    return $lines.ToArray()
}

function New-HizDemoCheck {
    param([string]$Id, [string]$Text, [string]$Status, [string]$Detail)
    return [pscustomobject]@{ Id = $Id; Text = $Text; Status = $Status; Detail = $Detail }
}

# Computed verdict lines and PASS / FAIL / INVALID checks per profile.
#   wall on : Hi-Z GpuDrivenScene primary ms <= $WallRatio x Frustum.
#   wall off: Hi-Z GpuDrivenScene primary ms <= Frustum + $HizBudgetMs.
# INVALID = a row has too few valid samples, Hi-Z was not Applied, or a value was not measured.
function Get-HizDemoVerdicts {
    param($Rows, [double]$HizBudgetMs = 0.4, [double]$WallRatio = 0.10, [double]$MinValidPercent = 80.0)
    $lines = New-Object System.Collections.Generic.List[string]
    $checks = New-Object System.Collections.Generic.List[object]
    $profiles = @($Rows | ForEach-Object { $_.Profile } | Select-Object -Unique)
    foreach ($p in $profiles) {
        $nOff = Find-HizDemoRow $Rows $p 'off' 'NoCull'
        $fOff = Find-HizDemoRow $Rows $p 'off' 'Frustum'
        $hOff = Find-HizDemoRow $Rows $p 'off' 'HiZ'
        $fOn = Find-HizDemoRow $Rows $p 'on' 'Frustum'
        $hOn = Find-HizDemoRow $Rows $p 'on' 'HiZ'

        if (($null -ne $nOff) -and ($null -ne $fOff)) {
            $lines.Add(('[{0}] Frustum vs NoCull, wall off: {1}' -f $p, (Format-HizDelta $fOff.Span $nOff.Span)))
        }
        if (($null -ne $fOn) -and ($null -ne $hOn)) {
            $lines.Add(('[{0}] Hi-Z vs Frustum, wall on: {1}, clusters drawn {2} -> {3}' -f $p,
                (Format-HizDelta $hOn.Span $fOn.Span), (Format-Cell $fOn.Drawn 'N0'), (Format-Cell $hOn.Drawn 'N0')))
            $status = 'INVALID'
            $detail = 'a row has too few valid samples, Hi-Z was not Applied, or a value was not measured'
            $ok = ($fOn.ValidPct -ge $MinValidPercent) -and ($hOn.ValidPct -ge $MinValidPercent) -and $hOn.Applied
            $ok = $ok -and (-not [double]::IsNaN($fOn.GdsPrimary)) -and (-not [double]::IsNaN($hOn.GdsPrimary)) -and ($fOn.GdsPrimary -gt 0)
            if ($ok) {
                $status = if ($hOn.GdsPrimary -le ($WallRatio * $fOn.GdsPrimary)) { 'PASS' } else { 'FAIL' }
                $detail = ('Hi-Z {0} ms, Frustum {1} ms' -f (Format-Cell $hOn.GdsPrimary 'N3'), (Format-Cell $fOn.GdsPrimary 'N3'))
            }
            $checks.Add((New-HizDemoCheck ('H1 ' + $p) ('wall on: Hi-Z GpuDrivenScene primary <= ' + (Format-Cell (100.0 * $WallRatio) 'N0') + '% of Frustum') $status $detail))
        }
        if (($null -ne $fOff) -and ($null -ne $hOff)) {
            $lines.Add(('[{0}] Hi-Z vs Frustum, wall off: {1} (GpuDrivenScene primary {2})' -f $p,
                (Format-HizDelta $hOff.Span $fOff.Span), (Format-HizDelta $hOff.GdsPrimary $fOff.GdsPrimary)))
            $status = 'INVALID'
            $detail = 'a row has too few valid samples, Hi-Z was not Applied, or a value was not measured'
            $ok = ($fOff.ValidPct -ge $MinValidPercent) -and ($hOff.ValidPct -ge $MinValidPercent) -and $hOff.Applied
            $ok = $ok -and (-not [double]::IsNaN($fOff.GdsPrimary)) -and (-not [double]::IsNaN($hOff.GdsPrimary))
            if ($ok) {
                $status = if ($hOff.GdsPrimary -le ($fOff.GdsPrimary + $HizBudgetMs)) { 'PASS' } else { 'FAIL' }
                $detail = ('Hi-Z {0} ms, Frustum {1} ms' -f (Format-Cell $hOff.GdsPrimary 'N3'), (Format-Cell $fOff.GdsPrimary 'N3'))
            }
            $checks.Add((New-HizDemoCheck ('H2 ' + $p) ('wall off: Hi-Z GpuDrivenScene primary <= Frustum + ' + (Format-Cell $HizBudgetMs) + ' ms') $status $detail))
        }
    }
    return [pscustomobject]@{ Lines = $lines.ToArray(); Checks = $checks.ToArray() }
}

# 0 all pass, 1 a check failed, 3 a check could not be evaluated (or there is no check).
function Get-HizDemoExitCode {
    param($Checks)
    $c = @($Checks)
    if ($c.Count -eq 0) { return 3 }
    if (@($c | Where-Object { $_.Status -eq 'FAIL' }).Count -gt 0) { return 1 }
    if (@($c | Where-Object { $_.Status -ne 'PASS' }).Count -gt 0) { return 3 }
    return 0
}

# Rows as ordered maps with NaN turned into null (ConvertTo-Json would write an invalid NaN token).
function ConvertTo-HizDemoJsonRows {
    param($Rows)
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($r in $Rows) {
        $o = [ordered]@{}
        foreach ($prop in $r.PSObject.Properties) {
            $v = $prop.Value
            if (($v -is [double]) -and [double]::IsNaN($v)) { $v = $null }
            $o[$prop.Name] = $v
        }
        $out.Add($o)
    }
    return $out.ToArray()
}
