# 同梱ライブラリの source を取得するスクリプト。third_party\ は Git に入れず、ここで毎回同じものを用意する。
# 版と SHA-256 はこのファイルに固定してある。一致しない download は使わない。
# 取得した archive は .deps-cache\ に残すので、2 回目以降は network を使わない。

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$tp    = Join-Path $root 'third_party'
$cache = Join-Path $root '.deps-cache'

# name は展開後の folder 名。url は公式の配布元、sha256 は検証済みの値。
$archives = @(
    @{ name = 'libogg-1.3.6';     url = 'https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.xz';
       sha256 = '5c8253428e181840cd20d41f3ca16557a9cc04bad4a3d04cce84808677fa1061' },
    @{ name = 'libvorbis-1.3.7';  url = 'https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.xz';
       sha256 = 'b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b' },
    @{ name = 'opus-1.6.1';       url = 'https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz';
       sha256 = '6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1' },
    @{ name = 'opusfile-0.12';    url = 'https://downloads.xiph.org/releases/opus/opusfile-0.12.tar.gz';
       sha256 = '118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b' },
    @{ name = 'libopusenc-0.2.1'; url = 'https://archive.mozilla.org/pub/opus/libopusenc-0.2.1.tar.gz';
       sha256 = '8298db61a8d3d63e41c1a80705baa8ce9ff3f50452ea7ec1c19a564fe106cbb9' },
    @{ name = 'flac-1.5.0';       url = 'https://downloads.xiph.org/releases/flac/flac-1.5.0.tar.xz';
       sha256 = 'f2c1c76592a82ffff8413ba3c4a1299b6c7ab06c734dee03fd88630485c2b920' }
)

# dr_libs は単一 header。commit を固定して取る。
$drCommit = 'dfe8377631000664666519fdb83da193fd8037f4'
$headers = @(
    @{ name = 'dr_wav.h'; sha256 = '03e70c1a2d9787cd7ed3e966c075bea7bac6373f759db9cd7ca9ccdfc4ec4493' },
    @{ name = 'dr_mp3.h'; sha256 = '997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f' },
    @{ name = 'LICENSE';  sha256 = 'dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15' }
)

function Get-Verified([string]$url, [string]$file, [string]$sha) {
    if (Test-Path $file) {
        if ((Get-FileHash $file -Algorithm SHA256).Hash -ieq $sha) { return }
        Remove-Item $file -Force
    }
    Write-Host "  download $url"
    $tmp = "$file.part"
    # PowerShell 5.1 の既定は古い TLS なので 1.2 を明示する。
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
    $got = (Get-FileHash $tmp -Algorithm SHA256).Hash
    if ($got -ine $sha) {
        Remove-Item $tmp -Force
        throw "SHA-256 mismatch for $url (expected $sha, got $got)"
    }
    Move-Item $tmp $file -Force
}

New-Item -ItemType Directory -Force $tp, $cache | Out-Null

foreach ($a in $archives) {
    $dest = Join-Path $tp $a.name
    $stamp = Join-Path $dest '.utagoe-sha256'
    if ((Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -ieq $a.sha256)) { continue }
    $file = Join-Path $cache (Split-Path $a.url -Leaf)
    Get-Verified $a.url $file $a.sha256
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Write-Host "  extract $($a.name)"
    # Windows 10 以降の tar (bsdtar) は .tar.gz と .tar.xz の両方を展開できる。
    # FLAC の archive には名前の無い entry があり、bsdtar は読み飛ばして終了 code 1 を返す。
    # 中身は揃っているので、終了 code ではなく展開結果で判定する。
    # PowerShell 5.1 は Stop のままだと native command の stderr を例外にするので、この呼び出しだけ Continue にする。
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & tar -xf $file -C $tp 2>&1 | ForEach-Object { "$_" } |
            Where-Object { $_ -notmatch 'empty or unreadable filename|Error exit delayed' } | ForEach-Object { Write-Host "  $_" }
    } finally { $ErrorActionPreference = $saved }
    if (-not (Test-Path (Join-Path $dest 'include')) -and -not (Test-Path (Join-Path $dest 'src'))) { throw "cannot extract $file" }
    Set-Content -Path $stamp -Value $a.sha256 -Encoding ascii
}

$dr = Join-Path $tp 'dr_libs'
New-Item -ItemType Directory -Force $dr | Out-Null
foreach ($h in $headers) {
    $file = Join-Path $cache ("dr_libs-" + $h.name)
    Get-Verified "https://raw.githubusercontent.com/mackron/dr_libs/$drCommit/$($h.name)" $file $h.sha256
    Copy-Item $file (Join-Path $dr $h.name) -Force
}

Write-Host 'deps: ready'
