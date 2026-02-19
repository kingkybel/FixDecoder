#!/usr/bin/env python3
"""
Download market/participant reference data and generate realistic FIX samples.

Default behavior:
- Creates 200 messages for each version file in data/samples/valid/*.messages
- Writes results to data/samples/realistic/<VERSION>_realistic_200.messages
- Includes repeating-group heavy messages and a mix of short/medium/very-long payloads
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import random
import re
import urllib.request
from datetime import UTC, date, datetime, time, timedelta
from pathlib import Path

UA = "FixDecoderSampleBot/1.0 (contact: github@kybelksties.com)"


def fetch_text(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/json,text/plain,*/*"})
    with urllib.request.urlopen(req, timeout=40) as resp:
        return resp.read().decode("utf-8", errors="replace")


def parse_fix_line(line: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for token in line.strip().split("|"):
        if not token or "=" not in token:
            continue
        k, v = token.split("=", 1)
        out.append((k, v))
    return out


def render_fix_line(fields: list[tuple[str, str]]) -> str:
    return "".join(f"{k}={v}|" for k, v in fields)


def set_or_add(fields: list[tuple[str, str]], tag: str, value: str) -> None:
    for i, (k, _) in enumerate(fields):
        if k == tag:
            fields[i] = (tag, value)
            return
    fields.append((tag, value))


def first_value(fields: list[tuple[str, str]], tag: str, default: str = "") -> str:
    for k, v in fields:
        if k == tag:
            return v
    return default


def sanitize_party(text: str, max_len: int = 12) -> str:
    clean = re.sub(r"[^A-Z0-9]", "", text.upper())
    if not clean:
        clean = "PARTY"
    return clean[:max_len]


def choose_unique(items: list[str], count: int) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for item in items:
        if item in seen:
            continue
        seen.add(item)
        out.append(item)
        if len(out) >= count:
            break
    return out


def load_reference_data() -> dict:
    # Equities + participant candidates from Nasdaq directories.
    all_rows: list[dict[str, str]] = []
    for url in (
        "https://www.nasdaqtrader.com/dynamic/SymDir/nasdaqlisted.txt",
        "https://www.nasdaqtrader.com/dynamic/SymDir/otherlisted.txt",
    ):
        text = fetch_text(url)
        reader = csv.DictReader(io.StringIO(text), delimiter="|")
        for row in reader:
            symbol = (row.get("Symbol") or row.get("ACT Symbol") or "").strip()
            name = (row.get("Security Name") or "").strip()
            etf = (row.get("ETF") or "N").strip()
            if not symbol or etf == "Y" or "File Creation Time" in symbol:
                continue
            all_rows.append({"symbol": symbol, "name": name})

    equities = []
    for pair in choose_unique([f"{r['symbol']}|{r['name']}" for r in all_rows], 120):
        s, n = pair.split("|", 1)
        equities.append({"symbol": s, "name": n})

    banks = choose_unique(
        [
            r["name"]
            for r in all_rows
            if re.search(r"\b(BANK|BANCORP|FINANCIAL|TRUST|HOLDINGS)\b", r["name"], re.IGNORECASE)
        ],
        40,
    )
    funds = choose_unique(
        [
            r["name"]
            for r in all_rows
            if re.search(r"\b(FUND|CAPITAL|ASSET|ADVIS|MANAGEMENT|PARTNERS)\b", r["name"], re.IGNORECASE)
        ],
        40,
    )
    companies = choose_unique(
        [
            r["name"]
            for r in all_rows
            if not re.search(r"\b(FUND|CAPITAL|ASSET|ADVIS|MANAGEMENT|BANK|BANCORP|TRUST)\b", r["name"], re.IGNORECASE)
        ],
        40,
    )

    # FX universe from ECB headers.
    ecb = fetch_text("https://www.ecb.europa.eu/stats/eurofxref/eurofxref-hist.csv")
    header = ecb.splitlines()[0].split(",")
    majors = [c for c in ["USD", "JPY", "GBP", "CHF", "CAD", "AUD", "NZD", "CNY", "NOK", "SEK"] if c in header]
    fx_pairs = [f"EUR/{c}" for c in majors] + ["USD/JPY", "GBP/USD", "USD/CHF", "AUD/USD"]
    fx_pairs = choose_unique(fx_pairs, 16)

    # Treasury instruments (CUSIP based).
    treas_url = (
        "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v1/debt/mspd/mspd_table_5"
        "?filter=record_date:gte:2025-01-01&sort=-record_date&page%5Bsize%5D=60"
    )
    treas = json.loads(fetch_text(treas_url))
    bonds = []
    for row in treas.get("data", []):
        cusip = (row.get("cusip") or "").strip()
        cls = (row.get("security_class1_desc") or "").strip()
        if not cusip or not cls:
            continue
        bonds.append(
            {
                "cusip": cusip,
                "class": cls,
                "maturity": (row.get("maturity_date") or "").replace("-", ""),
                "coupon": (row.get("interest_rate_pct") or ""),
            }
        )
    bonds = bonds[:30]

    # Repo/reference benchmarks.
    nyfed = json.loads(fetch_text("https://markets.newyorkfed.org/api/rates/all/latest.json"))
    repo = []
    for row in nyfed.get("refRates", []):
        t = (row.get("type") or "").strip()
        if t in {"SOFR", "BGCR", "TGCR", "EFFR", "SOFRAI"}:
            repo.append(t)
    repo = choose_unique(repo, 8)

    # Futures market codes.
    cftc = fetch_text("https://www.cftc.gov/dea/newcot/FinFutWk.txt")
    futures = []
    for row in csv.reader(io.StringIO(cftc)):
        if not row or row[0].startswith("Market and Exchange Names"):
            continue
        market = row[0].strip()
        code = row[3].strip() if len(row) > 3 else ""
        if not code or "CHICAGO" not in market.upper():
            continue
        futures.append(f"{code}:{market}")
    futures = choose_unique(futures, 24)

    return {
        "equities": equities,
        "participants": {"banks": banks, "funds": funds, "companies": companies},
        "fx_pairs": fx_pairs,
        "bonds": bonds,
        "repo": repo,
        "futures": futures,
        "sources": {
            "nasdaq_listed": "https://www.nasdaqtrader.com/dynamic/SymDir/nasdaqlisted.txt",
            "nasdaq_other": "https://www.nasdaqtrader.com/dynamic/SymDir/otherlisted.txt",
            "ecb_fx": "https://www.ecb.europa.eu/stats/eurofxref/eurofxref-hist.csv",
            "us_treasury": "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v1/debt/mspd/mspd_table_5",
            "nyfed_rates": "https://markets.newyorkfed.org/api/rates/all/latest.json",
            "cftc_fin_fut": "https://www.cftc.gov/dea/newcot/FinFutWk.txt",
        },
    }


def load_base_version_messages(base_samples_dir: Path) -> dict[str, list[list[tuple[str, str]]]]:
    out: dict[str, list[list[tuple[str, str]]]] = {}
    for path in sorted(base_samples_dir.glob("FIX*.messages")):
        lines = [ln.strip() for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip()]
        parsed = [parse_fix_line(ln) for ln in lines]
        if parsed:
            out[path.stem] = parsed
    return out


def payload_for_index(i: int) -> str:
    short = "INFO-" + str(i)
    medium = "ALERT-" + ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 12)
    very_long = "RISK-" + ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 110)
    if i % 40 == 0:
        return very_long
    if i % 7 == 0:
        return medium
    return short


def msg_time(seq: int) -> str:
    t = datetime.combine(date(2026, 2, 19), time(12, 0, 0), tzinfo=UTC) + timedelta(seconds=seq)
    return t.strftime("%Y%m%d-%H:%M:%S.000")


def mutate_template(
    tpl: list[tuple[str, str]],
    version: str,
    i: int,
    seq: int,
    ref: dict,
) -> list[tuple[str, str]]:
    rng = random.Random(1000 + i)
    fields = [(k, v) for (k, v) in tpl]

    banks = ref["participants"]["banks"] or ["GLOBAL BANK PLC"]
    funds = ref["participants"]["funds"] or ["ALPHA CAPITAL MGMT"]
    companies = ref["participants"]["companies"] or ["ACME INDUSTRIES"]

    sender = sanitize_party(funds[i % len(funds)])
    target = sanitize_party(banks[(i + 3) % len(banks)])

    set_or_add(fields, "34", str(seq))
    set_or_add(fields, "49", sender)
    set_or_add(fields, "56", target)
    set_or_add(fields, "52", msg_time(seq))

    # Keep transport mapping for FIX.5.x under FIXT.1.1
    if version == "FIX50":
        set_or_add(fields, "8", "FIXT.1.1")
        set_or_add(fields, "1128", "7")
        if first_value(fields, "35") == "A":
            set_or_add(fields, "1137", "7")
    elif version == "FIX50SP1":
        set_or_add(fields, "8", "FIXT.1.1")
        set_or_add(fields, "1128", "8")
        if first_value(fields, "35") == "A":
            set_or_add(fields, "1137", "8")
    elif version == "FIX50SP2":
        set_or_add(fields, "8", "FIXT.1.1")
        set_or_add(fields, "1128", "9")
        if first_value(fields, "35") == "A":
            set_or_add(fields, "1137", "9")

    # Replace instrument fields with real references.
    if any(k == "55" for k, _ in fields):
        mode = i % 5
        if mode == 0 and ref["equities"]:
            eq = ref["equities"][i % len(ref["equities"])]
            set_or_add(fields, "55", eq["symbol"])
            set_or_add(fields, "167", "CS")
        elif mode == 1 and ref["fx_pairs"]:
            set_or_add(fields, "55", ref["fx_pairs"][i % len(ref["fx_pairs"])] )
            set_or_add(fields, "167", "FOR")
            set_or_add(fields, "15", "USD")
        elif mode == 2 and ref["bonds"]:
            b = ref["bonds"][i % len(ref["bonds"])]
            set_or_add(fields, "55", b["cusip"])
            set_or_add(fields, "48", b["cusip"])
            set_or_add(fields, "22", "1")
            set_or_add(fields, "167", "TBOND")
            if b["maturity"]:
                set_or_add(fields, "541", b["maturity"])
        elif mode == 3 and ref["futures"]:
            code = ref["futures"][i % len(ref["futures"])] .split(":", 1)[0]
            set_or_add(fields, "55", f"FUT{code}")
            set_or_add(fields, "167", "FUT")
            set_or_add(fields, "207", "CME")
            set_or_add(fields, "200", "202603")
        elif ref["repo"]:
            set_or_add(fields, "55", ref["repo"][i % len(ref["repo"])] )
            set_or_add(fields, "167", "REPO")
            set_or_add(fields, "15", "USD")

    if any(k == "11" for k, _ in fields):
        set_or_add(fields, "11", f"{version}-ORD-{i+1:05d}")

    if any(k == "38" for k, _ in fields):
        set_or_add(fields, "38", str(100 + (i % 25) * 25))

    if any(k == "44" for k, _ in fields):
        set_or_add(fields, "44", f"{20 + (i % 70) * 1.37:.2f}")

    payload = payload_for_index(i)
    if any(k == "112" for k, _ in fields):
        set_or_add(fields, "112", payload)
    else:
        set_or_add(fields, "58", payload)

    if i % 9 == 0:
        trader = sanitize_party(companies[i % len(companies)])
        broker = sanitize_party(banks[(i + 5) % len(banks)])
        alloc_a = sanitize_party(funds[(i + 1) % len(funds)])
        alloc_b = sanitize_party(funds[(i + 2) % len(funds)])
        # repeating parties/allocation groups
        fields.extend(
            [
                ("453", "2"),
                ("448", trader),
                ("447", "D"),
                ("452", "12"),
                ("448", broker),
                ("447", "D"),
                ("452", "1"),
                ("78", "2"),
                ("79", alloc_a),
                ("80", "60"),
                ("79", alloc_b),
                ("80", "40"),
            ]
        )

    return fields


def build_group_heavy_message(version: str, seq: int, i: int, ref: dict) -> str:
    # Intentionally group-rich messages for parser stress.
    begin = {
        "FIX40": "FIX.4.0",
        "FIX41": "FIX.4.1",
        "FIX42": "FIX.4.2",
        "FIX43": "FIX.4.3",
        "FIX44": "FIX.4.4",
        "FIX50": "FIXT.1.1",
        "FIX50SP1": "FIXT.1.1",
        "FIX50SP2": "FIXT.1.1",
        "FIXT11": "FIXT.1.1",
    }[version]

    fields: list[tuple[str, str]] = [
        ("8", begin),
        ("35", "D"),
        ("34", str(seq)),
        ("49", sanitize_party(ref["participants"]["funds"][i % len(ref["participants"]["funds"])] if ref["participants"]["funds"] else "ALPHAFUND")),
        ("52", msg_time(seq)),
        ("56", sanitize_party(ref["participants"]["banks"][i % len(ref["participants"]["banks"])] if ref["participants"]["banks"] else "GLOBALBANK")),
        ("11", f"{version}-GRP-{i+1:05d}"),
        ("21", "1"),
        ("55", ref["equities"][i % len(ref["equities"])] ["symbol"] if ref["equities"] else "IBM"),
        ("167", "CS"),
        ("54", "1" if i % 2 == 0 else "2"),
        ("38", str(1000 + i * 3)),
        ("40", "2"),
        ("44", f"{75 + (i % 40) * 0.73:.2f}"),
        ("60", msg_time(seq + 30)),
        ("58", payload_for_index(i)),
        ("453", "3"),
        ("448", sanitize_party(ref["participants"]["companies"][i % len(ref["participants"]["companies"])] if ref["participants"]["companies"] else "ACME")),
        ("447", "D"),
        ("452", "12"),
        ("448", sanitize_party(ref["participants"]["banks"][(i + 1) % len(ref["participants"]["banks"])] if ref["participants"]["banks"] else "GLOBALBANK")),
        ("447", "D"),
        ("452", "1"),
        ("448", sanitize_party(ref["participants"]["funds"][(i + 2) % len(ref["participants"]["funds"])] if ref["participants"]["funds"] else "ALPHAFUND")),
        ("447", "D"),
        ("452", "24"),
        ("78", "2"),
        ("79", sanitize_party(ref["participants"]["funds"][(i + 3) % len(ref["participants"]["funds"])] if ref["participants"]["funds"] else "FUND1")),
        ("80", "700"),
        ("79", sanitize_party(ref["participants"]["funds"][(i + 4) % len(ref["participants"]["funds"])] if ref["participants"]["funds"] else "FUND2")),
        ("80", "300"),
        ("146", "3"),
        ("55", ref["fx_pairs"][i % len(ref["fx_pairs"])] if ref["fx_pairs"] else "EUR/USD"),
        ("167", "FOR"),
        ("55", ref["repo"][i % len(ref["repo"])] if ref["repo"] else "SOFR"),
        ("167", "REPO"),
        ("55", ref["bonds"][i % len(ref["bonds"])] ["cusip"] if ref["bonds"] else "9128202A4"),
        ("48", ref["bonds"][i % len(ref["bonds"])] ["cusip"] if ref["bonds"] else "9128202A4"),
        ("22", "1"),
        ("167", "TBOND"),
        ("268", "2"),
        ("269", "0"),
        ("270", "101.10"),
        ("271", "500"),
        ("269", "1"),
        ("270", "101.20"),
        ("271", "500"),
        ("555", "2"),
        ("600", "FUTLEG1"),
        ("624", "1"),
        ("687", "10"),
        ("566", "100.10"),
        ("600", "FUTLEG2"),
        ("624", "2"),
        ("687", "10"),
        ("566", "99.90"),
    ]

    if version == "FIX50":
        fields.insert(1, ("1128", "7"))
    elif version == "FIX50SP1":
        fields.insert(1, ("1128", "8"))
    elif version == "FIX50SP2":
        fields.insert(1, ("1128", "9"))

    return render_fix_line(fields)


def generate_for_version(version: str, templates: list[list[tuple[str, str]]], count: int, ref: dict) -> list[str]:
    out: list[str] = []
    seq = 1
    for i in range(count):
        if i % 10 == 0:
            out.append(build_group_heavy_message(version, seq, i, ref))
            seq += 1
            continue

        tpl = templates[i % len(templates)]
        mutated = mutate_template(tpl, version, i, seq, ref)
        out.append(render_fix_line(mutated))
        seq += 1
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate realistic FIX samples for every version")
    parser.add_argument("--base-samples-dir", default="data/samples/valid", help="directory containing FIX*.messages templates")
    parser.add_argument("--reference-dir", default="data/samples/reference", help="directory to persist downloaded reference data")
    parser.add_argument("--out-dir", default="data/samples/realistic", help="output directory")
    parser.add_argument("--count-per-version", type=int, default=200, help="messages per FIX version")
    args = parser.parse_args()

    base_dir = Path(args.base_samples_dir)
    reference_dir = Path(args.reference_dir)
    out_dir = Path(args.out_dir)
    reference_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    base = load_base_version_messages(base_dir)
    if not base:
        raise SystemExit(f"No FIX*.messages files found in {base_dir}")

    ref = load_reference_data()
    ref["generated_at"] = datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")

    ref_path = reference_dir / "realistic_reference_data.json"
    ref_path.write_text(json.dumps(ref, indent=2), encoding="utf-8")

    generated = []
    for version, templates in sorted(base.items()):
        messages = generate_for_version(version, templates, args.count_per_version, ref)
        out_file = out_dir / f"{version}_realistic_{args.count_per_version}.messages"
        out_file.write_text("\n".join(messages) + "\n", encoding="utf-8")
        generated.append((version, out_file, len(messages)))

    print(f"Wrote reference data: {ref_path}")
    for version, path, cnt in generated:
        print(f"Wrote {version:8s}: {path} ({cnt} messages)")
    print("Includes varying message lengths with periodic very-long payloads and repeating-group heavy records.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
