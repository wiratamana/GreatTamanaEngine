<#
  Image-equality soak test. Hi-Z must never remove a visible cluster.
  Geometry-only config, ClusterId debug view (one colour per cluster). For each sequence of camera poses and
  mover positions the sequence is replayed twice: Hi-Z mode (history runs frame after frame) and NoCull mode.
  The game view images must be byte-equal; the number of differing pixels is always printed.
  Exit code: 0 = all frames equal, 1 = some pixel differs, 2 = precondition failed, Hi-Z not active or a capture failed.
  Start the editor with the Game tab visible and the Frame Debugger closed. The scene must not be saved afterwards.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [int]$Seed = 1234,
    [string]$OutDir = (Join-Path $env:TEMP 'culling_soak'),
    [string]$Terrain = (Join-Path $PSScriptRoot '..\..\build\Project\terrain_clustered.gta'),
    [switch]$SkipLoad,
    [string[]]$MoverEntities = @(),
    [ValidateSet('terrain', 'grid', 'both')][string]$Scene = 'terrain',
    [string]$CameraEntity = 'Camera',
    [int]$RestPoses = 12,
    [int]$PanFrames = 60,
    [int]$TurnPairs = 6,
    [int]$MoverFrames = 60,
    [int]$FrameWait = 2,
    [int]$GridCount = 2000,
    [double]$GridSpacing = 2.0,
    [switch]$KeepAllImages
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-CullLib -BaseUrl $BaseUrl
Add-Type -AssemblyName System.Drawing

$script:Rng = New-Object System.Random($Seed)
$script:Results = New-Object System.Collections.Generic.List[object]
$script:Created = New-Object System.Collections.Generic.List[string]
$script:Saved = $null
$script:Fatal = $false

function Get-Rnd {
    param([double[]]$Range)
    return $Range[0] + ($Range[1] - $Range[0]) * $script:Rng.NextDouble()
}

# Scene definitions. Rest ranges are camera position (X/Y/Z), pitch and yaw in degrees.
function Get-SceneDef {
    param([string]$Name)
    if ($Name -eq 'terrain') {
        return [pscustomobject]@{
            Name = 'terrain'; Base = @(0, 900, 0); BasePitch = -10.0
            X = @(-600, 600); Y = @(820, 1300); Z = @(-600, 600); Pitch = @(-50, 10); Yaw = @(0, 360)
            WallPos = @(0, 900, 40); WallScale = @(30, 30, 0.2); MoverAmp = 40.0; MoverHome = @(0, 0, 0)
        }
    }
    return [pscustomobject]@{
        Name = 'grid'; Base = @(0, 0, -60); BasePitch = 0.0
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

# Replays $Frames in one mode and saves one PNG per frame. Returns $null or an error text.
function Save-SoakPhase {
    param([string]$Dir, [string]$Prefix, [string]$Mode, $Frames, [int]$Wait)
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    Set-GpuDrivenCfg @{ mode = $Mode } | Out-Null
    Set-SoakFrame $Frames[0]
    Wait-CullFrames 6
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        Set-SoakFrame $Frames[$i]
        Wait-CullFrames $Wait
        $err = Save-GameView (Join-Path $Dir ('{0}_{1:D3}.png' -f $Prefix, $i))
        if ($err) { return ("frame $i ($Mode): " + $err) }
    }
    return $null
}

# Pixel comparison of two PNGs. Equal hashes mean equal files; otherwise the decoded pixels are counted.
function Compare-Png {
    param([string]$A, [string]$B)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $ha = [BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($A)))
        $hb = [BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($B)))
    } finally { $sha.Dispose() }
    if ($ha -eq $hb) { return [pscustomobject]@{ Differing = 0; Total = 0; SizeMismatch = $false } }
    $bmpA = $null; $bmpB = $null
    try {
        $bmpA = New-Object System.Drawing.Bitmap($A)
        $bmpB = New-Object System.Drawing.Bitmap($B)
        if ($bmpA.Width -ne $bmpB.Width -or $bmpA.Height -ne $bmpB.Height) {
            return [pscustomobject]@{ Differing = [int]($bmpA.Width * $bmpA.Height); Total = [int]($bmpA.Width * $bmpA.Height); SizeMismatch = $true }
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
        $diff = 0
        for ($i = 0; $i -lt $n; $i += 4) {
            if ($ba[$i] -ne $bb[$i] -or $ba[$i + 1] -ne $bb[$i + 1] -or $ba[$i + 2] -ne $bb[$i + 2] -or $ba[$i + 3] -ne $bb[$i + 3]) { $diff++ }
        }
        return [pscustomobject]@{ Differing = $diff; Total = $w * $h; SizeMismatch = $false }
    } finally {
        if ($null -ne $bmpA) { $bmpA.Dispose() }
        if ($null -ne $bmpB) { $bmpB.Dispose() }
    }
}

