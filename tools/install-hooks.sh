#!/bin/sh
# Install the repo's git hooks (currently: pre-commit).
#
# Run from anywhere inside the repository:
#   sh tools/install-hooks.sh
#
# Copies tools/pre-commit -> .git/hooks/pre-commit and marks it executable.
# Re-running is safe (idempotent); it simply overwrites the hook.

set -e

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

hooks_dir="$(git rev-parse --git-path hooks)"
mkdir -p "$hooks_dir"

src="tools/pre-commit"
dst="$hooks_dir/pre-commit"

if [ ! -f "$src" ]; then
    echo "ERROR: $src not found (run from the repo root)." >&2
    exit 1
fi

cp "$src" "$dst"
chmod +x "$dst"

echo "Installed: $dst"
echo "The test suite now runs automatically before every commit."
echo "Bypass a single commit with: git commit --no-verify"
