function Get-LumaBootstrapFile {
    param([string]$Url,[string]$Destination,[string]$Hash,[int]$TimeoutSec)
    if ((Test-Path -LiteralPath $Destination -PathType Leaf) -and (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $Hash) { return }
    $partial = "$Destination.$([guid]::NewGuid().ToString('N')).part"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $partial -TimeoutSec $TimeoutSec
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Hash) { throw "SHA-256 mismatch: $Destination" }
        Move-Item -LiteralPath $partial -Destination $Destination -Force
    } finally {
        if (Test-Path -LiteralPath $partial -PathType Leaf) { Remove-Item -LiteralPath $partial -Force }
    }
}

function Install-LumaBootstrapExtraction {
    param(
        [string]$Target,[string[]]$Markers,[string[]]$Hashes,[scriptblock]$Extract,
        [ValidatePattern('\A[A-Za-z0-9][A-Za-z0-9._-]*\z')][string]$ContentDirectory
    )
    $targetPath = [IO.Path]::GetFullPath($Target).TrimEnd('\','/')
    $parent = Get-Item -LiteralPath (Split-Path -Parent $targetPath) -Force
    if (!$parent.PSIsContainer -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Bootstrap cache parent must be a regular directory.' }
    $name = [IO.Path]::GetFileName($targetPath)
    $targetPath = Join-Path $parent.FullName $name
    $lockPath = Join-Path $parent.FullName ".$name.extract.lock"
    $lock = [IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $stage = $null
    $backup = $null
    $published = $false
    try {
        $current = Get-Item -LiteralPath $targetPath -Force -ErrorAction SilentlyContinue
        if ($current -and (!$current.PSIsContainer -or ($current.Attributes -band [IO.FileAttributes]::ReparsePoint))) { throw 'Bootstrap cache target must be a regular directory.' }
        $stampName = '.luma-bootstrap-archives.sha256'
        $stamp = Join-Path $targetPath $stampName
        $fingerprint = $Hashes -join "`n"
        if ($current -and (Test-Path -LiteralPath $stamp -PathType Leaf) -and (Get-Content -LiteralPath $stamp -Raw) -ceq $fingerprint -and
            !($Markers | Where-Object { !(Test-Path -LiteralPath (Join-Path $targetPath $_) -PathType Leaf) })) { return }

        $id = [guid]::NewGuid().ToString('N')
        $stage = Join-Path $parent.FullName ".$name.stage-$id"
        New-Item -ItemType Directory -Path $stage | Out-Null
        & $Extract $stage
        $payload = if ($ContentDirectory) { Join-Path $stage $ContentDirectory } else { $stage }
        foreach ($marker in $Markers) {
            if (!(Test-Path -LiteralPath (Join-Path $payload $marker) -PathType Leaf)) { throw "Missing extracted marker: $marker" }
        }
        [IO.File]::WriteAllText((Join-Path $payload $stampName),$fingerprint,[Text.UTF8Encoding]::new($false))
        if ($current) {
            $backup = Join-Path $parent.FullName ".$name.previous-$id"
            [IO.Directory]::Move($targetPath,$backup)
        }
        try {
            [IO.Directory]::Move($payload,$targetPath)
            $published = $true
        } catch {
            if ($backup -and !(Test-Path -LiteralPath $targetPath)) { [IO.Directory]::Move($backup,$targetPath) }
            throw
        }
    } finally {
        foreach ($temporary in $stage,$(if ($published) { $backup })) {
            if (!$temporary) { continue }
            try {
                $item = Get-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
                if (!$item) { continue }
                if (!$item.PSIsContainer -or $item.Parent.FullName -ine $parent.FullName -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe bootstrap cache cleanup path.' }
                [IO.Directory]::Delete($item.FullName,$true)
            } catch { Write-Warning "Bootstrap left cache cleanup at $temporary. $($_.Exception.Message)" }
        }
        $lock.Dispose()
    }
}
