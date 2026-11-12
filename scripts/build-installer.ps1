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
if((Get-FileHash -LiteralPath ($payloadRoot+'/manifest.json') -Algorithm SHA256).Hash -ine $ManifestSha256){throw 'Immutable payload manifest hash mismatch'}
$manifest=Get-Content -LiteralPath ($payloadRoot+'/manifest.json') -Raw | ConvertFrom-Json
$version=[string]$manifest.version
if($version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$' -or [Diagnostics.FileVersionInfo]::GetVersionInfo($payloadRoot+'/LumaPlayer.exe').ProductVersion -cne $version){throw 'Wrong payload version'}
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
if(@(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse).Count -ne (@($manifest.files).Count+1)){throw 'Unexpected payload files'}
Assert-LumaPackagePublicFiles -Directory $payloadRoot
if(!$manifest.cudaIncluded){Assert-LumaPackageCoreCudaOmitted -Directory $payloadRoot}
foreach($file in $manifest.files){
 $path=[IO.Path]::GetFullPath((Join-Path $payloadRoot $file.path))
 if(!$path.StartsWith($payloadRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $file.sha256){throw 'Payload manifest mismatch'}
}
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
