#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/verovio" >&2
    exit 2
fi

verovio=$1
repo=$(cd "$(dirname "$0")/../.." && pwd)
fixture="$repo/tests/jsm/key-signature-accidentals.jsm"
tmp=$(mktemp -d)
output="$tmp/output.svg"
compact_output="$tmp/compact-output.svg"
trap 'rm -rf "$tmp"' EXIT

"$verovio" -r "$repo/data" -f auto -o "$output" "$fixture"
"$verovio" -r "$repo/data" -f auto -o "$compact_output" "$repo/tests/jsm/key-signature-accidentals-compact.jsm"

python3 "$repo/tests/jsm/compact_parity.py" --verovio "$verovio" --resources "$repo/data" \
    --canonical "$fixture" --compact "$repo/tests/jsm/key-signature-accidentals-compact.jsm"

rg -q 'id="event-b-flat-1"' "$output"
rg -q 'data-jsm-tone-id="tone-b-flat-1"' "$output"
rg -q 'data-jsm-measure-id="part-measure-1"' "$output"

written_accidentals=$(rg -U -o 'class="accid">[[:space:]]*<use' "$output" | rg -c '^class=')
if [[ $written_accidentals -ne 3 ]]; then
    echo "expected 3 written accidentals, found $written_accidentals" >&2
    exit 1
fi

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["strong-accent"]' \
    "$fixture" >"$tmp/strong-accent.jsm"
"$verovio" -r "$repo/data" -f jsm -o "$tmp/strong-accent.svg" "$tmp/strong-accent.jsm"
rg -U -q 'class="artic"[^>]*>[[:space:]]*<use[^>]*xlink:href="#E4AC-' "$tmp/strong-accent.svg"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["detached-legato"]' \
    "$fixture" >"$tmp/detached-legato.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/detached-legato.mei" "$tmp/detached-legato.jsm"
rg -q '<artic[^>]*artic="stacc"' "$tmp/detached-legato.mei"
rg -q '<artic[^>]*artic="ten"' "$tmp/detached-legato.mei"
! rg -q '<artic[^>]*artic="stacc ten"' "$tmp/detached-legato.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["spiccato"]' \
    "$fixture" >"$tmp/spiccato.jsm"
"$verovio" -r "$repo/data" -f jsm -o "$tmp/spiccato.svg" "$tmp/spiccato.jsm"
rg -U -q 'class="artic"[^>]*>[[:space:]]*<use[^>]*xlink:href="#E4A6-' "$tmp/spiccato.svg"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["scoop"]' \
    "$fixture" >"$tmp/scoop.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/scoop.mei" "$tmp/scoop.jsm"
rg -q '<artic[^>]*artic="scoop"' "$tmp/scoop.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["plop"]' \
    "$fixture" >"$tmp/plop.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/plop.mei" "$tmp/plop.jsm"
rg -q '<artic[^>]*artic="plop"' "$tmp/plop.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["doit"]' \
    "$fixture" >"$tmp/doit.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/doit.mei" "$tmp/doit.jsm"
rg -q '<artic[^>]*artic="doit"' "$tmp/doit.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["falloff"]' \
    "$fixture" >"$tmp/falloff.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/falloff.mei" "$tmp/falloff.jsm"
rg -q '<artic[^>]*artic="fall"' "$tmp/falloff.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["breath-mark"]' \
    "$fixture" >"$tmp/breath-mark.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/breath-mark.mei" "$tmp/breath-mark.jsm"
rg -q '<breath[^>]*staff="1"[^>]*tstamp="1.5"' "$tmp/breath-mark.mei"
if rg -q '<artic' "$tmp/breath-mark.mei"; then
    echo "breath-mark rendered as a generic articulation" >&2
    exit 1
fi

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].articulations = ["caesura"]' \
    "$fixture" >"$tmp/caesura.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/caesura.mei" "$tmp/caesura.jsm"
rg -q '<caesura[^>]*staff="1"[^>]*tstamp="1.5"' "$tmp/caesura.mei"

