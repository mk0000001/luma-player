function Get-LumaInstallerLockedModelFile {
 param([string]$SourceDirectory,$Spec)
 if($Spec.file -cnotmatch '^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(bin|gguf)$' -or
    $Spec.sha256 -cnotmatch '^[a-f0-9]{64}$' -or [long]$Spec.bytes -le 0){throw 'Invalid pinned model metadata.'}
 $file=Get-Item -LiteralPath (Join-Path $SourceDirectory $Spec.file) -Force
 if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
    $file.Length -ne $Spec.bytes -or (Get-FileHash -LiteralPath $file.FullName).Hash -ine $Spec.sha256){
    throw "Installer model does not match its pin: $($Spec.file)"
 }
 [pscustomobject]@{Name=$Spec.file;Source=$file.FullName;Bytes=$file.Length;Sha256=$Spec.sha256}
}

function New-LumaInstallerModelEntry {
 param($File,[ValidateSet('core','base','turbo','accurate','translation')][string]$Component)
 $destination=if($Component -eq 'translation'){'runtime\translation\models'}else{'runtime\models'}
 $extension=switch($Component){translation{'gguf'} base{'bin|xml'} default{'bin'}}
 if($File.Name -cnotmatch "^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.($extension)$" -or
    [IO.Path]::GetFileName($File.Source) -cne $File.Name -or ![IO.Path]::IsPathFullyQualified($File.Source) -or
    $File.Sha256 -cnotmatch '^[a-f0-9]{64}$' -or [long]$File.Bytes -le 0){throw 'Invalid installer model file.'}
 'Source: "'+$File.Source+'"; DestDir: "{app}\'+$destination+'"; Hash: "'+$File.Sha256+'"; Components: '+$Component+'; Flags: ignoreversion solidbreak uninsneveruninstall; Check: NeedsInstallFile('''+$destination+'\'+$File.Name+''', '''+$File.Bytes+''', '''+$File.Sha256+''')'
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

function New-LumaInstallerCudaEntry {
 param([string]$Name,[string]$Sha256,[object[]]$Files)
 $size=($Files | Measure-Object Bytes -Sum).Sum
 'Source: "{tmp}\'+$Name+'"; DestDir: "{app}"; ExternalSize: '+$size+'; Hash: "'+$Sha256+'"; Components: cuda; Flags: external extractarchive recursesubdirs createallsubdirs ignoreversion; Check: NeedsCuda'
}

function New-LumaInstallerCudaCheck {
 param([object[]]$Files)
 'function NeedsCuda: Boolean;'
 'begin'
 foreach($file in $Files){
  if($file.Path -cnotmatch '^runtime/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+$' -or $file.Path.Split('/') -contains '..' -or
     $file.Sha256 -cnotmatch '^[a-f0-9]{64}$' -or $file.Bytes -le 0){throw 'Invalid archive file pin.'}
  "  if NeedsInstallFile('$($file.Path.Replace('/','\'))', '$($file.Bytes)', '$($file.Sha256)') then"
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
