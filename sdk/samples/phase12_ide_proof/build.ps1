param(
    [string]$Configuration = 'Debug'
)

# The Phase 12 proof intentionally uses the resident guideXOS compiler from
# Developer Studio; this script documents the project build entry point for
# hosted tooling without compiling the target on the host.
Write-Host "Build through guideXOS Developer Studio ($Configuration)."
