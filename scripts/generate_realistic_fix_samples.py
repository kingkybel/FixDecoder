#!/usr/bin/env python3
"""
Download market/participant reference data and generate realistic FIX samples.

Default behavior per FIX version:
- 850 structurally valid messages
- 100 syntactically correct but semantically invalid messages
- 50 garbled messages

Outputs:
- <VERSION>_realistic_correct_<N>.messages
- <VERSION>_realistic_semantic_incorrect_<N>.messages
- <VERSION>_realistic_garbled_<N>.messages
- <VERSION>_realistic_<TOTAL>.messages (combined, kept for compatibility)
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import random
import re
import urllib.request
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import UTC, date, datetime, time, timedelta
from pathlib import Path

UA = "FixDecoderSampleBot/1.0 (contact: github@kybelksties.com)"


@dataclass
class Member:
    kind: str
    name: str
    required: bool
    children: list["Member"] = field(default_factory=list)


@dataclass
class DictionaryModel:
    field_numbers: dict[str, int]
    field_types: dict[str, str]
    messages: dict[str, list[Member]]
    components: dict[str, list[Member]]


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


def has_tag(fields: list[tuple[str, str]], tag: str) -> bool:
    return any(k == tag for k, _ in fields)


def remove_first_tag(fields: list[tuple[str, str]], tag: str) -> bool:
    for i, (k, _) in enumerate(fields):
        if k == tag:
            del fields[i]
            return True
    return False


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

    ecb = fetch_text("https://www.ecb.europa.eu/stats/eurofxref/eurofxref-hist.csv")
    header = ecb.splitlines()[0].split(",")
    majors = [c for c in ["USD", "JPY", "GBP", "CHF", "CAD", "AUD", "NZD", "CNY", "NOK", "SEK"] if c in header]
    fx_pairs = [f"EUR/{c}" for c in majors] + ["USD/JPY", "GBP/USD", "USD/CHF", "AUD/USD"]
    fx_pairs = choose_unique(fx_pairs, 16)

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

    nyfed = json.loads(fetch_text("https://markets.newyorkfed.org/api/rates/all/latest.json"))
    repo = []
    for row in nyfed.get("refRates", []):
        t = (row.get("type") or "").strip()
        if t in {"SOFR", "BGCR", "TGCR", "EFFR", "SOFRAI"}:
            repo.append(t)
    repo = choose_unique(repo, 8)

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


def parse_members(parent: ET.Element) -> list[Member]:
    out: list[Member] = []
    for child in parent:
        name = child.tag
        if name not in {"field", "component", "group"}:
            continue
        member_name = child.attrib.get("name", "")
        required = child.attrib.get("required", "N").upper().startswith("Y")
        children = parse_members(child) if name == "group" else []
        out.append(Member(kind=name, name=member_name, required=required, children=children))
    return out


def load_dictionary_model(dict_path: Path) -> DictionaryModel:
    root = ET.fromstring(dict_path.read_text(encoding="utf-8"))

    field_numbers: dict[str, int] = {}
    field_types: dict[str, str] = {}
    for f in root.findall("./fields/field"):
        name = f.attrib.get("name", "")
        number = f.attrib.get("number", "0")
        ftype = f.attrib.get("type", "STRING")
        if not name:
            continue
        try:
            field_numbers[name] = int(number)
        except ValueError:
            continue
        field_types[name] = ftype

    messages: dict[str, list[Member]] = {}
    for m in root.findall("./messages/message"):
        msg_type = m.attrib.get("msgtype", "")
        if not msg_type:
            continue
        messages[msg_type] = parse_members(m)

    components: dict[str, list[Member]] = {}
    for c in root.findall("./components/component"):
        cname = c.attrib.get("name", "")
        if cname:
            components[cname] = parse_members(c)

    return DictionaryModel(field_numbers=field_numbers, field_types=field_types, messages=messages, components=components)


def load_base_version_messages(base_samples_dir: Path) -> dict[str, list[list[tuple[str, str]]]]:
    out: dict[str, list[list[tuple[str, str]]]] = {}
    for path in sorted(base_samples_dir.glob("FIX*.messages")):
        lines = [ln.strip() for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip() and not ln.startswith("#")]
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


def first_member_tag(model: DictionaryModel, member: Member, seen_components: set[str] | None = None) -> int | None:
    seen_components = seen_components or set()
    if member.kind in {"field", "group"}:
        return model.field_numbers.get(member.name)
    if member.kind != "component":
        return None
    if member.name in seen_components:
        return None
    seen_components.add(member.name)
    members = model.components.get(member.name, [])
    for m in members:
        tag = first_member_tag(model, m, seen_components)
        if tag is not None:
            return tag
    return None


def collect_groups(model: DictionaryModel, members: list[Member], out: list[Member], seen_components: set[str] | None = None) -> None:
    seen_components = seen_components or set()
    for m in members:
        if m.kind == "group" and m.name in model.field_numbers and m.children:
            out.append(m)
            continue
        if m.kind == "component":
            if m.name in seen_components:
                continue
            seen_components.add(m.name)
            collect_groups(model, model.components.get(m.name, []), out, seen_components)


def collect_direct_groups(members: list[Member]) -> list[Member]:
    return [m for m in members if m.kind == "group"]


def collect_required_top_level_field_names(
    model: DictionaryModel, members: list[Member], out: list[str], seen_components: set[str] | None = None
) -> None:
    seen_components = seen_components or set()
    for m in members:
        if m.kind == "field" and m.required:
            out.append(m.name)
            continue
        if m.kind == "component" and m.required and m.name not in seen_components:
            seen_components.add(m.name)
            collect_required_top_level_field_names(model, model.components.get(m.name, []), out, seen_components)


def gen_field_value(model: DictionaryModel, field_name: str, ref: dict, seq: int, idx: int, rng: random.Random) -> str:
    funds = ref["participants"].get("funds") or ["ALPHA CAPITAL"]
    banks = ref["participants"].get("banks") or ["GLOBAL BANK"]
    comps = ref["participants"].get("companies") or ["ACME INC"]

    if field_name in {"PartyID", "AllocAccount", "ClientID", "OrderID", "ClOrdID", "OrigClOrdID", "ExecID"}:
        return sanitize_party(f"{funds[idx % len(funds)]}{seq}", 20)
    if field_name in {"PartyIDSource"}:
        return "D"
    if field_name in {"PartyRole"}:
        return str(rng.choice([1, 3, 12, 24]))
    if field_name in {"Side"}:
        return rng.choice(["1", "2"])
    if field_name in {"Price", "LastPx", "AvgPx", "StopPx"}:
        return f"{80 + (idx % 50) * 0.77:.2f}"
    if field_name in {"OrderQty", "LastQty", "LeavesQty", "CumQty", "AllocQty"}:
        return str(50 + (idx % 30) * 10)
    if field_name in {"Symbol"}:
        equities = ref.get("equities") or [{"symbol": "IBM"}]
        return equities[idx % len(equities)]["symbol"]
    if field_name in {"SecurityID"}:
        bonds = ref.get("bonds") or [{"cusip": "9128202A4"}]
        return bonds[idx % len(bonds)]["cusip"]
    if field_name in {"SecurityIDSource"}:
        return "1"
    if field_name in {"SecurityType"}:
        return rng.choice(["CS", "FOR", "TBOND", "FUT", "REPO"])
    if field_name in {"TransactTime", "SendingTime"}:
        return msg_time(seq)
    if field_name in {"Text", "TestReqID"}:
        return payload_for_index(idx)
    if field_name in {"SenderCompID"}:
        return sanitize_party(funds[idx % len(funds)])
    if field_name in {"TargetCompID"}:
        return sanitize_party(banks[idx % len(banks)])

    ftype = model.field_types.get(field_name, "STRING").upper()
    if ftype in {"INT", "SEQNUM", "LENGTH", "NUMINGROUP"}:
        return str(rng.randint(1, 100))
    if ftype in {"QTY", "PRICE", "PRICEOFFSET", "AMT", "PERCENTAGE", "DOUBLE", "FLOAT"}:
        return f"{rng.uniform(1.0, 250.0):.2f}"
    if ftype in {"BOOLEAN"}:
        return rng.choice(["Y", "N"])
    if ftype in {"CHAR"}:
        return rng.choice(["A", "B", "C", "D", "1", "2"])
    if ftype in {"UTCTIMESTAMP"}:
        return msg_time(seq)
    if ftype in {"UTCDATEONLY", "LOCALMKTDATE"}:
        return "20260219"
    if field_name.endswith("ID"):
        return sanitize_party(f"{comps[idx % len(comps)]}{seq}", 20)
    return sanitize_party(comps[idx % len(comps)], 16)


def build_group_block(
    model: DictionaryModel,
    group_member: Member,
    ref: dict,
    seq: int,
    idx: int,
    rng: random.Random,
    entry_count: int,
    semantic_invalid: bool,
) -> list[tuple[str, str]]:
    block: list[tuple[str, str]] = []
    group_tag = str(model.field_numbers[group_member.name])
    block.append((group_tag, str(entry_count)))

    for entry_idx in range(entry_count):
        for child in group_member.children:
            if child.kind != "field":
                continue
            tag = model.field_numbers.get(child.name)
            if tag is None:
                continue
            if semantic_invalid and entry_idx == entry_count - 1 and child.required and rng.random() < 0.5:
                # syntactically valid but semantically incomplete group entry
                semantic_invalid = False
                continue
            block.append((str(tag), gen_field_value(model, child.name, ref, seq + entry_idx, idx + entry_idx, rng)))
    return block


def insert_group_block_at_member_order(
    fields: list[tuple[str, str]],
    model: DictionaryModel,
    message_members: list[Member],
    group_member: Member,
    block: list[tuple[str, str]],
) -> None:
    group_index = -1
    group_tag = model.field_numbers.get(group_member.name)
    for i, member in enumerate(message_members):
        if member.kind == "group" and member.name == group_member.name:
            group_index = i
            break
        if group_tag is not None and member.kind == "component":
            first_tag = first_member_tag(model, member)
            if first_tag == group_tag:
                group_index = i
                break

    insert_at = len(fields)
    if group_index >= 0:
        for later in message_members[group_index + 1 :]:
            later_tag = first_member_tag(model, later)
            if later_tag is None:
                continue
            later_tag_str = str(later_tag)
            for idx, (tag, _) in enumerate(fields):
                if tag == later_tag_str:
                    insert_at = idx
                    break
            if insert_at != len(fields):
                break

    fields[insert_at:insert_at] = block


def ensure_required_members(
    fields: list[tuple[str, str]],
    model: DictionaryModel,
    members: list[Member],
    ref: dict,
    seq: int,
    idx: int,
    rng: random.Random,
    seen_components: set[str] | None = None,
) -> None:
    seen_components = seen_components or set()
    for member in members:
        if not member.required:
            continue
        if member.kind == "field":
            tag_num = model.field_numbers.get(member.name)
            if tag_num is None:
                continue
            tag = str(tag_num)
            if not has_tag(fields, tag):
                fields.append((tag, gen_field_value(model, member.name, ref, seq, idx, rng)))
            continue
        if member.kind == "group":
            if member.name not in model.field_numbers:
                continue
            group_block = build_group_block(model, member, ref, seq, idx, rng, entry_count=1, semantic_invalid=False)
            insert_group_block_at_member_order(fields, model, members, member, group_block)
            continue
        if member.kind == "component":
            if member.name in seen_components:
                continue
            seen_components.add(member.name)
            ensure_required_members(
                fields,
                model,
                model.components.get(member.name, []),
                ref,
                seq,
                idx,
                rng,
                seen_components,
            )


def mutate_template(
    tpl: list[tuple[str, str]],
    version: str,
    i: int,
    seq: int,
    ref: dict,
    model: DictionaryModel,
    semantic_invalid: bool,
) -> list[tuple[str, str]]:
    rng = random.Random(1000 + i)
    fields = [(k, v) for (k, v) in tpl]

    banks = ref["participants"]["banks"] or ["GLOBAL BANK PLC"]
    funds = ref["participants"]["funds"] or ["ALPHA CAPITAL MGMT"]

    sender = sanitize_party(funds[i % len(funds)])
    target = sanitize_party(banks[(i + 3) % len(banks)])

    set_or_add(fields, "34", str(seq))
    set_or_add(fields, "49", sender)
    set_or_add(fields, "56", target)
    set_or_add(fields, "52", msg_time(seq))

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

    if any(k == "55" for k, _ in fields):
        mode = i % 5
        if mode == 0 and ref["equities"]:
            eq = ref["equities"][i % len(ref["equities"])]
            set_or_add(fields, "55", eq["symbol"])
            set_or_add(fields, "167", "CS")
        elif mode == 1 and ref["fx_pairs"]:
            set_or_add(fields, "55", ref["fx_pairs"][i % len(ref["fx_pairs"])])
            set_or_add(fields, "167", "FOR")
            set_or_add(fields, "15", "USD")
        elif mode == 2 and ref["bonds"]:
            b = ref["bonds"][i % len(ref["bonds"])]
            set_or_add(fields, "55", b["cusip"])
            set_or_add(fields, "48", b["cusip"])
            set_or_add(fields, "22", "1")
            set_or_add(fields, "167", "TBOND")
        elif mode == 3 and ref["futures"]:
            code = ref["futures"][i % len(ref["futures"])].split(":", 1)[0]
            set_or_add(fields, "55", f"FUT{code}")
            set_or_add(fields, "167", "FUT")
            set_or_add(fields, "207", "CME")
            set_or_add(fields, "200", "202603")
        elif ref["repo"]:
            set_or_add(fields, "55", ref["repo"][i % len(ref["repo"])])
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

    msg_type = first_value(fields, "35")
    message_members = model.messages.get(msg_type, [])
    ensure_required_members(fields, model, message_members, ref, seq, i, rng)

    groups: list[Member] = []
    collect_groups(model, message_members, groups)
    direct_groups = [g for g in collect_direct_groups(message_members) if g.name in model.field_numbers and g.children]

    corrupted_via_group = False
    if groups and msg_type in {"D", "8", "AE", "AB", "A"}:
        group_pool = direct_groups if direct_groups else groups
        group = group_pool[i % len(group_pool)]
        # semantic_invalid path always includes a dictionary-known group and then corrupts it.
        entries = 2 if semantic_invalid else 1 + (i % 2)
        group_block = build_group_block(model, group, ref, seq, i, rng, entries, semantic_invalid)
        insert_group_block_at_member_order(fields, model, message_members, group, group_block)
        if semantic_invalid:
            for j, (k, v) in enumerate(fields):
                if k == str(model.field_numbers[group.name]):
                    fields[j] = (k, str(entries + 1))
                    break
            corrupted_via_group = True

    if semantic_invalid:
        required_names: list[str] = []
        collect_required_top_level_field_names(model, message_members, required_names)
        removed = False
        for name in required_names:
            tag_num = model.field_numbers.get(name)
            if tag_num is None:
                continue
            tag = str(tag_num)
            if tag in {"8", "35"}:
                continue
            if remove_first_tag(fields, tag):
                removed = True
                break
        if not removed:
            # last-resort semantic corruption: wrong MsgType while keeping syntax valid.
            set_or_add(fields, "35", "ZZ")

    return fields


def garble_message(message: str, i: int) -> str:
    mode = i % 4
    if mode == 0:
        # Break BeginString tag parsing.
        return message.replace("8=", "X=", 1)
    if mode == 1:
        # Corrupt MsgType tag key.
        return re.sub(r"\b35=", "35-", message, count=1)
    if mode == 2:
        # Remove all delimiters to prevent proper tokenization.
        return message.replace("|", "")
    # Drop BeginString token entirely.
    return re.sub(r"^8=[^|]*\|", "", message, count=1)


def generate_for_version(
    version: str,
    templates: list[list[tuple[str, str]]],
    num_correct: int,
    num_semantic_incorrect: int,
    num_garbled: int,
    ref: dict,
    model: DictionaryModel,
) -> tuple[list[str], list[str], list[str]]:
    correct: list[str] = []
    semantic_bad: list[str] = []
    garbled: list[str] = []

    seq = 1
    for i in range(num_correct):
        tpl = templates[i % len(templates)]
        correct.append(render_fix_line(mutate_template(tpl, version, i, seq, ref, model, semantic_invalid=False)))
        seq += 1

    semantic_templates: list[list[tuple[str, str]]] = []
    group_semantic_templates: list[list[tuple[str, str]]] = []
    for tpl in templates:
        msg_type = first_value(tpl, "35")
        members = model.messages.get(msg_type, [])
        groups: list[Member] = []
        collect_groups(model, members, groups)
        req_fields: list[str] = []
        collect_required_top_level_field_names(model, members, req_fields)
        if groups:
            group_semantic_templates.append(tpl)
        if groups or req_fields:
            semantic_templates.append(tpl)
    if group_semantic_templates:
        semantic_templates = group_semantic_templates
    if not semantic_templates:
        semantic_templates = templates

    for i in range(num_semantic_incorrect):
        tpl = semantic_templates[(i + num_correct) % len(semantic_templates)]
        semantic_bad.append(
            render_fix_line(mutate_template(tpl, version, i + num_correct, seq, ref, model, semantic_invalid=True))
        )
        seq += 1

    # Garbled messages are derived from otherwise realistic (and mostly valid) payloads.
    for i in range(num_garbled):
        source = correct[i % len(correct)] if correct else render_fix_line(templates[i % len(templates)])
        garbled.append(garble_message(source, i))

    return correct, semantic_bad, garbled


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate realistic FIX samples for every version")
    parser.add_argument("--base-samples-dir", default="data/samples/valid", help="directory containing FIX*.messages templates")
    parser.add_argument("--dict-dir", default="data/quickfix", help="directory containing FIX*.xml dictionaries")
    parser.add_argument("--reference-dir", default="data/samples/reference", help="directory to persist downloaded reference data")
    parser.add_argument("--out-dir", default="data/samples/realistic", help="output directory")
    parser.add_argument("--num-correct", "--num_correct", dest="num_correct", type=int, default=850)
    parser.add_argument("--num-semantic-incorrect", "--num_semantic_incorrect", dest="num_semantic_incorrect", type=int, default=100)
    parser.add_argument("--num-garbled", "--num_garbled", dest="num_garbled", type=int, default=50)
    args = parser.parse_args()

    if args.num_correct < 0 or args.num_semantic_incorrect < 0 or args.num_garbled < 0:
        raise SystemExit("num_correct, num_semantic_incorrect and num_garbled must be >= 0")

    base_dir = Path(args.base_samples_dir)
    dict_dir = Path(args.dict_dir)
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
        dict_path = dict_dir / f"{version}.xml"
        if not dict_path.exists():
            print(f"Skipping {version}: missing dictionary {dict_path}")
            continue
        model = load_dictionary_model(dict_path)
        correct, semantic_bad, garbled = generate_for_version(
            version,
            templates,
            args.num_correct,
            args.num_semantic_incorrect,
            args.num_garbled,
            ref,
            model,
        )

        total = len(correct) + len(semantic_bad) + len(garbled)
        correct_file = out_dir / f"{version}_realistic_correct_{len(correct)}.messages"
        semantic_file = out_dir / f"{version}_realistic_semantic_incorrect_{len(semantic_bad)}.messages"
        garbled_file = out_dir / f"{version}_realistic_garbled_{len(garbled)}.messages"
        combined_file = out_dir / f"{version}_realistic_{total}.messages"

        correct_file.write_text("\n".join(correct) + ("\n" if correct else ""), encoding="utf-8")
        semantic_file.write_text("\n".join(semantic_bad) + ("\n" if semantic_bad else ""), encoding="utf-8")
        garbled_file.write_text("\n".join(garbled) + ("\n" if garbled else ""), encoding="utf-8")
        combined_file.write_text("\n".join(correct + semantic_bad + garbled) + ("\n" if total else ""), encoding="utf-8")

        generated.append((version, correct_file, semantic_file, garbled_file, combined_file, total))

    print(f"Wrote reference data: {ref_path}")
    for version, correct_file, semantic_file, garbled_file, combined_file, total in generated:
        print(
            f"Wrote {version:8s}: {correct_file.name}, {semantic_file.name}, {garbled_file.name}, "
            f"{combined_file.name} (total {total})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
