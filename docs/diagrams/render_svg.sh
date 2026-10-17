#!/bin/bash
# Export docs/diagrams/*.excalidraw to SVG in a throw-away Docker container
# (Playwright image with Chromium; Excalidraw 0.17.6 loaded from unpkg).
#   bash docs/diagrams/render_svg.sh        (in WSL or Linux, needs Docker and internet)
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
docker run --rm -v "$DIR":/diagrams -w /tmp/render \
    mcr.microsoft.com/playwright:v1.48.2-noble \
    sh -c 'npm init -y >/dev/null && npm install --silent playwright@1.48.2 >/dev/null \
           && cp /diagrams/render.mjs . && node render.mjs /diagrams'
