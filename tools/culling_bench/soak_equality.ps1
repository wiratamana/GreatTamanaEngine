<#
  Image-equality soak test. Hi-Z never removes a visible cluster, and a camera turning back from nothing in view
  draws the same image as NoCull (scenario away-and-back: -AwayFrames away, one frame back). Needs GET /hiz_demo/geometry_only_features.
  Geometry-only config, ClusterId debug view (one colour per cluster). Each sequence of camera poses and mover
  positions is replayed in Hi-Z mode (history runs frame after frame) and in NoCull mode. NoCull is captured twice
  per pose: its own difference is the noise floor (cluster emit order is not deterministic, depth ties flip pixels).
  A frame FAILS when the Hi-Z difference is above max(8, 4 x noise) pixels, or when the differing pixels form a
  connected block of 16 or more pixels (a wrongly removed cluster looks like that). Raw numbers are always printed.
  Exit code: 0 = every scenario ran and no frame failed, 1 = a frame failed,
  2 = a precondition failed, Hi-Z was not active, a capture failed or a scenario was skipped. The away-and-back
  scenario also stops with 2 when game.skippedParts stays 0 after every away block (the camera never looked away).
  Start the editor with the Game tab visible and the Frame Debugger closed. The scene must not be saved afterwards.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [int]$Seed = 1234,
    [string]$OutDir = '',
    [string]$Terrain = '',
    [switch]$SkipLoad,
    [string[]]$MoverEntities = @(),
    [ValidateSet('terrain', 'grid', 'both')][string]$Scene = 'terrain',
    [string]$CameraEntity = 'Camera',
    [int]$RestPoses = 12,
    [int]$PanFrames = 60,
    [int]$TurnPairs = 6,
    [int]$MoverFrames = 60,
    [int]$AwayFrames = 30,
    [int]$FrameWait = 2,
    [int]$GridCount = 2000,
    [double]$GridSpacing = 2.0,
    [switch]$KeepAllImages
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP 'culling_soak' }
if (-not $Terrain) { $Terrain = Join-Path $ScriptDir '..\..\build\Project\terrain_clustered.gta' }
. (Join-Path $ScriptDir 'lib.ps1')
Initialize-CullLib -BaseUrl $BaseUrl
Add-Type -AssemblyName System.Drawing

$NoiseFloorPixels = 8
$NoiseFactor = 4
$BlockPixels = 16

$script:Rng = New-Object System.Random($Seed)
$script:Results = New-Object System.Collections.Generic.List[object]
$script:Created = New-Object System.Collections.Generic.List[string]
$script:Skipped = New-Object System.Collections.Generic.List[string]
$script:Saved = $null
$script:Probe = $null
$script:HizApplied = $false
$script:Fatal = $false

function Get-Rnd {
    param([double[]]$Range)
    return $Range[0] + ($Range[1] - $Range[0]) * $script:Rng.NextDouble()
}

# Scene definitions. Rest ranges are camera position (X/Y/Z), pitch and yaw in degrees.
# AwayPos / AwayRot is a camera pose with nothing of the scene in view (outside the mesh box for the terrain).
function Get-SceneDef {
    param([string]$Name)
    if ($Name -eq 'terrain') {
        return [pscustomobject]@{
            Name = 'terrain'; Base = @(0, 900, 0); BasePitch = -10.0; AwayPos = @(0, 4000, 0); AwayRot = @(-80.0, 0.0, 0.0)
            X = @(-600, 600); Y = @(820, 1300); Z = @(-600, 600); Pitch = @(-50, 10); Yaw = @(0, 360)
            WallPos = @(0, 900, 40); WallScale = @(30, 30, 0.2); MoverAmp = 40.0; MoverHome = @(0, 0, 0)
        }
    }
    return [pscustomobject]@{
        Name = 'grid'; Base = @(0, 0, -60); BasePitch = 0.0; AwayPos = @(0, 0, -60); AwayRot = @(0.0, 180.0, 0.0)
        X = @(-40, 40); Y = @(-40, 40); Z = @(-120, -40); Pitch = @(-15, 15); Yaw = @(-30, 30)
        WallPos = @(0, 0, -30); WallScale = @(20, 20, 0.2); MoverAmp = 8.0; MoverHome = @(0, 0, 8)
    }
}

