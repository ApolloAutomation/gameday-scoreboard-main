#!/usr/bin/env python3
"""Regenerate flight_airlines.h from Virtual Radar Server's airline list."""

import csv
import io
import json
import re
import sys
import unicodedata
import urllib.request
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "components" / "gameday" / "flight_airlines.h"
RAW = "https://raw.githubusercontent.com/vradarserver/standing-data/main"
TREE = "https://api.github.com/repos/vradarserver/standing-data/git/trees/main?recursive=1"
TOP = 300
NAME_MAX = 16

# Operators with no published routes that the panel sees often.
EXTRAS = [
    "EJA", "LXJ", "EJM", "VJT", "CNS", "DPJ", "EDG", "XSR", "JTZ", "TWY", "GPD", "KFS",
    "FDX", "UPS", "GTI", "ABX", "ATN", "CKS", "PAC", "SOO", "WGN", "NCR", "CAP", "RCH",
    "SAM", "CMB", "CNV", "PAT", "CGX", "BOE", "AMF", "JUS", "MDS", "FFL",
]

# Names the rules below get wrong or leave too long.
OVERRIDES = {
    "AAL": "American",
    "ACA": "Air Canada",
    "AFR": "Air France",
    "AMX": "Aeromexico",
    "ANA": "ANA",
    "ASA": "Alaska",
    "AUA": "Austrian",
    "BAW": "British Airways",
    "BEL": "Brussels",
    "CAL": "China Airlines",
    "CCA": "Air China",
    "CES": "China Eastern",
    "CPA": "Cathay Pacific",
    "CSN": "China Southern",
    "DLH": "Lufthansa",
    "EIN": "Aer Lingus",
    "EZY": "easyJet",
    "EJU": "easyJet Europe",
    "EZS": "easyJet Swiss",
    "FDX": "FedEx",
    "GTI": "Atlas Air",
    "IBE": "Iberia",
    "JAL": "Japan Airlines",
    "JZA": "Jazz",
    "KAL": "Korean Air",
    "KLM": "KLM",
    "LOT": "LOT",
    "LPE": "LATAM Peru",
    "NAX": "Norwegian",
    "NAY": "Naysa",
    "QFA": "Qantas",
    "RCH": "US Air Force",
    "SAM": "US Air Force",
    "CMB": "US Transcom",
    "CNV": "US Navy",
    "PAT": "US Army",
    "CGX": "Coast Guard",
    "SAS": "SAS",
    "SWR": "Swiss",
    "TAP": "TAP Portugal",
    "THY": "Turkish",
    "UAE": "Emirates",
    "UPS": "UPS",
    "VIR": "Virgin Atlantic",
    "VJT": "VistaJet",
    "WZZ": "Wizz Air",
    "AWE": "US Airways",
    "AXB": "AI Express",
    "AZU": "Azul",
    "GLO": "Gol",
    "TAM": "LATAM Brasil",
    "VIV": "Viva Aerobus",
    "TIB": "TRIP",
    "TAI": "Avianca",
    "ELY": "El Al",
    "AIZ": "Arkia",
    "BCS": "DHL",
    "DHK": "DHL",
    "CSA": "Czech Airlines",
    "AWQ": "AirAsia ID",
    "APG": "AirAsia PH",
    "AIQ": "Thai AirAsia",
    "BKP": "Bangkok Airways",
    "CSS": "SF Airlines",
    "SKU": "Sky Airline",
    "OKA": "Okay Airways",
    "EXS": "Jet2",
    "ARE": "LATAM Colombia",
    "LAN": "LATAM Chile",
    "GIA": "Garuda",
    "SLI": "Aeromexico Conn",
    "CPZ": "Compass",
    "BGA": "Airbus Beluga",
    "BLF": "Blue1",
    "CND": "Corendon",
    "CXI": "Corendon",
    "CAI": "Corendon",
    "IBB": "Binter",
}

