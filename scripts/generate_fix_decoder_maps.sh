#!/usr/bin/env bash
#
# Repository:  https://github.com/kingkybel/FixDecoder
# File Name:   scripts/generate_fix_decoder_maps.sh
# Description: Generate per-standard header maps from FIX tag number to decoder tag.
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

XML_DIR="${1:-data/quickfix}"
OUT_DIR="${2:-data/generated}"

if [[ ! -d "${XML_DIR}" ]]; then
  echo "Dictionary directory does not exist: ${XML_DIR}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

python3 "$(dirname "$0")/generate_fix_decoder_maps.py" "${XML_DIR}" "${OUT_DIR}"