function New-Frame {
    param([double[]]$Pos, [double[]]$Rot, $Movers = @())
    return [pscustomobject]@{ Pos = $Pos; Rot = $Rot; Movers = @($Movers) }
}

function New-RestFrames {
    param($Def, [int]$Count)
    $frames = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $Count; $i++) {
        $pos = @((Get-Rnd $Def.X), (Get-Rnd $Def.Y), (Get-Rnd $Def.Z))
        $frames.Add((New-Frame $pos @((Get-Rnd $Def.Pitch), (Get-Rnd $Def.Yaw), 0.0)))
    }
    return $frames.ToArray()
}

# Camera pans 2 degrees per frame.
function New-PanFrames {
    param($Def, [int]$Count)
    $frames = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $Count; $i++) { $frames.Add((New-Frame $Def.Base @($Def.BasePitch, (-60.0 + 2.0 * $i), 0.0))) }
    return $frames.ToArray()
}

# The camera turns 40 degrees in ONE frame, back and forth.
function New-TurnFrames {
    param($Def, [int]$Pairs)
    $frames = New-Object System.Collections.Generic.List[object]
    for ($p = 0; $p -lt $Pairs; $p++) {
        $y0 = Get-Rnd @(-90, 90)
        $frames.Add((New-Frame $Def.Base @($Def.BasePitch, $y0, 0.0)))
        $frames.Add((New-Frame $Def.Base @($Def.BasePitch, ($y0 + 40.0), 0.0)))
    }
    return $frames.ToArray()
}

# Index of the last away frame of each block built by New-AwayAndBackFrames.
function Get-AwayProbeFrames {
    param([int]$AwayFrames)
    $last = foreach ($block in 0..1) { $block * ($AwayFrames + 1) + $AwayFrames - 1 }
    return @($last)
}

# Camera looks away (AwayPos / AwayRot, nothing in view) for $AwayFrames frames, then one frame back at the base pose, twice.
function New-AwayAndBackFrames {
    param($Def, [int]$AwayFrames)
    $frames = New-Object System.Collections.Generic.List[object]
    foreach ($backYaw in @(0.0, 20.0)) {
        for ($i = 0; $i -lt $AwayFrames; $i++) { $frames.Add((New-Frame $Def.AwayPos $Def.AwayRot)) }
        $frames.Add((New-Frame $Def.Base @($Def.BasePitch, $backYaw, 0.0)))
    }
    return $frames.ToArray()
}

# Static camera, movers drift on smooth paths with a little seeded noise.
function New-MoverFrames {
    param($Def, [string[]]$Names, [int]$Count)
    $frames = New-Object System.Collections.Generic.List[object]
    $phase = @{}
    foreach ($n in $Names) { $phase[$n] = Get-Rnd @(0, 6.28) }
    for ($i = 0; $i -lt $Count; $i++) {
        $movers = New-Object System.Collections.Generic.List[object]
        foreach ($n in $Names) {
            $a = $Def.MoverAmp; $ph = $phase[$n]
            $pos = @(($Def.MoverHome[0] + $a * [math]::Sin(0.25 * $i + $ph) + (Get-Rnd @(-0.5, 0.5))),
                     ($Def.MoverHome[1] + 0.3 * $a * [math]::Cos(0.17 * $i + $ph)),
                     ($Def.MoverHome[2] + $a * [math]::Cos(0.2 * $i + $ph)))
            $movers.Add([pscustomobject]@{ Name = $n; Pos = $pos })
        }
        $frames.Add((New-Frame $Def.Base @($Def.BasePitch, 0.0, 0.0) $movers.ToArray()))
    }
    return $frames.ToArray()
}

function Set-SoakFrame {
    param($Frame)
    Set-CullCamera $CameraEntity $Frame.Pos $Frame.Rot
    foreach ($m in $Frame.Movers) { Set-CullTrs -Name $m.Name -Position $m.Pos }
}

