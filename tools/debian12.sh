#!/bin/sh
# Run a command inside the Debian 12 build root (ext/debian12/root) with bubblewrap, rootless.
# The project is mounted at /src. Usage: tools/debian12.sh <command...>
ROOT=$(cd "$(dirname "$0")/.." && pwd)
R="$ROOT/ext/debian12/root"
exec bwrap --unshare-user --uid 0 --gid 0 --unshare-pid --unshare-ipc \
    --bind "$R" / --dev /dev --proc /proc --tmpfs /tmp \
    --ro-bind /etc/resolv.conf /etc/resolv.conf \
    --bind "$ROOT" /src --chdir /src \
    --setenv HOME /root --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    --setenv DEBIAN_FRONTEND noninteractive \
    "$@"