jq '.score.parts[0].measures[0].staves[0].voices[0].events = [
        {type:"note", id:"ornament-event-1", onset:[0,2], duration:[1,2], order:0, noteType:"eighth",
            ornaments:["trill-mark"], tone:{id:"ornament-tone-1",
                pitch:{id:"ornament-pitch-1", kind:"pitched", step:"C", alter:0, octave:5}}},
        {type:"note", id:"ornament-event-2", onset:[1,2], duration:[1,2], order:1, noteType:"eighth",
            ornaments:["mordent"], tone:{id:"ornament-tone-2",
                pitch:{id:"ornament-pitch-2", kind:"pitched", step:"D", alter:0, octave:5}}},
        {type:"note", id:"ornament-event-3", onset:[2,2], duration:[1,2], order:2, noteType:"eighth",
            ornaments:["inverted-mordent"], tone:{id:"ornament-tone-3",
                pitch:{id:"ornament-pitch-3", kind:"pitched", step:"E", alter:0, octave:5}}},
        {type:"note", id:"ornament-event-4", onset:[3,2], duration:[1,2], order:3, noteType:"eighth",
            ornaments:["turn"], tone:{id:"ornament-tone-4",
                pitch:{id:"ornament-pitch-4", kind:"pitched", step:"F", alter:0, octave:5}}},
        {type:"note", id:"ornament-event-5", onset:[4,2], duration:[1,2], order:4, noteType:"eighth",
            ornaments:["inverted-turn"], tone:{id:"ornament-tone-5",
                pitch:{id:"ornament-pitch-5", kind:"pitched", step:"G", alter:0, octave:5}}},
        {type:"note", id:"ornament-event-6", onset:[5,2], duration:[1,2], order:5, noteType:"eighth",
            ornaments:["delayed-turn"], tone:{id:"ornament-tone-6",
                pitch:{id:"ornament-pitch-6", kind:"pitched", step:"A", alter:0, octave:5}}},
        {type:"rest", id:"ornament-fill", onset:[6,2], duration:[1,1], order:6, noteType:"quarter"}
    ]' "$fixture" >"$tmp/ornament-matrix.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/ornament-matrix.mei" "$tmp/ornament-matrix.jsm"
python3 - "$tmp/ornament-matrix.mei" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
ornaments = [
    element
    for element in root.iter()
    if element.tag.rsplit("}", 1)[-1] in {"trill", "mordent", "turn"}
]
expected = [
    ("trill", "#ornament-event-1", None, None),
    ("mordent", "#ornament-event-2", "lower", None),
    ("mordent", "#ornament-event-3", "upper", None),
    ("turn", "#ornament-event-4", "upper", None),
    ("turn", "#ornament-event-5", "lower", None),
    ("turn", "#ornament-event-6", "upper", "true"),
]
actual = [
    (
        element.tag.rsplit("}", 1)[-1],
        element.attrib.get("startid"),
        element.attrib.get("form"),
        element.attrib.get("delayed"),
    )
    for element in ornaments
]
if actual != expected:
    raise SystemExit(f"ornament matrix mismatch: expected {expected!r}, found {actual!r}")
if any(element.attrib.get("staff") != "1" for element in ornaments):
    raise SystemExit("expected every ornament to anchor to staff 1")
PY

jq '.score.parts[0].measures[0].staves[0].voices[0].events[5].ornaments = ["shake"]' \
    "$tmp/ornament-matrix.jsm" >"$tmp/unsupported-ornament.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/unsupported-ornament.svg" \
    "$tmp/unsupported-ornament.jsm" >"$tmp/unsupported-ornament.log" 2>&1; then
    echo "unsupported ornament was accepted" >&2
    exit 1
fi
rg -q 'JSM_UNSUPPORTED_ORNAMENT.*ornaments/0.*shake' "$tmp/unsupported-ornament.log"

echo "JSM native smoke passed: stable IDs present; written accidentals=$written_accidentals"

