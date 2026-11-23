param(
    # Driver directory (defaults to the folder of this script, i.e. drivers\s23aclfix of the current checkout)
    [string]$DriverDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'

$drvDir = $DriverDir
$pkgDir = Join-Path $drvDir 'pkg'
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=S23TouchTest2020' -and $_.HasPrivateKey } | Select-Object -First 1
if (-not $cert) {
    throw "Certificate CN=S23TouchTest2020 not found!"
}

$dstSys = Join-Path $pkgDir 's23aclfix.sys'
Copy-Item (Join-Path $drvDir 'out\s23aclfix.sys') $dstSys -Force
$sigSys = Set-AuthenticodeSignature -FilePath $dstSys -Certificate $cert -HashAlgorithm SHA256
Write-Output "Signed s23aclfix.sys: Status=$($sigSys.Status)"
if ($sigSys.Status -ne 'Valid' -and $sigSys.Status -ne 'UnknownError') {
    throw "Unexpected signing status: $($sigSys.Status)"
}
Get-Item $dstSys | Select-Object Name, Length
