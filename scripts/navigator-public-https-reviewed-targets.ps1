Set-StrictMode -Version Latest

$script:NavigatorPublicHttpsReviewedTargetAllowlistName = "guidexos-reviewed-public-https-v0.7"
$script:NavigatorPublicHttpsReviewedTargetAllowlistVersion = "v0.7"
$script:NavigatorPublicHttpsReviewedTargets = @(
    [pscustomobject]@{
        Url = "https://sha256.badssl.com/"
        Host = "sha256.badssl.com"
        Default = $true
        Reason = "Stable badssl DNS-hosted HTTPS endpoint used to prove real-world DNS, TCP, TLS, certificate, and hostname validation without enabling arbitrary public browsing."
    },
    [pscustomobject]@{
        Url = "https://example.com/"
        Host = "example.com"
        Default = $false
        Reason = "IANA example HTTPS page used as the first real HTML/Navigator public page target."
    },
    [pscustomobject]@{
        Url = "https://www.gnu.org/"
        Host = "www.gnu.org"
        Default = $false
        Reason = "GNU HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    },
    [pscustomobject]@{
        Url = "https://news.ycombinator.com/"
        Host = "news.ycombinator.com"
        Default = $false
        Reason = "Hacker News HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    },
    [pscustomobject]@{
        Url = "https://en.wikipedia.org/"
        Host = "en.wikipedia.org"
        Default = $false
        Reason = "English Wikipedia HTTPS homepage retained as a reviewed public HTML target for the post-example.com navigation sequence."
    },
    [pscustomobject]@{
        Url = "https://www.iana.org/"
        Host = "www.iana.org"
        Default = $false
        Reason = "IANA HTTPS homepage adds a standards-oriented HTML control with stable public hosting."
    },
    [pscustomobject]@{
        Url = "https://www.w3.org/"
        Host = "www.w3.org"
        Default = $false
        Reason = "W3C HTTPS homepage adds a standards-oriented HTML page with CSS and form markup."
    },
    [pscustomobject]@{
        Url = "https://www.python.org/"
        Host = "www.python.org"
        Default = $false
        Reason = "Python HTTPS homepage adds a large, production-hosted HTML page with scripts and responsive CSS."
    },
    [pscustomobject]@{
        Url = "https://www.rust-lang.org/"
        Host = "www.rust-lang.org"
        Default = $false
        Reason = "Rust HTTPS homepage adds a modern public HTML page with external resources."
    },
    [pscustomobject]@{
        Url = "https://www.kernel.org/"
        Host = "www.kernel.org"
        Default = $false
        Reason = "Kernel.org HTTPS homepage adds a public HTML page from a technically relevant production host."
    },
    [pscustomobject]@{
        Url = "https://www.mozilla.org/"
        Host = "www.mozilla.org"
        Default = $false
        Reason = "Mozilla HTTPS homepage adds a public HTML page with production certificate and resource diversity."
    },
    [pscustomobject]@{
        Url = "https://www.nasa.gov/"
        Host = "www.nasa.gov"
        Default = $false
        Reason = "NASA HTTPS homepage adds a public HTML page with a large real-world document and media references."
    },
    [pscustomobject]@{
        Url = "https://www.apache.org/"
        Host = "www.apache.org"
        Default = $false
        Reason = "Apache HTTPS homepage adds a stable open-source project HTML control."
    },
    [pscustomobject]@{
        Url = "https://www.rfc-editor.org/"
        Host = "www.rfc-editor.org"
        Default = $false
        Reason = "RFC Editor HTTPS homepage adds a standards-document HTML control."
    },
    [pscustomobject]@{
        Url = "https://upload.wikimedia.org/wikipedia/commons/4/47/PNG_transparency_demonstration_1.png"
        Host = "upload.wikimedia.org"
        Default = $false
        Reason = "Direct Wikimedia Commons PNG control for binary MIME handling, response bounds, and non-HTML navigation capture."
    },
    [pscustomobject]@{
        Url = "https://upload.wikimedia.org/wikipedia/commons/a/a9/Example.jpg"
        Host = "upload.wikimedia.org"
        Default = $false
        Reason = "Direct Wikimedia Commons JPEG control for public HTTPS JPEG decoding, response bounds, and non-HTML navigation capture."
    }
)

function Get-NavigatorPublicHttpsReviewedAllowlistName {
    return $script:NavigatorPublicHttpsReviewedTargetAllowlistName
}

function Get-NavigatorPublicHttpsReviewedAllowlistVersion {
    return $script:NavigatorPublicHttpsReviewedTargetAllowlistVersion
}

function Get-NavigatorPublicHttpsReviewedTargets {
    return @(
        $script:NavigatorPublicHttpsReviewedTargets | ForEach-Object {
            [pscustomobject]@{
                Url = [string]$_.Url
                Host = [string]$_.Host
                Default = [bool]$_.Default
                Reason = [string]$_.Reason
            }
        }
    )
}

function Get-NavigatorPublicHttpsReviewedTargetUrls {
    return [string[]]@(Get-NavigatorPublicHttpsReviewedTargets | ForEach-Object { $_.Url })
}

function Get-NavigatorPublicHttpsDefaultTarget {
    $defaultTarget = Get-NavigatorPublicHttpsReviewedTargets | Where-Object { $_.Default } | Select-Object -First 1
    if ($null -eq $defaultTarget) {
        throw "Navigator reviewed public HTTPS target allowlist does not define a default target."
    }
    return [string]$defaultTarget.Url
}

function Find-NavigatorPublicHttpsReviewedTarget {
    param([AllowNull()][string]$TargetUrl)

    if ([string]::IsNullOrWhiteSpace($TargetUrl)) {
        return $null
    }

    $candidate = $TargetUrl.Trim()
    try {
        $candidate = ([System.Uri]$candidate).AbsoluteUri
    } catch {
    }

    foreach ($target in Get-NavigatorPublicHttpsReviewedTargets) {
        if ([string]::Equals($target.Url, $candidate, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $target
        }
    }

    return $null
}

function Test-NavigatorPublicHttpsReviewedTarget {
    param([AllowNull()][string]$TargetUrl)

    $match = Find-NavigatorPublicHttpsReviewedTarget -TargetUrl $TargetUrl
    return [pscustomobject]@{
        Approved = ($null -ne $match)
        Match = $match
        AllowlistName = Get-NavigatorPublicHttpsReviewedAllowlistName
        ApprovedTargets = [string[]]@(Get-NavigatorPublicHttpsReviewedTargetUrls)
    }
}
