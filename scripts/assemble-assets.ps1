param([Parameter(Mandatory=$true)][string]$Manifest,
      [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
# .NET hashing: Get-FileHash lives in a script module that Windows PowerShell 5.1
# cannot load when started from PowerShell 7 (inherited PSModulePath).
function Get-Sha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose();$sha.Dispose() }
}
$directory=Split-Path -Parent $Manifest
$base=Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
$manifests=@($Manifest)
$extensions=if ($base.PSObject.Properties.Name -contains 'extensions') { $base.extensions } else { @() }
foreach ($extension in $extensions) {
    if ([IO.Path]::IsPathRooted($extension.manifest) -or $extension.manifest -match '(^|[\\/])\.\.([\\/]|$)') { throw 'Invalid extension manifest path' }
    $manifests+=Join-Path $directory $extension.manifest
}
[IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
$temporary="$Destination.tmp"
try {
    $output=[IO.File]::Open($temporary,[IO.FileMode]::Create,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
    $archives=@()
    try {
        foreach ($path in $manifests) {
            $record=Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
            if ($path -ne $Manifest -and ($record.PSObject.Properties.Name -contains 'extensions')) { throw 'Nested extensions are unsupported' }
            $start=$output.Position
            foreach ($part in $record.parts) {
                if ([IO.Path]::GetFileName($part.path) -ne $part.path) { throw 'Invalid asset part path' }
                $partPath=Join-Path (Split-Path -Parent $path) $part.path
                if ((Get-Item -LiteralPath $partPath).Length -ne $part.bytes) { throw 'Git LFS images missing or incomplete. Run: git lfs install; git lfs pull' }
                if ((Get-Sha256 $partPath) -ne $part.sha256) { throw "Asset part integrity mismatch: $($part.path)" }
                $inputStream=[IO.File]::OpenRead($partPath)
                try { $inputStream.CopyTo($output,1048576) } finally { $inputStream.Dispose() }
            }
            $size=$output.Position-$start
            if ($size -ne $record.bytes) { throw 'Complete asset archive size mismatch' }
            # Hash only this archive, preserving the original package unchanged.
            $output.Flush()
            $inputStream=[IO.File]::Open($temporary,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $sha=[Security.Cryptography.SHA256]::Create()
            try {
                [void]$inputStream.Seek($start,[IO.SeekOrigin]::Begin)
                $buffer=New-Object byte[] 1048576
                $remaining=$size
                while ($remaining -gt 0) {
                    $count=$inputStream.Read($buffer,0,[int][Math]::Min([long]$buffer.Length,$remaining))
                    if ($count -le 0) { throw 'Incomplete asset archive' }
                    [void]$sha.TransformBlock($buffer,0,$count,$buffer,0);$remaining-=$count
                }
                [void]$sha.TransformFinalBlock([byte[]]@(),0,0)
                $actual=([BitConverter]::ToString($sha.Hash)).Replace('-','').ToLowerInvariant()
                if ($actual -ne $record.sha256) { throw 'Complete asset archive integrity mismatch' }
            } finally { $inputStream.Dispose();$sha.Dispose() }
            $archives+=[ordered]@{offset=[long]$start;bytes=[long]$size}
        }
        if ($archives.Count -gt 1) {
            $index=[ordered]@{container='albert-pack-collection-v1';archives=@($archives)} | ConvertTo-Json -Depth 4 -Compress
            $bytes=[Text.Encoding]::UTF8.GetBytes($index)
            $output.Write($bytes,0,$bytes.Length)
            $length=[BitConverter]::GetBytes([long]$bytes.Length)
            if (-not [BitConverter]::IsLittleEndian) { [Array]::Reverse($length) }
            $output.Write($length,0,8)
            $magic=[Text.Encoding]::ASCII.GetBytes('ALBRTPX1');$output.Write($magic,0,8)
        }
    } finally { $output.Dispose() }
    Move-Item -Force -LiteralPath $temporary -Destination $Destination
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Host "Assets ready: $Destination"
