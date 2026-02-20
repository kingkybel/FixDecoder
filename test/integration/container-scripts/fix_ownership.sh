#!/usr/bin/env bash
set -euo pipefail

target_dir="${1:-/profiles}"
chown -R "${HOST_UID:?HOST_UID required}:${HOST_GID:?HOST_GID required}" "${target_dir}" || true
