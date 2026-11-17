#!/usr/bin/env python3
"""Generate Unicode tables for the regex engine (ADR-062 R3).

Usage:
    python tools/gen_unicode_regex_tables.py <ucd-dir> <output.inc>

<ucd-dir> must contain Unicode 15.1.0 files:
    UnicodeData.txt, Scripts.txt, ScriptExtensions.txt, PropList.txt,
    DerivedCoreProperties.txt, PropertyValueAliases.txt, CaseFolding.txt,
    emoji/emoji-data.txt

The generated table is pinned to the Unicode version of the input files.
"""

import os
import sys


def parse_range(span):
    span = span.strip()
    if ".." in span:
        first, last = span.split("..")
        return int(first, 16), int(last, 16)
    value = int(span, 16)
    return value, value


def parse_semicolon_file(path, want_extra=False):
    """Yield (first, last, fields-after-first) for each data line."""
    with open(path, encoding="utf-8") as source:
        for raw in source:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [part.strip() for part in line.split(";")]
            if len(fields) < 2:
                continue
            first, last = parse_range(fields[0])
            yield first, last, fields[1:]


def normalize(ranges):
    ranges.sort()
    merged = []
    for first, last in ranges:
        if merged and first <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], last))
        else:
            merged.append((first, last))
    return merged


def complement(ranges, maximum=0x10FFFF):
    result = []
    next_cp = 0
    for first, last in normalize(list(ranges)):
        if first > next_cp:
            result.append((next_cp, first - 1))
        next_cp = last + 1
    if next_cp <= maximum:
        result.append((next_cp, maximum))
    return result


def subtract(ranges, cut_first, cut_last):
    result = []
    for first, last in ranges:
        if last < cut_first or first > cut_last:
            result.append((first, last))
            continue
        if first < cut_first:
            result.append((first, cut_first - 1))
        if last > cut_last:
            result.append((cut_last + 1, last))
    return result


def parse_unicode_data(path):
    categories = {}
    assigned = []
    range_start = None
    range_category = None
    with open(path, encoding="utf-8") as source:
        for raw in source:
            fields = raw.rstrip("\n").split(";")
            if len(fields) < 3:
                continue
            cp = int(fields[0], 16)
            name = fields[1]
            category = fields[2]
            if name.endswith(", First>"):
                range_start = cp
                range_category = category
                continue
            if name.endswith(", Last>"):
                categories.setdefault(range_category, []).append(
                    (range_start, cp))
                assigned.append((range_start, cp))
                range_start = None
                range_category = None
                continue
            categories.setdefault(category, []).append((cp, cp))
            assigned.append((cp, cp))
    return categories, normalize(assigned)


def parse_aliases(path):
    gc_aliases = {}
    sc_aliases = {}
    with open(path, encoding="utf-8") as source:
        for raw in source:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [part.strip() for part in line.split(";")]
            if len(fields) < 2:
                continue
            prop = fields[0]
            if prop == "gc":
                canonical = fields[1]
                names = [part for part in fields[1:] if part]
                gc_aliases.setdefault(canonical, set()).update(names)
            elif prop == "sc":
                canonical = fields[1]
                names = [part for part in fields[1:] if part]
                sc_aliases.setdefault(canonical, set()).update(names)
    return gc_aliases, sc_aliases


def parse_properties(path):
    props = {}
    for first, last, rest in parse_semicolon_file(path):
        name = rest[0]
        props.setdefault(name, []).append((first, last))
    return {name: normalize(ranges) for name, ranges in props.items()}


def parse_scripts(path):
    scripts = {}
    for first, last, rest in parse_semicolon_file(path):
        scripts.setdefault(rest[0], []).append((first, last))
    return {name: normalize(ranges) for name, ranges in scripts.items()}


def parse_script_extensions(path):
    overrides = []
    for first, last, rest in parse_semicolon_file(path):
        codes = [part for part in rest[0].split() if part]
        overrides.append((first, last, set(codes)))
    return overrides


def parse_case_folding(path):
    folds = {}
    with open(path, encoding="utf-8") as source:
        for raw in source:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [part.strip() for part in line.split(";")]
            if len(fields) < 3:
                continue
            status = fields[1]
            if status not in ("C", "S"):
                continue
            folds[int(fields[0], 16)] = int(fields[2], 16)
    return folds


def sanitize(name):
    return "".join(ch if ch.isalnum() else "_" for ch in name)


