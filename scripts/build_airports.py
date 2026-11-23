#!/usr/bin/env python3
"""Regenerate the Flight Tracker's airport names.

Two files, both committed:

  firmware/data/airports.tsv          one airport a line: IATA, ICAO, the
                                      city the panel shows and, where two
                                      airports share it, the airport's short
                                      name. Made from OurAirports.
  components/gameday/flight_airports.h the same, packed for the panel.

OurAirports (https://ourairports.com/data/, public domain) lists every
airport. This script keeps each one with a three-letter IATA code and
scheduled service, and every large airport, and names it by the place
people know: the municipality, cleaned for a small panel (no "(Roissy)" or
", Essex", letters past Latin-1 folded to ASCII), or one of the overrides
below where the municipality misleads. A name with any letter past
Latin-1 is folded whole to ASCII ("Lodz", not "Lódz"). A former code
OurAirports keeps in an airport's keywords with its ICAO form, like PBI and
KPBI for DJT, gets a line of its own with the same name, since route
sources may still send it. Run it when OurAirports changes enough to
matter, review the .tsv's diff and commit both files.

The header packs the rows into one bit stream, in blocks of BLOCK airports
by IATA; kAirportHead[k] is block k's first IATA (base 26) and bit offset.
Each airport, high bit first: the IATA's gap from the one before (Rice
coded, none on a block's first); 2 bits for the ICAO (0 none, 1 "K" + IATA,
2 "C" + IATA, 3 in the ICAO index); then Huffman symbols for the city, SEP
and the airport part when there is one, and END. A word's first letter
(after the start, a space, a dash or SEP) has its own code table. The ICAO
index lists the mode-3 airports by ICAO in blocks the same way: each one's
gap (Rice coded) and its 13-bit place in the names.

  (no option)  download airports.csv, rewrite the .tsv and the header
  --cache D    keep and reuse the download in folder D
  --check      offline: the header is what the committed .tsv packs to, and
               the .tsv was made by these rules (with --cache, also that
               the .tsv is what the cached download makes)
  --selftest   offline checks on the cleaning rules and the packing
"""

import argparse
import hashlib
import inspect
import re
import sys
import unicodedata
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "firmware" / "data" / "airports.tsv"
OUT = ROOT / "components" / "gameday" / "flight_airports.h"
URL = "https://davidmegginson.github.io/ourairports-data/airports.csv"
BLOCK = 32
CITY_MAX = 24
PART_MAX = 12

