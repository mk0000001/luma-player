function Assert-LumaInstallerModelPin {
 param($Spec)
 if($Spec.file -cnotmatch '^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(bin|gguf|zip)$' -or
    $Spec.sha256 -cnotmatch '^[a-f0-9]{64}$' -or [long]$Spec.bytes -le 0 -or [long]$Spec.bytes -gt 10GB -or
    $Spec.revision -cnotmatch '^[a-f0-9]{40}$' -or $Spec.id -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid pinned model metadata'}
 $uri=[uri]$Spec.url
 $expected='/'+$Spec.id+'/resolve/'+$Spec.revision+'/'+$Spec.file
 if(!$uri.IsAbsoluteUri -or $Spec.url -cne "https://huggingface.co$expected"){throw 'Invalid pinned model URL'}
}

function New-LumaInstallerModelEntry {
 param($Spec,[string]$Destination,[ValidateSet('turbo','accurate','translation')][string]$Component)
 $expectedDestination=if($Component -eq 'translation'){'runtime/translation/models'}else{'runtime/models'}
 $extension=if($Component -eq 'translation'){'.gguf'}else{'.bin'}
 if($Destination -cne $expectedDestination -or !$Spec.file.EndsWith($extension,[StringComparison]::Ordinal)){throw 'Invalid model destination/component'}
 Assert-LumaInstallerModelPin -Spec $Spec
 $dest=$Destination.Replace('/','\')
 'Source: "'+$Spec.url+'"; DestDir: "{app}\'+$dest+'"; DestName: "'+$Spec.file+'"; ExternalSize: '+$Spec.bytes+'; Hash: "'+$Spec.sha256+'"; Components: '+$Component+'; Flags: external download ignoreversion uninsneveruninstall; Check: NeedsDownloadFile('''+$dest+'\'+$Spec.file+''', '''+$Spec.bytes+''', '''+$Spec.sha256+''')'
}

function Get-LumaInstallerBaseFiles {
 param([string]$ProjectRoot,$Lock)
 $spec=$Lock.encoder
 Assert-LumaInstallerModelPin -Spec $spec
 if($spec.archive -cne 'ggml-base-openvino.zip' -or $spec.file -cne 'ggml-base-models.zip' -or $spec.id -cne 'Intel/whisper.cpp-openvino-models'){
  throw 'Invalid Base encoder archive metadata.'
 }
 $archive=Get-Item -LiteralPath (Join-Path $ProjectRoot ('.deps/downloads/'+$spec.archive))
 if($archive.Length -ne $spec.bytes -or (Get-FileHash -LiteralPath $archive.FullName).Hash -ine $spec.sha256){throw 'Base encoder archive does not match its pin.'}
 $expected=@('ggml-base.bin','ggml-base-encoder-openvino.bin','ggml-base-encoder-openvino.xml')
 $zip=[IO.Compression.ZipFile]::OpenRead($archive.FullName)
 try {
  if($zip.Entries.Count -ne $expected.Count){throw 'Unexpected Base encoder archive entries.'}
  foreach($name in $expected){
   $entry=$zip.GetEntry($name)
   if(!$entry -or $entry.Length -le 0 -or $entry.Length -gt 256MB){throw "Invalid Base encoder archive file: $name"}
   $stream=$entry.Open()
   try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}
   finally{$stream.Dispose()}
   if($name -ceq $Lock.model.file -and $hash -cne $Lock.model.sha256){throw 'Base model in the archive differs from the pinned Whisper model.'}
   [pscustomobject]@{Path="runtime/models/$name";Bytes=$entry.Length;Sha256=$hash}
  }
 }finally{$zip.Dispose()}
}

function New-LumaInstallerCudaArchive {
 param([string]$Path,[object[]]$Files)
 $zip=[IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
 try {
  foreach($file in $Files){
   [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.Source,$file.Path,[IO.Compression.CompressionLevel]::Fastest) | Out-Null
  }
  $manifest=$zip.CreateEntry('runtime/cuda/manifest.json')
  $writer=[IO.StreamWriter]::new($manifest.Open(),[Text.UTF8Encoding]::new($false))
  try {
   $writer.Write((@{files=@($Files | Select-Object Path,Bytes,Sha256)} | ConvertTo-Json -Depth 4 -Compress))
  }finally{$writer.Dispose()}
 }finally{$zip.Dispose()}
 Assert-LumaPackageArchiveSize -Bytes (Get-Item -LiteralPath $Path).Length
}

function New-LumaInstallerArchiveEntry {
 param([string]$Url,[string]$Name,[string]$Sha256,[object[]]$Files,[ValidateSet('base','cuda')][string]$Component)
 $size=($Files | Measure-Object Bytes -Sum).Sum
 $dest=if($Component -eq 'base'){'{app}\runtime\models'}else{'{app}'}
 $preserve=if($Component -eq 'base'){' uninsneveruninstall'}else{''}
 'Source: "'+$Url+'"; DestDir: "'+$dest+'"; DestName: "'+$Name+'"; ExternalSize: '+$size+'; Hash: "'+$Sha256+'"; Components: '+$Component+'; Flags: external download extractarchive recursesubdirs createallsubdirs ignoreversion'+$preserve+'; Check: Needs'+$Component
}

function New-LumaInstallerArchiveCheck {
 param([ValidateSet('base','cuda')][string]$Component,[object[]]$Files)
 "function Needs${Component}: Boolean;"
 'begin'
 foreach($file in $Files){
  if($file.Path -cnotmatch '^runtime/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+$' -or $file.Path.Split('/') -contains '..' -or
     $file.Sha256 -cnotmatch '^[a-f0-9]{64}$' -or $file.Bytes -le 0){throw 'Invalid archive file pin.'}
  "  if NeedsDownloadFile('$($file.Path.Replace('/','\'))', '$($file.Bytes)', '$($file.Sha256)') then"
  '  begin Result := True; Exit; end;'
 }
 '  Result := False;'
 'end;'
}

function Assert-LumaInstallerOptionalFilesOmitted {
 param([string]$Directory)
 Assert-LumaPackageCoreCudaOmitted -Directory $Directory
 foreach($folder in 'runtime/models','runtime/translation/models'){
  if(Get-ChildItem -LiteralPath (Join-Path $Directory $folder) -Recurse -File -Force -ErrorAction SilentlyContinue){
   throw 'Windows installer payload contains optional AI model files.'
  }
 }
}

function Assert-LumaInstallerPayload {
 param([string]$Directory,$Manifest)
 $root=Get-Item -LiteralPath $Directory -Force
 if(!$root.PSIsContainer -or ($root.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Installer payload must be a regular directory'}
 $prefix=$root.FullName.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 $files=[Collections.Generic.Dictionary[string,IO.FileInfo]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($file in Assert-LumaPackagePublicFiles -Directory $root.FullName -PassThru){
  $relative=$file.FullName.Substring($prefix.Length).Replace('\','/')
  $files.Add($relative,$file)
 }
 if(!$files.Remove('manifest.json') -or $files.Count -ne @($Manifest.files).Count){throw 'Unexpected payload files'}
 foreach($entry in $Manifest.files){
  $relative=([string]$entry.path).Replace('\','/')
  $file=$null
  if(!$files.TryGetValue($relative,[ref]$file) -or $file.Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ine $entry.sha256){throw 'Payload manifest mismatch'}
  $null=$files.Remove($relative)
 }
}
