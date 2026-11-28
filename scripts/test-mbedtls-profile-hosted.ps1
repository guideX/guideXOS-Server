param(
    [string]$OutputRoot = '',
    [string]$OpenSsl = '',
    [int]$BasePort = 47430
)
# Hosted TLS / crypto / X.509 regression for the guideXOS Mbed TLS profile.
# Compiles the exact source list from guidexos/mbedtls_sources.mk with the
# guideXOS config pair for the Windows host, generates a disposable PKI with
# OpenSSL, then runs tests/tls/mbedtls_profile_hosted_test.c in crypto mode
# and against local OpenSSL TLS 1.2 servers. No guest or network access.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'Requires PowerShell 7.' }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repo ('out\mbedtls-hosted\' + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ'))
}
$out = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $out) { throw "Refusing to reuse $out" }
$pki = Join-Path $out 'pki'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Path $pki, $obj | Out-Null

& (Join-Path $PSScriptRoot 'verify-mbedtls-profile.ps1') -DependencyRoot (Join-Path $repo 'third_party\mbedtls') -RepoRoot $repo -Quiet
if ($LASTEXITCODE -ne 0) { throw 'Mbed TLS profile verification failed.' }

if ([string]::IsNullOrWhiteSpace($OpenSsl)) {
    $OpenSsl = (Get-Command openssl -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $OpenSsl) { $OpenSsl = 'C:\Program Files\Git\ucrt64\bin\openssl.exe' }
}
if (-not (Test-Path -LiteralPath $OpenSsl)) { throw "OpenSSL not found: $OpenSsl" }
$gcc = (Get-Command gcc -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $gcc) { $gcc = 'C:\mingw64\bin\gcc.exe' }
$make = (Get-Command mingw32-make -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $make) { $make = 'C:\mingw64\bin\mingw32-make.exe' }

function Invoke-Checked([string]$exe, [string[]]$arguments, [string]$what) {
    $o = & $exe @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE): $($o | Out-String)" }
    return $o
}