# Where the municipality isn't the place people know.
CITY = {
    "BSL": "Basel", "CGN": "Cologne", "EDI": "Edinburgh", "IAD": "Washington", "NRT": "Tokyo",
    "KUL": "Kuala Lumpur", "MXP": "Milan", "LIN": "Milan", "BGY": "Milan", "ATH": "Athens",
    "WMI": "Warsaw", "NYO": "Stockholm", "STN": "London", "LTN": "London",
    "SEN": "London", "LCY": "London", "LGW": "London", "PIE": "St. Petersburg", "SWF": "Newburgh",
    "ICN": "Seoul", "GMP": "Seoul", "KIX": "Osaka", "UKB": "Kobe", "TPE": "Taipei", "TSA": "Taipei",
    "CTS": "Sapporo", "BVA": "Paris", "XCR": "Paris", "HHN": "Frankfurt",
    "EIN": "Eindhoven", "TSF": "Venice", "CIA": "Rome", "PSA": "Pisa", "VCE": "Venice",
    "NCE": "Nice", "GVA": "Geneva", "ZRH": "Zurich", "VIE": "Vienna", "MUC": "Munich",
    "FRA": "Frankfurt", "DUS": "Düsseldorf", "HAM": "Hamburg", "BER": "Berlin", "CPH": "Copenhagen",
    "ARN": "Stockholm", "BMA": "Stockholm", "OSL": "Oslo", "HEL": "Helsinki", "KEF": "Reykjavík",
    "DXB": "Dubai", "DWC": "Dubai", "AUH": "Abu Dhabi", "DEL": "Delhi", "SYD": "Sydney",
    "MEL": "Melbourne", "AKL": "Auckland", "PVG": "Shanghai", "SHA": "Shanghai", "EZE": "Buenos Aires",
    "HNL": "Honolulu", "OGG": "Maui", "KOA": "Kona", "LIH": "Kauai", "CVG": "Cincinnati",
    "DFW": "Dallas-Fort Worth", "MSP": "Minneapolis", "PVD": "Providence", "TYS": "Knoxville",
    "SNA": "Orange County", "MNL": "Manila", "CDG": "Paris", "ORY": "Paris", "SVO": "Moscow",
    "LED": "St. Petersburg", "YUL": "Montréal", "SJU": "San Juan", "BOG": "Bogotá",
    "GRU": "São Paulo", "CGH": "São Paulo", "VCP": "Campinas", "GIG": "Rio de Janeiro",
    "SDU": "Rio de Janeiro", "RFD": "Rockford", "XNA": "Bentonville", "MHT": "Manchester NH",
    "BLV": "Belleville", "BRU": "Brussels", "LEJ": "Leipzig", "FMO": "Münster", "PAD": "Paderborn", "FKB": "Karlsruhe",
    "KSF": "Kassel", "EMA": "East Midlands", "KRK": "Kraków", "MRU": "Mauritius", "RUN": "Réunion",
    "FUE": "Fuerteventura", "ACE": "Lanzarote", "OTP": "Bucharest", "ZAG": "Zagreb", "LYS": "Lyon",
    "MRS": "Marseille", "LIL": "Lille", "TRN": "Turin", "TRS": "Trieste", "VRN": "Verona", "GOA": "Genoa",
    "FLR": "Florence", "LJU": "Ljubljana", "OSR": "Ostrava", "GRZ": "Graz", "OVD": "Asturias", "MAH": "Menorca",
    "RMU": "Murcia", "CFU": "Corfu", "CHQ": "Chania", "EFL": "Kefalonia", "ADB": "Izmir", "SKP": "Skopje",
    "TIA": "Tirana", "VVO": "Vladivostok", "NGO": "Nagoya", "ISB": "Islamabad", "MVD": "Montevideo",
    "MFM": "Macau", "DPS": "Bali", "KNO": "Medan", "GUM": "Guam", "CAY": "Cayenne", "ANU": "Antigua",
    "SXM": "St. Maarten", "TUC": "Tucumán", "JUJ": "Jujuy", "BRC": "Bariloche", "BJX": "León", "GOI": "Goa",
    "NTL": "Newcastle", "AVV": "Avalon", "LXA": "Lhasa", "XNN": "Xining", "KUT": "Kutaisi", "IAR": "Yaroslavl",
    "DJE": "Djerba", "CCS": "Caracas", "FEZ": "Fez", "ORN": "Oran", "GOT": "Gothenburg", "LGG": "Liège",
    "CRK": "Clark", "IPC": "Easter Island", "SID": "Sal", "BVC": "Boa Vista", "NAT": "Natal",
    "HPN": "White Plains", "AZA": "Phoenix-Mesa", "PIA": "Peoria",
    "USA": "Concord", "IST": "Istanbul", "SAW": "Istanbul", "NAP": "Naples", "BLQ": "Bologna",
    "TXL": "Berlin", "MAN": "Manchester", "BFS": "Belfast", "BHD": "Belfast", "DMK": "Bangkok",
    "BKK": "Bangkok", "CGK": "Jakarta", "HKG": "Hong Kong", "SIN": "Singapore", "DOH": "Doha",
    "KBP": "Kyiv", "PTY": "Panama City", "BLB": "Panama City", "PAC": "Panama City", "STT": "St. Thomas",
    "STX": "St. Croix", "JTR": "Santorini", "USM": "Koh Samui", "VPS": "Destin", "SPC": "La Palma",
    "GJL": "Jijel", "ENU": "Enugu", "IOM": "Isle of Man", "NOC": "Knock", "EVE": "Harstad-Narvik",
    "LLW": "Lilongwe", "OUD": "Oujda", "BEM": "Beni Mellal", "DXN": "Noida", "ECN": "Ercan",
    "BNX": "Banja Luka", "TZL": "Tuzla", "RTB": "Roatán", "MZT": "Mazatlán", "VRA": "Varadero",
    "HEA": "Herat", "NUM": "Neom", "GWD": "Gwadar", "TKK": "Chuuk", "ROR": "Palau", "KSA": "Kosrae",
    "MZG": "Penghu", "IBR": "Ibaraki", "FSZ": "Shizuoka", "KCZ": "Kochi", "SDJ": "Sendai",
    "YNY": "Yangyang", "ILO": "Iloilo", "RSI": "Red Sea", "UYU": "Uyuni", "ESM": "Esmeraldas",
    "DSY": "Dara Sakor", "SFS": "Subic Bay", "SPB": "St. Thomas", "SSB": "St. Croix", "COV": "Adana-Mersin", "YKF": "Waterloo", "MME": "Teesside",
    "HUY": "Humberside", "KIR": "Kerry", "LCG": "A Coruña", "LEN": "León", "EAS": "San Sebastián",
    "VIT": "Vitoria", "ETZ": "Metz", "RNS": "Rennes", "CUF": "Cuneo", "VBS": "Brescia", "AOI": "Ancona",
    "BWK": "Brac", "HDF": "Heringsdorf", "MBS": "Saginaw", "CMI": "Champaign", "CWA": "Wausau",
    "TRI": "Tri-Cities", "MQT": "Marquette", "SUN": "Sun Valley", "TTN": "Trenton", "OCJ": "Ocho Rios",
    "MTT": "Minatitlán", "TAM": "Tampico", "AKJ": "Asahikawa", "GAJ": "Yamagata", "FKS": "Fukushima",
    "RJA": "Rajahmundry", "KUU": "Kullu", "ABX": "Albury", "KGI": "Kalgoorlie", "WGA": "Wagga Wagga",
    "GET": "Geraldton", "AYQ": "Uluru", "CRV": "Crotone", "JSH": "Sitia", "GRK": "Killeen",
    "PIB": "Hattiesburg", "CIU": "Sault Ste. Marie", "CKB": "Clarksburg",
}

