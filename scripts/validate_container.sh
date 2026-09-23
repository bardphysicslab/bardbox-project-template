#!/usr/bin/env bash
# Validate one committed revision without mounting host files into the container.
set -euo pipefail
if [ "$#" -gt 1 ]; then
  echo 'Usage: bash scripts/validate_container.sh [revision]' >&2
  exit 2
fi
validation_root="$(git rev-parse --show-toplevel)"
cd "$validation_root"
validation_revision="$(git rev-parse --verify --end-of-options "${1:-HEAD}^{commit}")"
if [ -n "$(git status --porcelain)" ]; then
  echo 'Notice: local changes are excluded; only the printed commit is validated.' >&2
fi
printf 'Source commit: %s\n' "$validation_revision"
validation_tmp="$(mktemp -d)"
trap 'rm -rf "$validation_tmp"' EXIT
# The build context contains only committed development-container files.
git archive "$validation_revision" .devcontainer |
  docker build --iidfile "$validation_tmp/image-id" -f .devcontainer/Dockerfile -
validation_image="$(cat "$validation_tmp/image-id")"
docker image inspect "$validation_image" --format 'Image: {{.Id}} Platform: {{.Os}}/{{.Architecture}}'
# No bind mounts, host environment, socket, SSH forwarding, ports or hardware.
git archive "$validation_revision" |
  docker run --rm -i -e PYTHONDONTWRITEBYTECODE=1 "$validation_image" sh -ec '
    mkdir -p /tmp/bardbox-validation
    tar -xf - -C /tmp/bardbox-validation
    cd /tmp/bardbox-validation
    python -m venv /tmp/bardbox-test-env
    . /tmp/bardbox-test-env/bin/activate
    python --version
    python -m pip install --disable-pip-version-check -q -r requirements-dev.txt
    python -m pip list --format=json
    python -m pytest -p no:cacheprovider
  '
