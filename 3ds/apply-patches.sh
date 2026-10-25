#!/bin/sh
# Applies this repository's patches to the git submodules. Run once after
# cloning (the CI workflow runs it before building). Safe to run again.
set -e
cd "$(dirname "$0")/.."
if ! grep -q LiSendExecServerCmd third_party/moonlight-common-c/src/Limelight.h; then
    echo "Patching moonlight-common-c: Apollo server commands"
    git -C third_party/moonlight-common-c apply --whitespace=nowarn \
        "$PWD/patches/moonlight-common-c/0001-apollo-server-commands.patch"
fi
