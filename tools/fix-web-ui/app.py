#!/usr/bin/env python3
import json
import os
import subprocess
from pathlib import Path

from flask import Flask, render_template, request

APP_DIR = Path(__file__).resolve().parent
PARSER_BIN = APP_DIR / "bin" / "fix_web_parser"
DICT_DIR = Path(os.getenv("FIX_DICT_DIR", "/app/data/quickfix"))
PORT = int(os.getenv("PORT", "8081"))

app = Flask(__name__)


def parse_message(message: str) -> dict:
    if not PARSER_BIN.exists():
        return {
            "ok": False,
            "parse_error": f"Parser binary not found: {PARSER_BIN}",
            "fields": [],
            "validation_errors": [],
            "begin_string": "",
            "msg_type": "",
            "structurally_valid": False,
        }

    cmd = [str(PARSER_BIN), str(DICT_DIR), message]
    proc = subprocess.run(cmd, capture_output=True, text=True)

    if proc.returncode != 0:
        return {
            "ok": False,
            "parse_error": f"Parser execution failed: {proc.stderr.strip() or proc.stdout.strip()}",
            "fields": [],
            "validation_errors": [],
            "begin_string": "",
            "msg_type": "",
            "structurally_valid": False,
        }

    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        return {
            "ok": False,
            "parse_error": f"Parser returned invalid JSON: {exc}",
            "fields": [],
            "validation_errors": [],
            "begin_string": "",
            "msg_type": "",
            "structurally_valid": False,
        }


@app.get("/")
def index():
    sample = "8=FIX.4.4|35=D|49=BUY|56=SELL|34=2|52=20260219-12:00:00.000|11=ABC|21=1|55=IBM|54=1|60=20260219-12:00:00.000|38=100|40=2|44=123.45|"
    return render_template("index.html", message=sample, result=None)


@app.post("/parse")
def parse():
    message = request.form.get("message", "")
    result = parse_message(message)
    return render_template("index.html", message=message, result=result)


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=PORT)