jq '.score.conductorTrack.measures[0].events = [{
        id:"tempo-quarter-96", onset:[0,1], duration:[0,1], order:0, kind:"tempo",
        tempo:{beatUnit:"quarter", bpm:[96,1], transition:"immediate"}
    }]
    | .score.parts[0].measures[0].conductorEventRefs = ["tempo-quarter-96"]
    | .score.parts[0].measures[0].measureEvents += [{
        type:"directionRef", id:"tempo-quarter-96-ref", onset:[0,1], duration:[0,1], order:0,
        conductorEventRef:"tempo-quarter-96", staffId:"staff-1", placement:"above"
    }]' "$fixture" >"$tmp/metronome.jsm"
"$verovio" -r "$repo/data" -f jsm -o "$tmp/metronome.svg" "$tmp/metronome.jsm"
python3 - "$tmp/metronome.svg" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
tempo = next(
    (
        element
        for element in root.iter()
        if "tempo" in element.attrib.get("class", "").split()
    ),
    None,
)
if tempo is None or not list(tempo) or tempo.attrib.get("display") == "none" or tempo.attrib.get("visibility") == "hidden":
    raise SystemExit("expected a non-empty visible tempo SVG group")
if not any(
    element.attrib.get("font-family") == "Leipzig" and "\ueca5" in "".join(element.itertext())
    for element in tempo.iter()
):
    raise SystemExit("expected Leipzig quarter-note beat-unit glyph U+ECA5 in tempo SVG group")
if "96" not in "".join(tempo.itertext()):
    raise SystemExit("expected 96 in tempo SVG group")
PY

jq '.score.conductorTrack.measures[0].events = [{
        id:"metric-modulation", onset:[0,1], duration:[0,1], order:0, kind:"direction", value:null,
        metronome:{left:{beatUnit:"quarter"}, relation:"equals", rightNote:{beatUnit:"eighth", dots:1}}
    }]
    | .score.parts[0].measures[0].conductorEventRefs = ["metric-modulation"]
    | .score.parts[0].measures[0].measureEvents += [{
        type:"directionRef", id:"metric-modulation-ref", onset:[0,1], duration:[0,1], order:0,
        conductorEventRef:"metric-modulation", staffId:"staff-1", placement:"above"
    }]' "$fixture" >"$tmp/metric-modulation.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/metric-modulation.mei" "$tmp/metric-modulation.jsm"
python3 - "$tmp/metric-modulation.mei" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
tempo = next(element for element in root.iter() if element.tag.rsplit("}", 1)[-1] == "tempo")
children = list(tempo)
if tempo.attrib.get("place") != "above" or tempo.attrib.get("staff") != "1" or tempo.attrib.get("tstamp") != "1":
    raise SystemExit("expected metric modulation above staff 1 at timestamp 1")
if tempo.attrib.get("mm.unit") != "4" or "mm" in tempo.attrib or "midi.bpm" in tempo.attrib:
    raise SystemExit("tempo-less metric modulation has incorrect metronome attributes")
if [element.tag.rsplit("}", 1)[-1] for element in children] != ["rend", "rend"]:
    raise SystemExit("expected metric modulation Tempo children: rend, rend")
if children[0].attrib.get("glyph.auth") != "smufl" or "".join(children[0].itertext()) != "\ueca5":
    raise SystemExit("expected quarter-note glyph on metric-modulation left side")
if children[0].tail != " = ":
    raise SystemExit("expected equals relation in metric modulation")
if children[1].attrib.get("glyph.auth") != "smufl" or "".join(children[1].itertext()) != "\ueca7 \uecb7":
    raise SystemExit("expected dotted-eighth glyphs on metric-modulation right side")
PY

