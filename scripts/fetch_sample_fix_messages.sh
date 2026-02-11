#!/usr/bin/env bash
#
# Repository:  https://github.com/kingkybel/FixDecoder
# File Name:   scripts/fetch_sample_fix_messages.sh
# Description: Download and prepare sample FIX messages for tests.
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

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DICT_DIR="${1:-${ROOT_DIR}/data/quickfix}"
OUT_DIR="${ROOT_DIR}/data/samples/valid"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

BASE_URL="https://raw.githubusercontent.com/quickfix/quickfix/master/test/definitions/server"

mkdir -p "${OUT_DIR}"
rm -f "${OUT_DIR}"/*.messages

if [[ ! -d "${DICT_DIR}" ]]; then
    echo "Dictionary directory does not exist: ${DICT_DIR}" >&2
    exit 1
fi

download_and_extract() {
    local server_dir="$1"
    local out_file="$2"
    local appl_ver_id="${3:-}"
    local source_file="${TMP_DIR}/${server_dir}_8_AdminAndApplicationMessages.def"

    if ! curl -fsSL "${BASE_URL}/${server_dir}/8_AdminAndApplicationMessages.def" -o "${source_file}" 2>/dev/null; then
        echo "Skipping ${out_file}: no upstream sample definition for ${server_dir}" >&2
        return 0
    fi

    awk '
        BEGIN { soh = sprintf("%c", 1) }
        /^I8=/ {
            line = substr($0, 2)
            gsub(soh, "|", line)
            gsub("<TIME>", "20240210-12:00:00.000", line)
            if (line !~ /\|$/) {
                line = line "|"
            }
            print line
        }
    ' "${source_file}" > "${TMP_DIR}/${out_file}.raw"

    if [[ -n "${appl_ver_id}" ]]; then
        awk -v appl_ver_id="${appl_ver_id}" '
            {
                if ($0 !~ /\|1128=/) {
                    sub(/\|35=/, "|1128=" appl_ver_id "|35=")
                }
                print
            }
        ' "${TMP_DIR}/${out_file}.raw" > "${OUT_DIR}/${out_file}"
    else
        cp "${TMP_DIR}/${out_file}.raw" "${OUT_DIR}/${out_file}"
    fi
}

declare -A appl_ver_by_stem=(
    ["FIX50"]="7"
    ["FIX50SP1"]="8"
    ["FIX50SP2"]="9"
)

mapfile -t dict_files < <(find "${DICT_DIR}" -maxdepth 1 -type f -name 'FIX*.xml' -printf '%f\n' | sort -u)

if [[ ${#dict_files[@]} -eq 0 ]]; then
    echo "No FIX*.xml dictionaries found in ${DICT_DIR}" >&2
    exit 1
fi

generated_count=0
for dict_file in "${dict_files[@]}"; do
    stem="${dict_file%.xml}"
    server_dir="$(echo "${stem}" | tr '[:upper:]' '[:lower:]')"
    out_file="${stem}.messages"
    appl_ver_id="${appl_ver_by_stem[${stem}]:-}"
    download_and_extract "${server_dir}" "${out_file}" "${appl_ver_id}"
    if [[ -f "${OUT_DIR}/${out_file}" ]]; then
        generated_count=$((generated_count + 1))
    fi
done

awk '
    /8=FIXT\.1\.1\|/ &&
    /(\|35=A\||\|35=1\||\|35=2\||\|35=4\||\|35=5\|)/ {
        gsub(/\|1128=[^|]*\|/, "|")
        gsub(/\|1137=[^|]*\|/, "|")
        print
    }
' "${OUT_DIR}"/FIX50*.messages 2>/dev/null | awk '!seen[$0]++' > "${OUT_DIR}/FIXT11.messages" || true

if [[ ! -s "${OUT_DIR}/FIXT11.messages" ]]; then
    rm -f "${OUT_DIR}/FIXT11.messages"
fi

for f in "${OUT_DIR}"/*.messages; do
    [[ -e "${f}" ]] || continue
    awk '!seen[$0]++' "${f}" > "${TMP_DIR}/dedup.messages"
    mv "${TMP_DIR}/dedup.messages" "${f}"
done

echo "Wrote ${generated_count} sample message file(s) to ${OUT_DIR}"
