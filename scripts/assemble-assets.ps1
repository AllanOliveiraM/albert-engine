param([Parameter(Mandatory=$true)][string]$Manifest,
      [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$record=Get-Content -Raw -LiteralPath $Manifest | ConvertFrom-Json
$directory=Split-Path -Parent $Manifest
[IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
$temporary="$Destination.tmp"
try {
    $output=[IO.File]::Create($temporary)
    try {
        foreach ($part in $record.parts) {
            if ([IO.Path]::GetFileName($part.path) -ne $part.path) { throw 'Invalid asset part path' }
            $path=Join-Path $directory $part.path
            if ((Get-Item -LiteralPath $path).Length -ne $part.bytes) {
                throw "Git LFS images missing or incomplete. Run: git lfs install; git lfs pull"
            }
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $part.sha256) {
                throw "Asset part integrity mismatch: $($part.path)"
            }
            $inputStream=[IO.File]::OpenRead($path)
            try { $inputStream.CopyTo($output,1048576) } finally { $inputStream.Dispose() }
        }
    } finally { $output.Dispose() }
    if ((Get-Item -LiteralPath $temporary).Length -ne $record.bytes -or
        (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.sha256) {
        throw 'Complete asset library integrity mismatch'
    }
    Move-Item -Force -LiteralPath $temporary -Destination $Destination
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Host "Assets ready: $Destination $($record.bytes) bytes"
