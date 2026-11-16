$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/installer-support.ps1"
. "$PSScriptRoot/../scripts/package-support.ps1"
. "$PSScriptRoot/../scripts/bootstrap-support.ps1"
$root=Split-Path -Parent $PSScriptRoot
$count=0
$fixture=Join-Path $root ('.deps/installer-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
foreach($entry in @(@{name='ggml-hearttranscriptor-oss-q5_0.bin';component='core';dest='runtime\models';bytes=539212484L},
                   @{name='encoder.xml';component='base';dest='runtime\models';bytes=20L},
                   @{name='turbo.bin';component='turbo';dest='runtime\models';bytes=50L},
                   @{name='accurate.bin';component='accurate';dest='runtime\models';bytes=100L},
                   @{name='quality.gguf';component='translation';dest='runtime\translation\models';bytes=4624648896L})){
 $file=[pscustomobject]@{Name=$entry.name;Source=(Join-Path $fixture $entry.name);Bytes=$entry.bytes;Sha256=('1'*64)}
 $line=New-LumaInstallerModelEntry -File $file -Component $entry.component
 if(!$line.StartsWith('Source: "'+$file.Source+'"; DestDir: "{app}\'+$entry.dest+'";') -or
    $line -notlike ('*Hash: "'+$file.Sha256+'"*') -or $line -notlike '*solidbreak uninsneveruninstall*' -or
    $line -match '\b(download|external)\b'){
    throw 'An installer model must be embedded, separately compressed, and preserved on uninstall.'
 }
 if($line -notlike ("*', '"+$file.Bytes+"', '*")){throw 'Inno Check must pass the size as text, including weights larger than 2 GiB.'}
 if($line -notlike "*Components: $($entry.component);*"){throw 'The model was assigned to the wrong setup component.'}
 $count++
 $file.Name='../escape.bin'
 $rejected=$false
 try{New-LumaInstallerModelEntry -File $file -Component $entry.component | Out-Null}catch{$rejected=$true}
 if(!$rejected){throw 'Accepted a model path escape.'};$count++
}

$modelPath=Join-Path $fixture 'pinned.gguf'
[IO.File]::WriteAllText($modelPath,'test weight')
$spec=@{file='pinned.gguf';bytes=(Get-Item -LiteralPath $modelPath).Length;sha256=(Get-FileHash -LiteralPath $modelPath).Hash.ToLowerInvariant()}
$file=Get-LumaInstallerLockedModelFile -SourceDirectory $fixture -Spec $spec
if($file.Source -cne $modelPath -or $file.Sha256 -cne $spec.sha256){throw 'Verified model metadata differs from its pin.'}
$count++
[IO.File]::WriteAllText($modelPath,'test wrong!')
$rejected=$false
try{Get-LumaInstallerLockedModelFile -SourceDirectory $fixture -Spec $spec | Out-Null}catch{$rejected=$true}
if(!$rejected){throw 'Changed model bytes entered the installer.'};$count++
[IO.File]::Delete($modelPath)

$cudaName='LumaPlayer-0.0.0-windows-x64-cuda-runtime.zip'
$cudaHash='a'*64
$cuda=New-LumaInstallerCudaEntry -Name $cudaName -Sha256 $cudaHash -Files @(@{Bytes=1})
if(!$cuda.StartsWith('Source: "{tmp}\'+$cudaName+'";') -or $cuda -notlike ('*Hash: "'+$cudaHash+'"*') -or
    $cuda -notlike '*external extractarchive*' -or $cuda -match '\bdownload\b'){
    throw 'CUDA must extract only from the verified installer staging archive.'
}
$count++

$archiveFixture=Join-Path $fixture 'download-only-payload'
New-Item -ItemType Directory -Path (Join-Path $archiveFixture 'runtime/models') -Force | Out-Null
$weight=Join-Path $archiveFixture 'runtime/models/ggml-test.bin'
[IO.File]::WriteAllText($weight,'test only')
$rejected=$false
try{Assert-LumaInstallerOptionalFilesOmitted -Directory $archiveFixture}catch{$rejected=$_.Exception.Message -like '*model*'}
if(!$rejected){throw 'The installer accepted embedded AI weights.'}
[IO.File]::Delete($weight)
Assert-LumaInstallerOptionalFilesOmitted -Directory $archiveFixture
$count+=2

[IO.File]::WriteAllText((Join-Path $fixture 'manifest.json'),'{}')
$caught=$false
try{& "$root/scripts/build-installer.ps1" -Payload $fixture -Output ($fixture+'-new-output') -Compiler 'never-executed'}catch{$caught=$_.Exception.Message -eq 'Immutable payload manifest hash mismatch'}
if(!$caught -or (Test-Path ($fixture+'-new-output'))){throw 'mutated manifest was not rejected before output creation'}
$count++
$caught=$false
try{& "$root/scripts/build-installer.ps1" -Payload $fixture -Output $fixture -Compiler 'never-executed'}catch{$caught=$_.Exception.Message -like 'Use a new installer output*'}
if(!$caught -or [IO.File]::ReadAllText((Join-Path $fixture 'manifest.json')) -cne '{}'){throw 'existing output not preserved'}
$count++

$first=Join-Path $fixture 'first.txt'
$second=Join-Path $fixture 'second.txt'
[IO.File]::WriteAllText($first,'first')
[IO.File]::WriteAllText($second,'second')
$firstEntry=@{path='first.txt';bytes=5;sha256=(Get-FileHash -LiteralPath $first).Hash}
$secondEntry=@{path='second.txt';bytes=6;sha256=(Get-FileHash -LiteralPath $second).Hash}
$complete=@{files=@($firstEntry,$secondEntry)}
Assert-LumaInstallerPayload -Directory $fixture -Manifest $complete
$count++

$rejected=$false
try{Assert-LumaInstallerPayload -Directory $fixture -Manifest @{files=@($firstEntry,$firstEntry)}}catch{$rejected=$true}
if(!$rejected){throw 'duplicate entries must not stand in for missing manifest coverage'}
$count++

$hidden=Join-Path $fixture 'hidden.txt'
[IO.File]::WriteAllText($hidden,'hidden')
[IO.File]::SetAttributes($hidden,[IO.FileAttributes]::Hidden)
$rejected=$false
try{Assert-LumaInstallerPayload -Directory $fixture -Manifest $complete}catch{$rejected=$true}
if(!$rejected){throw 'hidden payload files must also be listed in the manifest'}
$count++

$target=Join-Path $fixture 'bootstrap-target'
New-Item -ItemType Directory -Path $target | Out-Null
[IO.File]::WriteAllText((Join-Path $target 'marker.txt'),'old cached extraction')
[IO.File]::WriteAllText((Join-Path $target 'old-only.txt'),'no longer shipped')
$pin1='1'*64
$pin2='2'*64
Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin1 -Extract {
 param($stage)
 [IO.File]::WriteAllText((Join-Path $stage 'marker.txt'),'first archive')
}
if([IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'first archive' -or (Test-Path -LiteralPath (Join-Path $target 'old-only.txt'))){throw 'obsolete extraction survived a verified bootstrap refresh'}
$count++

Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin1 -Extract { throw 'matching cache was re-extracted' }
$count++

$rejected=$false
try{
 Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin2 -Extract { throw 'fixture extraction failed' }
}catch{$rejected=$_.Exception.Message -eq 'fixture extraction failed'}
if(!$rejected -or [IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'first archive'){throw 'failed extraction did not preserve the previous cache'}
$count++

Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin2 -ContentDirectory 'archive-root' -Extract {
 param($stage)
 $nested=New-Item -ItemType Directory -Path (Join-Path $stage 'archive-root')
 [IO.File]::WriteAllText((Join-Path $nested.FullName 'marker.txt'),'second archive')
}
if([IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'second archive'){throw 'a changed archive pin did not refresh the extracted cache'}
$count++
Write-Output "PASS $count installer integrity cases"