jq '.score.conductorTrack.measures[0].events = [{
        id:"direction-dc", onset:[3,1], duration:[0,1], order:0, kind:"direction", value:"D.C."
    }]
    | .score.parts[0].measures[0].conductorEventRefs = ["direction-dc"]
    | .score.parts[0].measures[0].measureEvents += [{
        type:"directionRef", id:"direction-dc-ref", onset:[1,1], duration:[0,1], order:0,
        conductorEventRef:"direction-dc", staffId:"staff-1", placement:"below"
    }]
    | .score.parts[0].measures[0].navigation = {jump:"dc"}' "$fixture" >"$tmp/direction-dc.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/direction-dc.mei" "$tmp/direction-dc.jsm"
python3 - "$tmp/direction-dc.mei" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
directions = [element for element in root.iter() if element.tag.rsplit("}", 1)[-1] == "dir"]
if len(directions) != 1:
    raise SystemExit(f"expected one semantic Dir, found {len(directions)}")
direction = directions[0]
rendered_text = "".join(
    "".join(element.itertext())
    for element in direction.iter()
    if element.tag.rsplit("}", 1)[-1] == "rend"
)
if rendered_text != "D.C.":
    raise SystemExit("expected D.C. rendered text")
if direction.attrib.get("staff") != "1" or direction.attrib.get("place") != "below":
    raise SystemExit("expected D.C. below staff 1")
if direction.attrib.get("tstamp") != "2":
    raise SystemExit("expected D.C. at reference-onset timestamp 2")
if direction.attrib.get("type") != "dacapo":
    raise SystemExit("expected D.C. navigation type dacapo")
PY

jq '.score.conductorTrack.measures[0].events[0].value = "D.S. al Coda"' \
    "$tmp/direction-dc.jsm" >"$tmp/direction-ds-al-coda.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/direction-ds-al-coda.mei" "$tmp/direction-ds-al-coda.jsm"
rg -q '<dir[^>]*type="dalsegno"' "$tmp/direction-ds-al-coda.mei"
rg -q '<rend[^>]*halign="right"[^>]*>D.S. al Coda</rend>' "$tmp/direction-ds-al-coda.mei"

jq '.score.parts[0].measures[0].navigation = {markers:["segno"], jump:"ds"}' \
    "$fixture" >"$tmp/measure-navigation-ds.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/measure-navigation-ds.mei" \
    "$tmp/measure-navigation-ds.jsm"
python3 - "$tmp/measure-navigation-ds.mei" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
repeat_marks = [element for element in root.iter() if element.tag.rsplit("}", 1)[-1] == "repeatMark"]
directions = [element for element in root.iter() if element.tag.rsplit("}", 1)[-1] == "dir"]
if len(repeat_marks) != 1 or repeat_marks[0].attrib.get("func") != "segno":
    raise SystemExit("expected one measure-level repeatMark func=segno")
if repeat_marks[0].attrib.get("staff") != "1" or repeat_marks[0].attrib.get("tstamp") != "5":
    raise SystemExit("expected segno on staff 1 at measure-end timestamp 5")
if len(directions) != 1 or directions[0].attrib.get("type") != "dalsegno":
    raise SystemExit("expected one measure-level Dir type=dalsegno")
if directions[0].attrib.get("staff") != "1" or directions[0].attrib.get("tstamp") != "5":
    raise SystemExit("expected D.S. on staff 1 at measure-end timestamp 5")
rends = [element for element in directions[0] if element.tag.rsplit("}", 1)[-1] == "rend"]
if len(rends) != 1 or rends[0].attrib.get("halign") != "right" or "".join(rends[0].itertext()) != "D.S.":
    raise SystemExit("expected right-aligned D.S. direction text")
PY

jq '.score.conductorTrack.measures[0].events = [{
        id:"rehearsal-a", onset:[0,1], duration:[0,1], order:0, kind:"rehearsal", value:"A",
        rehearsal:{enclosure:"circle"}
    }]
    | .score.parts[0].measures[0].conductorEventRefs = ["rehearsal-a"]
    | .score.parts[0].measures[0].measureEvents += [{
        type:"directionRef", id:"rehearsal-a-ref", onset:[0,1], duration:[0,1], order:0,
        conductorEventRef:"rehearsal-a", staffId:"staff-1", placement:"above"
    }]' "$fixture" >"$tmp/rehearsal-circle.jsm"
