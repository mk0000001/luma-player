param(
 [Parameter(Mandatory=$true)][string]$Payload,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Compiler,
 [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ManifestSha256='c425ae3470822ee86fce802eae7b6475aa3d64ef212e8e6fdad022b09d94aaca',
 [switch]$SignUpdateManifest,
 [switch]$LegacyUpdateName
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/installer-support.ps1"
. "$PSScriptRoot/package-support.ps1"
$payloadRoot=[IO.Path]::GetFullPath($Payload)
$outRoot=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outRoot){throw 'Use a new installer output directory; existing outputs are preserved'}
if($outRoot.StartsWith($payloadRoot.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
 throw 'Installer output must be outside the embedded payload.'
}
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
if((Get-Content -LiteralPath ($projectRoot+'/VERSION') -Raw).Trim() -cne $version){throw 'Payload version differs from VERSION'}
$expectedCommit=(Get-LumaPackageSource -ProjectRoot $projectRoot).Commit
if($manifest.sourceCommit -cne $expectedCommit -or $manifest.sourceDirty -or
   $manifest.cudaIncluded -ne $false -or $manifest.translationModelIncluded -ne $false -or
   $manifest.packageFlavor -cne 'windows-x64'){throw 'Wrong immutable installer payload source'}
$flavor=if($LegacyUpdateName){'windows-x64-cuda'}else{'windows-x64'}
Assert-LumaInstallerPayload -Directory $payloadRoot -Manifest $manifest
if([Diagnostics.FileVersionInfo]::GetVersionInfo($payloadRoot+'/LumaPlayer.exe').ProductVersion -cne $version){throw 'Wrong payload version'}
Assert-LumaInstallerOptionalFilesOmitted -Directory $payloadRoot
if(!(Test-Path -LiteralPath "$payloadRoot/setup/luma-setup-hardware.exe" -PathType Leaf)){
 throw 'Use -ForInstaller when staging the setup payload.'
}
$a=Get-Content "$PSScriptRoot/../dependencies.lock.json" -Raw | ConvertFrom-Json
$t=Get-Content "$PSScriptRoot/../translation-dependencies.lock.json" -Raw | ConvertFrom-Json
$standardModels=@(Get-LumaPackageAsrFiles -ProjectRoot $projectRoot)
$baseFiles=@($standardModels | Where-Object Name -CNE $a.highQualityModel.file)
$turboFile=$standardModels | Where-Object Name -CEQ $a.highQualityModel.file
$accurateFile=Get-LumaInstallerLockedModelFile -SourceDirectory (Join-Path $projectRoot '.deps/models') -Spec $a.accurateModel
$translationFile=Get-LumaInstallerLockedModelFile -SourceDirectory (Join-Path $projectRoot '.deps/translation/models') -Spec $t.qualityModel
$standardEntries=@(
 foreach($file in $baseFiles) { New-LumaInstallerModelEntry $file 'base' }
 New-LumaInstallerModelEntry $turboFile 'turbo'
)
$fullEntries=@(New-LumaInstallerModelEntry $accurateFile 'accurate'; New-LumaInstallerModelEntry $translationFile 'translation')
$cudaFiles=@(Get-LumaPackageCudaFiles -ProjectRoot $projectRoot)
$downloadChecks=@(New-LumaInstallerCudaCheck $cudaFiles)
New-Item -ItemType Directory -Path $outRoot | Out-Null
$cudaName="LumaPlayer-$version-windows-x64-cuda-runtime.zip"
$cudaArchive=Join-Path $outRoot $cudaName
New-LumaInstallerCudaArchive -Path $cudaArchive -Files $cudaFiles
$cudaHash=(Get-FileHash -LiteralPath $cudaArchive).Hash.ToLowerInvariant()
$cudaDownloadBytes=(Get-Item -LiteralPath $cudaArchive).Length
$cudaDownloadMB=[int][math]::Ceiling($cudaDownloadBytes/1e6)
$cudaExpandedBytes=[long]($cudaFiles | Measure-Object Bytes -Sum).Sum
$baseExpandedBytes=[long]($baseFiles | Measure-Object Bytes -Sum).Sum
$cudaUrl="https://github.com/mk0000001/luma-player/releases/download/v$version/$cudaName"
$cudaEntry=New-LumaInstallerCudaEntry $cudaName $cudaHash $cudaFiles
Copy-Item "$PSScriptRoot/windows-installer.iss" ($outRoot+'/windows-installer.iss')
[IO.File]::WriteAllText($outRoot+'/downloads.iss',$cudaEntry,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllText($outRoot+'/download-checks.iss',($downloadChecks -join "`r`n"),[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines($outRoot+'/models-standard.iss',[string[]]$standardEntries,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllLines($outRoot+'/models-full.iss',[string[]]$fullEntries,[Text.UTF8Encoding]::new($true))
$compilerArgs=@("/DPayload=$payloadRoot","/DOutput=$outRoot","/DPlayerVersion=$version",
                "/DCudaUrl=$cudaUrl","/DCudaArchiveName=$cudaName","/DCudaSha256=$cudaHash","/DCudaDownloadBytes=$cudaDownloadBytes",
                "/DCudaDownloadMB=$cudaDownloadMB","/DCudaExpandedBytes=$cudaExpandedBytes","/DBaseExpandedBytes=$baseExpandedBytes",
                "/DTurboBytes=$($turboFile.Bytes)","/DAccurateBytes=$($accurateFile.Bytes)","/DTranslationBytes=$($translationFile.Bytes)")
$installerScript=Join-Path $outRoot 'windows-installer.iss'
& $Compiler @compilerArgs "/DInstallerFlavor=$flavor" $installerScript
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$exe=Get-Item ($outRoot+"/LumaPlayer-$version-setup.1-$flavor.exe")
Assert-LumaPackageArchiveSize -Bytes $exe.Length
$hash=(Get-FileHash -LiteralPath $exe.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($exe.FullName+'.sha256',"$hash  $($exe.Name)`n",[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($cudaArchive+'.sha256',"$cudaHash  $cudaName`n",[Text.UTF8Encoding]::new($false))
if($SignUpdateManifest){
 & (Join-Path $PSScriptRoot 'sign-update-manifest.ps1') -Version $version -InstallerPath $exe.FullName -OutputDirectory $outRoot | Out-Null
}
$fullName="LumaPlayer-$version-setup.1-windows-x64-models"
& $Compiler @compilerArgs '/DInstallerFlavor=windows-x64-models' '/DIncludeFullModels=1' $installerScript
if($LASTEXITCODE -ne 0){throw 'Full model installer compilation failed'}
$fullExe=Get-Item -LiteralPath (Join-Path $outRoot ($fullName+'.exe'))
$slices=@(Get-ChildItem -LiteralPath $outRoot -File -Filter ($fullName+'-*.bin') | Sort-Object Name)
if(!$slices.Count){throw 'Missing full model installer slices'}
$fullAssets=@($fullExe) + $slices
$fullChecksums=foreach($asset in $fullAssets){
 Assert-LumaPackageArchiveSize -Bytes $asset.Length
 $assetHash=(Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
 "$assetHash  $($asset.Name)"
}
[IO.File]::WriteAllLines((Join-Path $outRoot ($fullName+'.sha256')),[string[]]$fullChecksums,[Text.UTF8Encoding]::new($false))
[ordered]@{
 playerVersion=$manifest.version;installerVersion='1';bytes=$exe.Length;sha256=$hash
 cudaArchive=$cudaName;cudaBytes=$cudaDownloadBytes
 fullAssets=@($fullAssets | Select-Object Name,Length)
 models=@($standardModels.Name)+@($accurateFile.Name,$translationFile.Name)
} | ConvertTo-Json -Depth 4
