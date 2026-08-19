Set-StrictMode -Version Latest

$script:NavigatorPublicHttpsReviewedTargetAllowlistName = "guidexos-reviewed-public-https-v0.5"
$script:NavigatorPublicHttpsReviewedTargetAllowlistVersion = "v0.5"
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