"$verovio" -r "$repo/data" -f jsm -o "$tmp/rehearsal-circle.svg" "$tmp/rehearsal-circle.jsm"
python3 - "$tmp/rehearsal-circle.svg" <<'PY'
import sys
import xml.etree.ElementTree as ET

root = ET.parse(sys.argv[1]).getroot()
rehearsal = next(
    element for element in root.iter() if "reh" in element.attrib.get("class", "").split()
)
tags = [element.tag.rsplit("}", 1)[-1] for element in rehearsal.iter()]
if "ellipse" not in tags:
    raise SystemExit("expected circle rehearsal enclosure to render an SVG ellipse")
if "rect" in tags:
    raise SystemExit("circle rehearsal enclosure rendered an SVG rect")
PY

jq '.score.parts[0] as $source
    | [$source | .. | objects | .id? // empty] as $ids
    | (reduce $ids[] as $id ($source;
        walk(if type == "string" and . == $id then "second-" + . else . end))) as $second
    | ($second.contexts[0]
        | .id = "second-context-c-major"
        | .contentHash = ("b" * 64)
        | .key = {fifths: 0, mode: "major"}) as $changed
    | ($second
        | .name = "Second Clarinet"
        | .abbreviation = "2nd Cl."
        | .contexts += [$changed]
        | .measures[1].contextRef = $changed.id) as $second
    | .score.parts += [$second]
    | .score.views[0].partIds += [$second.id]' "$fixture" >"$tmp/multipart-context.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/multipart-context.mei" "$tmp/multipart-context.jsm"
rg -U -q '<scoreDef[^>]*>[[:space:]]*<staffGrp[^>]*>[[:space:]]*<staffDef[^>]*n="2"[^>]*>[[:space:]]*<keySig[^>]*sig="0"' \
    "$tmp/multipart-context.mei"

jq '.score.parts[0] as $part
    | ($part.contexts[0]
        | .id = "context-cancelled"
        | .contentHash = ("c" * 64)
        | .key = {fifths: 0, mode: "major", cancel: -2}) as $cancelled
    | .score.parts[0].contexts += [$cancelled]
    | .score.parts[0].measures[1].contextRef = $cancelled.id' "$fixture" >"$tmp/key-cancellation.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/key-cancellation.mei" "$tmp/key-cancellation.jsm"
rg -q '<keySig[^>]*sig="0"[^>]*cancelaccid="before"' "$tmp/key-cancellation.mei"

for invalid_cancel in 8 '"invalid"'; do
    jq ".score.parts[0].contexts[0].key.cancel = $invalid_cancel" "$fixture" >"$tmp/invalid-cancel.jsm"
    if "$verovio" -r "$repo/data" -f jsm -o "$tmp/invalid-cancel.svg" "$tmp/invalid-cancel.jsm" \
        >"$tmp/invalid-cancel.log" 2>&1; then
        echo "invalid key cancellation was accepted: $invalid_cancel" >&2
        exit 1
    fi
done

jq '(.score.parts[1].contexts[] | select(.id == "second-context-c-major") | .time) =
    {beats: [3], beatType: 8}' "$tmp/multipart-context.jsm" >"$tmp/conflicting-meter.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/conflicting-meter.svg" "$tmp/conflicting-meter.jsm" \
    >"$tmp/conflicting-meter.log" 2>&1; then
    echo "conflicting part-local meters were accepted" >&2
    exit 1
fi
rg -q 'JSM_CONFLICTING_METERS' "$tmp/conflicting-meter.log"

jq '.score.metadata.extensions["com.musicstand.musicxml.page-credits"] = [
    {attributes:{page:"1"}, items:[
        {kind:"credit-type", attributes:{}, text:"title"},
        {kind:"credit-words", attributes:{"default-y":"10", justify:"center", valign:"top",
            color:"#112233", "font-style":"italic", "font-weight":"bold"}, text:"JSM Header"}
    ]},
    {attributes:{page:"1"}, items:[
        {kind:"credit-words", attributes:{"default-y":"-1", justify:"right"}, text:"JSM Footer"}
    ]},
    {attributes:{page:"2"}, items:[
        {kind:"credit-words", attributes:{}, text:"Ignored Later Page"}
    ]}
]' "$fixture" >"$tmp/page-credits.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/page-credits.mei" "$tmp/page-credits.jsm"
rg -U -q '<pgHead[^>]*func="first"[^>]*>[[:space:]]*<rend[^>]*halign="center"[^>]*valign="top"[^>]*color="#112233"[^>]*fontstyle="italic"[^>]*fontweight="bold"[^>]*>JSM Header</rend>' \
    "$tmp/page-credits.mei"
