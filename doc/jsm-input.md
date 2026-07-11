# Native JSM input

The Music Stand fork accepts canonical JSM directly into Verovio's internal
document tree. It does not serialize through MusicXML, MEI, or Humdrum.

## Local build and smoke test

```sh
cmake -S cmake -B /tmp/verovio-jsm-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/verovio-jsm-build
tests/jsm/smoke.sh /tmp/verovio-jsm-build/verovio
```

Render canonical JSM explicitly or through auto-detection:

```sh
/tmp/verovio-jsm-build/verovio \
  -r data -f jsm -o /tmp/score.svg score.jsm
```

## Current local-test capability

The first vertical slice supports:

- canonical JSM `0.1.0` auto-detection;
- shared logical bars with multiple parts and staves;
- initial clef, key signature, and meter contexts;
- notes, chords, rests, measure rests, and spacers;
- standard note types, dots, pitch alterations, cue size, and common noteheads;
- staccato, tenuto, accent, marcato, staccatissimo, and fermata;
- ties, slurs, crescendo/diminuendo hairpins, and repeat barlines;
- stable event, tone, measure, staff, part, and spanner SVG metadata;
- global safe-ID, staff ownership, bar alignment, onset, and duration checks.

`pitch.alter` is treated as semantic pitch rather than a request to print an
accidental. The importer tracks key-signature and measure state and only emits
a written accidental when the state changes or the JSM marks it as courtesy.

## Not corpus-ready

This branch is for local rendering evaluation. Corpus generation must wait for
the remaining strict-capability work:

- compact-profile normalization and canonical/compact parity;
- multiple voices on one staff with onset-ordered accidental state;
- measure-boundary and mid-measure context changes;
- conductor directions, tempo, dynamics, rehearsal marks, and text;
- tuplets, beams, grace groups, cross-staff notation, and endings;
- view selection, encoded page/system breaks, and continuous layout;
- structured diagnostics and per-occurrence annotation geometry.
- legacy `Verovio.xcodeproj` source registration and Apple target verification.

Unsupported timing, unsafe identity, staff ownership, and duration mismatches
fail the load. `measureEvents` currently produce `JSM_UNSUPPORTED_VISIBLE`
warnings; treat any such warning as a failed strict corpus render.