# The airport's own short name, where two share a city.
PART = {
    "JFK": "JFK", "LGA": "LaGuardia", "EWR": "Newark", "IAH": "Bush", "HOU": "Hobby",
    "DCA": "Reagan", "IAD": "Dulles", "BWI": "BWI", "ORD": "O'Hare", "MDW": "Midway",
    "DAL": "Love Field", "LHR": "Heathrow", "LGW": "Gatwick", "STN": "Stansted",
    "LTN": "Luton", "LCY": "City", "SEN": "Southend", "CDG": "CDG", "ORY": "Orly",
    "BVA": "Beauvais", "NRT": "Narita", "HND": "Haneda", "KIX": "Kansai", "ITM": "Itami",
    "ICN": "Incheon", "GMP": "Gimpo", "PVG": "Pudong", "SHA": "Hongqiao", "PEK": "Capital",
    "PKX": "Daxing", "SVO": "Sheremetyevo", "DME": "Domodedovo", "VKO": "Vnukovo",
    "MXP": "Malpensa", "LIN": "Linate", "BGY": "Bergamo", "FCO": "Fiumicino", "CIA": "Ciampino",
    "ARN": "Arlanda", "BMA": "Bromma", "NYO": "Skavsta", "OSL": "Gardermoen", "TRF": "Torp",
    "EZE": "Ezeiza", "AEP": "Aeroparque", "GRU": "Guarulhos", "CGH": "Congonhas",
    "GIG": "Galeão", "SDU": "Santos Dumont", "IST": "IST", "SAW": "Sabiha", "DXB": "DXB",
    "DWC": "Al Maktoum", "BKK": "Suvarnabhumi", "DMK": "Don Mueang", "TPE": "Taoyuan",
    "TSA": "Songshan", "YYZ": "Pearson", "YTZ": "Billy Bishop", "YUL": "Trudeau",
    "MIA": "MIA", "OPF": "Opa-locka", "MCO": "MCO", "SFB": "Sanford", "BFS": "Aldergrove",
    "BHD": "City", "HHN": "Hahn", "FRA": "FRA", "WAW": "Chopin", "WMI": "Modlin",
    "LAX": "LAX", "LGB": "Long Beach", "JAC": "", "AGS": "", "AUG": "", "SGF": "",
    "SEA": "Sea-Tac", "BFI": "Boeing Field", "NLU": "Felipe Ángeles", "MEX": "Benito Juárez",
    "KBP": "Boryspil", "PTY": "Tocumen", "BLB": "Pacífico", "PAC": "Albrook", "CRL": "", "SPB": "Harbor", "SSB": "Harbor",
}

GENERIC = {
    "international", "intl", "int'l", "airport", "airfield", "aerodrome", "airpark", "regional",
    "municipal", "metropolitan", "intercontinental", "domestic", "national", "county", "air",
    "base", "executive", "the", "of", "and", "at", "field", "aeropuerto", "aeroporto", "aéroport",
    "flughafen", "lufthavn", "airstrip", "jetport", "terminal", "de", "do", "da", "del", "la",
    "el", "city", "memorial", "global", "heliport", "helipad", "seaplane", "sea", "plane", "force",
    "station", "naval", "army", "strip", "landing", "inc", "llc", "jasdf",
}

# Words a shortened name never ends on.
CONNECT = {"de", "do", "da", "dos", "das", "del", "la", "le", "les", "di", "of", "the", "on", "upon", "y", "e", "and",
           "an", "am", "bei", "-"}

# Letters NFKD leaves alone.
FOLD = {"ł": "l", "Ł": "L", "đ": "d", "Đ": "D", "ı": "i", "ŀ": "l", "Ŀ": "L", "ħ": "h", "Ħ": "H",
        "ŋ": "n", "Ŋ": "N", "ə": "e", "Ə": "E", "ß": "ss", "ø": "o", "Ø": "O", "æ": "ae", "Æ": "AE",
        "þ": "th", "Þ": "Th", "ð": "d", "Ð": "D"}
# Punctuation past Latin-1, folded alone.
MARKS = {"ʻ": "'", "ʼ": "'", "’": "'", "‘": "'", "–": "-", "—": "-"}


def ascii_of(s):
    return "".join(FOLD.get(ch) or unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode() for ch in s)


