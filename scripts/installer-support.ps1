function New-LumaInstallerModelEntry {
 param($Spec,[string]$Destination,[string]$Component)
 if($Destination -notin @('runtime/models','runtime/translation/models') -or $Component -notin @('accurate','translation')){throw 'Invalid model destination/component'}
 if($Spec.file -notmatch '^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(bin|gguf)$' -or $Spec.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$Spec.bytes -le 0 -or [long]$Spec.bytes -gt 10GB -or $Spec.revision -notmatch '^[a-f0-9]{40}$' -or $Spec.id -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid pinned model metadata'}
 $uri=[uri]$Spec.url
 $expected='/'+$Spec.id+'/resolve/'+$Spec.revision+'/'+$Spec.file
 if($uri.Scheme -cne 'https' -or $uri.Host -cne 'huggingface.co' -or !$uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -cne $expected){throw 'Invalid pinned model URL'}
 $dest=$Destination.Replace('/','\')
 'Source: "'+$Spec.url+'"; DestDir: "{app}\'+$dest+'"; DestName: "'+$Spec.file+'"; ExternalSize: '+$Spec.bytes+'; Hash: "'+$Spec.sha256+'"; Components: '+$Component+'; Flags: external download ignoreversion uninsneveruninstall; Check: NeedsModel('''+$dest+'\'+$Spec.file+''', '''+$Spec.sha256+''')'
}