rg -U -q '<pgFoot[^>]*>[[:space:]]*<rend[^>]*halign="right"[^>]*>JSM Footer</rend>' "$tmp/page-credits.mei"
if rg -q 'Ignored Later Page' "$tmp/page-credits.mei"; then
    echo "page credit from a later page was rendered as a first-page running element" >&2
    exit 1
fi

jq '.score.parts[0].measures[0].staves[0].voices[0].events[0].id = "unsafe id"' "$fixture" >"$tmp/unsafe.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/unsafe.svg" "$tmp/unsafe.jsm" >"$tmp/unsafe.log" 2>&1; then
    echo "unsafe ID was accepted" >&2
    exit 1
fi
rg -q 'JSM_UNSAFE_ID' "$tmp/unsafe.log"

jq '.score.parts[0].measures[0].staves[0].voices[0].events[1].id = "event-b-flat-1"' "$fixture" >"$tmp/duplicate.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/duplicate.svg" "$tmp/duplicate.jsm" >"$tmp/duplicate.log" 2>&1; then
    echo "duplicate ID was accepted" >&2
    exit 1
fi
rg -q 'JSM_DUPLICATE_ID' "$tmp/duplicate.log"

jq '.[4][0] = 999999' "$repo/tests/jsm/key-signature-accidentals-compact.jsm" >"$tmp/compact.jsm"
if "$verovio" -r "$repo/data" -f auto -o "$tmp/compact.svg" "$tmp/compact.jsm" >"$tmp/compact.log" 2>&1; then
    echo "out-of-range compact reference was accepted" >&2
    exit 1
fi
rg -q 'JSM_COMPACT_REF' "$tmp/compact.log"

jq '. as $doc | .[5] = {extraction: ["part",
    ($doc[3].i | index("accidental-regression-score")),
    ($doc[3].i | index("part-1")),
    {algorithm:"sha256", scope:"document", value:("c" * 64)}, null,
    [($doc[3].i | index("bar-1"))], true,
    [[($doc[3].i | index("bar-1")), ($doc[3].i | index("part-1"))]]
]}' "$repo/tests/jsm/key-signature-accidentals-compact.jsm" >"$tmp/compact-extraction.jsm"
"$verovio" -r "$repo/data" -f jsm -o "$tmp/compact-extraction.svg" "$tmp/compact-extraction.jsm"

jq '.[3].p[0].extensions = {"x.test": {padding:("a" * 1024)}}
    | .[4][4][0][8][0][5][0][1][0][2][0][0] = "c"
    | .[4][4][0][8][0][5][0][1][0][2][0][5] = [range(0;40000) | 0]' \
    "$repo/tests/jsm/key-signature-accidentals-compact.jsm" >"$tmp/compact-amplification.jsm"
if "$verovio" -r "$repo/data" -f jsm -o "$tmp/amplification.svg" "$tmp/compact-amplification.jsm" \
    >"$tmp/amplification.log" 2>&1; then
    echo "compact dictionary amplification was accepted" >&2
    exit 1
