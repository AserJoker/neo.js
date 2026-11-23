#!/usr/bin/env python3
"""Generate Unicode case-mapping / normalization tables.

Usage:
    python tools/gen_unicode_case_norm_tables.py <ucd-dir> <output.inc>

<ucd-dir> must contain Unicode 15.1.0 files:
    UnicodeData.txt, SpecialCasing.txt, DerivedNormalizationProps.txt,
    DerivedCoreProperties.txt

Generated tables (all sorted by code point):
    kSimpleLower / kSimpleUpper      simple case mappings (1:1)
    kLowerSpecial / kUpperSpecial    full case mappings (1:n, unconditional)
    kCanonicalDecomposition          canonical decomposition (tag-free)
    kCompatibilityDecomposition      compatibility decomposition (tags stripped)
    kComposition                     canonical composition pairs
    kCombiningClass                  non-zero canonical combining classes
    kCasedRanges / kCaseIgnorableRanges  for the Final_Sigma condition

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


def parse_property_ranges(path, wanted):
    ranges = {name: [] for name in wanted}
    with open(path, encoding="utf-8") as source:
        for raw in source:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [part.strip() for part in line.split(";")]
            if len(fields) < 2 or fields[1] not in ranges:
                continue
            ranges[fields[1]].append(parse_range(fields[0]))
    for name, spans in ranges.items():
        spans.sort()
        merged = []
        for first, last in spans:
            if merged and first <= merged[-1][1] + 1:
                merged[-1] = (merged[-1][0], max(merged[-1][1], last))
            else:
                merged.append((first, last))
        ranges[name] = merged
    return ranges


def parse_unicode_data(path):
    simple_lower = {}
    simple_upper = {}
    canonical = {}
    compatibility = {}
    combining = {}
    with open(path, encoding="utf-8") as source:
        for raw in source:
            fields = raw.rstrip("\n").split(";")
            if len(fields) < 15:
                continue
            name = fields[1]
            if name.endswith(", First>") or name.endswith(", Last>"):
                continue
            cp = int(fields[0], 16)
            ccc = int(fields[3])
            if ccc:
                combining[cp] = ccc
            decomposition = fields[5].strip()
            if decomposition:
                parts = decomposition.split()
                if parts[0].startswith("<"):
                    compatibility[cp] = [int(part, 16) for part in parts[1:]]
                else:
                    seq = [int(part, 16) for part in parts]
                    canonical[cp] = seq
                    compatibility[cp] = seq
            if fields[12]:
                simple_upper[cp] = int(fields[12], 16)
            if fields[13]:
                simple_lower[cp] = int(fields[13], 16)
    return simple_lower, simple_upper, canonical, compatibility, combining


def parse_special_casing(path):
    lower = {}
    upper = {}
    with open(path, encoding="utf-8") as source:
        for raw in source:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [part.strip() for part in line.split(";")]
            if len(fields) < 5:
                continue
            condition = fields[4]
            if condition:
                # 条件映射（Final_Sigma 等）由运行时的上下文算法处理。
                continue
            cp = int(fields[0], 16)
            lower[cp] = [int(part, 16) for part in fields[1].split()]
            upper[cp] = [int(part, 16) for part in fields[3].split()]
    return lower, upper


def parse_composition_exclusions(path):
    return parse_property_ranges(
        path, ["Full_Composition_Exclusion"])["Full_Composition_Exclusion"]


def in_ranges(cp, ranges):
    for first, last in ranges:
        if cp < first:
            return False
        if cp <= last:
            return True
    return False


def emit_sequence_arrays(name, mapping, out):
    for cp in sorted(mapping):
        seq = mapping[cp]
        values = ", ".join(f"0x{value:04X}u" for value in seq)
        out.write(f"constexpr uint32_t {name}_{cp:04X}[] = {{{values}}};\n")
    out.write("\n")


def emit_mapping_table(name, mapping, out):
    emit_sequence_arrays(name + "Seq", mapping, out)
    out.write(f"constexpr UnicodeMapping {name}[] = {{\n")
    for cp in sorted(mapping):
        out.write(
            f"    {{0x{cp:04X}u, {name}Seq_{cp:04X}, "
            f"{len(mapping[cp])}}},\n")
    out.write("};\n\n")


def emit_simple_table(name, mapping, out):
    out.write(f"constexpr UnicodeSimpleMapping {name}[] = {{\n")
    for cp in sorted(mapping):
        out.write(f"    {{0x{cp:04X}u, 0x{mapping[cp]:04X}u}},\n")
    out.write("};\n\n")


def emit_ranges(name, ranges, out):
    out.write(f"constexpr UnicodeRange {name}[] = {{\n")
    for first, last in ranges:
        out.write(f"    {{0x{first:04X}u, 0x{last:04X}u}},\n")
    out.write("};\n\n")


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    ucd, output = sys.argv[1], sys.argv[2]

    simple_lower, simple_upper, canonical, compatibility, combining = \
        parse_unicode_data(os.path.join(ucd, "UnicodeData.txt"))
    lower_special, upper_special = parse_special_casing(
        os.path.join(ucd, "SpecialCasing.txt"))
    exclusions = parse_composition_exclusions(
        os.path.join(ucd, "DerivedNormalizationProps.txt"))
    props = parse_property_ranges(
        os.path.join(ucd, "DerivedCoreProperties.txt"),
        ["Cased", "Case_Ignorable"])

    # 组合对：规范分解恰为两个码点且未被 Full_Composition_Exclusion 排除。
    composition = {}
    for cp, seq in canonical.items():
        if len(seq) != 2:
            continue
        if in_ranges(cp, exclusions):
            continue
        composition[(seq[0], seq[1])] = cp

    with open(output, "w", encoding="utf-8", newline="\n") as out:
        out.write("// Generated by tools/gen_unicode_case_norm_tables.py. "
                  "Do not edit.\n")
        out.write("// Source: Unicode 15.1.0 UCD.\n\n")

        emit_simple_table("kSimpleLower", simple_lower, out)
        emit_simple_table("kSimpleUpper", simple_upper, out)
        emit_mapping_table("kLowerSpecial", lower_special, out)
        emit_mapping_table("kUpperSpecial", upper_special, out)
        emit_mapping_table("kCanonicalDecomposition", canonical, out)
        emit_mapping_table("kCompatibilityDecomposition", compatibility, out)

        out.write("constexpr UnicodeComposition kComposition[] = {\n")
        for (first, second), composed in sorted(composition.items()):
            out.write(
                f"    {{0x{first:04X}u, 0x{second:04X}u, "
                f"0x{composed:04X}u}},\n")
        out.write("};\n\n")

        out.write("constexpr UnicodeCombiningClass kCombiningClass[] = {\n")
        for cp in sorted(combining):
            out.write(f"    {{0x{cp:04X}u, {combining[cp]}}},\n")
        out.write("};\n\n")

        emit_ranges("kCasedRanges", props["Cased"], out)
        emit_ranges("kCaseIgnorableRanges", props["Case_Ignorable"], out)

    print(
        f"simple lower: {len(simple_lower)}, simple upper: {len(simple_upper)}, "
        f"lower special: {len(lower_special)}, upper special: "
        f"{len(upper_special)}, canonical: {len(canonical)}, "
        f"compat: {len(compatibility)}, composition: {len(composition)}, "
        f"ccc: {len(combining)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
