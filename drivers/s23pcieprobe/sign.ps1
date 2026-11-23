param(
    # Driver directory (defaults to the folder of this script, i.e. drivers\s23pcieprobe of the current checkout)
    [string]$DriverDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'

$drvDir = $DriverDir
$pkgDir = Join-Path $drvDir 'pkg'
$stageDir = Join-Path $drvDir 'cat_stage'
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=S23TouchTest2020' -and $_.HasPrivateKey } | Select-Object -First 1
if (-not $cert) {
    throw "Certificate CN=S23TouchTest2020 not found!"
}

$cerPath = Join-Path $pkgDir 's23test.cer'
Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

Copy-Item (Join-Path $drvDir 'out\s23pcieprobe.sys') (Join-Path $stageDir 's23pcieprobe.sys') -Force
$stageSys = Join-Path $stageDir 's23pcieprobe.sys'
$sigSys = Set-AuthenticodeSignature -FilePath $stageSys -Certificate $cert -HashAlgorithm SHA256
Write-Output "Signed s23pcieprobe.sys: Status=$($sigSys.Status)"

Copy-Item $stageSys (Join-Path $pkgDir 's23pcieprobe.sys') -Force
Get-ChildItem $pkgDir | Select-Object Name, Length