# ---- source list and flags straight from the tracked manifest ----
$probe = Join-Path $out 'print-profile.mk'
@"
MBEDTLS_GUIDEXOS_REPO_ROOT := ..
include ../guidexos/mbedtls_sources.mk
print:
	@echo SOURCES=`$(MBEDTLS_GUIDEXOS_C_SOURCES)
	@echo CFLAGS=`$(MBEDTLS_GUIDEXOS_CFLAGS)
	@echo READY=`$(MBEDTLS_GUIDEXOS_LAYOUT_READY)
	@echo CONFIG=`$(MBEDTLS_GUIDEXOS_CONFIG_HEADER)
	@echo CRYPTOCONFIG=`$(MBEDTLS_GUIDEXOS_CRYPTO_CONFIG_HEADER)
"@ | Set-Content -LiteralPath $probe -Encoding ascii
$printed = Invoke-Checked $make @('-s', '-C', (Join-Path $repo 'kernel'), '-f', $probe, 'print') 'manifest probe'
$sources = @((($printed | Where-Object { $_ -like 'SOURCES=*' }) -replace '^SOURCES=', '').Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries))
$cflagsLine = (($printed | Where-Object { $_ -like 'CFLAGS=*' }) -replace '^CFLAGS=', '')
if ((($printed | Where-Object { $_ -like 'READY=*' }) -replace '^READY=', '') -ne '1') { throw 'Mbed TLS layout not ready.' }
if ($sources.Count -ne 57) { throw "Expected 57 profile sources, found $($sources.Count)." }
# Keep make's escaping (-DMBEDTLS_CONFIG_FILE=\"...\") and hand it to gcc via a
# response file so no shell re-quotes the config header names.
$rsp = Join-Path $out 'mbedtls-cflags.rsp'
$cfgHeader = (($printed | Where-Object { $_ -like 'CONFIG=*' }) -replace '^CONFIG=', '').Trim()
$cryptoHeader = (($printed | Where-Object { $_ -like 'CRYPTOCONFIG=*' }) -replace '^CRYPTOCONFIG=', '').Trim()
$rspTokens = @($cflagsLine.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) | Where-Object { $_ -notmatch 'CONFIG_FILE=' })
$rspTokens += ('-DMBEDTLS_CONFIG_FILE=\"' + $cfgHeader + '\"')
$rspTokens += ('-DTF_PSA_CRYPTO_CONFIG_FILE=\"' + $cryptoHeader + '\"')
Set-Content -LiteralPath $rsp -Value ($rspTokens -join ' ') -Encoding ascii
$flags = @("@$rsp")

# ---- disposable PKI ----
Push-Location $pki
try {
    $o = $OpenSsl
    $cnf = 'ext.cnf'
@"
[ca]
basicConstraints=critical,CA:TRUE
keyUsage=critical,keyCertSign,cRLSign
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid
[noca]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyCertSign
[ec_leaf]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:tls.guidexos.test
[rsa_leaf]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:rsa.guidexos.test
[exp_leaf]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:expired.guidexos.test
[noca_leaf]
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
subjectAltName=DNS:noca.guidexos.test
"@ | Set-Content $cnf -Encoding ascii
    Invoke-Checked $o @('req','-x509','-newkey','rsa:2048','-nodes','-keyout','root.key','-out','root.pem','-days','30','-subj','/CN=guideXOS RECOVERY2 Test Root','-sha256','-addext','basicConstraints=critical,CA:TRUE','-addext','keyUsage=critical,keyCertSign,cRLSign') 'root' | Out-Null
    Invoke-Checked $o @('req','-x509','-newkey','rsa:2048','-nodes','-keyout','other-root.key','-out','other-root.pem','-days','30','-subj','/CN=guideXOS Unrelated Root','-sha256','-addext','basicConstraints=critical,CA:TRUE','-addext','keyUsage=critical,keyCertSign,cRLSign') 'other root' | Out-Null
    # EC P-256 intermediate signed by the RSA root.
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','int.key') 'int key' | Out-Null
    Invoke-Checked $o @('req','-new','-key','int.key','-out','int.csr','-subj','/CN=guideXOS RECOVERY2 EC Intermediate') 'int csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','int.csr','-CA','root.pem','-CAkey','root.key','-CAcreateserial','-out','int.pem','-days','20','-sha256','-extfile',$cnf,'-extensions','ca') 'int cert' | Out-Null
    # EC P-256 leaf signed (ECDSA-SHA384) by the intermediate.
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','ec-leaf.key') 'ec leaf key' | Out-Null
    Invoke-Checked $o @('req','-new','-key','ec-leaf.key','-out','ec-leaf.csr','-subj','/CN=tls.guidexos.test') 'ec leaf csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','ec-leaf.csr','-CA','int.pem','-CAkey','int.key','-CAcreateserial','-out','ec-leaf.pem','-days','10','-sha384','-extfile',$cnf,'-extensions','ec_leaf') 'ec leaf' | Out-Null
    # RSA leaf signed directly by the root.
    Invoke-Checked $o @('req','-new','-newkey','rsa:2048','-nodes','-keyout','rsa-leaf.key','-out','rsa-leaf.csr','-subj','/CN=rsa.guidexos.test') 'rsa leaf csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','rsa-leaf.csr','-CA','root.pem','-CAkey','root.key','-CAcreateserial','-out','rsa-leaf.pem','-days','10','-sha256','-extfile',$cnf,'-extensions','rsa_leaf') 'rsa leaf' | Out-Null
    # Expired leaf: validity window entirely in the past.
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','exp-leaf.key') 'exp key' | Out-Null
    Invoke-Checked $o @('req','-new','-key','exp-leaf.key','-out','exp-leaf.csr','-subj','/CN=expired.guidexos.test') 'exp csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','exp-leaf.csr','-CA','int.pem','-CAkey','int.key','-CAcreateserial','-out','exp-leaf.pem','-not_before','20200101000000Z','-not_after','20200201000000Z','-sha256','-extfile',$cnf,'-extensions','exp_leaf') 'expired leaf' | Out-Null
    # Intermediate without the CA bit issuing a leaf.
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','noca-int.key') 'noca int key' | Out-Null
    Invoke-Checked $o @('req','-new','-key','noca-int.key','-out','noca-int.csr','-subj','/CN=guideXOS Non-CA Intermediate') 'noca csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','noca-int.csr','-CA','root.pem','-CAkey','root.key','-CAcreateserial','-out','noca-int.pem','-days','10','-sha256','-extfile',$cnf,'-extensions','noca') 'noca int' | Out-Null
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','noca-leaf.key') 'noca leaf key' | Out-Null
    Invoke-Checked $o @('req','-new','-key','noca-leaf.key','-out','noca-leaf.csr','-subj','/CN=noca.guidexos.test') 'noca leaf csr' | Out-Null
    Invoke-Checked $o @('x509','-req','-in','noca-leaf.csr','-CA','noca-int.pem','-CAkey','noca-int.key','-CAcreateserial','-out','noca-leaf.pem','-days','5','-sha256','-extfile',$cnf,'-extensions','noca_leaf') 'noca leaf' | Out-Null

    Set-Content 'ec-leaf-chain.pem' -Value ((Get-Content 'ec-leaf.pem' -Raw) + (Get-Content 'int.pem' -Raw)) -NoNewline -Encoding ascii
    Set-Content 'ec-leaf-only.pem' -Value (Get-Content 'ec-leaf.pem' -Raw) -NoNewline -Encoding ascii
    Set-Content 'rsa-leaf-chain.pem' -Value (Get-Content 'rsa-leaf.pem' -Raw) -NoNewline -Encoding ascii
    Set-Content 'expired-leaf-chain.pem' -Value ((Get-Content 'exp-leaf.pem' -Raw) + (Get-Content 'int.pem' -Raw)) -NoNewline -Encoding ascii
    Set-Content 'noca-intermediate-chain.pem' -Value ((Get-Content 'noca-leaf.pem' -Raw) + (Get-Content 'noca-int.pem' -Raw)) -NoNewline -Encoding ascii
    # Tampered leaf: flip one bit inside the DER signature, re-encode as PEM.
    Invoke-Checked $o @('x509','-in','ec-leaf.pem','-outform','DER','-out','ec-leaf.der') 'leaf der' | Out-Null
    $der = [System.IO.File]::ReadAllBytes((Join-Path $pki 'ec-leaf.der'))
    $der[$der.Length - 5] = $der[$der.Length - 5] -bxor 0x01
    $b64 = [Convert]::ToBase64String($der, [Base64FormattingOptions]::InsertLineBreaks) -replace "`r`n", "`n"
    Set-Content 'tampered-leaf-chain.pem' -Value ("-----BEGIN CERTIFICATE-----`n$b64`n-----END CERTIFICATE-----`n" + (Get-Content 'int.pem' -Raw)) -NoNewline -Encoding ascii

    # Independent OpenSSL signatures for the cross-check.
    Invoke-Checked $o @('ecparam','-name','prime256v1','-genkey','-noout','-out','ec256.key') 'ec256 key' | Out-Null
    Invoke-Checked $o @('ecparam','-name','secp384r1','-genkey','-noout','-out','ec384.key') 'ec384 key' | Out-Null
    Invoke-Checked $o @('ec','-in','ec256.key','-pubout','-outform','DER','-out','ec256-pub.der') 'ec256 pub' | Out-Null
    Invoke-Checked $o @('ec','-in','ec384.key','-pubout','-outform','DER','-out','ec384-pub.der') 'ec384 pub' | Out-Null
    Invoke-Checked $o @('pkey','-in','rsa-leaf.key','-pubout','-outform','DER','-out','rsa-pub.der') 'rsa pub' | Out-Null
    $l256 = @(); $l384 = @(); $lrsa = @()
    for ($i = 0; $i -lt 24; $i++) {
        $m = 'msg-{0:D2}.bin' -f $i
        $bytes = [byte[]]::new(37 + $i * 13)
        [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
        [System.IO.File]::WriteAllBytes((Join-Path $pki $m), $bytes)
        Invoke-Checked $o @('dgst','-sha256','-sign','ec256.key','-out',('ec256-{0:D2}.sig' -f $i),$m) 'ec256 sign' | Out-Null
        Invoke-Checked $o @('dgst','-sha384','-sign','ec384.key','-out',('ec384-{0:D2}.sig' -f $i),$m) 'ec384 sign' | Out-Null
        Invoke-Checked $o @('dgst','-sha256','-sign','rsa-leaf.key','-out',('rsa-{0:D2}.sig' -f $i),$m) 'rsa sign' | Out-Null
        $l256 += "$m ec256-{0:D2}.sig" -f $i; $l384 += "$m ec384-{0:D2}.sig" -f $i; $lrsa += "$m rsa-{0:D2}.sig" -f $i
    }
    Invoke-Checked $o @('dgst','-sha256','-sign','rsa-leaf.key','-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32','-out','rsa-pss-00.sig','msg-00.bin') 'rsa pss' | Out-Null
    $l256 | Set-Content 'ec256-sigs.txt' -Encoding ascii
    $l384 | Set-Content 'ec384-sigs.txt' -Encoding ascii
    $lrsa | Set-Content 'rsa-pkcs1-sigs.txt' -Encoding ascii
} finally { Pop-Location }