# Runs one scenario in both modes and compares frame by frame.
function Invoke-SoakScenario {
    param([string]$Scene, [string]$Name, $Frames)
    $dir = Join-Path (Join-Path $OutDir $Scene) $Name
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    Write-Host ("  scenario $Scene/$Name : " + $Frames.Count + ' frames')
    $row = [pscustomobject]@{ Scene = $Scene; Scenario = $Name; Frames = $Frames.Count; Compared = 0; EqualFrames = 0
        DiffFrames = 0; DiffPixels = 0; MaxDiff = 0; HizActive = $false; Error = '' }
    $err = Save-SoakPhase $dir 'hiz' 'HiZ' $Frames $FrameWait
    if (-not $err) {
        $st = Get-GpuDrivenState
        $row.HizActive = ([string](Get-Field $st 'game.occlusion' '') -eq 'Applied')
        $err = Save-SoakPhase $dir 'ref' 'NoCull' $Frames $FrameWait
    }
    if ($err) { $row.Error = $err; $script:Results.Add($row); return }
    for ($i = 0; $i -lt $Frames.Count; $i++) {
        $a = Join-Path $dir ('ref_{0:D3}.png' -f $i)
        $b = Join-Path $dir ('hiz_{0:D3}.png' -f $i)
        $c = Compare-Png $a $b
        $row.Compared++
        if ($c.Differing -eq 0) {
            $row.EqualFrames++
            if (-not $KeepAllImages) { Remove-Item -LiteralPath $a, $b -Force }
        } else {
            $row.DiffFrames++
            $row.DiffPixels += $c.Differing
            if ($c.Differing -gt $row.MaxDiff) { $row.MaxDiff = $c.Differing }
            Write-Host ("    frame $i : $($c.Differing) of $($c.Total) pixels differ" + $(if ($c.SizeMismatch) { ' (image size mismatch)' } else { '' }))
        }
    }
    $script:Results.Add($row)
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
        # Grid entities are named 'InstanceGrid <batch> <index>'; the first batch is assumed.
        foreach ($i in 0..2) {
            $n = "InstanceGrid 0 $i"
            try { Set-CullTrs -Name $n -Position $def.MoverHome; $moverNames += $n } catch { }
        }
        if ($moverNames.Count -eq 0) { Write-Warning 'No grid mover entity found; the mover scenario is skipped for the grid.' }
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
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    Show-SplitViews
    Start-Sleep -Seconds 2
    $script:Saved = Get-FeatureStates
    $savedCfg = Save-GpuDrivenCfg
    $savedCam = Get-CullCameraPose $CameraEntity

    # Deterministic config: geometry only, no temporal features, one colour per cluster.
    foreach ($name in $script:Saved.Keys) {
        if ($name -match '^(Shadow|VolumetricLight|Atmosphere|Ssgi|Gtao|ToneMapping)') { Set-FeatureTriple $name $false $false $false }
    }
    Set-GpuDrivenCfg @{ debugView = 'ClusterId'; collectStats = 'false'; cone = 'false'; enabled = 'true' } | Out-Null

    Write-Host ("Seed $Seed, output folder $OutDir")
    Write-Host 'NOTE  MeshHasCoincidentTriangles is not exposed over HTTP. Coincident triangles can swap winners between draw orders; check the mesh offline if a pixel differs.'
    $scenes = if ($Scene -eq 'both') { @('terrain', 'grid') } else { @($Scene) }
    foreach ($s in $scenes) {
        Invoke-SoakScene $s
        if ($script:Fatal) { break }
    }

    Write-Host ''
    Write-Host 'scene    scenario  frames equal differing  diffPixels(total)  maxPixels/frame  hizActive'
    $totalDiff = 0
    foreach ($r in $script:Results) {
        Write-Host ('{0,-8} {1,-9} {2,6} {3,5} {4,9} {5,18} {6,16}  {7}{8}' -f $r.Scene, $r.Scenario, $r.Frames, $r.EqualFrames,
            $r.DiffFrames, $r.DiffPixels, $r.MaxDiff, $r.HizActive, $(if ($r.Error) { '  ERROR: ' + $r.Error } else { '' }))
        $totalDiff += $r.DiffPixels
        if ($r.Error) { $exitCode = 2 }
        elseif (-not $r.HizActive) {
            Write-Host ("    Hi-Z was not Applied in scenario $($r.Scene)/$($r.Scenario): the result proves nothing.")
            $exitCode = 2
        }
    }
    if ($script:Fatal) { $exitCode = 2 }
    if ($exitCode -eq 0 -and $totalDiff -gt 0) { $exitCode = 1 }
    if ($exitCode -eq 0) { Write-Host 'PASS  0 differing pixels in every frame.' }
    elseif ($exitCode -eq 1) { Write-Host ("FAIL  $totalDiff differing pixels in total. A differing pixel is a wrongly removed cluster (or a draw-order tie). Images are kept in $OutDir.") }
    else { Write-Host 'STOP  precondition, capture or Hi-Z activity problem. See messages above.' }
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