DROP = {
    "airlines", "air lines", "airline", "airways", "aviation", "international", "inc", "ltd",
    "limited", "llc", "s.a.", "sa", "company", "co", "corporation", "corp", "group", "plc",
}


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def short_name(name):
    s = re.sub(r"\s*\([^)]*\)", "", name).strip()
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().strip()
    words = s.replace(",", " ").split()
    while len(words) > 1:
        two = " ".join(words[-2:]).lower()
        if len(words) > 2 and two in DROP:
            words = words[:-2]
        elif words[-1].lower() in DROP or words[-1].lower().rstrip(".") in DROP:
            words = words[:-1]
        else:
            break
    while len(words) > 1 and len(" ".join(words)) > NAME_MAX:
        words = words[:-1]
    return " ".join(words)[:NAME_MAX].rstrip()


def ranked_codes(tree):
    size = defaultdict(int)
    for e in tree["tree"]:
        m = re.match(r"routes/schema-01/[A-Z0-9]/([A-Z]{3})-[^/]*\.csv$", e["path"])
        if m and e.get("type") == "blob":
            size[m.group(1)] += e.get("size", 0)
    return [c for c, _ in sorted(size.items(), key=lambda kv: (-kv[1], kv[0]))]


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    text = get(f"{RAW}/airlines/schema-01/airlines.csv").decode("utf-8-sig")
    by_icao = {}
    for r in csv.DictReader(io.StringIO(text)):
        icao = r["ICAO"].strip()
        if re.fullmatch(r"[A-Z]{3}", icao) and icao not in by_icao:
            by_icao[icao] = r
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("the repository's file list came back truncated")
    keep = []
    for code in ranked_codes(tree):
        if code in by_icao and code not in keep:
            keep.append(code)
        if len(keep) >= TOP:
            break
    if len(keep) < TOP:
        raise SystemExit(f"only {len(keep)} airlines ranked; the route list looks wrong")
    for code in EXTRAS:
        if code in by_icao and code not in keep:
            keep.append(code)
    lines = [
        "// Generated by scripts/build_airlines.py. Do not edit.",
        "#pragma once",
        "",
        "namespace flights {",
        "",
        "struct AirlineName {",
        "  char icao[4];",
        "  char iata[3];",
        f"  char name[{NAME_MAX + 1}];",
        "};",
        "",
        "// Sorted by ICAO prefix, for a binary search.",
        "static const AirlineName kAirlines[] = {",
    ]
    for code in sorted(keep):
        r = by_icao[code]
        iata = r["IATA"].strip()
        if not re.fullmatch(r"[A-Z0-9]{2}", iata):
            iata = ""
        name = OVERRIDES.get(code) or short_name(r["Name"])
        lines.append(f"    {{{c_str(code)}, {c_str(iata)}, {c_str(name)}}},")
    lines += ["};", "", "}", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(keep)} airlines")


def selftest():
    cases = {
        "Delta Air Lines": "Delta",
        "Southwest Airlines": "Southwest",
        "JetBlue Airways": "JetBlue",
        "Republic Airways": "Republic",
        "Executive Jet Management": "Executive Jet",
        "Air Canada": "Air Canada",
        "Korean Air": "Korean Air",
        "China Eastern Airlines": "China Eastern",
        "Kalitta Air": "Kalitta Air",
        "Air Transport International": "Air Transport",
        "SkyWest Airlines": "SkyWest",
        "Avianca (Aerovias del Continente Americano)": "Avianca",
        "Emirates": "Emirates",
        "Aerolineas Argentinas S.A.": "Aerolineas",
        "Air Alg\u00e9rie": "Air Algerie",
    }
    bad = 0
    for name, want in cases.items():
        got = short_name(name)
        if got != want:
            print(f"short_name({name!r}) = {got!r}, want {want!r}")
            bad += 1
    print("selftest:", "ok" if not bad else f"{bad} failures")
    return 1 if bad else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    main()