# Replays $Frames in one mode, one PNG per frame ('<prefix>2_NNN.png' for -Twice). HiZ mode sets $script:HizApplied
# when any frame reads occlusion Applied; -ProbeFrames add game.skippedParts to $script:Probe. Returns $null or an error.
function Save-SoakPhase {
    param([string]$Dir, [string]$Prefix, [string]$Mode, $Frames, [int]$Wait, [switch]$Twice, [int[]]$ProbeFrames = @())
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    Set-GpuDrivenCfg @{ mode = $Mode } | Out-Null
    Set-SoakFrame $Frames[0]
    Wait-CullFrames 6
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        Set-SoakFrame $Frames[$i]
        Wait-CullFrames $Wait
        $err = Save-GameView (Join-Path $Dir ('{0}_{1:D3}.png' -f $Prefix, $i))
        if ($err) { return ("frame $i ($Mode): " + $err) }
        if (($Mode -eq 'HiZ') -and (-not $script:HizApplied)) {
            $script:HizApplied = ([string](Get-Field (Get-GpuDrivenState) 'game.occlusion' '') -eq 'Applied')
        }
        if ($ProbeFrames -contains $i) {
            $script:Probe.Add([double](Get-Field (Get-GpuDrivenState) 'game.skippedParts' 0))
        }
        if ($Twice) {
            Wait-CullFrames $Wait
            $err = Save-GameView (Join-Path $Dir ('{0}2_{1:D3}.png' -f $Prefix, $i))
            if ($err) { return ("frame $i ($Mode, second capture): " + $err) }
        }
    }
    return $null
}

# Size of the largest 8-connected group in a list of pixel indices (y * width + x).
function Get-LargestBlock {
    param([System.Collections.Generic.List[int]]$Pixels, [int]$Width)
    if ($Pixels.Count -eq 0) { return 0 }
    $left = New-Object 'System.Collections.Generic.HashSet[int]'
    foreach ($p in $Pixels) { [void]$left.Add($p) }
    $stack = New-Object 'System.Collections.Generic.Stack[int]'
    $best = 0
    foreach ($start in $Pixels) {
        if (-not $left.Remove($start)) { continue }
        $size = 0
        $stack.Push($start)
        while ($stack.Count -gt 0) {
            $p = $stack.Pop()
            $size++
            $x = $p % $Width
            foreach ($dy in -1, 0, 1) {
                foreach ($dx in -1, 0, 1) {
                    if ($dx -eq 0 -and $dy -eq 0) { continue }
                    $nx = $x + $dx
                    if ($nx -lt 0 -or $nx -ge $Width) { continue }
                    $q = $p + $dy * $Width + $dx
                    if ($left.Remove($q)) { $stack.Push($q) }
                }
            }
        }
        if ($size -gt $best) { $best = $size }
    }
    return $best
}

# Pixel comparison of two PNGs. Equal hashes mean equal files; otherwise the decoded pixels are counted.
# Returns Differing (pixels), MaxBlock (largest connected group of differing pixels), Total, SizeMismatch.
function Compare-Png {
    param([string]$A, [string]$B)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $ha = [BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($A)))
        $hb = [BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($B)))
    } finally { $sha.Dispose() }
    if ($ha -eq $hb) { return [pscustomobject]@{ Differing = 0; MaxBlock = 0; Total = 0; SizeMismatch = $false } }
    $bmpA = $null; $bmpB = $null
    try {
        $bmpA = New-Object System.Drawing.Bitmap($A)
        $bmpB = New-Object System.Drawing.Bitmap($B)
        if ($bmpA.Width -ne $bmpB.Width -or $bmpA.Height -ne $bmpB.Height) {
            $all = [int]($bmpA.Width * $bmpA.Height)
            return [pscustomobject]@{ Differing = $all; MaxBlock = $all; Total = $all; SizeMismatch = $true }
        }
        $w = $bmpA.Width; $h = $bmpA.Height
        $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
        $fmt = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $mode = [System.Drawing.Imaging.ImageLockMode]::ReadOnly
        $da = $bmpA.LockBits($rect, $mode, $fmt)
        $db = $bmpB.LockBits($rect, $mode, $fmt)
        $n = $da.Stride * $h
        $ba = New-Object byte[] $n
        $bb = New-Object byte[] $n
        [System.Runtime.InteropServices.Marshal]::Copy($da.Scan0, $ba, 0, $n)
        [System.Runtime.InteropServices.Marshal]::Copy($db.Scan0, $bb, 0, $n)
        $bmpA.UnlockBits($da)
        $bmpB.UnlockBits($db)
        $diff = New-Object 'System.Collections.Generic.List[int]'
        for ($i = 0; $i -lt $n; $i += 4) {
            if ($ba[$i] -ne $bb[$i] -or $ba[$i + 1] -ne $bb[$i + 1] -or $ba[$i + 2] -ne $bb[$i + 2] -or $ba[$i + 3] -ne $bb[$i + 3]) {
                $diff.Add([int]($i -shr 2))
            }
        }
        return [pscustomobject]@{ Differing = $diff.Count; MaxBlock = (Get-LargestBlock $diff $w); Total = $w * $h; SizeMismatch = $false }
    } finally {
        if ($null -ne $bmpA) { $bmpA.Dispose() }
        if ($null -ne $bmpB) { $bmpB.Dispose() }
    }
}

