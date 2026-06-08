#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

git submodule update --init --recursive || true

clone_if_missing() {
    local path="$1"
    local url="$2"

    if [ -d "$path/.git" ]; then
        return
    fi

    if [ -n "$(find "$path" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
        return
    fi

    rm -rf "$path"
    git clone --depth 1 "$url" "$path"
}

clone_if_missing ext/gtest https://github.com/google/googletest.git
clone_if_missing ext/gmock https://github.com/google/googlemock.git
clone_if_missing ext/gulrak-filesystem https://github.com/gulrak/filesystem.git
