#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FUGAZI_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKSPACE="$(cd "$FUGAZI_DIR/.." && pwd)"   # siblings: Fugazi, Catastrophe, Jawaka
IMAGE="${MLP1_TOOLCHAIN_IMAGE:-ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:latest}"
echo "=== Building Fugazi for MLP1 (workspace: $WORKSPACE) ==="
docker run --rm -v "$WORKSPACE":/workspace -w /workspace/Fugazi "$IMAGE" make -C ports/mlp1
echo "=== Build complete: ports/mlp1/pak/bin/fugazi ==="