# A frame fails when the Hi-Z difference is far above the NoCull-against-NoCull noise, or forms a block.
function Test-FrameFails {
    param([int]$HizDiff, [int]$Noise, [int]$Block)
    $limit = [math]::Max($NoiseFloorPixels, $NoiseFactor * $Noise)
    return (($HizDiff -gt $limit) -or ($Block -ge $BlockPixels))
}

# Runs one scenario in both modes and compares frame by frame.
function Invoke-SoakScenario {
    param([string]$Scene, [string]$Name, $Frames, [int[]]$ProbeFrames = @())
    $dir = Join-Path (Join-Path $OutDir $Scene) $Name
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    Write-Host ("  scenario $Scene/$Name : " + $Frames.Count + ' frames')
    $row = [pscustomobject]@{ Scene = $Scene; Scenario = $Name; Frames = $Frames.Count; Compared = 0; EqualFrames = 0
        DiffFrames = 0; FailFrames = 0; DiffPixels = 0; MaxDiff = 0; MaxNoise = 0; MaxBlock = 0; HizActive = $false; Error = '' }
    $script:Probe = New-Object System.Collections.Generic.List[double]
    $script:HizApplied = $false
    $err = Save-SoakPhase $dir 'hiz' 'HiZ' $Frames $FrameWait -ProbeFrames $ProbeFrames
    if (-not $err -and $ProbeFrames.Count -gt 0 -and (($script:Probe | Measure-Object -Maximum).Maximum -lt 1)) {
        $err = 'camera never looked away: skippedParts = 0'
    }
    if (-not $err) {
        $row.HizActive = $script:HizApplied
        $err = Save-SoakPhase $dir 'ref' 'NoCull' $Frames $FrameWait -Twice
    }
    if ($err) { $row.Error = $err; $script:Results.Add($row); return }
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        $ref = Join-Path $dir ('ref_{0:D3}.png' -f $i)
        $ref2 = Join-Path $dir ('ref2_{0:D3}.png' -f $i)
        $hiz = Join-Path $dir ('hiz_{0:D3}.png' -f $i)
        $c = Compare-Png $ref $hiz
        $noise = (Compare-Png $ref $ref2).Differing
        $fails = Test-FrameFails $c.Differing $noise $c.MaxBlock
        $row.Compared++
        if ($c.Differing -eq 0) { $row.EqualFrames++ } else { $row.DiffFrames++ }
        $row.DiffPixels += $c.Differing
        if ($c.Differing -gt $row.MaxDiff) { $row.MaxDiff = $c.Differing }
        if ($noise -gt $row.MaxNoise) { $row.MaxNoise = $noise }
        if ($c.MaxBlock -gt $row.MaxBlock) { $row.MaxBlock = $c.MaxBlock }
        if ($fails) { $row.FailFrames++ }
        if ($c.Differing -gt 0 -or $noise -gt 0) {
            Write-Host ("    frame $i : hizDiff=$($c.Differing) noise=$noise largestBlock=$($c.MaxBlock) of $($c.Total) px" +
                $(if ($c.SizeMismatch) { ' (image size mismatch)' } else { '' }) + $(if ($fails) { '  FAIL' } else { '  ok' }))
        }
        if (-not $fails -and -not $KeepAllImages) { Remove-Item -LiteralPath $ref, $ref2, $hiz -Force }
    }
    $script:Results.Add($row)
}