def emit_ranges(identifier, ranges, out):
    out.write(f"constexpr CodePointRange {identifier}[] = {{\n")
    for first, last in ranges:
        out.write(f"    {{0x{first:04X}u, 0x{last:04X}u}},\n")
    out.write("};\n\n")


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    ucd, output = sys.argv[1], sys.argv[2]

    categories, assigned = parse_unicode_data(
        os.path.join(ucd, "UnicodeData.txt"))
    gc_aliases, sc_aliases = parse_aliases(
        os.path.join(ucd, "PropertyValueAliases.txt"))
    scripts = parse_scripts(os.path.join(ucd, "Scripts.txt"))
    overrides = parse_script_extensions(
        os.path.join(ucd, "ScriptExtensions.txt"))
    folds = parse_case_folding(os.path.join(ucd, "CaseFolding.txt"))

    binary = {}
    binary.update(parse_properties(os.path.join(ucd, "PropList.txt")))
    binary.update(parse_properties(
        os.path.join(ucd, "DerivedCoreProperties.txt")))
    binary.update(parse_properties(
        os.path.join(ucd, "emoji", "emoji-data.txt")))

    # General_Category: subcategories + one-letter groups + Cn.
    gc_tables = {}
    for category, ranges in categories.items():
        gc_tables[category] = normalize(ranges)
    gc_tables["Cn"] = complement(assigned)
    for group in ("L", "M", "N", "P", "S", "Z", "C"):
        merged = []
        for category, ranges in categories.items():
            if category.startswith(group):
                merged.extend(ranges)
        if group == "C":
            merged.extend(gc_tables["Cn"])
        gc_tables[group] = normalize(merged)

    # Script: unlisted code points are Unknown (Zzzz).
    script_by_code = {}
    for canonical, aliases in sc_aliases.items():
        for alias in aliases:
            script_by_code[alias] = canonical
    script_tables = {}
    all_script_codes = set()
    for name, ranges in scripts.items():
        code = script_by_code.get(name, name)
        script_tables[code] = normalize(ranges)
        all_script_codes.add(code)
    listed = []
    for ranges in scripts.values():
        listed.extend(ranges)
    script_tables["Zzzz"] = complement(listed)
    all_script_codes.add("Zzzz")

    # Script_Extensions: start from Script, then apply explicit overrides.
    scx_tables = {code: list(ranges) for code, ranges in script_tables.items()}
    for first, last, codes in overrides:
        for code in all_script_codes:
            scx_tables[code] = subtract(scx_tables[code], first, last)
        for code in codes:
            canonical = script_by_code.get(code, code)
            scx_tables.setdefault(canonical, []).append((first, last))
            all_script_codes.add(canonical)
    scx_tables = {code: normalize(ranges) for code, ranges in scx_tables.items()}

    # Special binary properties.
    binary["Any"] = [(0, 0x10FFFF)]
    binary["ASCII"] = [(0, 0x7F)]
    binary["Assigned"] = complement(gc_tables["Cn"])

    with open(output, "w", encoding="utf-8", newline="\n") as out:
        out.write("// Generated by tools/gen_unicode_regex_tables.py. "
                  "Do not edit.\n")
        out.write("// Source: Unicode 15.1.0 UCD.\n\n")

        entries = []  # (alias name, identifier)

        for category in sorted(gc_tables):
            identifier = "kGc" + sanitize(category)
            emit_ranges(identifier, gc_tables[category], out)
            aliases = gc_aliases.get(category, {category})
            for alias in sorted(aliases):
                entries.append(("gc:" + alias, identifier))

        for code in sorted(script_tables):
            identifier = "kScript" + sanitize(code)
            emit_ranges(identifier, script_tables[code], out)
            aliases = sc_aliases.get(code, {code})
            for alias in sorted(aliases):
                entries.append(("sc:" + alias, identifier))

        for code in sorted(scx_tables):
            identifier = "kScx" + sanitize(code)
            emit_ranges(identifier, scx_tables[code], out)
            aliases = sc_aliases.get(code, {code})
            for alias in sorted(aliases):
                entries.append(("scx:" + alias, identifier))

        for name in sorted(binary):
            identifier = "kBin" + sanitize(name)
            emit_ranges(identifier, binary[name], out)
            entries.append((name, identifier))

        out.write("struct PropertyEntry {\n")
        out.write("    const char* name;\n")
        out.write("    const CodePointRange* ranges;\n")
        out.write("    std::size_t count;\n")
        out.write("};\n\n")
        out.write("constexpr PropertyEntry kProperties[] = {\n")
        for name, identifier in entries:
            out.write(f'    {{"{name}", {identifier}, '
                      f"sizeof({identifier}) / sizeof(CodePointRange)}},\n")
        out.write("};\n\n")

        # Simple case folding: canonical C+S mapping.
        out.write("constexpr FoldPair kSimpleFold[] = {\n")
        for source in sorted(folds):
            out.write(f"    {{0x{source:04X}u, 0x{folds[source]:04X}u}},\n")
        out.write("};\n\n")

        # Bidirectional orbit pairs for case-insensitive class matching.
        orbit = set()
        for source, target in folds.items():
            if source != target:
                orbit.add((source, target))
                orbit.add((target, source))
        out.write("constexpr FoldPair kFoldOrbit[] = {\n")
        for source, target in sorted(orbit):
            out.write(f"    {{0x{source:04X}u, 0x{target:04X}u}},\n")
        out.write("};\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
