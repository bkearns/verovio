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
            "contentHash": "e35fe1df69a8d627881b5b7b0731bd2af0b7d38f51cd8050c188b04fd08ee375",
            "id": "flute-context",
            "key": {
              "fifths": 0,
              "mode": "major"
            },
            "staves": [
              {
                "clef": {
                  "line": 2,
                  "sign": "G"
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
                          0,
                          1
                        ],
                        "grace": {
                          "groupId": "grace-group-1",
                          "order": 0,
                          "slash": true
                        },
                        "id": "grace-1",
                        "noteType": "eighth",
                        "onset": [
                          0,
                          1
                        ],
                        "order": 0,
                        "staffId": "flute-staff",
                        "tone": {
                          "id": "grace-1-tone",
                          "pitch": {
                            "alter": 0,
                            "id": "grace-1-pitch",
                            "kind": "pitched",
                            "octave": 5,
                            "step": "D"
                          }
                        },
                        "type": "note"
                      },
                      {
                        "duration": [
                          0,
                          1
                        ],
                        "grace": {
                          "groupId": "grace-group-1",
                          "order": 1,
                          "slash": true
                        },
                        "id": "grace-2",
                        "noteType": "eighth",
                        "onset": [
                          0,
                          1
                        ],
                        "order": 1,
                        "staffId": "flute-staff",
                        "tone": {
                          "id": "grace-2-tone",
                          "pitch": {
                            "alter": 0,
                            "id": "grace-2-pitch",
                            "kind": "pitched",
                            "octave": 5,
                            "step": "E"
                          }
                        },
                        "type": "note"
                      },
                      {
                        "duration": [
                          4,
                          1
                        ],
                        "id": "principal",
                        "noteType": "whole",
                        "onset": [
                          0,
                          1
                        ],
                        "order": 2,
                        "staffId": "flute-staff",
                        "tone": {
                          "id": "principal-tone",
                          "pitch": {
                            "alter": 0,
                            "id": "principal-pitch",
                            "kind": "pitched",
                            "octave": 5,
                            "step": "F"
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