# Grid entities are 'InstanceGrid <batch> <index>'. The newest batch is the highest one that exists.
function Find-GridMovers {
    param($Def)
    $batch = 0
    for ($b = 1; $b -le 64; $b++) {
        try { Set-CullTrs -Name "InstanceGrid $b 0" -Position $Def.MoverHome } catch { break }
        $batch = $b
    }
    if ($batch -eq 0) { return @() }
    $names = @("InstanceGrid $batch 0")
    foreach ($i in 1..2) {
        $n = "InstanceGrid $batch $i"
        try { Set-CullTrs -Name $n -Position $Def.MoverHome; $names += $n } catch { }
    }
    return $names
}

function Invoke-SoakScene {
    param([string]$Name)
    $def = Get-SceneDef $Name
    Write-Host ("Scene '$Name'")
    $moverNames = @()
    if ($Name -eq 'terrain') {
        if (-not $SkipLoad) {
            $terrainPath = [System.IO.Path]::GetFullPath($Terrain)
            if (-not (Test-Path -LiteralPath $terrainPath)) { throw "Terrain asset not found: $terrainPath (use -Terrain or -SkipLoad)" }
            $tn = Add-CullAsset $terrainPath
            $script:Created.Add($tn)
            $script:TerrainName = $tn
        }
        if ($script:TerrainName) { $moverNames = @($script:TerrainName) }
        elseif ($MoverEntities.Count -gt 0) { $moverNames = @($MoverEntities) }
    } else {
        if ($script:TerrainName -and $script:Created.Contains($script:TerrainName)) {
            [void](Remove-CullEntity $script:TerrainName)
            [void]$script:Created.Remove($script:TerrainName)
            $script:TerrainName = ''
        }
        Invoke-CullPost '/spawn_instance_grid' @{ count = $GridCount; spacing = $GridSpacing } | Out-Null
        $moverNames = @(Find-GridMovers $def)
    }
    if ($moverNames.Count -eq 0) {
        $script:Skipped.Add("$Name/movers (no mover entity found)")
        Write-Warning "No mover entity found: the mover scenario is skipped for '$Name'. The run cannot pass."
    }

    Set-CullCamera $CameraEntity $def.Base @($def.BasePitch, 0, 0)
    Start-Sleep -Seconds 2
    Wait-CullFrames 10
    $pre = Test-CullPreconditions
    foreach ($w in $pre.Warnings) { Write-Host ('WARN  ' + $w) }
    if (-not $pre.Ok) {
        foreach ($f in $pre.Failures) { Write-Host ('FAIL  ' + $f) }
        $script:Fatal = $true
        return
    }

    Invoke-SoakScenario $Name 'rest' (New-RestFrames $def $RestPoses)

    $wall = Add-CullPrimitive 'cube' ('SoakWall_' + $Name) $def.WallPos $def.WallScale
    $script:Created.Add($wall)
    Invoke-SoakScenario $Name 'pan' (New-PanFrames $def $PanFrames)
    Invoke-SoakScenario $Name 'turn40' (New-TurnFrames $def $TurnPairs)
    Invoke-SoakScenario $Name 'away-and-back' (New-AwayAndBackFrames $def $AwayFrames) -ProbeFrames (Get-AwayProbeFrames $AwayFrames)
    if ($moverNames.Count -gt 0) {
        Invoke-SoakScenario $Name 'movers' (New-MoverFrames $def $moverNames $MoverFrames)
        foreach ($n in $moverNames) { try { Set-CullTrs -Name $n -Position $def.MoverHome } catch { } }
    }
    [void](Remove-CullEntity $wall)
    [void]$script:Created.Remove($wall)
}

