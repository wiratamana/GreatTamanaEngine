<#
  Preconditions of every culling timing or equality run.
  Exit code 0 = all pass, 1 = a precondition failed, 2 = the editor is not reachable.
  Read-only: sends GET requests only.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://127.0.0.1:8080'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib.ps1')
Initialize-CullLib -BaseUrl $BaseUrl

try {
    $null = Invoke-CullGet '/profiler'
} catch {
    Write-Host ('Editor not reachable at ' + $BaseUrl + ': ' + $_)
    exit 2
}

$result = Test-CullPreconditions
foreach ($w in $result.Warnings) { Write-Host ('WARN  ' + $w) }
foreach ($f in $result.Failures) { Write-Host ('FAIL  ' + $f) }
if ($result.Ok) {
    Write-Host 'PASS  all preconditions hold.'
    exit 0
}
Write-Host ('STOP  ' + $result.Failures.Count + ' precondition(s) failed. Fix them before measuring.')
exit 1
