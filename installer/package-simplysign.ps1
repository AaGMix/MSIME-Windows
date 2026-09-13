# Build a distributable installer and sign it with the real Certum Code Signing certificate
# exposed by SimplySign Desktop.
#
# Typical usage (Version is positional):
#   pwsh -File .\package-simplysign.ps1 1.2.3
#   pwsh -File .\package-simplysign.ps1 1.2.3 -IncludeSymbols
#
# Start SimplySign Desktop and connect the virtual card before running this script. Signtool may
# display a SimplySign PIN prompt for each signing operation. The private key never leaves
# SimplySign; signtool selects the certificate from the CurrentUser\My store by SHA-1 thumbprint.

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [Alias('TargetVersion')]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    # Published CI installers contain matching PDBs. They make local packaging substantially slower,
    # so this local entry keeps them opt-in, like test-symbols.ps1.
    [switch]$IncludeSymbols,

    # Force CMake configure before the incremental build.
    [switch]$Reconfigure,

    # Normally the single valid Certum code-signing certificate is selected automatically. Pass the
    # thumbprint when more than one is connected through SimplySign Desktop.
    [string]$CertificateThumbprint,

    [ValidateNotNullOrEmpty()]
    [string]$TimestampUrl = 'http://time.certum.pl',

    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$CodeSigningEku = '1.3.6.1.5.5.7.3.3'
$LocalTestCertificateSubject = 'CN=Metasequoia IME Local Test Code Signing'

function Find-SignTool {
    $kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidate = Get-ChildItem -LiteralPath $kitsBin -Directory -ErrorAction SilentlyContinue |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if (-not $candidate) {
        throw '找不到 signtool.exe；请安装 Windows SDK。'
    }
    return $candidate
}

