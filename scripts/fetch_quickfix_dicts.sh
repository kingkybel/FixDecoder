#!/usr/bin/env bash
#
# Repository:  https://github.com/kingkybel/FixDecoder
# File Name:   scripts/fetch_quickfix_dicts.sh
# Description: Download QuickFIX XML dictionaries.
#
# Copyright (C) 2026 Dieter J Kybelksties <github@kybelksties.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
# @date: 2026-02-11
# @author: Dieter J Kybelksties
#

set -euo pipefail

DEST=${1:-data/quickfix}
BASE_URL=${BASE_URL:-https://raw.githubusercontent.com/quickfix/quickfix/master/spec}
BASE_URL=${BASE_URL%/}

mkdir -p "$DEST"

if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
  echo "Neither curl nor wget found. Install one to download dictionaries." >&2
  exit 1
fi

discover_remote_files() {
  local discovery_url="${DISCOVERY_URL:-}"
  local index

  if [[ -z "${discovery_url}" ]] && [[ "${BASE_URL}" == "https://raw.githubusercontent.com/quickfix/quickfix/master/spec" ]]; then
    discovery_url="https://api.github.com/repos/quickfix/quickfix/contents/spec"
  fi

  if [[ -n "${discovery_url}" ]]; then
    if command -v curl >/dev/null 2>&1; then
      index=$(curl -fsSL "${discovery_url}" || true)
    else
      index=$(wget -qO- "${discovery_url}" || true)
    fi
    if [[ -z "${index}" ]]; then
      return 1
    fi

    mapfile -t files < <(
      printf '%s\n' "${index}" \
        | grep -oE '"name"[[:space:]]*:[[:space:]]*"FIX[A-Za-z0-9]+\.xml"' \
        | sed -E 's/.*"FIX/FIX/; s/"$//' \
        | sort -u
    )
  else
    if command -v curl >/dev/null 2>&1; then
      index=$(curl -fsSL "${BASE_URL}/" || true)
    else
      index=$(wget -qO- "${BASE_URL}/" || true)
    fi
    if [[ -z "${index}" ]]; then
      return 1
    fi

    mapfile -t files < <(
      printf '%s\n' "${index}" \
        | grep -oE 'FIX[A-Za-z0-9]+\.xml' \
        | sort -u
    )
  fi

  if [[ ${#files[@]} -eq 0 ]]; then
    return 1
  fi

  return 0
}

declare -a files=()

if ! discover_remote_files; then
  if compgen -G "${DEST}/FIX*.xml" >/dev/null 2>&1; then
    mapfile -t files < <(find "${DEST}" -maxdepth 1 -type f -name 'FIX*.xml' -printf '%f\n' | sort -u)
  fi
fi

if [[ ${#files[@]} -eq 0 ]]; then
  files=(
    FIX40.xml
    FIX41.xml
    FIX42.xml
    FIX43.xml
    FIX44.xml
    FIX50.xml
    FIX50SP1.xml
    FIX50SP2.xml
    FIXT11.xml
  )
fi

echo "Dictionary file set (${#files[@]}): ${files[*]}"

for f in "${files[@]}"; do
  url="$BASE_URL/$f"
  dest_file="$DEST/$f"

  if command -v curl >/dev/null 2>&1; then
    if [[ -f "$dest_file" ]]; then
      echo "Checking for updates: $url"
      curl -fsSL -z "$dest_file" -o "$dest_file" "$url"
    else
      echo "Downloading $url"
      curl -fsSL -o "$dest_file" "$url"
    fi
  else
    # wget handles timestamping with -N and saves to DEST with -P
    echo "Checking for updates: $url"
    wget -qN -P "$DEST" "$url"
  fi
done
