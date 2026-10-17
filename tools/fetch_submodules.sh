#!/bin/sh
# Fetch git submodules as pinned-commit tarballs from codeload.github.com (much faster than git here).
# Usage: fetch_submodules.sh <repo dir> <submodule path>...
repo="$1"; shift
cd "$repo" || exit 1
for p in "$@"; do
    sha=$(git ls-tree HEAD "$p" | awk '{print $3}')
    url=$(git config -f .gitmodules "submodule.$p.url")
    slug=$(echo "$url" | sed -E 's#https://github.com/##; s#\.git$##')
    if [ -n "$(ls -A "$p" 2>/dev/null)" ]; then echo "skip $p (not empty)"; continue; fi
    mkdir -p "$p"
    if curl -sfL --retry 3 --max-time 900 "https://codeload.github.com/$slug/tar.gz/$sha" | tar xz --strip-components=1 -C "$p"; then
        echo "ok   $p @ ${sha%${sha#???????}}"
    else
        echo "FAIL $p"
    fi
done
