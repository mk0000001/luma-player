param([Parameter(Mandatory=$true)][string]$Payload,[Parameter(Mandatory=$true)][string]$Output,[Parameter(Mandatory=$true)][string]$Compiler)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/installer-support.ps1"
. "$PSScriptRoot/package-support.ps1"
$payloadRoot=[IO.Path]::GetFullPath($Payload)
$outRoot=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outRoot){throw 'Use a new installer output directory; existing outputs are preserved'}
if((Get-FileHash -LiteralPath ($payloadRoot+'/manifest.json') -Algorithm SHA256).Hash -ine 'c425ae3470822ee86fce802eae7b6475aa3d64ef212e8e6fdad022b09d94aaca'){throw 'Immutable payload manifest hash mismatch'}
$manifest=Get-Content -LiteralPath ($payloadRoot+'/manifest.json') -Raw | ConvertFrom-Json
if($manifest.version -cne '0.1.0-alpha.10' -or [Diagnostics.FileVersionInfo]::GetVersionInfo($payloadRoot+'/LumaPlayer.exe').ProductVersion -cne $manifest.version){throw 'Wrong payload version'}
if($manifest.sourceCommit -cne '3535669c816d1d4af34165f514798e5640fd325e' -or $manifest.sourceDirty -or @($manifest.files).Count -ne 756){throw 'Wrong immutable payload source'}
if(@(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse).Count -ne (@($manifest.files).Count+1)){throw 'Unexpected payload files'}
Assert-LumaPackagePublicFiles -Directory $payloadRoot
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
& $Compiler ('/DPayload='+$payloadRoot) ('/DOutput='+$outRoot) ($outRoot+'/windows-installer.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$exe=Get-Item ($outRoot+'/LumaPlayer-0.1.0-alpha.10-setup.1-windows-x64-cuda.exe')
$hash=(Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($exe.FullName+'.sha256',"$hash  $($exe.Name)`n",[Text.UTF8Encoding]::new($false))
[ordered]@{playerVersion=$manifest.version;installerVersion='1';bytes=$exe.Length;sha256=$hash;models=@($a.accurateModel.file,$t.qualityModel.file)} | ConvertTo-Json
