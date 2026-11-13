#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Applies the patches that `series` lists for one tree to a checkout of that tree, in order.
# Fails when a patch in this directory is not listed exactly once, or a listed one is missing.
#
#   chibios/patches/apply-series.sh <checkout> <tree>
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
checkout="${1:?the checkout to patch}"
tree="${2:?the tree named in the series file}"

declare -A listed=()
selected=()
mapfile -t lines < "$here/series"
for line in "${lines[@]}"; do
    case "$line" in
        '' | '#'*) continue ;;
    esac
    read -r line_tree patch extra <<< "$line"
    if [[ -z $patch || -n $extra ]]; then
        echo "error: series line is not '<tree> <patch>': $line" >&2
        exit 1
    fi
    if [[ -n ${listed[$patch]:-} ]]; then
        echo "error: $patch is listed more than once in the series" >&2
        exit 1
    fi
    if [[ ! -f $here/$patch ]]; then
        echo "error: the series lists $patch, which is not in $here" >&2
        exit 1
    fi
    listed[$patch]=1
    if [[ $line_tree == "$tree" ]]; then
        selected+=("$patch")
    fi
done

for file in "$here"/*.patch; do
    if [[ -z ${listed[$(basename "$file")]:-} ]]; then
        echo "error: $(basename "$file") is in $here but not in the series" >&2
        exit 1
    fi
done

if (( ${#selected[@]} == 0 )); then
    echo "error: the series lists no patch for the tree '$tree'" >&2
    exit 1
fi

# Its own repository, so that git apply takes paths from this checkout even inside another one.
git -C "$checkout" init -q

for patch in "${selected[@]}"; do
    git -C "$checkout" apply --whitespace=nowarn "$here/$patch"
done