def fold(s):
    """Latin-1 kept, else the whole name to ASCII."""
    s = "".join(MARKS.get(ch, ch) for ch in s)
    if any(ord(ch) >= 0x100 for ch in s):
        s = ascii_of(s)
    s = "".join(c for c in s if (0x20 <= ord(c) < 0x7F and c != "|") or 0xC0 <= ord(c) <= 0xFF and c not in "×÷")
    return re.sub(r"\s+", " ", s).strip()


def whole(s, max_len):
    """Whole words up to max_len, no dangling "de" or "do"."""
    words, out = s.split(), []
    for w in words:
        if out and len(" ".join(out + [w])) > max_len:
            while out and out[-1].lower() in CONNECT:
                out.pop()
            break
        out.append(w)
    return " ".join(out)


def city_of(muni):
    s = re.sub(r"\s*\([^)]*\)", "", muni or "")
    s = re.split(r"\s*/\s*|,", s)[0]
    s = fold(s)
    s = re.sub(r"\s+(Township|Twp|County|Parish|Municipality|District)$", "", s)
    s = re.sub(r"-(Borneo|Sumatra|Celebes|Java|Papua)$", "", s)
    if s.isupper() and len(s) > 3:
        s = s.title()
    s = re.sub(r"\b(St|Ste)(?= )", r"\1.", s)
    return whole(s, CITY_MAX)


def name_city(name):
    """No municipality: the airport's own name, generic words dropped."""
    s = fold(re.sub(r"\s*\([^)]*\)", "", name or ""))
    s = re.split(r"\s*/\s*|,", s)[0]
    words = [w for w in s.split() if w.lower().strip(".,") not in GENERIC]
    return whole(" ".join(words), CITY_MAX)


def part_of(name, city):
    """The airport's own name: "Chicago O'Hare ..." to "O'Hare"."""
    s = "".join(MARKS.get(ch, ch) for ch in re.sub(r"\s*\([^)]*\)", "", name))
    drop = {w.lower() for w in re.split(r"[\s/-]+", city)}
    keep = [w for w in re.split(r"[\s/]+", s) if w and fold(w).lower().strip(".,") not in GENERIC | drop]
    keep = [w.strip(",") for w in keep if not re.fullmatch(r"[A-Z]\.?|-", w)]
    if not keep or len(keep) > 2:
        return ""
    out = fold(" ".join(keep))
    return out if len(out) <= PART_MAX else ""


def wanted(r):
    return re.fullmatch(r"[A-Z]{3}", r.get("iata_code") or "") is not None and (
        r.get("scheduled_service") == "yes" or r.get("type") == "large_airport") and r.get("type") != "closed"


def former_codes(records, keep):
    """{former IATA: (its ICAO, today's IATA)} from keywords."""
    taken = {r.get("iata_code") for r in records}
    taken |= {r.get(k) for r in records for k in ("ident", "icao_code", "gps_code")}
    seen = {}
    for code, (_, r) in sorted(keep.items()):
        words = sorted({w.strip() for w in (r.get("keywords") or "").split(",")})
        for w in words:
            if re.fullmatch(r"[A-Z]{3}", w) and w not in taken:
                icao = [x for x in words if re.fullmatch(r"[A-Z]" + w, x) and (x not in taken or x == r.get("ident"))]
                if icao:
                    seen.setdefault(w, []).append((icao[0], code))
    return {w: v[0] for w, v in seen.items() if len(v) == 1}


def rows_of(records):
    """[(iata, icao, city, part)] sorted by IATA."""
    keep = {}
    for r in records:
        if not wanted(r):
            continue
        code = r["iata_code"]
        old = keep.get(code)
        rank = {"large_airport": 0, "medium_airport": 1}.get(r["type"], 2)
        if old is None or rank < old[0]:
            keep[code] = (rank, r)
    rows = []
    for code, (_, r) in keep.items():
        icao = (r.get("icao_code") or "").strip()
        if not re.fullmatch(r"[A-Z]{4}", icao):
            gps = (r.get("gps_code") or "").strip()
            icao = gps if re.fullmatch(r"[A-Z]{4}", gps) else ""
        city = CITY.get(code) or city_of(r.get("municipality")) or name_city(r["name"])
        rows.append([code, icao, city, r["name"], r.get("iso_region", "")])
    share = {}
    for code, _, city, _, region in rows:
        if city:
            share.setdefault((region, city), []).append(code)
    out = []
    for code, icao, city, name, region in rows:
        part = PART.get(code)
        if part is None:
            part = part_of(name, city) if city and len(share[(region, city)]) > 1 else ""
        if part == city:
            part = ""
        out.append((code, icao, city, part))
    names = {r[0]: r for r in out}
    for old, (icao, now) in former_codes(records, keep).items():
        out.append((old, icao, names[now][2], names[now][3]))
    return sorted(out)


