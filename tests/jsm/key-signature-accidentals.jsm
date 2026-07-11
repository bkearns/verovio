{
  "format": "JSM",
  "version": "0.1.0",
  "profile": "canonical",
  "score": {
    "id": "accidental-regression-score",
    "metadata": { "title": "Key signature accidental regression" },
    "logicalBars": [
      { "id": "bar-1", "number": "1", "duration": [4, 1] },
      { "id": "bar-2", "number": "2", "duration": [4, 1] }
    ],
    "conductorTrack": { "measures": [] },
    "parts": [
      {
        "id": "part-1",
        "name": "Clarinet",
        "staves": [{ "id": "staff-1", "number": 1, "name": "Clarinet" }],
        "voiceLanes": [{ "id": "lane-1", "homeStaffId": "staff-1" }],
        "contexts": [
          {
            "id": "context-b-flat",
            "contentHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "time": { "beats": [4], "beatType": 4 },
            "key": { "fifths": -2, "mode": "major" },
            "staves": [{ "staffId": "staff-1", "clef": { "sign": "G", "line": 2 } }]
          }
        ],
        "measures": [
          {
            "id": "part-measure-1",
            "barId": "bar-1",
            "duration": [4, 1],
            "contextRef": "context-b-flat",
            "conductorEventRefs": [],
            "staves": [
              {
                "staffId": "staff-1",
                "voices": [
                  {
                    "id": "voice-1-measure-1",
                    "laneId": "lane-1",
                    "events": [
                      { "type": "note", "id": "event-b-flat-1", "onset": [0, 1], "duration": [1, 1], "order": 0, "noteType": "quarter", "tone": { "id": "tone-b-flat-1", "accidental": "flat", "pitch": { "id": "pitch-b-flat-1", "kind": "pitched", "step": "B", "alter": -1, "octave": 4 } } },
                      { "type": "note", "id": "event-e-flat-1", "onset": [1, 1], "duration": [1, 1], "order": 1, "noteType": "quarter", "tone": { "id": "tone-e-flat-1", "accidental": "flat", "pitch": { "id": "pitch-e-flat-1", "kind": "pitched", "step": "E", "alter": -1, "octave": 5 } } },
                      { "type": "note", "id": "event-b-natural-1", "onset": [2, 1], "duration": [1, 1], "order": 2, "noteType": "quarter", "tone": { "id": "tone-b-natural-1", "accidental": "natural", "pitch": { "id": "pitch-b-natural-1", "kind": "pitched", "step": "B", "alter": 0, "octave": 4 } } },
                      { "type": "note", "id": "event-b-natural-2", "onset": [3, 1], "duration": [1, 1], "order": 3, "noteType": "quarter", "tone": { "id": "tone-b-natural-2", "accidental": "natural", "pitch": { "id": "pitch-b-natural-2", "kind": "pitched", "step": "B", "alter": 0, "octave": 4 } } }
                    ]
                  }
                ]
              }
            ],
            "measureEvents": [],
            "spanners": [],
            "activeSpannerRefs": []
          },
          {
            "id": "part-measure-2",
            "barId": "bar-2",
            "duration": [4, 1],
            "contextRef": "context-b-flat",
            "conductorEventRefs": [],
            "staves": [
              {
                "staffId": "staff-1",
                "voices": [
                  {
                    "id": "voice-1-measure-2",
                    "laneId": "lane-1",
                    "events": [
                      { "type": "note", "id": "event-b-flat-2", "onset": [0, 1], "duration": [1, 1], "order": 0, "noteType": "quarter", "tone": { "id": "tone-b-flat-2", "accidental": "flat", "pitch": { "id": "pitch-b-flat-2", "kind": "pitched", "step": "B", "alter": -1, "octave": 4 } } },
                      { "type": "note", "id": "event-b-flat-3", "onset": [1, 1], "duration": [1, 1], "order": 1, "noteType": "quarter", "tone": { "id": "tone-b-flat-3", "accidental": "flat", "pitch": { "id": "pitch-b-flat-3", "kind": "pitched", "step": "B", "alter": -1, "octave": 4 } } },
                      { "type": "note", "id": "event-e-natural-1", "onset": [2, 1], "duration": [1, 1], "order": 2, "noteType": "quarter", "tone": { "id": "tone-e-natural-1", "accidental": "natural", "pitch": { "id": "pitch-e-natural-1", "kind": "pitched", "step": "E", "alter": 0, "octave": 5 } } },
                      { "type": "note", "id": "event-e-flat-2", "onset": [3, 1], "duration": [1, 1], "order": 3, "noteType": "quarter", "tone": { "id": "tone-e-flat-2", "accidental": "flat", "pitch": { "id": "pitch-e-flat-2", "kind": "pitched", "step": "E", "alter": -1, "octave": 5 } } }
                    ]
                  }
                ]
              }
            ],
            "measureEvents": [],
            "spanners": [],
            "activeSpannerRefs": []
          }
        ]
      }
    ],
    "views": [{ "id": "view-score", "kind": "conductor", "name": "Score", "partIds": ["part-1"] }]
  }
}
