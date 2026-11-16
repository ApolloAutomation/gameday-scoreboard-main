#!/usr/bin/env python3
"""Firmware size and parser memory against firmware/budget.json.

CI measures every build and fails a change that goes over the budget, or that
grows more in one go than the budget's max_growth allows against the base
branch, with the numbers in the error and in the job summary.

    python3 scripts/size_budget.py firmware <build dir> <variant> <out.json>
        Flash (the OTA image), static internal RAM and the main loop's
        deepest stack chain (both from the ELF) of one build: the folder
        build-action leaves, with its .ota.bin and .elf.
    python3 scripts/size_budget.py host <heap.json> <out.json>
        The parser figures `make -C tests heap` printed (tests/heap_budget.cpp).
    python3 scripts/size_budget.py check <sizes dir> [--base <dir>] [--base-label <text>] [--growth-ok]
        Every *.json under the sizes dir against the budget; with --base, also
        against the base branch's figures. --growth-ok (the PR's size-growth-ok
        label) lets a figure grow more than max_growth, with a warning.
    python3 scripts/size_budget.py growth-defaults
        Fails when budget.json's max_growth isn't GROWTH_DEFAULTS, as a
        release needs.
    python3 scripts/size_budget.py seed <sizes dir>
        Prints a budget with 10% headroom (at least 1 KB) over those figures.
    python3 scripts/size_budget.py --selftest

Static internal RAM is every allocated section the ELF places in the
ESP32-S3's internal data RAM (0x3FC88000 to 0x3FD00000). .dram0.dummy stands
in for the IRAM code that shares that memory, so this is the same figure as
ESPHome's "RAM:" line, and it is internal RAM the heap never gets.

loop_stack is the sum of the stack frames of LOOP_CHAIN, the main loop's
deepest known call chain (a Favorite teams read that redraws the second
area's clock), from each function's `entry a1, N` in the ELF. The loop's
stack is 16 KB (loop_task_stack_size in gameday-common.yaml) and the chain's
tail (snprintf and the clock's helpers) adds about 2 KB more.
"""

import json
import math
import os
import struct
import sys
import tempfile
from pathlib import Path

BUDGET = Path(__file__).resolve().parent.parent / "firmware" / "budget.json"
DRAM = (0x3FC88000, 0x3FD00000)
SHF_ALLOC = 0x2
HEADROOM = 0.10
HEADROOM_MIN = 1024  # so a 20 B read is not over budget at its next 16-letter string
# max_growth a release ships with
GROWTH_DEFAULTS = {"flash": 40960, "static_ram": 4096, "json": 20480, "heap": 20480}

# What each figure is, for the summary and the errors.
LABELS = {
    "flash": "flash (OTA image)",
    "static_ram": "static internal RAM",
    "json": "JSON documents (PSRAM on the panel)",
    "heap": "other heap (internal RAM on the panel)",
    "loop_stack": "main loop's deepest stack chain",
}

# The chain loop_stack adds up, outermost first: (namespace prefix, name).
_GAMEDAY = "7esphome7gameday16GamedayComponent"
LOOP_CHAIN = [("7esphome", "loop_task")] + [(_GAMEDAY, n) for n in (
    "loop", "apply_job_", "apply_fav_job_", "fav_no_event_", "fav_choose_", "emit_", "emit_a2_", "a2_fields_",
    "a2_clock_line_")]


def static_ram(elf: bytes) -> int:
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    total = 0
    for i in range(shnum):
        _name, _type, flags, addr, _off, size = struct.unpack_from("<IIIIII", elf, shoff + i * shentsize)
        if flags & SHF_ALLOC and DRAM[0] <= addr < DRAM[1]:
            total += size
    return total