# ---- compile ----
$kernelDir = Join-Path $repo 'kernel'
$objs = @()
Push-Location $kernelDir
try {
    foreach ($src in $sources) {
        $name = ($src -replace '^\.\./third_party/mbedtls/', '' -replace '[\\/]', '_') -replace '\.c$', '.o'
        $o = Join-Path $obj $name
        Invoke-Checked $gcc (@('-x','c','-O2','-c',$src,'-o',$o,'-I..') + $flags) "compile $src" | Out-Null
        $objs += $o
    }
    $test = Join-Path $repo 'tests\tls\mbedtls_profile_hosted_test.c'
    $exe = Join-Path $out 'mbedtls_profile_hosted_test.exe'
    Invoke-Checked $gcc (@('-x','c','-O2',$test,'-I..') + $flags + @('-x','none') + $objs + @('-o',$exe,'-lws2_32','-lbcrypt')) 'link test' | Out-Null
} finally { Pop-Location }

$log = Join-Path $out 'hosted-results.txt'
$crypto = & $exe crypto $pki 2>&1
$cryptoExit = $LASTEXITCODE
$crypto | Set-Content -LiteralPath $log -Encoding utf8
$crypto | Write-Output

# ---- TLS 1.2 handshakes against local OpenSSL servers ----
$cases = @(
    @{ Name='ecdhe-ecdsa'; Cert='ec-leaf.pem'; Key='ec-leaf.key'; Chain='int.pem'; Host='tls.guidexos.test'; Ca='root.pem'; Expect='ok'; Extra=@('-cipher','ECDHE-ECDSA-AES128-GCM-SHA256') },
    @{ Name='ecdhe-rsa-pkcs1'; Cert='rsa-leaf.pem'; Key='rsa-leaf.key'; Chain=$null; Host='rsa.guidexos.test'; Ca='root.pem'; Expect='ok'; Extra=@('-cipher','ECDHE-RSA-AES256-GCM-SHA384','-sigalgs','RSA+SHA256') },
    # The client's TLS 1.2 signature_algorithms list does not offer RSA-PSS, so a
    # PSS-only server must be refused ("no shared signature algorithms").
    @{ Name='rsa-pss-only-server-refused'; Cert='rsa-leaf.pem'; Key='rsa-leaf.key'; Chain=$null; Host='rsa.guidexos.test'; Ca='root.pem'; Expect='fail'; Flags='0'; Extra=@('-cipher','ECDHE-RSA-AES128-GCM-SHA256','-sigalgs','rsa_pss_rsae_sha256') },
    # With OpenSSL's default list (PSS preferred) the server must fall back to a
    # scheme the client actually offered.
    @{ Name='ecdhe-rsa-default-sigalgs'; Cert='rsa-leaf.pem'; Key='rsa-leaf.key'; Chain=$null; Host='rsa.guidexos.test'; Ca='root.pem'; Expect='ok'; Extra=@('-cipher','ECDHE-RSA-AES128-GCM-SHA256') },
    @{ Name='wrong-hostname'; Cert='ec-leaf.pem'; Key='ec-leaf.key'; Chain='int.pem'; Host='wrong.guidexos.test'; Ca='root.pem'; Expect='fail'; Flags='4'; Extra=@() },
    @{ Name='untrusted-root'; Cert='ec-leaf.pem'; Key='ec-leaf.key'; Chain='int.pem'; Host='tls.guidexos.test'; Ca='other-root.pem'; Expect='fail'; Flags='8'; Extra=@() },
    @{ Name='expired-leaf'; Cert='exp-leaf.pem'; Key='exp-leaf.key'; Chain='int.pem'; Host='expired.guidexos.test'; Ca='root.pem'; Expect='fail'; Flags='1'; Extra=@() }
)
$tlsFail = 0
$port = $BasePort
foreach ($c in $cases) {
    $port++
    $srvArgs = @('s_server','-tls1_2','-accept',"127.0.0.1:$port",'-naccept','1','-cert',(Join-Path $pki $c.Cert),'-key',(Join-Path $pki $c.Key),'-quiet') + $c.Extra
    if ($c.Chain) { $srvArgs += @('-cert_chain',(Join-Path $pki $c.Chain)) }
    $srvOut = Join-Path $out "server-$($c.Name).log"
    $srv = Start-Process -FilePath $OpenSsl -ArgumentList $srvArgs -PassThru -NoNewWindow -RedirectStandardOutput $srvOut -RedirectStandardError "$srvOut.err"
    try {
        $deadline = (Get-Date).AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 200
            $listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
        } while (-not $listening -and (Get-Date) -lt $deadline -and -not $srv.HasExited)
        $args2 = @('tls', "$port", $c.Host, (Join-Path $pki $c.Ca), $c.Expect)
        if ($c.ContainsKey('Flags')) { $args2 += $c.Flags }
        $r = & $exe @args2 2>&1
        $code = $LASTEXITCODE
        $line = "TLS_CASE $($c.Name) expect=$($c.Expect) exit=$code :: $(($r | Out-String).Trim() -replace "`r?`n", ' | ')"
        Write-Output $line
        Add-Content -LiteralPath $log -Value $line
        if ($code -ne 0) { $tlsFail++ }
    } finally {
        if (-not $srv.HasExited) { Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue }
    }
}

$summary = "MBEDTLS_PROFILE_HOSTED_RESULT=$(if ($cryptoExit -eq 0 -and $tlsFail -eq 0) { 'PASS' } else { 'FAIL' }) CRYPTO_EXIT=$cryptoExit TLS_CASES=$($cases.Count) TLS_FAILURES=$tlsFail OUTPUT=$out"
Add-Content -LiteralPath $log -Value $summary
Write-Output $summary
if ($cryptoExit -ne 0 -or $tlsFail -ne 0) { exit 1 }
