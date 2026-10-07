# Build dependencies

## PacMan (v0.3)

The AMD64 Server build depends on the external PacMan Git source repository.
The exact v0.3 source revision is recorded in
[`scripts/pacman-revision.txt`](../scripts/pacman-revision.txt).

`scripts/build-pacman-package.ps1` obtains that commit from the local sibling
repository (`D:\dev\pacman\guidexos` by default, or
`GUIDEXOS_PACMAN_REPOSITORY`) using `git archive`, then compiles and packages
the extracted committed snapshot under the Server build directory. It never
compiles from the sibling checkout's working tree and does not require the
sibling HEAD to match the pin. Dirty or newer sibling files therefore do not
affect a Server production build. The required commit must already exist in
the local Git object database; ordinary builds do not fetch it from a network
remote. If it is missing, the build stops with the required SHA and the
repository path.

To intentionally advance PacMan for a later Server phase, first establish and
test the desired PacMan commit against the Server integration, then update the
tracked SHA in `scripts/pacman-revision.txt` in that same Server change. Review
the PacMan integration changes and package/build evidence together. A newer
PacMan checkout alone never advances the Server dependency.

For v0.3, the pin is the last committed PacMan source before audio integration.
The later audio feature remains a separate Server feature phase.