def _sections(elf: bytes) -> list:
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("not a 32-bit little-endian ELF")
    shoff, = struct.unpack_from("<I", elf, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", elf, 0x2E)
    return [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]


def frame_sizes(elf: bytes, chain: list) -> list:
    """Each function's `entry a1, N` frame, matched by mangled name.

    `_ZN` or `_ZNK`, the prefix, then the name's length and the name, then
    the end of the name or an ABI tag: lambdas (`_ZZ...`) and clones
    (`$isra$0`, `.part.0`) are left out. Overloads take the largest.
    """
    secs = _sections(elf)
    found = {}
    for _name, typ, _flags, _addr, off, size, link, _info, _align, entsize in secs:
        if typ != 2:  # 2 is SHT_SYMTAB, the symbol table
            continue
        stroff = secs[link][4]
        for i in range(size // (entsize or 16)):
            st_name, value, _size, _info, _other, shndx = struct.unpack_from("<IIIBBH", elf, off + i * 16)
            end = elf.index(b"\0", stroff + st_name)
            sym = elf[stroff + st_name:end].decode("ascii", "replace")
            for k, (prefix, fn) in enumerate(chain):
                for head in ("_ZN", "_ZNK"):
                    stem = f"{head}{prefix}{len(fn)}{fn}"
                    if sym.startswith(stem) and sym[len(stem):len(stem) + 1] in ("E", "B") \
                            and "$" not in sym and "." not in sym:
                        found.setdefault(k, []).append((sym, value, shndx))
    out = []
    for k, (prefix, fn) in enumerate(chain):
        if k not in found:
            raise SystemExit(f"loop_stack: no symbol for {fn} in the ELF")
        best = 0
        for sym, value, shndx in found[k]:
            if not 0 < shndx < len(secs):
                raise SystemExit(f"loop_stack: {sym} is not in a section")
            _n, _t, _f, addr, off, size, *_ = secs[shndx]
            at = off + value - addr
            if not addr <= value < addr + size - 2:
                raise SystemExit(f"loop_stack: {sym} is outside its section")
            b0, b1, b2 = elf[at:at + 3]
            if b0 != 0x36 or b1 & 0x0F != 1:
                raise SystemExit(f"loop_stack: {sym} does not start with entry a1 (bytes {elf[at:at + 3].hex()})")
            best = max(best, ((b2 << 4) | (b1 >> 4)) * 8)
        out.append((fn, best))
    return out


def loop_stack(elf: bytes) -> int:
    return sum(n for _fn, n in frame_sizes(elf, LOOP_CHAIN))


def measure_firmware(build: Path, variant: str) -> dict:
    ota = sorted(build.glob("*.ota.bin"))
    elf = sorted(build.glob("*.elf"))
    if len(ota) != 1 or len(elf) != 1:
        raise SystemExit(f"{build}: expected one .ota.bin and one .elf, found {len(ota)} and {len(elf)}")
    data = elf[0].read_bytes()
    return {"firmware": {variant: {"flash": ota[0].stat().st_size, "static_ram": static_ram(data),
                                   "loop_stack": loop_stack(data)}}}


def load_sizes(folder: Path) -> dict:
    """Every *.json under folder, merged: {"firmware": {...}, "host": {...}}."""
    out = {}
    for f in sorted(folder.rglob("*.json")):
        for group, items in json.loads(f.read_text(encoding="utf-8")).items():
            out.setdefault(group, {}).update(items)
    return out


def fmt(n) -> str:
    return "" if n is None else f"{n:,}"


def check(budget: dict, sizes: dict, base: dict, base_label: str, growth_ok: bool = False) -> tuple:
    """Returns (errors, summary rows, warnings)."""
    errors, rows, warnings = [], [], []
    growth = budget.get("max_growth", {})
    for group in ("firmware", "host"):
        limits = budget.get(group, {})
        measured = sizes.get(group, {})
        for name in sorted(set(limits) | set(measured)):
            if name not in measured:
                errors.append(f"budget.json has {group}.{name} but nothing measured it. Remove the entry if it is gone.")
                continue
            if name not in limits:
                errors.append(f"{group}.{name} has no budget. Add it to firmware/budget.json "
                              f"(seed: python3 scripts/size_budget.py seed <sizes>).")
                continue
            for metric in sorted(set(limits[name]) - set(measured[name])):
                errors.append(f"budget.json has {group}.{name}.{metric} but nothing measured it. "
                              "Remove the entry if it is gone.")
            for metric, now in sorted(measured[name].items()):
                cap = limits[name].get(metric)
                was = base.get(group, {}).get(name, {}).get(metric)
                what = f"{name} {LABELS.get(metric, metric)}"
                if cap is None:
                    errors.append(f"{what}: no budget in firmware/budget.json")
                elif now > cap:
                    errors.append(f"{what} is {now:,} B, over its budget of {cap:,} B by {now - cap:,} B")
                if was is not None and metric in growth and now - was > growth[metric]:
                    msg = (f"{what} grew {now - was:,} B ({was:,} to {now:,} B) against {base_label}; "
                           f"one change may add at most {growth[metric]:,} B")
                    if growth_ok:
                        warnings.append(msg)
                    else:
                        errors.append(f"{msg}. If this growth is intended, the maintainer adds the "
                                      "size-growth-ok label to the PR and re-runs the Size budget job.")
                rows.append((name, LABELS.get(metric, metric), was, now, cap))
    return errors, rows, warnings


def summary(rows: list, errors: list, base_label: str, warnings: list = ()) -> str:
    lines = ["## Size budget", ""]
    lines.append("Over budget:" if errors else "Everything is within the budget in firmware/budget.json.")
    lines += [f"- {e}" for e in errors]
    lines += [f"- Allowed by the size-growth-ok label: {w}" for w in warnings]
    lines += ["", f"Base: {base_label}" if base_label else "No base figures (not a pull request, or none found).", ""]
    lines += ["| Build or read | Figure | Base | Now | Change | Budget | Left |", "|---|---|--:|--:|--:|--:|--:|"]
    for name, label, was, now, cap in rows:
        change = "" if was is None else f"{now - was:+,}"
        left = "" if cap is None else f"{cap - now:,}"
        lines.append(f"| {name} | {label} | {fmt(was)} | {fmt(now)} | {change} | {fmt(cap)} | {left} |")
    return "\n".join(lines) + "\n"


def seed(sizes: dict) -> dict:
    out = {}
    for group in ("firmware", "host"):
        out[group] = {name: {m: max(math.ceil(v * (1 + HEADROOM)), v + HEADROOM_MIN) for m, v in sorted(figs.items())}
                      for name, figs in sorted(sizes.get(group, {}).items())}
    return out


def run_check(args: list) -> int:
    sizes_dir = Path(args[0])
    base_dir = Path(args[args.index("--base") + 1]) if "--base" in args else None
    base_label = args[args.index("--base-label") + 1] if "--base-label" in args else ""
    budget = json.loads(BUDGET.read_text(encoding="utf-8"))
    base = load_sizes(base_dir) if base_dir and base_dir.is_dir() else {}
    if not base:
        base_label = ""
    errors, rows, warnings = check(budget, load_sizes(sizes_dir), base, base_label or "the base branch",
                                   "--growth-ok" in args)
    text = summary(rows, errors, base_label, warnings)
    out = os.environ.get("GITHUB_STEP_SUMMARY")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write(text)
    print(text)
    for e in errors:
        print(f"::error file=firmware/budget.json::{e}")
    for w in warnings:
        print(f"::warning file=firmware/budget.json::size-growth-ok label: {w}")
    return 1 if errors else 0


def growth_defaults(budget: dict) -> list:
    """Errors when max_growth isn't what a release ships with."""
    have = budget.get("max_growth", {})
    return [f"max_growth.{k} is {have.get(k)}, a release needs {v} (size_budget.py GROWTH_DEFAULTS)"
            for k, v in sorted(GROWTH_DEFAULTS.items()) if have.get(k) != v] + \
           [f"max_growth.{k} isn't in GROWTH_DEFAULTS" for k in sorted(set(have) - set(GROWTH_DEFAULTS))]


def selftest() -> None:
    # A three-section ELF: one DRAM section counted, one flash and one
    # non-allocated section left out.
    secs = [(0, 0, 0, 0), (SHF_ALLOC, 0x3FC9BD00, 0, 300), (SHF_ALLOC, 0x42000020, 0, 5000), (0, 0x3FC90000, 0, 7)]
    head = bytearray(52)
    head[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", head, 0x20, 52)
    struct.pack_into("<HH", head, 0x2E, 40, len(secs))
    body = b"".join(struct.pack("<IIIIIIIIII", 0, 1, f, a, o, s, 0, 0, 4, 0) for f, a, o, s in secs)
    assert static_ram(bytes(head) + body) == 300

    # Symbols: apply_job_ (entry a1, 1600) and its lambda.
    names = (b"\0_ZN7esphome7gameday16GamedayComponent10apply_job_Ev"
             b"\0_ZZN7esphome7gameday16GamedayComponent10apply_job_EvENKUlvE_clEv\0")
    lam = names.index(b"_ZZ")
    text = b"\x36\x81\x0c\x00" + b"\x36\x41\x00\x00"
    syms = b"\0" * 16 + struct.pack("<IIIBBH", 1, 0x42000000, 4, 0x12, 0, 1) + \
        struct.pack("<IIIBBH", lam, 0x42000004, 4, 0x12, 0, 1)
    off_text, off_syms = 52, 52 + len(text)
    off_str = off_syms + len(syms)
    off_sh = off_str + len(names)
    shdrs = [(0, 0, 0, 0, 0, 0, 0), (1, 0x6, 0x42000000, off_text, len(text), 0, 0),
             (2, 0, 0, off_syms, len(syms), 3, 16), (3, 0, 0, off_str, len(names), 0, 0)]
    head = bytearray(52)
    head[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", head, 0x20, off_sh)
    struct.pack_into("<HH", head, 0x2E, 40, len(shdrs))
    elf = bytes(head) + text + syms + names + b"".join(
        struct.pack("<IIIIIIIIII", 0, t, f, a, o, s, l, 0, 4, e) for t, f, a, o, s, l, e in shdrs)
    assert frame_sizes(elf, [(_GAMEDAY, "apply_job_")]) == [("apply_job_", 1600)]
    try:
        frame_sizes(elf, [(_GAMEDAY, "apply_job_"), (_GAMEDAY, "emit_")])
        raise AssertionError("a missing symbol must fail")
    except SystemExit as e:
        assert "no symbol for emit_" in str(e), e
    # A function without an entry opcode is an error.
    bad = elf[:off_text] + b"\x00\x00\x00" + elf[off_text + 3:]
    try:
        frame_sizes(bad, [(_GAMEDAY, "apply_job_")])
        raise AssertionError("a function without entry must fail")
    except SystemExit as e:
        assert "does not start with entry" in str(e), e

    budget = {"max_growth": {"flash": 100}, "firmware": {"a": {"flash": 1000, "static_ram": 50}}}
    ok, rows, _ = check(budget, {"firmware": {"a": {"flash": 900, "static_ram": 50}}}, {}, "dev")
    assert ok == [] and len(rows) == 2
    over, _, _ = check(budget, {"firmware": {"a": {"flash": 1001, "static_ram": 50}}}, {}, "dev")
    assert len(over) == 1 and "over its budget" in over[0]
    grew, _, _ = check(budget, {"firmware": {"a": {"flash": 950, "static_ram": 50}}},
                       {"firmware": {"a": {"flash": 800}}}, "dev")
    assert len(grew) == 1 and "grew 150 B" in grew[0] and "re-runs the Size budget job" in grew[0], grew
    # The label turns growth into a warning, never the ceiling.
    grew, _, warned = check(budget, {"firmware": {"a": {"flash": 1950, "static_ram": 50}}},
                            {"firmware": {"a": {"flash": 800}}}, "dev", growth_ok=True)
    assert len(grew) == 1 and "over its budget" in grew[0] and len(warned) == 1 and "grew" in warned[0], (grew, warned)
    assert "label" not in warned[0], warned
    new, _, _ = check(budget, {"firmware": {"a": {"flash": 1, "static_ram": 1}, "b": {"flash": 1}}}, {}, "dev")
    assert len(new) == 1 and "no budget" in new[0]
    gone, _, _ = check(budget, {}, {}, "dev")
    assert len(gone) == 1 and "nothing measured" in gone[0]
    assert growth_defaults({"max_growth": dict(GROWTH_DEFAULTS)}) == []
    assert len(growth_defaults({"max_growth": {**GROWTH_DEFAULTS, "flash": 1}})) == 1
    # A figure the budget caps but the build no longer reports is not a pass.
    half, rows, _ = check(budget, {"firmware": {"a": {"flash": 900}}}, {}, "dev")
    assert len(half) == 1 and "a.static_ram" in half[0] and "nothing measured" in half[0], half
    assert len(rows) == 1
    assert seed({"host": {"x": {"json": 20000, "heap": 18}}}) == {"firmware": {}, "host": {"x": {"json": 22000, "heap": 1042}}}

    with tempfile.TemporaryDirectory() as d:
        Path(d, "one").mkdir()
        Path(d, "one", "a.json").write_text('{"firmware": {"a": {"flash": 1}}}')
        Path(d, "b.json").write_text('{"firmware": {"b": {"flash": 2}}, "host": {"x": {"heap": 3}}}')
        assert load_sizes(Path(d)) == {"firmware": {"a": {"flash": 1}, "b": {"flash": 2}}, "host": {"x": {"heap": 3}}}
    print("size_budget selftest ok")


def write(path: str, data: dict) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(data, indent=2))


def main(argv: list) -> int:
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    if argv[:1] == ["firmware"] and len(argv) == 4:
        write(argv[3], measure_firmware(Path(argv[1]), argv[2]))
        return 0
    if argv[:1] == ["host"] and len(argv) == 3:
        write(argv[2], {"host": json.loads(Path(argv[1]).read_text(encoding="utf-8"))})
        return 0
    if argv[:1] == ["check"] and len(argv) >= 2:
        return run_check(argv[1:])
    if argv[:1] == ["growth-defaults"] and len(argv) == 1:
        errors = growth_defaults(json.loads(BUDGET.read_text(encoding="utf-8")))
        for e in errors:
            print(f"::error file=firmware/budget.json::{e}. Put it back before the release.")
        return 1 if errors else 0
    if argv[:1] == ["seed"] and len(argv) == 2:
        print(json.dumps(seed(load_sizes(Path(argv[1]))), indent=2))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