function Get-SimplySignCertificate {
    param([string]$Thumbprint)

    $now = Get-Date
    $normalizedThumbprint = $Thumbprint -replace '\s', ''
    $candidates = @(
        Get-ChildItem -Path 'Cert:\CurrentUser\My' |
            Where-Object {
                $_.HasPrivateKey -and
                $_.NotBefore -le $now -and
                $_.NotAfter -gt $now -and
                ($_.EnhancedKeyUsageList.ObjectId -contains $CodeSigningEku) -and
                $_.Subject -ne $LocalTestCertificateSubject -and
                $_.Issuer -match '(?i)Certum'
            }
    )

    if ($normalizedThumbprint) {
        $candidates = @(
            $candidates | Where-Object {
                ($_.Thumbprint -replace '\s', '') -ieq $normalizedThumbprint
            }
        )
        if ($candidates.Count -eq 0) {
            throw @"
在 Cert:\CurrentUser\My 中找不到可用的代码签名证书：$normalizedThumbprint
请先启动 SimplySign Desktop、连接虚拟卡并确认该证书带有私钥且仍在有效期内。
"@
        }
    }
    elseif ($candidates.Count -eq 0) {
        throw @'
没有发现 SimplySign 提供的有效 Certum 代码签名证书。
请先启动 SimplySign Desktop，在手机端生成 OTP 并连接虚拟卡，然后重新运行本脚本。
'@
    }
    elseif ($candidates.Count -gt 1) {
        $details = $candidates | ForEach-Object {
            "  $($_.Thumbprint)  $($_.Subject)  有效期至 $($_.NotAfter.ToString('yyyy-MM-dd'))"
        }
        throw "发现多个可用的 Certum 代码签名证书，请用 -CertificateThumbprint 指定其中一个：`n$($details -join "`n")"
    }

    return $candidates[0]
}

function Invoke-SimplySign {
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)]$Certificate
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "待签名文件不存在：$LiteralPath"
    }

    $resolvedPath = (Resolve-Path -LiteralPath $LiteralPath).Path
    $thumbprint = $Certificate.Thumbprint -replace '\s', ''
    Write-Host "正在使用 SimplySign 签名：$resolvedPath"
    & $script:signTool sign /sha1 $thumbprint /s My /fd sha256 /tr $TimestampUrl /td sha256 /v $resolvedPath
    if ($LASTEXITCODE -ne 0) {
        throw "signtool.exe 签名失败，退出码：$LASTEXITCODE；文件：$resolvedPath"
    }

    & $script:signTool verify /pa /all /v $resolvedPath
    if ($LASTEXITCODE -ne 0) {
        throw "signtool.exe 签名校验失败，退出码：$LASTEXITCODE；文件：$resolvedPath"
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
    if (-not $signature.SignerCertificate -or
        (($signature.SignerCertificate.Thumbprint -replace '\s', '') -ine $thumbprint)) {
        throw "签名证书指纹与选择的 SimplySign 证书不一致：$resolvedPath"
    }
    if (-not $signature.TimeStamperCertificate) {
        throw "签名中没有可信时间戳：$resolvedPath"
    }
    Write-Host "签名有效：$($signature.SignerCertificate.Subject)"
}

# Fail before compiling anything if SimplySign is not connected. Merely having its desktop process
# installed or running is not enough: the cloud certificate appears in CurrentUser\My only while the
# virtual card is connected.
$certificate = Get-SimplySignCertificate -Thumbprint $CertificateThumbprint
$script:signTool = Find-SignTool
Write-Host "SimplySign 证书：$($certificate.Subject)"
Write-Host "证书指纹：$($certificate.Thumbprint)"
Write-Host "有效期至：$($certificate.NotAfter.ToString('yyyy-MM-dd HH:mm:ss'))"
Write-Host "时间戳服务：$TimestampUrl"

$repoRoot = Split-Path -Parent $PSScriptRoot
$tsfCompile = Join-Path $repoRoot 'windows\scripts\lcompile-release-both.ps1'
$serverCompile = Join-Path $repoRoot 'server\scripts\lcompile-release.ps1'
$settingsDir = Join-Path $repoRoot 'ui-html\webview2\settings\ime-settings'
foreach ($required in @($tsfCompile, $serverCompile, (Join-Path $settingsDir 'package.json'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "缺少组件构建入口：$required"
    }
}

# Prepare-PackageFiles writes the requested version into msime_setup.iss. Keep that implementation
# as the single source of installer naming/version logic, but do not leave a local packaging command
# changing the tracked template after it finishes.
$issPath = Join-Path $PSScriptRoot 'msime_setup.iss'
$tsfResourcePath = Join-Path $repoRoot 'windows\src\IME\MetasequoiaIME.rc'
$originalIss = [IO.File]::ReadAllBytes($issPath)
$originalTsfResource = [IO.File]::ReadAllBytes($tsfResourcePath)
$installerPath = Join-Path $PSScriptRoot "Output\MetasequoiaIME_Setup_v$Version.exe"

Push-Location $PSScriptRoot
try {
    # The command-line version drives both Inno Setup and the TSF DLL's VERSIONINFO. Restore the
    # tracked resource template afterwards; the compiled DLL keeps the requested version.
    python (Join-Path $repoRoot 'scripts\apply_version.py') --version $Version
    if ($LASTEXITCODE -ne 0) { throw "TSF 版本资源写入失败 ($LASTEXITCODE)" }

    Push-Location (Join-Path $repoRoot 'windows')
    try { & $tsfCompile -Reconfigure:$Reconfigure } finally { Pop-Location }

    Push-Location (Join-Path $repoRoot 'server')
    try { & $serverCompile -Reconfigure:$Reconfigure } finally { Pop-Location }

    Push-Location $settingsDir
    try {
        pnpm run build
        if ($LASTEXITCODE -ne 0) { throw "Settings page build failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }

    & (Join-Path $PSScriptRoot 'Prepare-PackageFiles.ps1') `
        -TargetVersion $Version `
        -IncludeSymbols:$IncludeSymbols `
        -RepoRoot $repoRoot `
        -TsfDirectory windows `
        -ServerDirectory server `
        -UiHtmlDirectory ui-html `
        -NoticesDirectory . `
        -HelpCodeDirectory engine/helpcode

    # This is the only payload binary requiring a real signature: uiAccess=true is ignored by
    # Windows unless the executable carries a trusted Authenticode signature. The outer installer is
    # signed after compilation. This deliberately matches the formal release workflow.
    Invoke-SimplySign `
        -LiteralPath (Join-Path $PSScriptRoot 'server_exe\MetasequoiaImeServer.exe') `
        -Certificate $certificate

    & (Join-Path $PSScriptRoot 'Compile-Installer.ps1') -IsccPath $IsccPath
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "Inno Setup 没有生成预期的安装包：$installerPath"
    }

    Invoke-SimplySign -LiteralPath $installerPath -Certificate $certificate
    $hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash

    Write-Host ''
    Write-Host 'SimplySign 正式签名安装包已生成：'
    Write-Host "  $installerPath"
    Write-Host "  SHA256: $hash"
}
finally {
    [IO.File]::WriteAllBytes($issPath, $originalIss)
    [IO.File]::WriteAllBytes($tsfResourcePath, $originalTsfResource)
    Pop-Location
}
