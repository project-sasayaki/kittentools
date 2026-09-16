#!/bin/sh
OUTPUT="$1"
GIT="${2:-git}"
VERSION=$("$GIT" describe --always --dirty=-dirty 2>/dev/null || echo unknown)
echo "#define GIT_VERSION \"$VERSION\"" > "$OUTPUT"
