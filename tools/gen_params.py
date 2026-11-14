#!/usr/bin/env python3
"""Generate Common Sense EQ's parameter header and LV2 metadata.

This file is the single source of truth for the parameter list.  It writes:

  src/cse/param_info.hpp      C++ parameter/port table
  lv2/common-sense-eq.ttl     full LV2 plugin description
  lv2/manifest.ttl            plugin + UI + factory presets

Run from the repository root:  python3 tools/gen_params.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "cse" / "param_info.hpp"
TTL = ROOT / "lv2" / "common-sense-eq.ttl"
MANIFEST = ROOT / "lv2" / "manifest.ttl"

BASE = "https://github.com/tylorismusic-commits/common-sense-eq"
PLUGIN = BASE
UI = BASE + "#ui"

BANDS = [
    ("Low Cut", "low_cut", 20.0, 0.71, 24),
    ("Low Shelf", "low_shelf", 75.0, 0.71, 24),
    ("Bell 1", "bell1", 100.0, 0.60, 24),
    ("Bell 2", "bell2", 270.0, 0.78, 24),
    ("Bell 3", "bell3", 750.0, 0.30, 24),
    ("Bell 4", "bell4", 2500.0, 0.20, 24),
    ("High Shelf", "high_shelf", 4600.0, 0.71, 24),
    ("High Cut", "high_cut", 20000.0, 0.71, 24),
]

# (symbol, name, min, max, default, unit, kind, group, points)
PARAMS = []


def p(sym, name, lo, hi, default, unit, kind, group, points=None):
    PARAMS.append(dict(sym=sym, name=name, lo=lo, hi=hi, default=default,
                       unit=unit, kind=kind, group=group,
                       points=points or []))


p("enable", "Enabled", 0, 1, 1, None, "toggle", "global")
p("output_gain", "Output Gain", -24, 24, 0, "db", "float", "global")
p("analyzer_mode", "Analyzer", 0, 1, 1, None, "enum", "global",
  [("PRE", 0), ("POST", 1)])
p("q_couple", "Q-Couple", 0, 1, 0, None, "toggle", "global")
p("processing", "Processing", 0, 3, 0, None, "enum", "global",
  [("STEREO", 0), ("MONO", 1), ("LEFT", 2), ("RIGHT", 3)])

SLOTS = [
    ("on", "On", 0, 1, 1, None, "toggle", None),
    ("freq", "Frequency", 20, 20000, None, "hz", "log", None),
    ("gain", "Gain", -30, 30, 0, "db", "float", None),
    ("q", "Q", 0.1, 25, None, None, "log", None),
    ("slope", "Slope", 12, 24, None, None, "enum", [("12", 12), ("24", 24)]),
    ("dyn_on", "Dynamic", 0, 1, 0, None, "toggle", None),
    ("dyn_threshold", "Threshold", -60, 0, -24, "db", "float", None),
    ("dyn_ratio", "Ratio", 1, 20, 4, None, "float", None),
    ("dyn_attack", "Attack", 0.1, 100, 10, "ms", "log", None),
    ("dyn_release", "Release", 10, 2000, 150, "ms", "log", None),
    ("dyn_range", "Range", 0, 24, 6, "db", "float", None),
    ("dyn_mode", "Dynamic Mode", 0, 1, 0, None, "enum",
     [("DOWN", 0), ("UP", 1)]),
]

for i, (label, sym, freq, q, slope) in enumerate(BANDS, start=1):
    group = f"band{i}"
    defaults = {"freq": freq, "q": q, "slope": slope}
    for slot, name, lo, hi, default, unit, kind, points in SLOTS:
        d = defaults.get(slot, default)
        p(f"{sym}_{slot}", f"{label} {name}", lo, hi, d, unit, kind, group,
          points)

# Telemetry ports.
SPECTRUM_COUNT = 32
GR_COUNT = 8

# Port layout: audio first, then the control parameters, then telemetry.
PORT_AUDIO_IN_L = 0
PORT_AUDIO_IN_R = 1
PORT_AUDIO_OUT_L = 2
PORT_AUDIO_OUT_R = 3
PORT_PARAM_BASE = 4
PARAM_COUNT = len(PARAMS)
PORT_SPECTRUM_BASE = PORT_PARAM_BASE + PARAM_COUNT
PORT_GR_BASE = PORT_SPECTRUM_BASE + SPECTRUM_COUNT
PORT_COUNT = PORT_GR_BASE + GR_COUNT

for i, item in enumerate(PARAMS):
    item["index"] = PORT_PARAM_BASE + i

UNIT_TTL = {"db": "units:db", "hz": "units:hz", "ms": "units:ms",
            "pc": "units:pc"}
UNIT_CPP = {"db": "Unit::Db", "hz": "Unit::Hz", "ms": "Unit::Ms",
            "pc": "Unit::Percent"}

# ---------------------------------------------------------------------------
# Factory presets.  Only overrides are listed; everything else is the default.
# Keys are "band:<slot>" or a global symbol.
# ---------------------------------------------------------------------------
PRESETS = [
    ("Flat", {}),
    ("Vocal Presence", {
        "band:3:gain": -2.0,
        "band:4:freq": 300.0, "band:4:gain": -1.5, "band:4:q": 1.00,
        "band:5:gain": 2.0, "band:5:q": 0.80,
        "band:6:freq": 2800.0, "band:6:gain": 3.5, "band:6:q": 1.20,
        "band:7:freq": 7000.0, "band:7:gain": 1.5, "band:7:q": 0.70,
    }),
    ("Kick Punch", {
        "band:2:freq": 65.0, "band:2:gain": 3.5, "band:2:q": 0.90,
        "band:3:freq": 110.0, "band:3:gain": -2.5, "band:3:q": 1.00,
        "band:4:freq": 300.0, "band:4:gain": -3.0, "band:4:q": 1.20,
        "band:5:gain": -1.0,
        "band:7:freq": 4000.0, "band:7:gain": 1.5,
    }),
    ("Air & Shine", {
        "band:6:freq": 2500.0, "band:6:gain": 1.0,
        "band:7:freq": 6800.0, "band:7:gain": 3.0, "band:7:q": 0.65,
        "band:8:freq": 18000.0,
    }),
    ("Dynamic De-Ess", {
        "band:6:freq": 5600.0, "band:6:gain": -4.0, "band:6:q": 2.50,
        "band:6:dyn_on": 1, "band:6:dyn_threshold": -30.0,
        "band:6:dyn_ratio": 5.0, "band:6:dyn_attack": 0.5,
        "band:6:dyn_release": 80.0, "band:6:dyn_range": 8.0,
    }),
    ("Dynamic Low Control", {
        "band:2:freq": 80.0, "band:2:gain": -3.0, "band:2:q": 0.80,
        "band:2:dyn_on": 1, "band:2:dyn_threshold": -28.0,
        "band:2:dyn_ratio": 3.0, "band:2:dyn_attack": 15.0,
        "band:2:dyn_release": 220.0, "band:2:dyn_range": 9.0,
    }),
]


def fmt(v):
    if isinstance(v, float):
        if v.is_integer():
            return str(int(v))
        text = f"{v:.6f}".rstrip("0").rstrip(".")
        return text if text else "0"
    return str(v)


def cpp_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def preset_value(key):
    """Return (symbol, value) for a preset override key."""
    if key.startswith("band:"):
        _, band, slot = key.split(":")
        idx = int(band) - 1
        _, sym, *_ = BANDS[idx]
        return f"{sym}_{slot}", None
    return key, None


def override_for(key, value):
    if key.startswith("band:"):
        _, band, slot = key.split(":")
        idx = int(band) - 1
        return f"{BANDS[idx][1]}_{slot}", value
    return key, value


# ---------------------------------------------------------------------------
# C++ header
# ---------------------------------------------------------------------------
def write_header():
    out = []
    out.append("// Common Sense EQ - param_info.hpp")
    out.append("// SPDX-License-Identifier: GPL-3.0-or-later")
    out.append("// Generated by tools/gen_params.py.  Do not edit by hand.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace cse {")
    out.append("")
    out.append("enum class Unit { Unitless, Db, Hz, Ms, Percent };")
    out.append("")
    out.append("struct ScalePoint { float value; const char* label; };")
    out.append("")
    out.append("struct ParamInfo {")
    out.append("  uint32_t index;")
    out.append("  const char* symbol;")
    out.append("  const char* name;")
    out.append("  float min;")
    out.append("  float max;")
    out.append("  float def;")
    out.append("  Unit unit;")
    out.append("  bool logarithmic;")
    out.append("  bool toggle;")
    out.append("  bool enumeration;")
    out.append("  int group;")
    out.append("  const ScalePoint* points;")
    out.append("  int pointCount;")
    out.append("};")
    out.append("")
    out.append("inline constexpr uint32_t kBandCount = 8;")
    out.append("inline constexpr uint32_t kSpectrumCount = %d;" % SPECTRUM_COUNT)
    out.append("inline constexpr uint32_t kGrCount = %d;" % GR_COUNT)
    out.append("")
    out.append("enum BandSlot : uint32_t {")
    out.append("  kSlotOn = 0, kSlotFreq, kSlotGain, kSlotQ, kSlotSlope,")
    out.append("  kSlotDynOn, kSlotDynThreshold, kSlotDynRatio, kSlotDynAttack,")
    out.append("  kSlotDynRelease, kSlotDynRange, kSlotDynMode,")
    out.append("  kSlotCount")
    out.append("};")
    out.append("")
    out.append("enum PortIndex : uint32_t {")
    out.append("  PORT_AUDIO_IN_L = %d," % PORT_AUDIO_IN_L)
    out.append("  PORT_AUDIO_IN_R = %d," % PORT_AUDIO_IN_R)
    out.append("  PORT_AUDIO_OUT_L = %d," % PORT_AUDIO_OUT_L)
    out.append("  PORT_AUDIO_OUT_R = %d," % PORT_AUDIO_OUT_R)
    for item in PARAMS:
        out.append('  PORT_%s = %d,' % (item["sym"].upper(), item["index"]))
    out.append("  PORT_SPECTRUM_1 = %d," % PORT_SPECTRUM_BASE)
    for i in range(2, SPECTRUM_COUNT + 1):
        out.append("  PORT_SPECTRUM_%d = %d," % (i, PORT_SPECTRUM_BASE + i - 1))
    out.append("  PORT_GR_1 = %d," % PORT_GR_BASE)
    for i in range(2, GR_COUNT + 1):
        out.append("  PORT_GR_%d = %d," % (i, PORT_GR_BASE + i - 1))
    out.append("  PORT_COUNT = %d" % PORT_COUNT)
    out.append("};")
    out.append("")
    out.append("inline constexpr uint32_t kBandParamBase = %d;" % (PORT_PARAM_BASE + 5))
    out.append("inline constexpr uint32_t bandParam(int band, BandSlot slot) {")
    out.append("  return kBandParamBase + static_cast<uint32_t>(band) *")
    out.append("         static_cast<uint32_t>(kSlotCount) +")
    out.append("         static_cast<uint32_t>(slot);")
    out.append("}")
    out.append("")
    out.append("inline constexpr int kGroupCount = 10;")
    out.append("inline constexpr const char* kGroupNames[kGroupCount] = {")
    out.append('    "Global", "Low Cut", "Low Shelf", "Bell 1", "Bell 2",')
    out.append('    "Bell 3", "Bell 4", "High Shelf", "High Cut", "Meters"};')
    out.append("")

    # Scale point arrays.
    for item in PARAMS:
        if item["points"]:
            name = "kScale_" + item["sym"]
            entries = ", ".join(
                '{%s, "%s"}' % (fmt(v), label.replace('"', '\\"'))
                for label, v in item["points"])
            out.append("inline constexpr ScalePoint %s[] = {%s};" % (name, entries))
    out.append("")

    out.append("inline constexpr ParamInfo kParams[] = {")
    for item in PARAMS:
        unit = UNIT_CPP.get(item["unit"] or "", "Unit::Unitless")
        kind = item["kind"]
        log = "true" if kind == "log" else "false"
        toggle = "true" if kind == "toggle" else "false"
        enum = "true" if kind == "enum" else "false"
        group = {"global": 0}.get(item["group"], 0)
        if item["group"].startswith("band"):
            group = int(item["group"][4:])
        pts = ("kScale_" + item["sym"]) if item["points"] else "nullptr"
        count = len(item["points"])
        out.append(
            '    {%d, "%s", "%s", %s, %s, %s, %s, %s, %s, %s, %d, %s, %d},' %
            (item["index"], item["sym"], cpp_escape(item["name"]),
             fmt(item["lo"]), fmt(item["hi"]), fmt(item["default"]), unit,
             log, toggle, enum, group, pts, count))
    out.append("};")
    out.append("inline constexpr int kParamCount = %d;" % PARAM_COUNT)
    out.append("")
    out.append("inline constexpr const ParamInfo* paramsBegin() { return kParams; }")
    out.append("inline constexpr const ParamInfo* paramsEnd() {")
    out.append("  return kParams + kParamCount;")
    out.append("}")
    out.append("")
    out.append("inline const ParamInfo* infoFor(uint32_t index) {")
    out.append("  for (int i = 0; i < kParamCount; ++i) {")
    out.append("    if (kParams[i].index == index) return &kParams[i];")
    out.append("  }")
    out.append("  return nullptr;")
    out.append("}")
    out.append("")

    # Defaults used by the UI.
    out.append("inline constexpr float kBandDefaultFreq[8] = {%s};" %
               ", ".join(fmt(b[2]) for b in BANDS))
    out.append("inline constexpr float kBandDefaultQ[8] = {%s};" %
               ", ".join(fmt(b[3]) for b in BANDS))
    out.append("inline constexpr int kBandDefaultSlope[8] = {%s};" %
               ", ".join(str(b[4]) for b in BANDS))
    out.append("")

    # Presets: flat override lists.
    out.append("struct PresetValue { uint32_t port; float value; };")
    out.append("struct PresetSpec { const char* name; const PresetValue* values; int count; };")
    for pi, (name, overrides) in enumerate(PRESETS):
        entries = []
        for key, value in overrides.items():
            sym, val = override_for(key, value)
            item = next(p for p in PARAMS if p["sym"] == sym)
            entries.append("{%d, %s}" % (item["index"], fmt(float(val))))
        out.append("inline constexpr PresetValue kPreset%dValues[] = {%s};" %
                   (pi, ", ".join(entries) if entries else "{0, 0}"))
    out.append("inline constexpr PresetSpec kPresets[] = {")
    for pi, (name, overrides) in enumerate(PRESETS):
        count = len(overrides)
        out.append('    {"%s", kPreset%dValues, %d},' % (name, pi, count))
    out.append("};")
    out.append("inline constexpr int kPresetCount = %d;" % len(PRESETS))
    out.append("")
    out.append("inline constexpr uint32_t kSpectrumBase = PORT_SPECTRUM_1;")
    out.append("inline constexpr uint32_t kGrBase = PORT_GR_1;")
    out.append("")
    out.append("} // namespace cse")
    out.append("")
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text("\n".join(out))


# ---------------------------------------------------------------------------
# LV2 TTL
# ---------------------------------------------------------------------------
def port_block(item):
    lines = ["\t\t["]
    lines.append("\t\t\ta lv2:ControlPort , lv2:InputPort ;")
    lines.append(f"\t\t\tlv2:index {item['index']} ;")
    lines.append(f'\t\t\tlv2:symbol "{item["sym"]}" ;')
    lines.append(f'\t\t\tlv2:name "{item["name"]}" ;')
    lines.append(f"\t\t\tpg:group <{PLUGIN}#{item['group']}> ;")
    lines.append(f"\t\t\tlv2:default {fmt(item['default'])} ;")
    lines.append(f"\t\t\tlv2:minimum {fmt(item['lo'])} ;")
    lines.append(f"\t\t\tlv2:maximum {fmt(item['hi'])} ;")
    unit = UNIT_TTL.get(item["unit"] or "")
    if unit:
        lines.append(f"\t\t\tunits:unit {unit} ;")
    if item["kind"] == "toggle":
        lines.append("\t\t\tlv2:portProperty lv2:integer , lv2:toggled ;")
    elif item["kind"] == "enum":
        lines.append("\t\t\tlv2:portProperty lv2:integer , lv2:enumeration ;")
    elif item["kind"] == "log":
        lines.append("\t\t\tlv2:portProperty lv2:logarithmic ;")
    for label, value in item["points"]:
        lines.append(
            f'\t\t\tlv2:scalePoint [ rdfs:label "{label}" ; rdf:value {fmt(value)} ] ;')
    lines.append("\t\t] ,")
    return lines


def telemetry_block(index, sym, name, lo, hi, unit):
    lines = ["\t\t["]
    lines.append("\t\t\ta lv2:ControlPort , lv2:OutputPort ;")
    lines.append(f"\t\t\tlv2:index {index} ;")
    lines.append(f'\t\t\tlv2:symbol "{sym}" ;')
    lines.append(f'\t\t\tlv2:name "{name}" ;')
    lines.append(f"\t\t\tpg:group <{PLUGIN}#meters> ;")
    lines.append(f"\t\t\tlv2:default {fmt(lo)} ;")
    lines.append(f"\t\t\tlv2:minimum {fmt(lo)} ;")
    lines.append(f"\t\t\tlv2:maximum {fmt(hi)} ;")
    if unit:
        lines.append(f"\t\t\tunits:unit {UNIT_TTL[unit]} ;")
    lines.append("\t\t] ,")
    return lines


def write_ttl():
    out = []
    out.append("@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .")
    out.append("@prefix doap: <http://usefulinc.com/ns/doap#> .")
    out.append("@prefix foaf: <http://xmlns.com/foaf/0.1/> .")
    out.append("@prefix idpy: <http://harrisonconsoles.com/lv2/inlinedisplay#> .")
    out.append("@prefix opts: <http://lv2plug.in/ns/ext/options#> .")
    out.append("@prefix pg:   <http://lv2plug.in/ns/ext/port-groups#> .")
    out.append("@prefix rdf:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .")
    out.append("@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .")
    out.append("@prefix units: <http://lv2plug.in/ns/extensions/units#> .")
    out.append("@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .")
    out.append("")
    out.append(f"@prefix cse: <{PLUGIN}> .")
    out.append("")
    out.append(f"<{PLUGIN}>")
    out.append("\ta lv2:Plugin , lv2:EQPlugin ;")
    out.append("\tlv2:binary <common-sense-eq.so> ;")
    out.append("\tlv2:majorVersion 0 ;")
    out.append("\tlv2:minorVersion 1 ;")
    out.append("\tlv2:microVersion 0 ;")
    out.append(f"\tlv2:project <{PLUGIN}> ;")
    out.append("\tlv2:optionalFeature lv2:hardRTCapable , idpy:queue_draw , idpy:in_gui ;")
    out.append("\tlv2:extensionData idpy:interface ;")
    out.append("\tdoap:license <https://spdx.org/licenses/GPL-3.0-or-later.html> ;")
    out.append('\tdoap:name "Common Sense EQ" ;')
    out.append('\tdoap:description "Eight-band channel EQ with dynamic bands, an analyzer, and inline mixer-strip displays." ;')
    out.append('\tdoap:shortdesc "Dynamic 8-band channel EQ with inline displays." ;')
    out.append(f"\tfoaf:homepage <{PLUGIN}> ;")
    out.append(f"\tui:ui <{UI}> ;")
    out.append("\tlv2:port")
    for i, (label, desig) in enumerate((("in_l", "left"), ("in_r", "right"),
                                       ("out_l", "left"), ("out_r", "right"))):
        direction = "InputPort" if i < 2 else "OutputPort"
        kind = "Input" if i < 2 else "Output"
        group = "input" if i < 2 else "output"
        out.append("\t\t[")
        out.append(f"\t\t\ta lv2:AudioPort , lv2:{direction} ;")
        out.append(f"\t\t\tlv2:index {i} ;")
        out.append(f'\t\t\tlv2:symbol "{label}" ;')
        out.append(f'\t\t\tlv2:name "{kind} {"Left" if desig == "left" else "Right"}" ;')
        out.append(f"\t\t\tpg:group <{PLUGIN}#{group}> ;")
        out.append(f"\t\t\tlv2:designation pg:{desig} ;")
        out.append("\t\t] ,")
    for item in PARAMS:
        out.extend(port_block(item))
    for i in range(SPECTRUM_COUNT):
        out.extend(telemetry_block(PORT_SPECTRUM_BASE + i,
                                   f"spectrum_{i + 1}",
                                   f"Spectrum {i + 1}", -100, 6, "db"))
    for i in range(GR_COUNT):
        out.extend(telemetry_block(PORT_GR_BASE + i,
                                   f"dyn_gain_{i + 1}",
                                   f"Band {i + 1} Dynamic Gain", -24, 24,
                                   "db"))
    # Remove the trailing comma on the final port.
    for i in range(len(out) - 1, -1, -1):
        if out[i] == "\t\t] ,":
            out[i] = "\t\t] ."
            break
    out.append("")
    out.append(f"<{PLUGIN}#input> a pg:Group ; rdfs:label \"Input\" .")
    out.append(f"<{PLUGIN}#output> a pg:Group ; rdfs:label \"Output\" .")
    out.append(f"<{PLUGIN}#global> a pg:Group ; rdfs:label \"Global\" .")
    for i in range(1, 9):
        out.append(f"<{PLUGIN}#band{i}> a pg:Group ; rdfs:label \"{BANDS[i-1][0]}\" .")
    out.append(f"<{PLUGIN}#meters> a pg:Group ; rdfs:label \"Meters\" .")
    out.append("")
    TTL.write_text("\n".join(out))

    m = []
    m.append("@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .")
    m.append("@prefix pset: <http://lv2plug.in/ns/ext/presets#> .")
    m.append("@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .")
    m.append("@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .")
    m.append("@prefix opts: <http://lv2plug.in/ns/ext/options#> .")
    m.append("")
    m.append(f"<{PLUGIN}>")
    m.append("\ta lv2:Plugin ;")
    m.append("\tlv2:binary <common-sense-eq.so> ;")
    m.append("\trdfs:seeAlso <common-sense-eq.ttl> ;")
    m.append(f"\tui:ui <{UI}> .")
    m.append("")
    m.append(f"<{UI}>")
    m.append("\ta ui:X11UI ;")
    m.append("\tui:binary <CommonSenseEQUI.so> ;")
    m.append("\tlv2:requiredFeature ui:idleInterface , ui:parent ;")
    m.append("\tlv2:optionalFeature ui:resize , opts:options ;")
    m.append("\tlv2:extensionData ui:idleInterface , ui:resize ;")
    m.append("\topts:supportedOption ui:scaleFactor ;")
    m.append("\trdfs:seeAlso <common-sense-eq.ttl> .")
    m.append("")
    # Presets include every control port so a host resets unlisted bands.
    for pi, (name, overrides) in enumerate(PRESETS):
        values = {item["sym"]: item["default"] for item in PARAMS}
        for key, value in overrides.items():
            sym, _ = override_for(key, value)
            values[sym] = value
        m.append(f"<{PLUGIN}#preset_{pi}>")
        m.append("\ta pset:Preset ;")
        m.append(f"\tlv2:appliesTo <{PLUGIN}> ;")
        m.append(f'\trdfs:label "{name}" ;')
        m.append("\tlv2:port")
        items = list(values.items())
        for i, (sym, value) in enumerate(items):
            tail = " ." if i == len(items) - 1 else " ,"
            m.append(f'\t\t[ lv2:symbol "{sym}" ; pset:value {fmt(value)} ]{tail}')
        m.append("")
    MANIFEST.write_text("\n".join(m))


if __name__ == "__main__":
    write_header()
    write_ttl()
    print(f"wrote {HEADER.relative_to(ROOT)}")
    print(f"wrote {TTL.relative_to(ROOT)}")
    print(f"wrote {MANIFEST.relative_to(ROOT)}")
    print(f"{PARAM_COUNT} params, {PORT_COUNT} ports, {len(PRESETS)} presets")
