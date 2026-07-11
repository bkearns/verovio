{
  "$schema": "../../specs/jsm/jsm-canonical.schema.json",
  "format": "JSM",
  "profile": "canonical",
  "score": {
    "conductorTrack": {
      "measures": [
        {
          "barId": "bar-1",
          "events": [],
          "id": "conductor-measure-1"
        }
      ]
    },
    "id": "jsm-four-bar-signal-154965",
    "logicalBars": [
      {
        "duration": [
          4,
          1
        ],
        "id": "bar-1",
        "number": "1"
      }
    ],
    "metadata": {
      "composer": "MusicStand Procedural Composer",
      "rights": "Procedurally generated test fixture",
      "title": "Four-Bar Signal 154965"
    },
    "parts": [
      {
        "abbreviation": "Fl.",
        "contexts": [
          {
            "contentHash": "ffe58cf8e886736e725fecc9048e60ee3401ad1c42cca848a5dd28806470f23d",
            "id": "flute-context",
            "key": {
              "fifths": 0,
              "mode": "major"
            },
            "staves": [
              {
                "clef": {
                  "line": 3,
                  "sign": "percussion"
                },
                "staffId": "flute-staff"
              }
            ],
            "time": {
              "beatType": 4,
              "beats": [
                4
              ]
            }
          }
        ],
        "id": "flute-part",
        "instrument": {
          "id": "flute-instrument",
          "midiChannel": 1,
          "midiProgram": 74,
          "name": "Flute",
          "percussion": true,
          "sound": "wind.flutes.flute",
          "transposition": {
            "chromatic": 0,
            "diatonic": 0,
            "octave": 0
          }
        },
        "measures": [
          {
            "activeSpannerRefs": [],
            "barId": "bar-1",
            "conductorEventRefs": [],
            "contextRef": "flute-context",
            "duration": [
              4,
              1
            ],
            "id": "flute-measure-1",
            "measureEvents": [],
            "spanners": [],
            "staves": [
              {
                "staffId": "flute-staff",
                "voices": [
                  {
                    "events": [
                      {
                        "duration": [
                          1,
                          1
                        ],
                        "id": "unpitched-1",
                        "noteType": "quarter",
                        "onset": [
                          0,
                          1
                        ],
                        "order": 0,
                        "staffId": "flute-staff",
                        "stem": "up",
                        "tone": {
                          "id": "unpitched-1-tone",
                          "notehead": "normal",
                          "pitch": {
                            "displayOctave": 4,
                            "displayStep": "F",
                            "id": "unpitched-1-pitch",
                            "instrumentId": "flute-instrument",
                            "kind": "unpitched"
                          }
                        },
                        "type": "note"
                      },
                      {
                        "duration": [
                          1,
                          1
                        ],
                        "id": "unpitched-2",
                        "noteType": "quarter",
                        "onset": [
                          1,
                          1
                        ],
                        "order": 1,
                        "staffId": "flute-staff",
                        "stem": "down",
                        "tone": {
                          "id": "unpitched-2-tone",
                          "notehead": "cross",
                          "pitch": {
                            "displayOctave": 5,
                            "displayStep": "C",
                            "id": "unpitched-2-pitch",
                            "instrumentId": "flute-instrument",
                            "kind": "unpitched"
                          }
                        },
                        "type": "note"
                      },
                      {
                        "duration": [
                          1,
                          1
                        ],
                        "id": "unpitched-3",
                        "noteType": "quarter",
                        "onset": [
                          2,
                          1
                        ],
                        "order": 2,
                        "staffId": "flute-staff",
                        "stem": "up",
                        "tone": {
                          "id": "unpitched-3-tone",
                          "notehead": "diamond",
                          "pitch": {
                            "displayOctave": 5,
                            "displayStep": "A",
                            "id": "unpitched-3-pitch",
                            "instrumentId": "flute-instrument",
                            "kind": "unpitched"
                          }
                        },
                        "type": "note"
                      },
                      {
                        "duration": [
                          1,
                          1
                        ],
                        "id": "unpitched-4",
                        "noteType": "quarter",
                        "onset": [
                          3,
                          1
                        ],
                        "order": 3,
                        "staffId": "flute-staff",
                        "stem": "down",
                        "tone": {
                          "id": "unpitched-4-tone",
                          "notehead": "triangle",
                          "pitch": {
                            "displayOctave": 5,
                            "displayStep": "E",
                            "id": "unpitched-4-pitch",
                            "instrumentId": "flute-instrument",
                            "kind": "unpitched"
                          }
                        },
                        "type": "note"
                      }
                    ],
                    "id": "flute-voice-measure-1",
                    "laneId": "flute-lane"
                  }
                ]
              }
            ]
          }
        ],
        "name": "Flute",
        "staves": [
          {
            "id": "flute-staff",
            "name": "Flute",
            "number": 1
          }
        ],
        "voiceLanes": [
          {
            "homeStaffId": "flute-staff",
            "id": "flute-lane"
          }
        ]
      }
    ],
    "provenance": {
      "generatorSeed": "1",
      "sources": [
        {
          "creator": "MusicStand Procedural Composer",
          "id": "procedural-source",
          "kind": "procedural",
          "rightsStatus": "generated",
          "title": "Four-Bar Signal 154965"
        }
      ],
      "splitGroupId": "jsm-four-bar-signal-154965",
      "steps": [
        {
          "id": "composition-step",
          "operation": "compose",
          "parameters": {
            "seed": 1,
            "style": "lyrical-concert-band"
          },
          "producer": {
            "name": "jsm-parity-composer",
            "version": "0.1.0"
          }
        }
      ]
    },
    "views": [
      {
        "id": "flute-part-view",
        "kind": "part",
        "layout": {
          "mode": "paginated",
          "spacing": "normal",
          "systemBreakBarIds": []
        },
        "name": "Flute",
        "partIds": [
          "flute-part"
        ]
      }
    ]
  },
  "version": "0.1.0"
}
