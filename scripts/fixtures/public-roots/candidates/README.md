# Shipped-Root Candidate Metadata

This directory is reserved for commit-safe Navigator shipped-root candidate metadata and linked review artifacts.

Rules:

- Do not store PEM bundles here.
- Do not store private keys here.
- Do not store secret local paths here.
- Candidate metadata may record candidate IDs, rotation IDs, SHA-256 digests, root counts, sizes, source labels, and reviewed public HTTPS evidence links.
- Generators should default to `logs/` for local preparation and promotion output.
- Only copy reviewed metadata or evidence-link JSON here when you intentionally want a commit-safe record in the repository.

The helper scripts for this workflow are:

- `scripts/prepare-navigator-shipped-root-candidate.ps1`
- `scripts/promote-navigator-public-https-evidence.ps1`
