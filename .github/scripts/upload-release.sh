#!/usr/bin/env bash
# Attach files to the draft pre-release for a tag, creating it if no other
# workflow has yet. The Windows and Linux release workflows both run on every
# tag and either may finish first; whichever loses the race to create the
# release uploads into the one the other made.
#
# usage: upload-release.sh TAG FILE...
# Needs GH_TOKEN and GITHUB_SHA (set by GitHub Actions).
set -euo pipefail

tag=$1
shift

for attempt in 1 2 3 4 5; do
    if gh release view "$tag" >/dev/null 2>&1; then
        gh release upload "$tag" --clobber "$@"
        exit 0
    fi
    if gh release create "$tag" "$@" \
        --title "Reone $tag" \
        --notes "Commit: $GITHUB_SHA" \
        --draft --prerelease; then
        exit 0
    fi
    echo "creating the release for $tag failed (attempt $attempt); checking whether it exists now"
    sleep $((attempt * 10))
done
exit 1
