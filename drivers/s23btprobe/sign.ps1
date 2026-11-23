param(
    # Driver directory (defaults to the folder of this script, i.e. drivers\s23btprobe of the current checkout)
    [string]$DriverDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'

$drvDir = $DriverDir
$pkgDir = Join-Path $drvDir 'pkg'
$stageDir = Join-Path $drvDir 'cat_stage'
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object {
    $_.Subject -eq 'CN=S23TouchTest2020' -and $_.HasPrivateKey
} | Select-Object -First 1

if (-not $cert) {
    throw "Certificate CN=S23TouchTest2020 not found!"
}

$cerPath = Join-Path $pkgDir 's23test.cer'
Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

$stageSys = Join-Path $stageDir 's23btprobe.sys'
Copy-Item (Join-Path $drvDir 'out\s23btprobe.sys') $stageSys -Force

$sigSys = Set-AuthenticodeSignature -FilePath $stageSys -Certificate $cert -HashAlgorithm SHA256
Write-Output "Signed s23btprobe.sys: Status=$($sigSys.Status) Thumbprint=$($sigSys.SignerCertificate.Thumbprint)"
if ($sigSys.Status -ne 'Valid' -and $sigSys.Status -ne 'UnknownError') {
    throw "Unexpected Authenticode signing status: $($sigSys.Status)"
}

Copy-Item $stageSys (Join-Path $pkgDir 's23btprobe.sys') -Force
Remove-Item -Recurse -Force $stageDir
Get-ChildItem $pkgDir | Select-Object Name, Length
