# Runs the physics debug scenarios against a running editor and checks the gates of thresholds.json.
# ASCII only. Exit code 0 only when every gate passes.
param(
    [Parameter(Mandatory = $true)][string]$Model,
    [Parameter(Mandatory = $true)][string]$Clip,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [ValidateSet('default', 'legacy')][string]$Preset = 'default',
    [string]$Thresholds = '',
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [string]$Baseline = '',
    [string[]]$GateIds = @()
)

$ErrorActionPreference = 'Stop'
if ($Thresholds -eq '') { $Thresholds = Join-Path $PSScriptRoot 'thresholds.json' }
$GateIds = @($GateIds | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })

$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Inv = [System.Globalization.CultureInfo]::InvariantCulture
$Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('physdbg_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Tmp | Out-Null
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# One HTTP call through curl.exe. The reply body goes to a file so UTF-8 survives.
function Invoke-Api {
    param([string]$Method, [string]$Path, [string]$Body = $null, [string]$OutFile = '', [int]$TimeoutSec = 10)
    $target = $OutFile
    if ($target -eq '') { $target = Join-Path $Tmp 'reply.bin' }
    if (Test-Path $target) { Remove-Item $target -Force }
    $curlArgs = @('-s', '-S', '--max-time', "$TimeoutSec", '-o', $target, '-w', '%{http_code}', '-X', $Method)
    if ($null -ne $Body) {
        $bodyFile = Join-Path $Tmp 'body.json'
        [System.IO.File]::WriteAllText($bodyFile, $Body, $Utf8)
        $curlArgs += @('-H', 'Content-Type: application/json', '--data-binary', "@$bodyFile")
    }
    $curlArgs += ($BaseUrl + $Path)
    $code = & curl.exe @curlArgs
    $status = 0
    [void][int]::TryParse(("$code").Trim(), [ref]$status)
    $text = ''
    if ($OutFile -eq '' -and (Test-Path $target)) { $text = [System.IO.File]::ReadAllText($target, $Utf8) }
    return [pscustomobject]@{ Status = $status; Text = $text }
}

function ConvertTo-Obj([string]$Text) {
    try { return ($Text | ConvertFrom-Json) } catch { return $null }
}

function Has-Prop($Obj, [string]$Name) {
    return ($null -ne $Obj) -and ($null -ne $Obj.PSObject.Properties[$Name])
}

# Walks a dotted path such as collision.bind_pose_pushed_joints. Missing -> $null.
function Get-Path($Obj, [string]$Path) {
    $cur = $Obj
    foreach ($part in $Path.Split('.')) {
        if ($null -eq $cur) { return $null }
        $p = $cur.PSObject.Properties[$part]
        if ($null -eq $p) { return $null }
        $cur = $p.Value
    }
    return $cur
}

function Get-GroupMap($Json) {
    $map = @{}
    if ($null -ne $Json -and (Has-Prop $Json 'groups') -and $null -ne $Json.groups) {
        foreach ($g in @($Json.groups)) { $map[[string]$g.key] = $g }
    }
    return $map
}

function Get-Median([double[]]$Values) {
    if ($Values.Count -eq 0) { return $null }
    $s = @($Values | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return [double]$s[[int](($n - 1) / 2)] }
    return ([double]$s[$n / 2 - 1] + [double]$s[$n / 2]) / 2.0
}

function Get-Number($Value) {
    if ($null -eq $Value) { return $null }
    $d = 0.0
    if ([double]::TryParse(([string]::Format($Inv, '{0}', $Value)), [System.Globalization.NumberStyles]::Float, $Inv, [ref]$d)) { return $d }
    return $null
}

# Metric of one gate in one result: a field path, an aggregate, or a group entry.
function Get-GateValue($Json, $Gate, [string]$Group) {
    if ($null -eq $Json) { return $null }
    if (Has-Prop $Gate 'field') { return Get-Number (Get-Path $Json $Gate.field) }
    $node = $null
    if (Has-Prop $Gate 'aggregate') { $node = Get-Path $Json $Gate.aggregate }
    else { $node = (Get-GroupMap $Json)[$Group] }
    if ($null -eq $node) { return $null }
    $metric = Get-Path $node $Gate.metric
    if ($null -eq $metric) { return $null }
    return Get-Number (Get-Path $metric $Gate.stat)
}

$BaselineCache = @{}
function Get-BaselineJson([string]$Stem) {
    if ($Baseline -eq '') { return $null }
    if ($BaselineCache.ContainsKey($Stem)) { return $BaselineCache[$Stem] }
    $file = Join-Path $Baseline ($Stem + '.json')
    $obj = $null
    if (Test-Path $file) { $obj = ConvertTo-Obj ([System.IO.File]::ReadAllText($file, $Utf8)) }
    $BaselineCache[$Stem] = $obj
    return $obj
}

# Drops physics_ms and every bend_joint_floor_degrees key, recursively.
function Remove-Volatile($Node) {
    if ($null -eq $Node) { return }
    if ($Node -is [System.Array]) { foreach ($e in $Node) { Remove-Volatile $e }; return }
    if ($Node -isnot [System.Management.Automation.PSCustomObject]) { return }
    foreach ($name in @('physics_ms', 'bend_joint_floor_degrees')) {
        if (Has-Prop $Node $name) { $Node.PSObject.Properties.Remove($name) }
    }
    foreach ($p in @($Node.PSObject.Properties)) { Remove-Volatile $p.Value }
}

# True when both replies are equal except physics_ms and the bend floor key.
function Test-JsonIdentical($A, $B) {
    if ($null -eq $A -or $null -eq $B) { return $false }
    Remove-Volatile $A
    Remove-Volatile $B
    return ((ConvertTo-Json $A -Depth 30 -Compress) -ceq (ConvertTo-Json $B -Depth 30 -Compress))
}

function Format-Num($Value) {
    if ($null -eq $Value) { return 'n/a' }
    return ([double]$Value).ToString('G6', $Inv)
}

$Rows = New-Object System.Collections.Generic.List[object]
function Add-Row([string]$Id, [string]$Scenario, [string]$Group, $Measured, [string]$Op, [string]$Limit, [bool]$Pass) {
    $verdict = 'FAIL'
    if ($Pass) { $verdict = 'PASS' }
    $line = '{0,-3} {1,-8} {2,-26} {3,12} {4,-10} {5,14} {6}' -f $Id, $Scenario, $Group, (Format-Num $Measured), $Op, $Limit, $verdict
    Write-Host $line
    $Rows.Add([pscustomobject]@{ Id = $Id; Pass = $Pass })
}

# ---- load gates ----
$gateDoc = ConvertTo-Obj ([System.IO.File]::ReadAllText($Thresholds, $Utf8))
if ($null -eq $gateDoc -or -not (Has-Prop $gateDoc 'gates')) { Write-Host "Bad thresholds file: $Thresholds"; exit 2 }
$gates = @($gateDoc.gates)
if ($GateIds.Count -gt 0) { $gates = @($gates | Where-Object { $GateIds -contains $_.id }) }

# Scenarios and repeat counts the gates need. S1..S4 always run.
$scenarioDefs = @{
    'S1' = @{ Frames = 204; Hz = 60; Warmup = 0; Settings = $null }
    'S2' = @{ Frames = 489; Hz = 144; Warmup = 0; Settings = $null }
    'S3' = @{ Frames = 816; Hz = 240; Warmup = 0; Settings = $null }
    'S4' = @{ Frames = 600; Hz = 60; Warmup = 120; Settings = $null }
    'S5' = @{ Frames = 204; Hz = 60; Warmup = 0; Settings = $null }
    'S6' = @{ Frames = 204; Hz = 60; Warmup = 0; Settings = @{ collision_enabled = $false } }
    'S7' = @{ Frames = 204; Hz = 60; Warmup = 0; Settings = @{ collision_fit_enabled = $false } }
    'S8' = @{ Frames = 68; Hz = 20; Warmup = 0; Settings = $null }
    'S9' = @{ Frames = 204; Hz = 60; Warmup = 0; Settings = @{ bend_joint_floor_degrees = 0 } }
}
$repeats = @{ 'S1' = 1; 'S2' = 1; 'S3' = 1; 'S4' = 1 }
foreach ($g in $gates) {
    if ((Has-Prop $g 'preset') -and $g.preset -ne $Preset) { continue }
    $ids = @()
    if (Has-Prop $g 'scenarios') { $ids += @($g.scenarios) }
    if (Has-Prop $g 'scenario') { $ids += $g.scenario }
    foreach ($id in $ids) {
        if (-not $repeats.ContainsKey($id)) { $repeats[$id] = 1 }
        if (Has-Prop $g 'repeat') { if ([int]$g.repeat -gt $repeats[$id]) { $repeats[$id] = [int]$g.repeat } }
        if ((Has-Prop $g 'check') -and $g.check -eq 'determinism' -and $repeats[$id] -lt 2) { $repeats[$id] = 2 }
    }
}

$results = @{}
$rawFirst = @{}
$rawSecond = @{}
$entity = ''
$exitCode = 1

try {
    # ---- setup ----
    $hello = Invoke-Api -Method 'GET' -Path '/http_hello_world'
    if ($hello.Text -ne 'hello world') { throw 'editor does not answer GET /http_hello_world' }

    $body = ConvertTo-Json @{ gta_path = $Model } -Compress
    $spawn = Invoke-Api -Method 'POST' -Path '/instantiate_asset' -Body $body -TimeoutSec 120
    $spawnObj = ConvertTo-Obj $spawn.Text
    if ($spawn.Status -ne 200 -or $null -eq $spawnObj -or -not (Has-Prop $spawnObj 'name')) { throw ('instantiate_asset failed: ' + $spawn.Text) }
    $entity = [string]$spawnObj.name
    Write-Host "entity: $entity"

    function Set-PresetName([string]$Name) {
        $r = Invoke-Api -Method 'GET' -Path ('/physics_debug/preset?name=' + $Name)
        if ($r.Status -ne 200) { throw ("preset $Name failed: " + $r.Text) }
    }

    function Save-Images([string]$Tag) {
        foreach ($view in @('negz', 'posx')) {
            $png = Join-Path $OutDir ($Tag + '_' + $view + '.png')
            $tmpPng = Join-Path $Tmp ($Tag + '_' + $view + '.png')
            $r = Invoke-Api -Method 'GET' -Path ('/physics_debug/image?entity=' + [uri]::EscapeDataString($entity) + '&view=' + $view) -OutFile $tmpPng -TimeoutSec 20
            if ($r.Status -eq 200) { Move-Item -Force $tmpPng $png }
            else { Write-Host ("warning: image $view for $Tag answered " + $r.Status) }
        }
    }

    function Invoke-Scenario([string]$Id) {
        $def = $scenarioDefs[$Id]
        $presetName = $Preset
        if ($Id -eq 'S5') { $presetName = 'legacy' }
        Set-PresetName $presetName
        $req = [ordered]@{ entity = $entity; frames = $def.Frames; render_hz = $def.Hz; warmup_frames = $def.Warmup }
        if ($null -ne $def.Settings) { $req['settings'] = $def.Settings }
        $reqText = ConvertTo-Json $req -Compress -Depth 6
        $n = $repeats[$Id]
        $medians = @()
        $p95s = @()
        for ($i = 1; $i -le $n; ++$i) {
            $r = Invoke-Api -Method 'POST' -Path '/physics_debug/run' -Body $reqText -TimeoutSec 90
            $suffix = ''
            if ($i -gt 1) { $suffix = "_run$i" }
            [System.IO.File]::WriteAllText((Join-Path $OutDir ($Id + '_' + $presetName + $suffix + '.json')), $r.Text, $Utf8)
            $obj = ConvertTo-Obj $r.Text
            if ($r.Status -ne 200 -or $null -eq $obj) { Write-Host ("scenario $Id run $i failed: status " + $r.Status + ' ' + $r.Text); return }
            if ($i -eq 1) { $results[$Id] = $obj; $rawFirst[$Id] = $r.Text }
            if ($i -eq 2) { $rawSecond[$Id] = $r.Text }
            $m = Get-Number (Get-Path $obj 'physics_ms.median')
            if ($null -ne $m) { $medians += $m }
        }
        # Median of the run medians, kept in memory only.
        if ($n -gt 1 -and $medians.Count -eq $n -and (Has-Prop $results[$Id] 'physics_ms') -and $null -ne $results[$Id].physics_ms) {
            $results[$Id].physics_ms.median = Get-Median ([double[]]$medians)
        }
        Save-Images ($Id + '_' + $presetName)
    }

    # S4 needs a bind pose: run it before a clip is played.
    Set-PresetName $Preset
    $state = Invoke-Api -Method 'GET' -Path ('/physics_debug/state?entity=' + [uri]::EscapeDataString($entity))
    [System.IO.File]::WriteAllText((Join-Path $OutDir ('state_' + $Preset + '.json')), $state.Text, $Utf8)
    if ($repeats.ContainsKey('S4')) { Invoke-Scenario 'S4' }

    $playBody = ConvertTo-Json @{ entity = $entity; animation_gta_path = $Clip; frame = 0; speed = 1; loop = $true } -Compress
    $play = Invoke-Api -Method 'POST' -Path '/physics_debug/play' -Body $playBody
    if ($play.Status -ne 200) { throw ('physics_debug/play failed: ' + $play.Text) }

    foreach ($id in @('S1', 'S2', 'S3', 'S5', 'S6', 'S7', 'S8', 'S9')) {
        if ($repeats.ContainsKey($id)) { Invoke-Scenario $id }
    }

    # ---- gates ----
    Write-Host ''
    Write-Host ('{0,-3} {1,-8} {2,-26} {3,12} {4,-10} {5,14} {6}' -f 'id', 'scenario', 'group', 'measured', 'op', 'limit', 'result')
    foreach ($gate in $gates) {
        if ((Has-Prop $gate 'preset') -and $gate.preset -ne $Preset) { continue }
        if (Has-Prop $gate 'check') {
            if ($gate.check -eq 'determinism') {
                $id = [string]$gate.scenario
                $same = $false
                if ($rawFirst.ContainsKey($id) -and $rawSecond.ContainsKey($id)) {
                    $same = Test-JsonIdentical (ConvertTo-Obj $rawFirst[$id]) (ConvertTo-Obj $rawSecond[$id])
                }
                Add-Row $gate.id $id '-' $null 'identical' 'run1==run2' $same
            } elseif ($gate.check -eq 'identity') {
                $id = [string]$gate.scenario
                $same = $false
                if ($rawFirst.ContainsKey($id) -and $Baseline -ne '') {
                    $file = Join-Path $Baseline ([string]$gate.baseline + '.json')
                    if (Test-Path $file) {
                        $same = Test-JsonIdentical (ConvertTo-Obj $rawFirst[$id]) (ConvertTo-Obj ([System.IO.File]::ReadAllText($file, $Utf8)))
                    }
                }
                Add-Row $gate.id $id '-' $null 'identical' ([string]$gate.baseline) $same
            }
            continue
        }

        $op = [string]$gate.op
        $scenarioIds = @()
        if (Has-Prop $gate 'scenarios') { $scenarioIds = @($gate.scenarios) } else { $scenarioIds = @([string]$gate.scenario) }
        $scenarioLabel = ($scenarioIds -join '/')

        $baseJson = $null
        if (Has-Prop $gate 'baseline') { $baseJson = Get-BaselineJson ([string]$gate.baseline) }

        # Group list. "*" means every group of the baseline file (or of the result).
        $groupList = @('')
        if (Has-Prop $gate 'groups') {
            $groupList = @()
            foreach ($gr in @($gate.groups)) {
                if ($gr -eq '*') {
                    $src = $baseJson
                    if ($null -eq $src) { $src = $results[$scenarioIds[0]] }
                    foreach ($k in (Get-GroupMap $src).Keys) { $groupList += [string]$k }
                } else { $groupList += [string]$gr }
            }
            $groupList = @($groupList | Sort-Object)
        } elseif (Has-Prop $gate 'aggregate') { $groupList = @([string]$gate.aggregate) }
        elseif (Has-Prop $gate 'field') { $groupList = @([string]$gate.field) }
        if ((Has-Prop $gate 'except') -and (Has-Prop $gate 'groups')) {
            $except = @($gate.except | ForEach-Object { [string]$_ })
            $groupList = @($groupList | Where-Object { $except -cnotcontains $_ })
        }

        foreach ($group in $groupList) {
            $values = @()
            foreach ($sid in $scenarioIds) { $values += , (Get-GateValue $results[$sid] $gate $group) }
            $measured = $null
            $limit = $null
            $limitText = Format-Num ([double]$gate.value)
            $pass = $false
            $ok = $true
            foreach ($v in $values) { if ($null -eq $v) { $ok = $false } }

            if ($ok) {
                if ($op -eq 'ratio<=') {
                    if ($values[1] -ne 0) {
                        $measured = $values[0] / $values[1]
                        $limit = [double]$gate.value
                        $pass = ($measured -le $limit)
                    }
                } elseif ($op -eq 'absdiff<=') {
                    $measured = [math]::Abs($values[0] - $values[1])
                    $limit = [double]$gate.value
                    $pass = ($measured -le $limit)
                } elseif ($op -eq 'within_rel') {
                    $measured = $values[0]
                    $ref = Get-GateValue $baseJson $gate $group
                    if ($null -ne $ref) {
                        $tolerance = [double]$gate.value * [math]::Abs($ref) + 1e-9
                        $pass = ([math]::Abs($measured - $ref) -le $tolerance)
                        $limitText = 'ref ' + (Format-Num $ref)
                    }
                } else {
                    $measured = $values[0]
                    $limit = [double]$gate.value
                    if (Has-Prop $gate 'baseline') {
                        $ref = Get-GateValue $baseJson $gate $group
                        if ($null -eq $ref) { $limit = $null } else { $limit = [double]$gate.value * $ref }
                    }
                    if ($null -ne $limit -and (Has-Prop $gate 'floor')) { $limit = [math]::Max($limit, [double]$gate.floor) }
                    if ($null -ne $limit) {
                        $limitText = Format-Num $limit
                        if ($op -eq '<=') { $pass = ($measured -le $limit) }
                        elseif ($op -eq '>=') { $pass = ($measured -ge $limit) }
                        elseif ($op -eq '==') { $pass = ([math]::Abs($measured - $limit) -lt 1e-9) }
                    }
                }
            }
            Add-Row $gate.id $scenarioLabel $group $measured $op $limitText $pass
        }
    }

    $failed = @($Rows | Where-Object { -not $_.Pass }).Count
    Write-Host ''
    Write-Host ("gates: {0} checked, {1} failed" -f $Rows.Count, $failed)
    if ($Rows.Count -gt 0 -and $failed -eq 0) { $exitCode = 0 }
}
catch {
    Write-Host ('error: ' + $_.Exception.Message)
    $exitCode = 2
}
finally {
    try { Invoke-Api -Method 'GET' -Path '/physics_debug/preset?name=default' | Out-Null } catch { }
    if ($entity -ne '') {
        try {
            $delBody = ConvertTo-Json @{ name = $entity } -Compress
            Invoke-Api -Method 'POST' -Path '/delete_entity' -Body $delBody | Out-Null
        } catch { }
    }
    if (Test-Path $Tmp) { Remove-Item -Recurse -Force $Tmp }
}
exit $exitCode
