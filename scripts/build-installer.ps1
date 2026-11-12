param(
 [Parameter(Mandatory=$true)][string]$Payload,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Compiler,
 [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ManifestSha256='c425ae3470822ee86fce802eae7b6475aa3d64ef212e8e6fdad022b09d94aaca',
 [switch]$SignUpdateManifest
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/installer-support.ps1"
. "$PSScriptRoot/package-support.ps1"
$payloadRoot=[IO.Path]::GetFullPath($Payload)
$outRoot=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outRoot){throw 'Use a new installer output directory; existing outputs are preserved'}
$manifestFile=Get-Item -LiteralPath (Join-Path $payloadRoot 'manifest.json') -Force
if($manifestFile.PSIsContainer -or ($manifestFile.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Installer manifest must be a regular file'}
$manifestStream=[IO.File]::Open($manifestFile.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
 if((Get-FileHash -InputStream $manifestStream -Algorithm SHA256).Hash -ine $ManifestSha256){throw 'Immutable payload manifest hash mismatch'}
 $manifestStream.Position=0
 $reader=[IO.StreamReader]::new($manifestStream,[Text.Encoding]::UTF8,$true)
 try {$manifest=$reader.ReadToEnd() | ConvertFrom-Json} finally {$reader.Dispose()}
} finally {$manifestStream.Dispose()}
$version=[string]$manifest.version
if($version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$'){throw 'Wrong payload version'}
$projectRoot=Split-Path -Parent $PSScriptRoot
if($version -ceq '0.1.0-alpha.10'){
 $expectedCommit='3535669c816d1d4af34165f514798e5640fd325e'
 $installerVersion=$version+'-installer.1'
}else{
 if((Get-Content -LiteralPath ($projectRoot+'/VERSION') -Raw).Trim() -cne $version){throw 'Payload version differs from VERSION'}
 $expectedCommit=(Get-LumaPackageSource -ProjectRoot $projectRoot).Commit
 $installerVersion=$version
}
$flavor=if($manifest.cudaIncluded -eq $true){'windows-x64-cuda'}else{'windows-x64'}
if($manifest.sourceCommit -cne $expectedCommit -or $manifest.sourceDirty -or
   ($version -ceq '0.1.0-alpha.10' -and !$manifest.cudaIncluded) -or
   ($version -cne '0.1.0-alpha.10' -and $manifest.packageFlavor -cne $flavor)){throw 'Wrong immutable installer payload source'}
if($SignUpdateManifest -and !$manifest.cudaIncluded){throw 'The existing automatic-update channel requires the CUDA installer; the core installer is for manual installation.'}
Assert-LumaInstallerPayload -Directory $payloadRoot -Manifest $manifest
if([Diagnostics.FileVersionInfo]::GetVersionInfo($payloadRoot+'/LumaPlayer.exe').ProductVersion -cne $version){throw 'Wrong payload version'}
if(!$manifest.cudaIncluded){Assert-LumaPackageCoreCudaOmitted -Directory $payloadRoot}
$a=Get-Content "$PSScriptRoot/../dependencies.lock.json" -Raw | ConvertFrom-Json
$t=Get-Content "$PSScriptRoot/../translation-dependencies.lock.json" -Raw | ConvertFrom-Json
$models=@(New-LumaInstallerModelEntry $a.accurateModel 'runtime/models' 'accurate';New-LumaInstallerModelEntry $t.qualityModel 'runtime/translation/models' 'translation')
New-Item -ItemType Directory -Path $outRoot | Out-Null
Copy-Item "$PSScriptRoot/windows-installer.iss" ($outRoot+'/windows-installer.iss')
[IO.File]::WriteAllText($outRoot+'/models.iss',($models -join "`r`n"),[Text.UTF8Encoding]::new($true))
& $Compiler ('/DPayload='+$payloadRoot) ('/DOutput='+$outRoot) ('/DPlayerVersion='+$version) ('/DInstallerVersion='+$installerVersion) ('/DPayloadFlavor='+$flavor) ($outRoot+'/windows-installer.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$exe=Get-Item ($outRoot+"/LumaPlayer-$version-setup.1-$flavor.exe")
$hash=(Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($exe.FullName+'.sha256',"$hash  $($exe.Name)`n",[Text.UTF8Encoding]::new($false))
if($SignUpdateManifest){
 & (Join-Path $PSScriptRoot 'sign-update-manifest.ps1') -Version $version -InstallerPath $exe.FullName -OutputDirectory $outRoot | Out-Null
}
[ordered]@{playerVersion=$manifest.version;installerVersion='1';bytes=$exe.Length;sha256=$hash;models=@($a.accurateModel.file,$t.qualityModel.file)} | ConvertTo-Json