$exitCode = 0
$savedCfg = $null
$savedCam = $null
$script:TerrainName = ''
try {
    try { $null = Invoke-CullGet '/profiler' } catch { Write-Host ('Editor not reachable at ' + $BaseUrl + ': ' + $_); exit 2 }
    try { $prefixes = @(Get-GeometryOnlyPrefixes) } catch { Write-Host ('Fixed-cost feature list unavailable: ' + $_); exit 2 }
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    Show-SplitViews
    Start-Sleep -Seconds 2
    $script:Saved = Get-FeatureStates
    $savedCfg = Save-GpuDrivenCfg
    $savedCam = Get-CullCameraPose $CameraEntity

    # Deterministic config: geometry only, no temporal features, one colour per cluster.
    foreach ($name in $script:Saved.Keys) {
        if (Test-GeometryOnlyFeature $name $prefixes) { Set-FeatureTriple $name $false $false $false }
    }
    Set-GpuDrivenCfg @{ debugView = 'ClusterId'; collectStats = $false; cone = $false; enabled = $true } | Out-Null

    Write-Host ("Seed $Seed, output folder $OutDir")
    Write-Host ("Verdict: a frame fails when hizDiff > max($NoiseFloorPixels, $NoiseFactor x noise) or largestBlock >= $BlockPixels pixels.")
    Write-Host 'NOTE  MeshHasCoincidentTriangles is not exposed over HTTP. Coincident triangles can swap winners between draw orders; check the mesh offline if a block differs.'
    $scenes = if ($Scene -eq 'both') { @('terrain', 'grid') } else { @($Scene) }
    foreach ($s in $scenes) {
        Invoke-SoakScene $s
        if ($script:Fatal) { break }
    }

    Write-Host ''
    Write-Host 'scene    scenario      frames equal differing  FAILED  diffPx(total)  maxHizDiff  maxNoise  maxBlock  hizActive'
    $totalFail = 0
    $stop = $script:Fatal -or ($script:Skipped.Count -gt 0)
    foreach ($r in $script:Results) {
        Write-Host ('{0,-8} {1,-13} {2,6} {3,5} {4,9} {5,7} {6,14} {7,11} {8,9} {9,9}  {10}{11}' -f $r.Scene, $r.Scenario, $r.Frames, $r.EqualFrames,
            $r.DiffFrames, $r.FailFrames, $r.DiffPixels, $r.MaxDiff, $r.MaxNoise, $r.MaxBlock, $r.HizActive, $(if ($r.Error) { '  ERROR: ' + $r.Error } else { '' }))
        $totalFail += $r.FailFrames
        if ($r.Error) { $stop = $true }
        elseif (-not $r.HizActive) {
            Write-Host ("    Hi-Z was not Applied in scenario $($r.Scene)/$($r.Scenario): the result proves nothing.")
            $stop = $true
        }
    }
    foreach ($s in $script:Skipped) { Write-Host ('SKIPPED  ' + $s) }

    if ($totalFail -gt 0) { $exitCode = 1 } elseif ($stop) { $exitCode = 2 }
    if ($exitCode -eq 0) { Write-Host 'PASS  every scenario ran and no frame is above the noise limit.' }
    elseif ($exitCode -eq 1) { Write-Host ("FAIL  $totalFail frame(s) above the noise limit or with a block of $BlockPixels+ pixels. Images of those frames are kept in $OutDir.") }
    else { Write-Host 'STOP  precondition, capture, Hi-Z activity or skipped scenario. The run proves nothing. See messages above.' }
} finally {
    Write-Host 'Cleanup: removing created entities, restoring camera, settings and feature states.'
    for ($i = $script:Created.Count - 1; $i -ge 0; $i--) {
        if (-not (Remove-CullEntity $script:Created[$i])) { Write-Warning ("Could not delete entity '" + $script:Created[$i] + "'.") }
    }
    if ($Scene -ne 'terrain') { Write-Host 'NOTE  the instance grid cannot be bulk-deleted over HTTP. Close the editor without saving.' }
    try { if ($null -ne $savedCam) { Set-CullCamera $CameraEntity $savedCam.Position $savedCam.Rotation } } catch { Write-Warning ('Camera restore failed: ' + $_) }
    try { if ($null -ne $savedCfg) { Set-GpuDrivenCfg $savedCfg | Out-Null } } catch { Write-Warning ('GpuDrivenScene restore failed: ' + $_) }
    try { if ($null -ne $script:Saved) { Restore-FeatureStates $script:Saved } } catch { Write-Warning ('Feature restore failed: ' + $_) }
}
exit $exitCode
