# Native JSM smoke tests

Run the native canonical/compact JSM smoke and pixel-parity test against a local build:

```sh
tests/jsm/smoke.sh /path/to/verovio
```

The compact fixture is generated only by the Rust reference converter:

```bash
tests/jsm/generate_compact_fixture.py --converter /path/to/jsm-convert
```

The smoke covers strict canonical/compact pixel parity, root extraction-envelope
decoding, compact reference validation, and stable native JSM identifiers.

The accidental regression fixture uses a two-flat key. Repeated B-flats and
E-flats must not receive written accidentals merely because `pitch.alter` is
nonzero. Naturals and a same-measure return to E-flat remain visible.