def rules():
    """A hash of everything that turns the download into the .tsv."""
    src = "".join(inspect.getsource(f) for f in (ascii_of, fold, whole, city_of, name_city, part_of, wanted,
                                                  former_codes, rows_of))
    data = repr((sorted(CITY.items()), sorted(PART.items()), sorted(GENERIC), sorted(FOLD.items()),
                 sorted(MARKS.items()), sorted(CONNECT), CITY_MAX, PART_MAX))
    return hashlib.sha256((src + data).encode()).hexdigest()[:16]


def tsv_text(rows):
    lines = [f"# Generated by scripts/build_airports.py from OurAirports (public domain). Rules {rules()}.",
             "# IATA\tICAO\tcity\tairport, where two share the city"]
    lines += ["\t".join(r) for r in rows]
    return "\n".join(lines) + "\n"


def read_tsv(text):
    rules_line, rows = "", []
    for line in text.splitlines():
        if line.startswith("#"):
            m = re.search(r"Rules ([0-9a-f]{16})", line)
            rules_line = m.group(1) if m else rules_line
            continue
        if line:
            f = line.split("\t")
            rows.append(tuple((f + ["", "", "", ""])[:4]))
    return rules_line, rows


# ---- Packing (format in the docstring) -------------------------------------------

SEP, END = 256, 257
IATA_RICE = 2
ICAO_RICE = 7
ENTRY_BITS = 13


def b26(code):
    v = 0
    for c in code:
        v = v * 26 + ord(c) - 65
    return v