fi
rg -q 'JSM_RESOURCE_LIMIT' "$tmp/amplification.log"

for event_fixture in grace tuplet beam percussion; do
    "$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/event-$event_fixture.mei" \
        "$repo/tests/jsm/event-$event_fixture.jsm"
done
rg -q '<note[^>]*grace="unacc"[^>]*stem.mod="1slash"' "$tmp/event-grace.mei"
rg -q '<tuplet[^>]*num="3"[^>]*numbase="2"[^>]*bracket.visible="true"' "$tmp/event-tuplet.mei"
rg -q '<beam([ >])' "$tmp/event-beam.mei"
rg -q '<note[^>]*loc="' "$tmp/event-percussion.mei"
rg -q '<note[^>]*stem.dir="up"' "$tmp/event-percussion.mei"
rg -q '<note[^>]*stem.dir="down"' "$tmp/event-percussion.mei"

jq '.score.parts[0] as $part
    | ($part.contexts[0]
        | .id = "mid-measure-clef-context"
        | .contentHash = ("d" * 64)
        | .staves[0].clef = {sign:"F", line:4}) as $changed
    | .score.parts[0].contexts += [$changed]
    | .score.parts[0].measures[0].measureEvents += [{
        type:"contextChange", id:"mid-measure-clef-change", onset:[2,1], duration:[0,1], order:0,
        contextRef:$changed.id, staffIds:[$part.staves[0].id]
    }]' "$fixture" >"$tmp/mid-measure-context.jsm"
"$verovio" -r "$repo/data" -f jsm -t mei -o "$tmp/mid-measure-context.mei" "$tmp/mid-measure-context.jsm"
python3 - "$tmp/mid-measure-context.mei" <<'PY'
import pathlib
import sys

mei = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
positions = [
    mei.index('xml:id="event-b-flat-1"'),
    mei.index('xml:id="event-e-flat-1"'),
    mei.index('shape="F" line="4"'),
    mei.index('xml:id="event-b-natural-1"'),
]
if positions != sorted(positions):
    raise SystemExit("mid-measure clef was not materialized at its exact layer boundary")
PY

declare -A invalid_context_changes=(
    [unknown]='.score.parts[0].measures[0].measureEvents[-1].contextRef = "missing-context"'
    [ownership]='.score.parts[0].measures[0].measureEvents[-1].staffIds = ["other-staff"]'
    [onset]='.score.parts[0].measures[0].measureEvents[-1].onset = [3,2]'
    [duration]='.score.parts[0].measures[0].measureEvents[-1].duration = [1,1]'
    [incomplete]='del(.score.parts[0].contexts[-1].key)'
    [type]='.score.parts[0].measures[0].measureEvents[-1].type = "futureContext"'
)
declare -A invalid_context_codes=(
    [unknown]=JSM_UNKNOWN_CONTEXT
    [ownership]=JSM_CONTEXT_STAFF_OWNERSHIP
    [onset]=JSM_UNALIGNED_CONTEXT_ONSET
    [duration]=JSM_INVALID_CONTEXT_DURATION
    [incomplete]=JSM_INCOMPLETE_CONTEXT
    [type]=JSM_UNSUPPORTED_MEASURE_EVENT
)
for invalid in "${!invalid_context_changes[@]}"; do
    jq "${invalid_context_changes[$invalid]}" "$tmp/mid-measure-context.jsm" >"$tmp/invalid-context-$invalid.jsm"
    if "$verovio" -r "$repo/data" -f jsm -o "$tmp/invalid-context-$invalid.svg" \
        "$tmp/invalid-context-$invalid.jsm" >"$tmp/invalid-context-$invalid.log" 2>&1; then
        echo "invalid context change was accepted: $invalid" >&2
        exit 1
    fi
    rg -q "${invalid_context_codes[$invalid]}" "$tmp/invalid-context-$invalid.log"
done

echo "JSM smoke passed: compact validation, contexts, credits, and native event engraving"
