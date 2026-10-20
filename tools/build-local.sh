#!/usr/bin/env bash
# build-local.sh — Local Docker build helper for OmniLaunch on Linux/macOS
set -euo pipefail

MODE="release"
VARIANT="sysmodule"

for arg in "$@"; do
    case "$arg" in
        --mode=*) MODE="${arg#*=}" ;;
        --variant=*) VARIANT="${arg#*=}" ;;
        release|debug) MODE="$arg" ;;
        sysmodule|homebrew) VARIANT="$arg" ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_IMAGE="switchu-build:latest"

if ! docker image inspect "$BUILD_IMAGE" >/dev/null 2>&1; then
    echo "Docker image $BUILD_IMAGE not found. Building image from tools/Dockerfile.build..."
    docker build -t "$BUILD_IMAGE" -f "$REPO_ROOT/tools/Dockerfile.build" "$REPO_ROOT"
fi

if [ -z "${OMNILAUNCH_AUTH_SECRET:-}" ]; then
    if [ -f "$HOME/.omnilaunch/auth-secret" ]; then
        export OMNILAUNCH_AUTH_SECRET="$(tr -d '\r\n' < "$HOME/.omnilaunch/auth-secret")"
    elif [ -f "$HOME/.switchu/client-key" ]; then
        export OMNILAUNCH_AUTH_SECRET="$(tr -d '\r\n' < "$HOME/.switchu/client-key")"
    else
        echo "Error: OMNILAUNCH_AUTH_SECRET is missing! Set it in your environment or place it in ~/.switchu/client-key" >&2
        exit 1
    fi
fi

mkdir -p "$REPO_ROOT/.logs"
LOG_FILE="$REPO_ROOT/.logs/build-local.log"

echo "Building $MODE/$VARIANT in Docker container ($BUILD_IMAGE)..."
docker run --rm \
    -v "$REPO_ROOT:/src" \
    -v switchu-xmake:/root/.xmake \
    -e OMNILAUNCH_AUTH_SECRET \
    -e OMNILAUNCH_AUTH_KID="${OMNILAUNCH_AUTH_KID:-k1}" \
    "$BUILD_IMAGE" \
    bash /src/tools/build-inside.sh "$MODE" "$VARIANT" off off off off off 2>&1 | tee "$LOG_FILE"

echo "Build complete. Artifacts placed under $REPO_ROOT/artifacts/"