def un26(v, n):
    return "".join(chr(65 + v // 26 ** p % 26) for p in range(n - 1, -1, -1))


def icao_mode(iata, icao):
    if not icao:
        return 0
    if icao == "K" + iata:
        return 1
    if icao == "C" + iata:
        return 2
    return 3


def huffman(freq):
    """Code lengths, at most 15 bits."""
    import heapq
    if len(freq) == 1:
        return {next(iter(freq)): 1}
    heap = [(f, i, (s,)) for i, (s, f) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    depth = {s: 0 for s in freq}
    n = len(heap)
    while len(heap) > 1:
        f1, _, a = heapq.heappop(heap)
        f2, _, b = heapq.heappop(heap)
        for s in a + b:
            depth[s] += 1
        heapq.heappush(heap, (f1 + f2, n, a + b))
        n += 1
    if max(depth.values()) > 15:
        raise SystemExit("a Huffman code over 15 bits")
    return depth


def canonical(lengths):
    """Symbols by (length, symbol), their codes, and counts by length."""
    order = sorted(lengths, key=lambda s: (lengths[s], s))
    codes, code, prev = {}, 0, 0
    counts = [0] * 16
    for s in order:
        code <<= lengths[s] - prev
        prev = lengths[s]
        codes[s] = (code, lengths[s])
        counts[lengths[s]] += 1
        code += 1
    return order, codes, counts


class Bits:
    def __init__(self):
        self.out = bytearray()
        self.n = 0

    def put(self, v, k):
        for i in range(k - 1, -1, -1):
            if self.n % 8 == 0:
                self.out.append(0)
            if (v >> i) & 1:
                self.out[-1] |= 0x80 >> (self.n % 8)
            self.n += 1

    def rice(self, v, k):
        self.put(0, v >> k)
        self.put(1, 1)
        self.put(v & ((1 << k) - 1), k)


def symbols(city, part):
    out = list(city.encode("latin-1"))
    if part:
        out += [SEP] + list(part.encode("latin-1"))
    return out + [END]


def starts_word(prev):
    return prev is None or prev in (0x20, 0x2D, SEP)


class Packed:
    """Everything the header holds."""

    def __init__(self, rows):
        if len(rows) >= 1 << ENTRY_BITS:
            raise SystemExit("too many airports for the ICAO index")
        freq = ({}, {})
        for _, _, city, part in rows:
            prev = None
            for s in symbols(city, part):
                f = freq[0 if starts_word(prev) else 1]
                f[s] = f.get(s, 0) + 1
                prev = s
        self.tables = [canonical(huffman(f)) for f in freq]
        bits, self.heads, index = Bits(), [], []
        prev = None
        for i, (iata, icao, city, part) in enumerate(rows):
            v = b26(iata)
            if i % BLOCK == 0:
                self.heads.append((v, bits.n))
            else:
                bits.rice(v - prev - 1, IATA_RICE)
            prev = v
            mode = icao_mode(iata, icao)
            bits.put(mode, 2)
            if mode == 3:
                index.append((b26(icao), i))
            last = None
            for s in symbols(city, part):
                bits.put(*self.tables[0 if starts_word(last) else 1][1][s])
                last = s
        self.stream = bytes(bits.out)
        index.sort()
        ib, self.icao_heads = Bits(), []
        for j, (v, i) in enumerate(index):
            if j % BLOCK == 0:
                self.icao_heads.append((v, ib.n))
            else:
                ib.rice(v - index[j - 1][0] - 1, ICAO_RICE)
            ib.put(i, ENTRY_BITS)
        self.icao_stream = bytes(ib.out)
        self.icao_count = len(index)
        self.count = len(rows)

    def size(self):
        syms = sum(2 * len(t[0]) + 16 for t in self.tables)
        return len(self.stream) + len(self.icao_stream) + 8 * (len(self.heads) + len(self.icao_heads)) + syms


class Reader:
    def __init__(self, data, at):
        self.data, self.at = data, at

    def bit(self):
        b = (self.data[self.at >> 3] >> (7 - (self.at & 7))) & 1
        self.at += 1
        return b

    def get(self, k):
        v = 0
        for _ in range(k):
            v = v << 1 | self.bit()
        return v

    def rice(self, k):
        q = 0
        while self.bit() == 0:
            q += 1
        return q << k | self.get(k)

    def symbol(self, table):
        order, _, counts = table
        code = first = idx = 0
        for n in range(1, 16):
            code = code << 1 | self.bit()
            if code - first < counts[n]:
                return order[idx + code - first]
            idx += counts[n]
            first = (first + counts[n]) << 1
        raise ValueError("a bad code")


def unpack(p):
    """The rows back and the ICAO index, as the panel reads them."""
    rows = []
    for k, (v, at) in enumerate(p.heads):
        r = Reader(p.stream, at)
        for j in range(min(BLOCK, p.count - k * BLOCK)):
            if j:
                v += r.rice(IATA_RICE) + 1
            iata = un26(v, 3)
            mode = r.get(2)
            text, prev = bytearray(), None
            while True:
                s = r.symbol(p.tables[0 if starts_word(prev) else 1])
                if s == END:
                    break
                text.append(0x7C if s == SEP else s)
                prev = s
            city, _, part = text.decode("latin-1").partition("|")
            rows.append([iata, ["", "K" + iata, "C" + iata, "?"][mode], city, part])
    for k, (v, at) in enumerate(p.icao_heads):
        r = Reader(p.icao_stream, at)
        for j in range(min(BLOCK, p.icao_count - k * BLOCK)):
            if j:
                v += r.rice(ICAO_RICE) + 1
            rows[r.get(ENTRY_BITS)][1] = un26(v, 4)
    return [tuple(x) for x in rows]


def header_text(rows):
    p = Packed(rows)
    lines = [
        "// Generated by scripts/build_airports.py from firmware/data/airports.tsv",
        "// (OurAirports, public domain). Do not edit.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace flights {",
        "",
        f"static const uint16_t kAirportCount = {p.count};",
        f"static const uint16_t kAirportIcaoCount = {p.icao_count};",
        f"static const uint8_t kAirportBlock = {BLOCK};",
        f"static const uint8_t kAirportIataRice = {IATA_RICE};",
        f"static const uint8_t kAirportIcaoRice = {ICAO_RICE};",
        f"static const uint8_t kAirportEntryBits = {ENTRY_BITS};",
        f"static const uint16_t kAirportSep = {SEP};",
        f"static const uint16_t kAirportEnd = {END};",
        "",
        "// A block's first code, base 26, and bit offset.",
        "struct AirportHead {",
        "  uint32_t code;",
        "  uint32_t bit;",
        "};",
        "",
    ]
    for k, (order, _, counts) in enumerate(p.tables):
        what = "first letters" if k == 0 else "other letters"
        lines += [f"// Huffman counts by length, then symbols: {what}",
                  f"static const uint8_t kAirportCounts{k}[16] = {{" + ", ".join(map(str, counts)) + "};",
                  f"static const uint16_t kAirportSymbols{k}[] = {{"]
        lines += wrap([str(s) for s in order])
        lines += ["};", ""]
    lines += ["static const AirportHead kAirportHead[] = {"]
    lines += wrap([f"{{{v}, {b}}}" for v, b in p.heads])
    lines += ["};", "", "static const AirportHead kAirportIcaoHead[] = {"]
    lines += wrap([f"{{{v}, {b}}}" for v, b in p.icao_heads])
    lines += ["};", "", f"static const uint8_t kAirportBits[{len(p.stream)}] = {{"]
    lines += wrap([str(b) for b in p.stream])
    lines += ["};", "", f"static const uint8_t kAirportIcaoBits[{len(p.icao_stream)}] = {{"]
    lines += wrap([str(b) for b in p.icao_stream])
    lines += ["};", "", "}", ""]
    return "\n".join(lines)


def wrap(items, width=116):
    out, line = [], "   "
    for it in items:
        if len(line) + len(it) + 2 > width:
            out.append(line)
            line = "   "
        line += " " + it + ","
    if line.strip():
        out.append(line)
    return out


def table_bytes(rows):
    return Packed(rows).size()


# ---- Download --------------------------------------------------------------------


def download(cache):
    if cache:
        path = Path(cache) / "airports.csv"
        if path.exists():
            return path.read_text(encoding="utf-8")
    req = urllib.request.Request(URL, headers={"User-Agent": "curl/8.0"})
    with urllib.request.urlopen(req, timeout=120) as r:
        text = r.read().decode("utf-8")
    if cache:
        Path(cache).mkdir(parents=True, exist_ok=True)
        (Path(cache) / "airports.csv").write_text(text, encoding="utf-8")
    return text


def records(text):
    import csv
    import io
    return list(csv.DictReader(io.StringIO(text)))


def check(cache):
    bad = []
    if not TSV.exists() or not OUT.exists():
        print(f"FAIL: {TSV.relative_to(ROOT)} or {OUT.relative_to(ROOT)} is missing", file=sys.stderr)
        return 1
    text = TSV.read_text(encoding="utf-8")
    made_by, rows = read_tsv(text)
    if made_by != rules():
        bad.append(f"{TSV.relative_to(ROOT)} was made by other rules ({made_by}, now {rules()}): rerun the script")
    if [r[0] for r in rows] != sorted({r[0] for r in rows}):
        bad.append("the .tsv isn't sorted by IATA, or repeats one")
    if OUT.read_text(encoding="utf-8") != header_text(rows):
        bad.append(f"{OUT.relative_to(ROOT)} is stale: rerun the script")
    if unpack(Packed(rows)) != [tuple(r) for r in rows]:
        bad.append("the packed table doesn't read back")
    if cache and tsv_text(rows_of(records(download(cache)))) != text:
        bad.append("the .tsv differs from what the cached download makes")
    for b in bad:
        print(f"FAIL: {b}", file=sys.stderr)
    if bad:
        return 1
    print(f"flight_airports.h is current: {len(rows)} airports, {table_bytes(rows)} bytes")
    return 0


def selftest():
    bad = 0

    def eq(got, want, what):
        nonlocal bad
        if got != want:
            print(f"FAIL {what}: {got!r}, want {want!r}")
            bad += 1

    eq(city_of("Paris (Roissy-en-France, Val-d'Oise)"), "Paris", "a parenthesis")
    eq(city_of("Cincinnati / Covington"), "Cincinnati", "a slash")
    eq(city_of("London, Essex"), "London", "a county after a comma")
    eq(city_of("Köln (Cologne)"), "Köln", "Latin-1 kept")
    eq(city_of("İstanbul"), "Istanbul", "past Latin-1 folded")
    eq(city_of("Łódź"), "Lodz", "folded whole, not half")
    eq(city_of("České Budějovice"), "Ceske Budejovice", "every letter folded")
    eq(city_of("St Louis"), "St. Louis", "St. with its period")
    eq(city_of("Ste Agathe"), "Ste. Agathe", "Ste. with its period")
    eq(city_of("St. Petersburg"), "St. Petersburg", "St. kept")
    eq(city_of("Saint-Laurent"), "Saint-Laurent", "Saint left alone")
    eq(city_of("Stockholm"), "Stockholm", "St inside a word")
    eq(city_of("Nowy Dwór Mazowiecki Long Town"), "Nowy Dwór Mazowiecki", "whole words only")
    eq(city_of("São José do Rio Preto"), "São José do Rio Preto", "a long name whole")
    eq(city_of("WHATI"), "Whati", "capitals")
    eq(city_of(""), "", "none")
    eq(city_of("Saint-Laurent-du-Maroni"), "Saint-Laurent-du-Maroni", "one long word kept whole")
    eq(city_of("Castellón de la Plana Longer"), "Castellón de la Plana", "no dangling words")
    eq(city_of("Castellón de la Plananananana"), "Castellón", "no dangling de la")
    eq(name_city("Arutua Airport"), "Arutua", "a name with no municipality")
    eq(part_of("Chicago O'Hare International Airport", "Chicago"), "O'Hare", "O'Hare")
    eq(part_of("Chicago Midway International Airport", "Chicago"), "Midway", "Midway")
    eq(part_of("London Heathrow Airport", "London"), "Heathrow", "Heathrow")
    eq(part_of("Rhode Island T. F. Green International Airport", "Providence"), "", "too many words")
    eq(part_of("Dallas Love Field", "Dallas"), "Love", "a field")
    eq(fold("Zürich – Kloten"), "Zürich - Kloten", "a dash")
    eq(fold("Ka’anapali"), "Ka'anapali", "a quote mark alone")
    eq(part_of("King County International Airport - Boeing Field", "Seattle"), "King Boeing", "a lone dash")
    for code, name in list(CITY.items()) + list(PART.items()):
        eq(fold(name), name, f"{code}'s override is folded already")
    recs = [
        {"iata_code": "ORD", "icao_code": "KORD", "type": "large_airport", "scheduled_service": "yes",
         "municipality": "Chicago", "name": "Chicago O'Hare International Airport", "iso_country": "US"},
        {"iata_code": "MDW", "icao_code": "KMDW", "type": "large_airport", "scheduled_service": "yes",
         "municipality": "Chicago", "name": "Chicago Midway International Airport", "iso_country": "US"},
        {"iata_code": "SDF", "icao_code": "KSDF", "type": "large_airport", "scheduled_service": "yes",
         "municipality": "Louisville", "name": "Louisville Muhammad Ali International Airport", "iso_country": "US"},
        {"iata_code": "YYZ", "icao_code": "CYYZ", "type": "large_airport", "scheduled_service": "yes",
         "municipality": "Toronto", "name": "Toronto Pearson International Airport", "iso_country": "CA"},
        {"iata_code": "XYZ", "icao_code": "", "type": "small_airport", "scheduled_service": "no",
         "municipality": "Nowhere", "name": "Nowhere Strip", "iso_country": "US"},
        {"iata_code": "GRU", "icao_code": "SBGR", "type": "large_airport", "scheduled_service": "yes",
         "municipality": "São Paulo", "name": "Guarulhos", "iso_country": "BR"},
        {"iata_code": "ABC", "icao_code": "", "type": "medium_airport", "scheduled_service": "yes",
         "municipality": "Albacete", "name": "Albacete Airport", "iso_country": "ES"},
    ]
    rows = rows_of(recs)
    eq([r[0] for r in rows], ["ABC", "GRU", "MDW", "ORD", "SDF", "YYZ"], "kept and sorted")
    eq(rows[3], ("ORD", "KORD", "Chicago", "O'Hare"), "a shared city")
    eq(rows[4], ("SDF", "KSDF", "Louisville", ""), "a city alone")
    eq(rows[0][1], "", "no ICAO")
    p = Packed(rows)
    eq(unpack(p), rows, "packs and reads back")
    eq(p.icao_count, 1, "only GRU needs the ICAO index")
    eq([icao_mode(r[0], r[1]) for r in rows], [0, 3, 1, 1, 1, 2], "ICAO modes")
    many = [(un26(i, 3), "", "Town %d" % i, "") for i in range(0, 3000, 7)]
    many = [(r[0], "E%s" % r[0] if i % 3 else "", r[2], "Big" if i % 5 == 0 else "") for i, r in enumerate(many)]
    eq(unpack(Packed(many)), many, "many blocks read back")
    eq(read_tsv(tsv_text(rows))[1], rows, "the .tsv reads back")
    eq(read_tsv(tsv_text(rows))[0], rules(), "the .tsv names its rules")
    renamed = [
        {"iata_code": "DJT", "icao_code": "KDJT", "ident": "KDJT", "type": "large_airport",
         "scheduled_service": "yes", "municipality": "West Palm Beach", "name": "Trump International Airport",
         "iso_region": "US-FL", "keywords": "PBI, KPBI, MFW, South Florida"},
        {"iata_code": "FLL", "icao_code": "KFLL", "ident": "KFLL", "type": "large_airport",
         "scheduled_service": "yes", "municipality": "Fort Lauderdale", "name": "Fort Lauderdale Airport",
         "iso_region": "US-FL", "keywords": "MFW, South Florida"},
        {"iata_code": "JFK", "icao_code": "KJFK", "ident": "KJFK", "type": "large_airport",
         "scheduled_service": "yes", "municipality": "New York", "name": "John F. Kennedy Airport",
         "iso_region": "US-NY", "keywords": "NYC, Idlewild, EWR, KEWR"},
        {"iata_code": "EWR", "icao_code": "KEWR", "ident": "KEWR", "type": "large_airport",
         "scheduled_service": "yes", "municipality": "Newark", "name": "Newark Liberty Airport",
         "iso_region": "US-NJ", "keywords": "NYC"},
    ]
    eq(rows_of(renamed), [("DJT", "KDJT", "West Palm Beach", ""), ("EWR", "KEWR", "Newark", ""),
                          ("FLL", "KFLL", "Fort Lauderdale", ""), ("JFK", "KJFK", "New York", "JFK"),
                          ("PBI", "KPBI", "West Palm Beach", "")],
       "a former code from keywords, with its ICAO; a metro code or a code in use not")
    print("build_airports selftest:", "ok" if not bad else f"{bad} failures")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", help="keep and reuse the download in this folder")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.check:
        return check(a.cache)
    rows = rows_of(records(download(a.cache)))
    TSV.parent.mkdir(parents=True, exist_ok=True)
    TSV.write_text(tsv_text(rows), encoding="utf-8")
    OUT.write_text(header_text(rows), encoding="utf-8")
    print(f"wrote {TSV.relative_to(ROOT)} and {OUT.relative_to(ROOT)}: {len(rows)} airports, {table_bytes(rows)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
