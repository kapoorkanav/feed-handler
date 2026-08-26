#!/usr/bin/env bash
# Build (if needed) and drop into the dev container with the repo mounted at /work.
set -euo pipefail

IMAGE=feed-handler-dev

docker build -t "$IMAGE" .

docker run --rm -it \
    --privileged \
    --cap-add=NET_ADMIN \
    -v "$(pwd)":/work \
    -w /work \
    "$IMAGE" \
    bash
