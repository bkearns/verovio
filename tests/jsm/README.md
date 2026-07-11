# Native JSM smoke tests

Run the native canonical JSM smoke test against a local build:

```sh
tests/jsm/smoke.sh /path/to/verovio
```

The accidental regression fixture uses a two-flat key. Repeated B-flats and
E-flats must not receive written accidentals merely because `pitch.alter` is
nonzero. Naturals and a same-measure return to E-flat remain visible.
