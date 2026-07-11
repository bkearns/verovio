/////////////////////////////////////////////////////////////////////////////
// Name:        iojsm.cpp
// Purpose:     Native JSM input adapter
/////////////////////////////////////////////////////////////////////////////

/**
 * Module: Decode canonical JSM into Verovio's native score model.
 * Correctness: Native JSM and its MusicXML projection engrave identically for supported notation fixtures.
 * Last revised: 2026-07-10
 * Last changed: Added strict native event and position spanner engraving.
 */

#include "iojsm.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "accid.h"
#include "artic.h"
#include "beam.h"
#include "breath.h"
#include "caesura.h"
#include "chord.h"
#include "clef.h"
#include "doc.h"
#include "dynam.h"
#include "fermata.h"
#include "gliss.h"
#include "grpsym.h"
#include "hairpin.h"
#include "jsonxx.h"
#include "keysig.h"
#include "label.h"
#include "labelabbr.h"
#include "layer.h"
#include "lb.h"
#include "mdiv.h"
#include "measure.h"
#include "metersig.h"
#include "mrest.h"
#include "note.h"
#include "octave.h"
#include "pb.h"
#include "pedal.h"
#include "pgfoot.h"
#include "pghead.h"
#include "reh.h"
#include "rend.h"
#include "rest.h"
#include "sb.h"
#include "score.h"
#include "section.h"
#include "slur.h"
#include "space.h"
#include "staff.h"
#include "staffdef.h"
#include "staffgrp.h"
#include "tempo.h"
#include "text.h"
#include "tie.h"
#include "trill.h"
#include "tuplet.h"
#include "vrv.h"

namespace vrv {

namespace {

    using JObject = jsonxx::Object;
    using JArray = jsonxx::Array;

    bool Fail(const std::string &code, const std::string &path, const std::string &message)
    {
        LogError("%s path=%s message=%s", code.c_str(), path.c_str(), message.c_str());
        return false;
    }

    const JObject *ObjectAt(
        const JObject &parent, const std::string &key, const std::string &path, bool required = true)
    {
        if (parent.has<JObject>(key)) return &parent.get<JObject>(key);
        if (required) Fail("JSM_REQUIRED_OBJECT", path + "/" + key, "expected object");
        return nullptr;
    }

    const JArray *ArrayAt(const JObject &parent, const std::string &key, const std::string &path, bool required = true)
    {
        if (parent.has<JArray>(key)) return &parent.get<JArray>(key);
        if (required) Fail("JSM_REQUIRED_ARRAY", path + "/" + key, "expected array");
        return nullptr;
    }

    bool StringAt(const JObject &parent, const std::string &key, const std::string &path, std::string &value,
        bool required = true)
    {
        if (parent.has<jsonxx::String>(key)) {
            value = parent.get<jsonxx::String>(key);
            return true;
        }
        if (required) return Fail("JSM_REQUIRED_STRING", path + "/" + key, "expected string");
        return false;
    }

    int IntAt(const JObject &parent, const std::string &key, int fallback)
    {
        if (!parent.has<jsonxx::Number>(key)) return fallback;
        const long double value = parent.get<jsonxx::Number>(key);
        if (!std::isfinite(static_cast<double>(value)) || value != std::floor(value)
            || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            return fallback;
        return static_cast<int>(value);
    }

    bool IntegerAt(const JObject &parent, const std::string &key, const std::string &path, int &value,
        bool required = true, int fallback = 0)
    {
        if (!parent.has<jsonxx::Number>(key)) {
            if (required) return Fail("JSM_REQUIRED_INTEGER", path + "/" + key, "expected integer");
            value = fallback;
            return true;
        }
        const long double source = parent.get<jsonxx::Number>(key);
        if (!std::isfinite(static_cast<double>(source)) || source != std::floor(source)
            || source < std::numeric_limits<int>::min() || source > std::numeric_limits<int>::max()) {
            return Fail("JSM_INVALID_INTEGER", path + "/" + key, "expected integer");
        }
        value = static_cast<int>(source);
        return true;
    }

    bool BoolAt(const JObject &parent, const std::string &key, bool fallback)
    {
        return parent.has<jsonxx::Boolean>(key) ? parent.get<jsonxx::Boolean>(key) : fallback;
    }

    bool RationalAt(const JObject &parent, const std::string &key, const std::string &path, long double &value)
    {
        const JArray *rational = ArrayAt(parent, key, path, false);
        if (!rational || rational->size() != 2 || !rational->has<jsonxx::Number>(0)
            || !rational->has<jsonxx::Number>(1))
            return Fail("JSM_INVALID_RATIONAL", path + "/" + key, "expected [numerator, denominator]");
        const long double numerator = rational->get<jsonxx::Number>(0);
        const long double denominator = rational->get<jsonxx::Number>(1);
        if (!std::isfinite(static_cast<double>(numerator)) || !std::isfinite(static_cast<double>(denominator))
            || numerator != std::floor(numerator) || denominator != std::floor(denominator) || denominator <= 0) {
            return Fail(
                "JSM_INVALID_RATIONAL", path + "/" + key, "expected integral numerator and positive denominator");
        }
        value = numerator / denominator;
        return true;
    }

    data_DURATION DurationFromType(const std::string &type)
    {
        static const std::map<std::string, data_DURATION> durations = { { "maxima", DURATION_maxima },
            { "long", DURATION_long }, { "breve", DURATION_breve }, { "whole", DURATION_1 }, { "half", DURATION_2 },
            { "quarter", DURATION_4 }, { "eighth", DURATION_8 }, { "16th", DURATION_16 }, { "32nd", DURATION_32 },
            { "64th", DURATION_64 }, { "128th", DURATION_128 }, { "256th", DURATION_256 }, { "512th", DURATION_512 },
            { "1024th", DURATION_1024 }, { "2048th", DURATION_2048 } };
        auto iter = durations.find(type);
        return (iter == durations.end()) ? DURATION_NONE : iter->second;
    }

    std::u32string MetronomeGlyph(const std::string &type)
    {
        static const std::map<std::string, std::u32string> glyphs = { { "breve", U"\xECA0" }, { "whole", U"\xECA2" },
            { "half", U"\xECA3" }, { "quarter", U"\xECA5" }, { "eighth", U"\xECA7" }, { "16th", U"\xECA9" },
            { "32nd", U"\xECAB" }, { "64th", U"\xECAD" }, { "128th", U"\xECAF" }, { "256th", U"\xECB1" } };
        auto iter = glyphs.find(type);
        return (iter == glyphs.end()) ? std::u32string() : iter->second;
    }

    data_PITCHNAME PitchName(const std::string &step)
    {
        if (step == "C") return PITCHNAME_c;
        if (step == "D") return PITCHNAME_d;
        if (step == "E") return PITCHNAME_e;
        if (step == "F") return PITCHNAME_f;
        if (step == "G") return PITCHNAME_g;
        if (step == "A") return PITCHNAME_a;
        if (step == "B") return PITCHNAME_b;
        return PITCHNAME_NONE;
    }

    data_ACCIDENTAL_WRITTEN WrittenAccidental(int alter)
    {
        if (alter == -2) return ACCIDENTAL_WRITTEN_ff;
        if (alter == -1) return ACCIDENTAL_WRITTEN_f;
        if (alter == 1) return ACCIDENTAL_WRITTEN_s;
        if (alter == 2) return ACCIDENTAL_WRITTEN_x;
        if (alter == 0) return ACCIDENTAL_WRITTEN_n;
        return ACCIDENTAL_WRITTEN_NONE;
    }

    data_ACCIDENTAL_GESTURAL GesturalAccidental(int alter)
    {
        if (alter == -2) return ACCIDENTAL_GESTURAL_ff;
        if (alter == -1) return ACCIDENTAL_GESTURAL_f;
        if (alter == 1) return ACCIDENTAL_GESTURAL_s;
        if (alter == 2) return ACCIDENTAL_GESTURAL_ss;
        return ACCIDENTAL_GESTURAL_n;
    }

    int KeyAlter(const std::string &step, int fifths)
    {
        static const std::string sharps[] = { "F", "C", "G", "D", "A", "E", "B" };
        static const std::string flats[] = { "B", "E", "A", "D", "G", "C", "F" };
        if (fifths > 0) {
            for (int i = 0; i < std::min(fifths, 7); ++i) {
                if (step == sharps[i]) return 1;
            }
        }
        else {
            for (int i = 0; i < std::min(-fifths, 7); ++i) {
                if (step == flats[i]) return -1;
            }
        }
        return 0;
    }

    data_ARTICULATION Articulation(const std::string &name)
    {
        if (name == "staccato") return ARTICULATION_stacc;
        if (name == "tenuto") return ARTICULATION_ten;
        if (name == "accent") return ARTICULATION_acc;
        if (name == "strong-accent") return ARTICULATION_marc;
        if (name == "marcato") return ARTICULATION_marc;
        if (name == "staccatissimo") return ARTICULATION_stacciss;
        if (name == "spiccato") return ARTICULATION_spicc;
        if (name == "scoop") return ARTICULATION_scoop;
        if (name == "plop") return ARTICULATION_plop;
        if (name == "doit") return ARTICULATION_doit;
        if (name == "falloff") return ARTICULATION_fall;
        return ARTICULATION_NONE;
    }

    bool AddPitch(Note *note, const JObject &pitch, const JObject &tone, std::map<std::string, int> &accidentalState,
        int fifths, const std::string &path)
    {
        std::string kind;
        if (!StringAt(pitch, "kind", path, kind)) return false;
        if (kind != "pitched") return Fail("JSM_UNSUPPORTED_PITCH", path + "/kind", "only pitched notes are supported");
        std::string step;
        if (!StringAt(pitch, "step", path, step)) return false;
        const data_PITCHNAME pname = PitchName(step);
        if (pname == PITCHNAME_NONE) return Fail("JSM_INVALID_PITCH", path + "/step", "expected A through G");
        const int octave = IntAt(pitch, "octave", -100);
        if (octave < 0 || octave > 9) return Fail("JSM_INVALID_PITCH", path + "/octave", "expected octave 0 through 9");
        note->SetPname(pname);
        note->SetOct(octave);
        const int alter = IntAt(pitch, "alter", 0);
        const data_ACCIDENTAL_WRITTEN accidental = WrittenAccidental(alter);
        if (accidental == ACCIDENTAL_WRITTEN_NONE) {
            return Fail(
                "JSM_UNSUPPORTED_ACCIDENTAL", path + "/alter", "only double-flat through double-sharp are supported");
        }
        const std::string stateKey = step + std::to_string(octave);
        const int activeAlter
            = accidentalState.contains(stateKey) ? accidentalState.at(stateKey) : KeyAlter(step, fifths);
        const std::string accidentalName = tone.get<jsonxx::String>("accidental", "none");
        const bool courtesy = accidentalName.rfind("courtesy-", 0) == 0;
        if (alter != 0 || alter != activeAlter || courtesy) {
            Accid *accid = new Accid();
            accid->SetAccidGes(GesturalAccidental(alter));
            // pitch.alter is semantic. A written accidental is only needed when it
            // changes the active key/measure state, or when explicitly courtesy.
            if (alter != activeAlter || courtesy) accid->SetAccid(accidental);
            note->AddChild(accid);
        }
        accidentalState[stateKey] = alter;
        return true;
    }

    bool AddArticulations(
        Object *event, const JObject &source, Measure *measure, const std::string &path, int staffNumber, int beatType)
    {
        const JArray *values = ArrayAt(source, "articulations", path, false);
        if (!values) return true;
        std::vector<data_ARTICULATION> result;
        for (unsigned int i = 0; i < values->size(); ++i) {
            if (!values->has<jsonxx::String>(i)) {
                return Fail(
                    "JSM_INVALID_ARTICULATION", path + "/articulations/" + std::to_string(i), "expected string");
            }
            const std::string name = values->get<jsonxx::String>(i);
            if (name == "fermata") {
                Fermata *fermata = new Fermata();
                fermata->SetID(event->GetID() + "-fermata");
                fermata->SetStartid("#" + event->GetID());
                fermata->m_unsupported.push_back({ "jsm-event-id", event->GetID() });
                measure->AddChild(fermata);
                continue;
            }
            if (name == "breath-mark") {
                Breath *breath = new Breath();
                long double onset = 0.0L;
                long double duration = 0.0L;
                if (!RationalAt(source, "onset", path, onset) || !RationalAt(source, "duration", path, duration))
                    return false;
                breath->SetStaff({ staffNumber });
                breath->SetTstamp(static_cast<double>(onset + duration) * beatType / 4.0 + 0.5);
                measure->AddChild(breath);
                continue;
            }
            if (name == "caesura") {
                Caesura *caesura = new Caesura();
                long double onset = 0.0L;
                long double duration = 0.0L;
                if (!RationalAt(source, "onset", path, onset) || !RationalAt(source, "duration", path, duration))
                    return false;
                caesura->SetStaff({ staffNumber });
                caesura->SetTstamp(static_cast<double>(onset + duration) * beatType / 4.0 + 0.5);
                measure->AddChild(caesura);
                continue;
            }
            if (event->Is(REST) || event->Is(MREST)) {
                return Fail("JSM_UNSUPPORTED_REST_ARTICULATION", path + "/articulations/" + std::to_string(i), name);
            }
            if (name == "detached-legato") {
                Artic *staccato = new Artic();
                staccato->SetArtic({ ARTICULATION_stacc });
                event->AddChild(staccato);
                Artic *tenuto = new Artic();
                tenuto->SetArtic({ ARTICULATION_ten });
                event->AddChild(tenuto);
                continue;
            }
            const data_ARTICULATION value = Articulation(name);
            if (value == ARTICULATION_NONE) {
                return Fail("JSM_UNSUPPORTED_ARTICULATION", path + "/articulations/" + std::to_string(i), name);
            }
            result.push_back(value);
        }
        if (!result.empty()) {
            Artic *artic = new Artic();
            artic->SetArtic(result);
            event->AddChild(artic);
        }
        return true;
    }

    bool SetDuration(
        DurationInterface *duration, const JObject &event, const std::string &path, bool tupletMember = false)
    {
        std::string noteType;
        data_DURATION value = DURATION_NONE;
        int inferredDots = 0;
        if (StringAt(event, "noteType", path, noteType, false)) {
            value = DurationFromType(noteType);
        }
        else {
            const JArray *rational = ArrayAt(event, "duration", path, false);
            if (!rational || rational->size() != 2 || !rational->has<jsonxx::Number>(0)
                || !rational->has<jsonxx::Number>(1) || rational->get<jsonxx::Number>(1) == 0) {
                return Fail("JSM_REQUIRED_DURATION", path + "/duration", "noteType or rational duration is required");
            }
            const long double quarters = rational->get<jsonxx::Number>(0) / rational->get<jsonxx::Number>(1);
            static const std::vector<std::pair<long double, data_DURATION>> bases
                = { { 8.0L, DURATION_breve }, { 4.0L, DURATION_1 }, { 2.0L, DURATION_2 }, { 1.0L, DURATION_4 },
                      { 0.5L, DURATION_8 }, { 0.25L, DURATION_16 }, { 0.125L, DURATION_32 }, { 0.0625L, DURATION_64 } };
            for (const auto &[base, candidate] : bases) {
                long double factor = 1.0L;
                for (int dots = 0; dots <= 4; ++dots) {
                    if (std::abs(quarters - base * factor) < 1e-12L) {
                        value = candidate;
                        inferredDots = dots;
                        break;
                    }
                    factor += 1.0L / std::pow(2.0L, dots + 1);
                }
                if (value != DURATION_NONE) break;
            }
        }
        if (value == DURATION_NONE) return Fail("JSM_UNSUPPORTED_DURATION", path + "/noteType", noteType);
        duration->SetDur(value);
        const int dots = IntAt(event, "dots", inferredDots);
        if (dots < 0 || dots > 4) return Fail("JSM_INVALID_DOTS", path + "/dots", "expected 0 through 4");
        duration->SetDots(dots);
        long double encodedDuration = 0.0L;
        if (!RationalAt(event, "duration", path, encodedDuration)) return false;
        static const std::map<data_DURATION, long double> quarterValues
            = { { DURATION_breve, 8.0L }, { DURATION_1, 4.0L }, { DURATION_2, 2.0L }, { DURATION_4, 1.0L },
                  { DURATION_8, 0.5L }, { DURATION_16, 0.25L }, { DURATION_32, 0.125L }, { DURATION_64, 0.0625L },
                  { DURATION_128, 0.03125L }, { DURATION_256, 0.015625L }, { DURATION_512, 0.0078125L },
                  { DURATION_1024, 0.00390625L }, { DURATION_2048, 0.001953125L } };
        auto quarterValue = quarterValues.find(value);
        if (quarterValue != quarterValues.end()) {
            long double dotFactor = 1.0L;
            for (int i = 0; i < dots; ++i) dotFactor += 1.0L / std::pow(2.0L, i + 1);
            const bool grace = ObjectAt(event, "grace", path, false) != nullptr;
            if (!grace && !tupletMember && std::abs(encodedDuration - quarterValue->second * dotFactor) > 1e-12L) {
                return Fail("JSM_DURATION_MISMATCH", path + "/duration",
                    "rational duration does not match noteType and dots outside a tuplet");
            }
        }
        return true;
    }

    bool AddTone(Note *note, const JObject &tone, std::map<std::string, int> &accidentalState, int fifths,
        const std::string &path)
    {
        const JObject *pitch = ObjectAt(tone, "pitch", path);
        if (!pitch) return false;
        const std::string kind = pitch->get<jsonxx::String>("kind", "");
        if (kind == "unpitched") {
            std::string step;
            if (!StringAt(*pitch, "displayStep", path + "/pitch", step)) return false;
            const data_PITCHNAME pname = PitchName(step);
            const int octave = IntAt(*pitch, "displayOctave", -100);
            if (pname == PITCHNAME_NONE || octave < 0 || octave > 9) {
                return Fail("JSM_INVALID_UNPITCHED", path + "/pitch", "invalid display pitch");
            }
            note->SetLoc(note->CalcLoc(pname, octave, -2));
        }
        else if (!AddPitch(note, *pitch, tone, accidentalState, fifths, path + "/pitch")) {
            return false;
        }
        if (tone.has<jsonxx::String>("notehead")) {
            static const std::map<std::string, data_HEADSHAPE_list> noteheads
                = { { "normal", HEADSHAPE_list_NONE }, { "cross", HEADSHAPE_list_plus },
                      { "diamond", HEADSHAPE_list_diamond }, { "triangle", HEADSHAPE_list_rtriangle },
                      { "slash", HEADSHAPE_list_slash }, { "square", HEADSHAPE_list_square },
                      { "x-circle", HEADSHAPE_list_slash }, { "none", HEADSHAPE_list_NONE } };
            const std::string value = tone.get<jsonxx::String>("notehead");
            auto iter = noteheads.find(value);
            if (iter == noteheads.end()) return Fail("JSM_UNSUPPORTED_NOTEHEAD", path + "/notehead", value);
            if (iter->second != HEADSHAPE_list_NONE) {
                data_HEADSHAPE shape;
                shape.SetHeadShapeList(iter->second);
                note->SetHeadShape(shape);
            }
            if (value == "none") note->SetHeadVisible(BOOLEAN_false);
        }
        return true;
    }

    template <typename T> bool SetEventAppearance(T *element, const JObject &event, const std::string &path)
    {
        if (event.has<jsonxx::String>("stem")) {
            const std::string value = event.get<jsonxx::String>("stem");
            if (value == "up")
                element->SetStemDir(STEMDIRECTION_up);
            else if (value == "down")
                element->SetStemDir(STEMDIRECTION_down);
            else if (value == "none")
                element->SetStemVisible(BOOLEAN_false);
            else if (value != "auto")
                return Fail("JSM_INVALID_STEM", path + "/stem", value);
        }
        const JObject *grace = ObjectAt(event, "grace", path, false);
        if (grace) {
            const bool slash = BoolAt(*grace, "slash", false);
            element->SetGrace(slash ? GRACE_unacc : GRACE_acc);
            if (slash) element->SetStemMod(STEMMODIFIER_1slash);
        }
        return true;
    }

    std::string SpannerEventId(const JObject &spanner, const std::string &endpoint)
    {
        const JObject *source = ObjectAt(spanner, endpoint, "/score/parts/measures/spanners", false);
        if (!source || source->get<jsonxx::String>("kind", "") != "event") return {};
        return source->get<jsonxx::String>("eventId", "");
    }

    const JObject *TupletStartingAt(const JObject &measure, const std::string &eventId)
    {
        const JArray *spanners = ArrayAt(measure, "spanners", "/score/parts/measures", false);
        if (!spanners) return nullptr;
        for (unsigned int i = 0; i < spanners->size(); ++i) {
            if (!spanners->has<JObject>(i)) continue;
            const JObject &spanner = spanners->get<JObject>(i);
            if (spanner.get<jsonxx::String>("kind", "") == "tuplet" && SpannerEventId(spanner, "start") == eventId)
                return &spanner;
        }
        return nullptr;
    }

    bool IsCue(const JObject &event)
    {
        const JObject *cue = ObjectAt(event, "cue", "/score/parts/measures/staves/voices/events", false);
        return cue && BoolAt(*cue, "printed", false);
    }

    struct StaffBinding {
        int number = 0;
        std::string partId;
        std::string staffId;
        int fifths = 0;
        std::string contextId;
    };

    struct PendingSpanner {
        const JObject *source = nullptr;
        Measure *measure = nullptr;
        std::string path;
        std::string partId;
        int staffNumber = 0;
        int beatType = 4;
    };

    struct PendingContextChange {
        const JObject *source = nullptr;
        const JObject *context = nullptr;
        std::string path;
        long double onset = 0.0L;
        std::set<std::string> staffIds;
    };

    std::string JsonValue(const jsonxx::Value &value);

    const JObject *ContextClefForStaff(const JObject &context, const std::string &staffId)
    {
        const JArray *staves = ArrayAt(context, "staves", "/score/parts/contexts", false);
        if (!staves) return nullptr;
        for (unsigned int i = 0; i < staves->size(); ++i) {
            if (!staves->has<JObject>(i)) continue;
            const JObject &staff = staves->get<JObject>(i);
            if (staff.get<jsonxx::String>("staffId", "") == staffId) {
                return ObjectAt(staff, "clef", "/score/parts/contexts/staves", false);
            }
        }
        return nullptr;
    }

    bool SameObject(const JObject *left, const JObject *right)
    {
        return (left ? JsonValue(jsonxx::Value(*left)) : std::string())
            == (right ? JsonValue(jsonxx::Value(*right)) : std::string());
    }

    bool ValidateIds(const jsonxx::Value &value, const std::string &path, std::set<std::string> &ids)
    {
        static const std::regex safeId("^[A-Za-z_][A-Za-z0-9_.:-]*$");
        if (value.is<JObject>()) {
            const JObject &object = value.get<JObject>();
            for (const auto &[key, child] : object.kv_map()) {
                const std::string childPath = path + "/" + key;
                if (key == "id") {
                    if (!child->is<jsonxx::String>()) return Fail("JSM_INVALID_ID", childPath, "expected string");
                    const std::string &id = child->get<jsonxx::String>();
                    if (!std::regex_match(id, safeId)) {
                        return Fail("JSM_UNSAFE_ID", childPath, "ID must match [A-Za-z_][A-Za-z0-9_.:-]*");
                    }
                    if (!ids.insert(id).second) return Fail("JSM_DUPLICATE_ID", childPath, id);
                }
                if (!ValidateIds(*child, childPath, ids)) return false;
            }
        }
        else if (value.is<JArray>()) {
            const JArray &array = value.get<JArray>();
            for (unsigned int i = 0; i < array.size(); ++i) {
                if (!ValidateIds(*array.values().at(i), path + "/" + std::to_string(i), ids)) return false;
            }
        }
        return true;
    }

    const JObject *InitialContext(const JObject &part)
    {
        const JArray *contexts = ArrayAt(part, "contexts", "/score/parts", false);
        if (!contexts || contexts->empty()) return nullptr;
        std::string contextRef;
        const JArray *measures = ArrayAt(part, "measures", "/score/parts", false);
        if (measures && !measures->empty() && measures->has<JObject>(0)) {
            StringAt(measures->get<JObject>(0), "contextRef", "/score/parts/measures/0", contextRef, false);
        }
        for (unsigned int i = 0; i < contexts->size(); ++i) {
            if (!contexts->has<JObject>(i)) continue;
            const JObject &context = contexts->get<JObject>(i);
            if (!contextRef.empty() && context.has<jsonxx::String>("id")
                && context.get<jsonxx::String>("id") == contextRef)
                return &context;
        }
        return contexts->has<JObject>(0) ? &contexts->get<JObject>(0) : nullptr;
    }

    const JObject *ContextById(const JObject &part, const std::string &contextId)
    {
        const JArray *contexts = ArrayAt(part, "contexts", "/score/parts", false);
        if (!contexts) return nullptr;
        for (unsigned int i = 0; i < contexts->size(); ++i) {
            if (!contexts->has<JObject>(i)) continue;
            const JObject &context = contexts->get<JObject>(i);
            if (context.has<jsonxx::String>("id") && context.get<jsonxx::String>("id") == contextId) return &context;
        }
        return nullptr;
    }

    void AddTextLines(Object *parent, const std::string &value)
    {
        std::stringstream stream(value);
        std::string line;
        bool first = true;
        while (std::getline(stream, line)) {
            if (!first) parent->AddChild(new Lb());
            Text *text = new Text();
            text->SetText(UTF8to32(line));
            parent->AddChild(text);
            first = false;
        }
    }

    void AddPartLabels(Object *parent, const JObject &part)
    {
        if (part.has<jsonxx::String>("name")) {
            Label *label = new Label();
            AddTextLines(label, part.get<jsonxx::String>("name"));
            parent->AddChild(label);
        }
        if (part.has<jsonxx::String>("abbreviation")) {
            LabelAbbr *label = new LabelAbbr();
            AddTextLines(label, part.get<jsonxx::String>("abbreviation"));
            parent->AddChild(label);
        }
    }

    void AddDocumentHeader(Doc *doc, const JObject &score)
    {
        const JObject *metadata = ObjectAt(score, "metadata", "/score", false);
        if (!metadata) {
            doc->GenerateMEIHeader();
            return;
        }
        pugi::xml_node meiHead = doc->m_header.append_child("meiHead");
        pugi::xml_node fileDesc = meiHead.append_child("fileDesc");
        pugi::xml_node titleStmt = fileDesc.append_child("titleStmt");
        pugi::xml_node title = titleStmt.append_child("title");
        if (metadata->has<jsonxx::String>("title")) {
            title.text().set(metadata->get<jsonxx::String>("title").c_str());
        }
        pugi::xml_node respStmt = titleStmt.append_child("respStmt");
        if (metadata->has<jsonxx::String>("composer")) {
            pugi::xml_node composer = respStmt.append_child("persName");
            composer.append_attribute("role").set_value("composer");
            composer.text().set(metadata->get<jsonxx::String>("composer").c_str());
        }
        pugi::xml_node pubStmt = fileDesc.append_child("pubStmt");
        if (metadata->has<jsonxx::String>("rights")) {
            pugi::xml_node availability = pubStmt.append_child("availability");
            availability.append_child("distributor").text().set(metadata->get<jsonxx::String>("rights").c_str());
        }
    }

    std::string ExtensionAttribute(const JObject *attributes, const std::string &name)
    {
        return (attributes && attributes->has<jsonxx::String>(name)) ? attributes->get<jsonxx::String>(name)
                                                                     : std::string();
    }

    float ExtensionFloatAttribute(const JObject *attributes, const std::string &name)
    {
        const std::string value = ExtensionAttribute(attributes, name);
        if (value.empty()) return 0.0F;
        char *end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        return (end == value.c_str()) ? 0.0F : parsed;
    }

    void AddPageCredits(Score *score, const JObject &scoreSource)
    {
        const JObject *metadata = ObjectAt(scoreSource, "metadata", "/score", false);
        const JObject *extensions = metadata ? ObjectAt(*metadata, "extensions", "/score/metadata", false) : nullptr;
        const JArray *credits = extensions
            ? ArrayAt(*extensions, "com.musicstand.musicxml.page-credits", "/score/metadata/extensions", false)
            : nullptr;
        if (!credits) return;

        PgHead *head = nullptr;
        PgFoot *foot = nullptr;
        for (unsigned int creditIndex = 0; creditIndex < credits->size(); ++creditIndex) {
            if (!credits->has<JObject>(creditIndex)) continue;
            const JObject &credit = credits->get<JObject>(creditIndex);
            const JObject *creditAttributes = ObjectAt(credit, "attributes", "/score/metadata/extensions", false);
            if (ExtensionAttribute(creditAttributes, "page") != "1") continue;
            const JArray *items = ArrayAt(credit, "items", "/score/metadata/extensions", false);
            if (!items) continue;
            for (unsigned int itemIndex = 0; itemIndex < items->size(); ++itemIndex) {
                if (!items->has<JObject>(itemIndex)) continue;
                const JObject &item = items->get<JObject>(itemIndex);
                if (!item.has<jsonxx::String>("kind") || item.get<jsonxx::String>("kind") != "credit-words") continue;
                const JObject *attributes = ObjectAt(item, "attributes", "/score/metadata/extensions", false);
                Rend *rend = new Rend();
                rend->SetColor(ExtensionAttribute(attributes, "color"));
                rend->SetHalign(
                    rend->AttHorizontalAlign::StrToHorizontalalignment(ExtensionAttribute(attributes, "justify")));
                rend->SetValign(
                    rend->AttVerticalAlign::StrToVerticalalignment(ExtensionAttribute(attributes, "valign")));
                rend->SetFontstyle(rend->AttTypography::StrToFontstyle(ExtensionAttribute(attributes, "font-style")));
                rend->SetFontweight(
                    rend->AttTypography::StrToFontweight(ExtensionAttribute(attributes, "font-weight")));
                Text *text = new Text();
                text->SetText(UTF8to32(item.get<jsonxx::String>("text", "")));
                rend->AddChild(text);
                if (ExtensionFloatAttribute(attributes, "default-y") < 0.0F) {
                    if (!foot) foot = new PgFoot();
                    foot->AddChild(rend);
                }
                else {
                    if (!head) head = new PgHead();
                    head->SetFunc(PGFUNC_first);
                    head->AddChild(rend);
                }
            }
        }
        if (head) score->GetScoreDef()->AddChild(head);
        if (foot) score->GetScoreDef()->AddChild(foot);
    }

    int GreatestCommonDivisor(int left, int right)
    {
        while (right != 0) {
            const int remainder = left % right;
            left = right;
            right = remainder;
        }
        return std::abs(left);
    }

    void CollectPpq(const jsonxx::Value &value, int &ppq)
    {
        if (value.is<JObject>()) {
            const JObject &object = value.get<JObject>();
            for (const auto &[key, child] : object.kv_map()) {
                if ((key == "onset" || key == "duration") && child->is<JArray>()) {
                    const JArray &rational = child->get<JArray>();
                    if (rational.size() == 2 && rational.has<jsonxx::Number>(1)) {
                        const int denominator = static_cast<int>(rational.get<jsonxx::Number>(1));
                        if (denominator > 0) {
                            const long long next
                                = static_cast<long long>(ppq) / GreatestCommonDivisor(ppq, denominator) * denominator;
                            if (next <= 32768) ppq = static_cast<int>(next);
                        }
                    }
                }
                CollectPpq(*child, ppq);
            }
        }
        else if (value.is<JArray>()) {
            const JArray &array = value.get<JArray>();
            for (const jsonxx::Value *child : array.values()) CollectPpq(*child, ppq);
        }
    }

    void AddTranspositionAndPpq(StaffDef *staffDef, const JObject &part, int ppq)
    {
        const JObject *instrument = ObjectAt(part, "instrument", "/score/parts", false);
        const JObject *transposition
            = instrument ? ObjectAt(*instrument, "transposition", "/score/parts/instrument", false) : nullptr;
        if (transposition) {
            const int octave = IntAt(*transposition, "octave", 0);
            staffDef->SetTransDiat(IntAt(*transposition, "diatonic", 0) + 7 * octave);
            staffDef->SetTransSemi(IntAt(*transposition, "chromatic", 0) + 12 * octave);
        }
        staffDef->SetPpq(ppq);
    }

    bool HasPresentationBreak(const JArray &parts, unsigned int barIndex, const std::string &key)
    {
        for (unsigned int partIndex = 0; partIndex < parts.size(); ++partIndex) {
            if (!parts.has<JObject>(partIndex)) continue;
            const JArray *measures = ArrayAt(parts.get<JObject>(partIndex), "measures", "/score/parts", false);
            if (!measures || !measures->has<JObject>(barIndex)) continue;
            const JObject *presentation
                = ObjectAt(measures->get<JObject>(barIndex), "presentation", "/score/parts/measures", false);
            if (presentation && BoolAt(*presentation, key, false)) return true;
        }
        return false;
    }

    double RationalValue(const JArray &rational)
    {
        if (rational.size() != 2 || !rational.has<jsonxx::Number>(0) || !rational.has<jsonxx::Number>(1)
            || rational.get<jsonxx::Number>(1) == 0)
            return 0.0;
        return static_cast<double>(rational.get<jsonxx::Number>(0) / rational.get<jsonxx::Number>(1));
    }

    data_STAFFREL Placement(const JObject &source)
    {
        const std::string value = source.get<jsonxx::String>("placement", "auto");
        if (value == "above") return STAFFREL_above;
        if (value == "below") return STAFFREL_below;
        return STAFFREL_NONE;
    }

    int InitialBeatType(const JObject &part)
    {
        const JObject *context = InitialContext(part);
        const JObject *time = context ? ObjectAt(*context, "time", "/score/parts/contexts", false) : nullptr;
        return time ? IntAt(*time, "beatType", 4) : 4;
    }

    bool AddDirectionRef(const JObject &reference, const JObject &conductor, const std::string &path,
        const std::string &defaultStaffId, const std::map<std::string, StaffBinding> &staffBindings, int beatType,
        std::set<std::string> &renderedOnce, Measure *measure)
    {
        std::string referenceId;
        std::string conductorId;
        std::string kind;
        std::string staffId;
        if (!StringAt(reference, "id", path, referenceId)
            || !StringAt(reference, "conductorEventRef", path, conductorId)
            || !StringAt(conductor, "kind", "/score/conductorTrack", kind))
            return false;
        if (!StringAt(reference, "staffId", path, staffId, false)) staffId = defaultStaffId;
        auto binding = staffBindings.find(staffId);
        if (binding == staffBindings.end()) return Fail("JSM_UNKNOWN_STAFF", path + "/staffId", staffId);

        const JArray *onset = ArrayAt(reference, "onset", path, false);
        const double timestamp = 1.0 + (onset ? RationalValue(*onset) * beatType / 4.0 : 0.0);
        const std::vector<int> staffNumbers{ binding->second.number };
        ControlElement *control = nullptr;
        const JObject *metronome
            = kind == "direction" ? ObjectAt(conductor, "metronome", "/score/conductorTrack/events", false) : nullptr;

        // MusicXML projects global tempo and rehearsal events only on the first
        // referring staff, while dynamics remain part-local.
        if ((kind == "tempo" || kind == "rehearsal" || metronome) && renderedOnce.contains(conductorId)) return true;
        const bool firstRendering = !renderedOnce.contains(conductorId);

        if (kind == "tempo") {
            Tempo *tempo = new Tempo();
            tempo->SetTstamp(timestamp);
            tempo->SetPlace(Placement(reference));
            tempo->SetStaff(staffNumbers);
            if (conductor.has<jsonxx::String>("value")) {
                Text *text = new Text();
                text->SetText(UTF8to32(conductor.get<jsonxx::String>("value")));
                tempo->AddChild(text);
            }
            const JObject *tempoSource = ObjectAt(conductor, "tempo", "/score/conductorTrack/events", false);
            const JArray *bpm
                = tempoSource ? ArrayAt(*tempoSource, "bpm", "/score/conductorTrack/events/tempo", false) : nullptr;
            if (tempoSource && tempoSource->has<jsonxx::String>("beatUnit")) {
                const std::string beatUnit = tempoSource->get<jsonxx::String>("beatUnit");
                tempo->SetMmUnit(DurationFromType(beatUnit));
                Rend *rend = new Rend();
                rend->SetGlyphAuth("smufl");
                Text *text = new Text();
                text->SetText(MetronomeGlyph(beatUnit));
                rend->AddChild(text);
                tempo->AddChild(rend);
            }
            if (bpm) {
                const double value = RationalValue(*bpm);
                tempo->SetMidiBpm(value);
                tempo->SetMm(value);
                Text *separator = new Text();
                separator->SetText(UTF8to32(" = "));
                tempo->AddChild(separator);
                std::ostringstream bpmText;
                bpmText << value;
                Text *text = new Text();
                text->SetText(UTF8to32(bpmText.str()));
                tempo->AddChild(text);
            }
            control = tempo;
            renderedOnce.insert(conductorId);
        }
        else if (kind == "rehearsal") {
            if (!conductor.has<jsonxx::String>("value")) {
                return Fail(
                    "JSM_INVALID_DIRECTION", "/score/conductorTrack/events/value", "rehearsal value must be text");
            }
            Reh *rehearsal = new Reh();
            rehearsal->SetPlace(Placement(reference));
            rehearsal->SetStaff(staffNumbers);
            Rend *rend = new Rend();
            const JObject *rehearsalSource = ObjectAt(conductor, "rehearsal", "/score/conductorTrack/events", false);
            const std::string enclosure = rehearsalSource ? rehearsalSource->get<jsonxx::String>("enclosure", "") : "";
            rend->SetRend(enclosure == "circle" ? TEXTRENDITION_circle : TEXTRENDITION_box);
            Text *text = new Text();
            text->SetText(UTF8to32(conductor.get<jsonxx::String>("value")));
            rend->AddChild(text);
            rehearsal->AddChild(rend);
            control = rehearsal;
            renderedOnce.insert(conductorId);
        }
        else if (kind == "direction" && metronome) {
            const JObject *left = ObjectAt(*metronome, "left", "/score/conductorTrack/events/metronome", false);
            const JObject *right = ObjectAt(*metronome, "rightNote", "/score/conductorTrack/events/metronome", false);
            const std::string relation = metronome->get<jsonxx::String>("relation", "");
            if (!left || !right || !left->has<jsonxx::String>("beatUnit") || !right->has<jsonxx::String>("beatUnit")
                || relation != "equals") {
                return Fail("JSM_UNSUPPORTED_DIRECTION", "/score/conductorTrack/events/metronome",
                    "expected left and rightNote beat units with an equals relation");
            }
            Tempo *tempo = new Tempo();
            tempo->SetTstamp(timestamp);
            tempo->SetPlace(Placement(reference));
            tempo->SetStaff(staffNumbers);
            tempo->SetMmUnit(DurationFromType(left->get<jsonxx::String>("beatUnit")));
            for (const JObject *note : { left, right }) {
                Rend *rend = new Rend();
                rend->SetGlyphAuth("smufl");
                std::u32string glyph = MetronomeGlyph(note->get<jsonxx::String>("beatUnit"));
                for (int dot = 0; dot < IntAt(*note, "dots", 0); ++dot) glyph += U" \xECB7";
                Text *text = new Text();
                text->SetText(glyph);
                rend->AddChild(text);
                tempo->AddChild(rend);
                if (note == left) {
                    Text *separator = new Text();
                    separator->SetText(UTF8to32(" = "));
                    tempo->AddChild(separator);
                }
            }
            control = tempo;
            renderedOnce.insert(conductorId);
        }
        else if (kind == "direction") {
            const JObject *value = ObjectAt(conductor, "value", "/score/conductorTrack/events", false);
            if (!value || !value->has<jsonxx::String>("dynamic")) {
                return Fail("JSM_UNSUPPORTED_DIRECTION", "/score/conductorTrack/events/value",
                    "only direction values with a dynamic string are currently engraved");
            }
            Dynam *dynamic = new Dynam();
            dynamic->SetTstamp(timestamp);
            dynamic->SetPlace(Placement(reference));
            dynamic->SetStaff(staffNumbers);
            Text *text = new Text();
            text->SetText(UTF8to32(value->get<jsonxx::String>("dynamic")));
            dynamic->AddChild(text);
            control = dynamic;
        }
        else {
            return Fail("JSM_UNSUPPORTED_DIRECTION", "/score/conductorTrack/events/kind", kind);
        }

        control->SetID(firstRendering ? conductorId : conductorId + "-staff-" + std::to_string(binding->second.number));
        renderedOnce.insert(conductorId);
        control->m_unsupported.push_back({ "jsm-conductor-event-id", conductorId });
        control->m_unsupported.push_back({ "jsm-direction-ref-id", referenceId });
        control->m_unsupported.push_back({ "jsm-staff-id", staffId });
        measure->AddChild(control);
        return true;
    }

    bool AddClef(Object *parent, const JObject &source, const std::string &path, bool materializeNone = false)
    {
        const std::string sign = source.get<jsonxx::String>("sign", "G");
        if (sign == "none" && !materializeNone) return true;
        Clef *clef = new Clef();
        if (sign == "none")
            clef->SetShape(CLEFSHAPE_NONE);
        else if (sign == "G")
            clef->SetShape(CLEFSHAPE_G);
        else if (sign == "F")
            clef->SetShape(CLEFSHAPE_F);
        else if (sign == "C")
            clef->SetShape(CLEFSHAPE_C);
        else if (sign == "percussion")
            clef->SetShape(CLEFSHAPE_perc);
        else {
            delete clef;
            return Fail("JSM_UNSUPPORTED_CLEF", path + "/sign", sign);
        }
        if (sign != "none" && sign != "percussion") clef->SetLine(IntAt(source, "line", sign == "F" ? 4 : 2));
        int octaveChange = 0;
        if (!IntegerAt(source, "octaveChange", path, octaveChange, false)) {
            delete clef;
            return false;
        }
        switch (std::abs(octaveChange)) {
            case 0: break;
            case 1: clef->SetDis(OCTAVE_DIS_8); break;
            case 2: clef->SetDis(OCTAVE_DIS_15); break;
            case 3: clef->SetDis(OCTAVE_DIS_22); break;
            default:
                delete clef;
                return Fail("JSM_UNSUPPORTED_CLEF", path + "/octaveChange", "expected an integer from -3 through 3");
        }
        if (octaveChange < 0)
            clef->SetDisPlace(STAFFREL_basic_below);
        else if (octaveChange > 0)
            clef->SetDisPlace(STAFFREL_basic_above);
        parent->AddChild(clef);
        return true;
    }

    bool AddKeySig(Object *parent, const JObject &key, const std::string &path)
    {
        int fifths = 0;
        if (!IntegerAt(key, "fifths", path, fifths)) return false;
        if (fifths < -7 || fifths > 7) return Fail("JSM_UNSUPPORTED_KEY", path + "/fifths", "expected -7 through 7");
        KeySig *keySig = new KeySig();
        keySig->SetSig({ std::abs(fifths), fifths < 0 ? ACCIDENTAL_WRITTEN_f : ACCIDENTAL_WRITTEN_s });
        if (key.kv_map().contains("cancel")) {
            int cancel = 0;
            if (!IntegerAt(key, "cancel", path, cancel)) {
                delete keySig;
                return false;
            }
            if (cancel < -7 || cancel > 7) {
                delete keySig;
                return Fail("JSM_UNSUPPORTED_KEY", path + "/cancel", "expected -7 through 7");
            }
            keySig->SetCancelaccid(CANCELACCID_before);
        }
        if (key.has<jsonxx::String>("mode")) {
            const std::string mode = key.get<jsonxx::String>("mode");
            const data_MODE parsedMode = keySig->AttKeySigAnl::StrToMode(mode, false);
            if (parsedMode == MODE_NONE && mode != "none") {
                delete keySig;
                return Fail("JSM_UNSUPPORTED_KEY", path + "/mode", mode);
            }
            keySig->SetMode(parsedMode);
        }
        parent->AddChild(keySig);
        return true;
    }

    bool AddMeter(Object *parent, const JObject &time, const std::string &path)
    {
        const std::string symbol = time.get<jsonxx::String>("symbol", "");
        if (!symbol.empty() && symbol != "common" && symbol != "cut" && symbol != "senza-misura") {
            return Fail("JSM_UNSUPPORTED_METER", path + "/symbol", symbol);
        }
        MeterSig *meter = new MeterSig();
        if (symbol == "common")
            meter->SetSym(METERSIGN_common);
        else if (symbol == "cut")
            meter->SetSym(METERSIGN_cut);
        else if (symbol == "senza-misura") {
            meter->SetVisible(BOOLEAN_false);
            meter->SetForm(METERFORM_norm);
        }
        if (symbol != "senza-misura") {
            const JArray *beats = ArrayAt(time, "beats", path, false);
            if (!beats || beats->empty()) {
                delete meter;
                return Fail("JSM_INVALID_METER", path + "/beats", "expected at least one beat group");
            }
            std::vector<int> count;
            for (unsigned int i = 0; i < beats->size(); ++i) {
                if (!beats->has<jsonxx::Number>(i)) {
                    delete meter;
                    return Fail("JSM_INVALID_METER", path + "/beats", "expected integer beat groups");
                }
                const long double value = beats->get<jsonxx::Number>(i);
                if (!std::isfinite(static_cast<double>(value)) || value != std::floor(value) || value <= 0
                    || value > std::numeric_limits<int>::max()) {
                    delete meter;
                    return Fail("JSM_INVALID_METER", path + "/beats", "beat groups must be positive");
                }
                count.push_back(static_cast<int>(value));
            }
            int beatType = 0;
            if (!IntegerAt(time, "beatType", path, beatType)) {
                delete meter;
                return false;
            }
            if (beatType <= 0) {
                delete meter;
                return Fail("JSM_INVALID_METER", path + "/beatType", "expected a positive integer");
            }
            meter->SetCount({ count, count.size() > 1 ? MeterCountSign::Plus : MeterCountSign::None });
            meter->SetUnit(beatType);
        }
        parent->AddChild(meter);
        return true;
    }

    bool AddInitialContext(
        StaffDef *staffDef, const JObject &part, const std::string &staffId, bool &meterAdded, ScoreDef *scoreDef)
    {
        const JObject *initial = InitialContext(part);
        if (!initial) return true;
        const JObject &context = *initial;
        const JArray *contextStaves = ArrayAt(context, "staves", "/score/parts/contexts", false);
        if (contextStaves) {
            for (unsigned int i = 0; i < contextStaves->size(); ++i) {
                if (!contextStaves->has<JObject>(i)) continue;
                const JObject &contextStaff = contextStaves->get<JObject>(i);
                if (!contextStaff.has<jsonxx::String>("staffId")
                    || contextStaff.get<jsonxx::String>("staffId") != staffId)
                    continue;
                const JObject *clef = ObjectAt(contextStaff, "clef", "/score/parts/contexts/staves", false);
                if (clef && !AddClef(staffDef, *clef, "/score/parts/contexts/staves/clef")) return false;
                break;
            }
        }
        const JObject *key = ObjectAt(context, "key", "/score/parts/contexts", false);
        if (key && !AddKeySig(staffDef, *key, "/score/parts/contexts/key")) return false;
        if (!meterAdded) {
            const JObject *time = ObjectAt(context, "time", "/score/parts/contexts", false);
            if (time) {
                if (!AddMeter(scoreDef, *time, "/score/parts/contexts/time")) return false;
                meterAdded = true;
            }
        }
        return true;
    }

    int InitialFifths(const JObject &part)
    {
        const JObject *context = InitialContext(part);
        if (!context) return 0;
        const JObject *key = ObjectAt(*context, "key", "/score/parts/contexts", false);
        return key ? IntAt(*key, "fifths", 0) : 0;
    }

    std::string InitialContextId(const JObject &part)
    {
        const JObject *context = InitialContext(part);
        return (context && context->has<jsonxx::String>("id")) ? context->get<jsonxx::String>("id") : std::string();
    }

    std::string JsonString(const std::string &value)
    {
        std::ostringstream out;
        out << '"';
        for (unsigned char ch : value) {
            switch (ch) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (ch < 0x20) {
                        const char hex[] = "0123456789abcdef";
                        out << "\\u00" << hex[ch >> 4] << hex[ch & 15];
                    }
                    else
                        out << static_cast<char>(ch);
            }
        }
        out << '"';
        return out.str();
    }

    std::string JsonValue(const jsonxx::Value &value)
    {
        if (value.is<jsonxx::Null>()) return "null";
        if (value.is<jsonxx::Boolean>()) return value.get<jsonxx::Boolean>() ? "true" : "false";
        if (value.is<jsonxx::String>()) return JsonString(value.get<jsonxx::String>());
        if (value.is<jsonxx::Number>()) {
            std::ostringstream out;
            out << std::setprecision(20) << value.get<jsonxx::Number>();
            return out.str();
        }
        if (value.is<JArray>()) {
            std::ostringstream out;
            out << '[';
            const JArray &array = value.get<JArray>();
            for (size_t i = 0; i < array.size(); ++i) {
                if (i) out << ',';
                out << JsonValue(*array.values()[i]);
            }
            out << ']';
            return out.str();
        }
        if (value.is<JObject>()) {
            std::ostringstream out;
            out << '{';
            bool first = true;
            for (const auto &entry : value.get<JObject>().kv_map()) {
                if (!first) out << ',';
                first = false;
                out << JsonString(entry.first) << ':' << JsonValue(*entry.second);
            }
            out << '}';
            return out.str();
        }
        return "null";
    }

    std::string JsonObject(const std::vector<std::pair<std::string, std::string>> &fields)
    {
        std::ostringstream out;
        out << '{';
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i) out << ',';
            out << JsonString(fields[i].first) << ':' << fields[i].second;
        }
        out << '}';
        return out.str();
    }

    std::string JsonArray(const std::vector<std::string> &items)
    {
        std::ostringstream out;
        out << '[';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out << ',';
            out << items[i];
        }
        out << ']';
        return out.str();
    }

    class CompactJsmDecoder {
    public:
        explicit CompactJsmDecoder(const JArray &root) : m_root(root) {}

        bool Decode(std::string &canonical)
        {
            if (m_root.size() < 5 || !m_root.has<jsonxx::String>(0) || m_root.get<jsonxx::String>(0) != "JSM"
                || !m_root.has<jsonxx::String>(1) || m_root.get<jsonxx::String>(1) != "0.1.0"
                || !m_root.has<jsonxx::String>(2) || m_root.get<jsonxx::String>(2) != "c" || !m_root.has<JObject>(3)
                || !m_root.has<JArray>(4))
                return Fail("JSM_PROFILE", "/", "expected JSM 0.1.0 compact envelope");
            const JObject &dict = m_root.get<JObject>(3);
            if (!dict.has<JArray>("s") || !dict.has<JArray>("i") || !dict.has<JArray>("p"))
                return Fail("JSM_COMPACT_DICTIONARY", "/3", "expected s, i, and p dictionaries");
            m_strings = &dict.get<JArray>("s");
            m_ids = &dict.get<JArray>("i");
            m_tones = &dict.get<JArray>("p");
            if (m_strings->size() > 262144 || m_ids->size() > 262144 || m_tones->size() > 262144)
                return Fail("JSM_RESOURCE_LIMIT", "/3", "compact dictionary exceeds 262144 entries");
            std::string score;
            if (!Score(m_root.get<JArray>(4), score)) return false;
            std::vector<std::pair<std::string, std::string>> fields = { { "format", "\"JSM\"" },
                { "version", "\"0.1.0\"" }, { "profile", "\"canonical\"" }, { "score", score } };
            const jsonxx::Value *extras = At(m_root, 5);
            if (extras && !extras->is<jsonxx::Null>()) {
                if (!extras->is<JObject>()) return Fail("JSM_COMPACT_EXTRAS", "/5", "expected object or null");
                for (const auto &entry : extras->get<JObject>().kv_map()) {
                    if (entry.first == "extraction") {
                        if (!entry.second->is<JArray>())
                            return Fail("JSM_COMPACT_EXTRACTION", "/5/extraction", "expected tuple");
                        std::string extraction;
                        if (!Extraction(entry.second->get<JArray>(), extraction)) return false;
                        fields.push_back({ "extraction", extraction });
                    }
                    else
                        fields.push_back({ entry.first, JsonValue(*entry.second) });
                }
            }
            canonical = JsonObject(fields);
            if (canonical.size() > 64 * 1024 * 1024)
                return Fail("JSM_RESOURCE_LIMIT", "/", "expanded compact document exceeds 64 MiB");
            return true;
        }

    private:
        const jsonxx::Value *At(const JArray &array, size_t index) const
        {
            return index < array.size() ? array.values()[index] : nullptr;
        }
        bool Ref(const JArray *table, const jsonxx::Value *reference, std::string &result, const std::string &path)
        {
            if (!reference || !reference->is<jsonxx::Number>())
                return Fail("JSM_COMPACT_REF", path, "expected reference");
            const long double raw = reference->get<jsonxx::Number>();
            if (raw < 0 || raw != std::floor(raw) || raw >= table->size())
                return Fail("JSM_COMPACT_REF", path, "reference outside dictionary");
            const std::string expanded = JsonValue(*table->values()[static_cast<size_t>(raw)]);
            if (expanded.size() > 32 * 1024 * 1024 - m_expandedReferenceBytes)
                return Fail("JSM_RESOURCE_LIMIT", path, "expanded compact references exceed 32 MiB");
            m_expandedReferenceBytes += expanded.size();
            result = expanded;
            return true;
        }
        bool Id(const JArray &a, size_t i, std::string &out) { return Ref(m_ids, At(a, i), out, "/compact/id"); }
        bool Str(const JArray &a, size_t i, std::string &out)
        {
            return Ref(m_strings, At(a, i), out, "/compact/string");
        }
        bool Tone(const jsonxx::Value *v, std::string &out) { return Ref(m_tones, v, out, "/compact/tone"); }
        void OptionalRaw(const JArray &a, size_t i, const std::string &name,
            std::vector<std::pair<std::string, std::string>> &fields) const
        {
            const jsonxx::Value *v = At(a, i);
            if (v && !v->is<jsonxx::Null>()) fields.push_back({ name, JsonValue(*v) });
        }
        bool RefArray(const jsonxx::Value *v, const JArray *table, std::string &out)
        {
            if (!v || !v->is<JArray>()) return Fail("JSM_COMPACT_ARRAY", "/compact", "expected reference array");
            if (v->get<JArray>().size() > 262144)
                return Fail("JSM_RESOURCE_LIMIT", "/compact", "compact collection exceeds 262144 entries");
            std::vector<std::string> items;
            for (auto item : v->get<JArray>().values()) {
                std::string decoded;
                if (!Ref(table, item, decoded, "/compact/ref")) return false;
                items.push_back(decoded);
            }
            out = JsonArray(items);
            return true;
        }
        template <typename F> bool MapArray(const jsonxx::Value *v, std::string &out, F fn)
        {
            if (!v || !v->is<JArray>()) return Fail("JSM_COMPACT_ARRAY", "/compact", "expected array");
            if (v->get<JArray>().size() > 262144)
                return Fail("JSM_RESOURCE_LIMIT", "/compact", "compact collection exceeds 262144 entries");
            std::vector<std::string> items;
            for (auto item : v->get<JArray>().values()) {
                if (!item->is<JArray>()) return Fail("JSM_COMPACT_TUPLE", "/compact", "expected tuple");
                std::string decoded;
                if (!(this->*fn)(item->get<JArray>(), decoded)) return false;
                items.push_back(decoded);
            }
            out = JsonArray(items);
            return true;
        }
        bool LogicalBar(const JArray &a, std::string &out)
        {
            std::string id, number;
            if (a.size() < 3 || !Id(a, 0, id) || !Str(a, 1, number)) return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "number", number }, { "duration", JsonValue(*At(a, 2)) } };
            const char *names[] = { "implicit", "navigation", "anchor", "extensions" };
            for (size_t i = 0; i < 4; ++i) OptionalRaw(a, 3 + i, names[i], f);
            out = JsonObject(f);
            return true;
        }
        bool ConductorEvent(const JArray &a, std::string &out)
        {
            std::string id;
            if (a.size() < 5 || !Id(a, 0, id)) return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "onset", JsonValue(*At(a, 1)) }, { "duration", JsonValue(*At(a, 2)) },
                      { "order", JsonValue(*At(a, 3)) }, { "kind", JsonValue(*At(a, 4)) } };
            OptionalRaw(a, 5, "value", f);
            OptionalRaw(a, 6, "tempo", f);
            const jsonxx::Value *extras = At(a, 7);
            if (extras && extras->is<JObject>())
                for (const auto &e : extras->get<JObject>().kv_map()) f.push_back({ e.first, JsonValue(*e.second) });
            out = JsonObject(f);
            return true;
        }
        bool ConductorMeasure(const JArray &a, std::string &out)
        {
            std::string id, bar, events;
            if (a.size() < 3 || !Id(a, 0, id) || !Id(a, 1, bar)
                || !MapArray(At(a, 2), events, &CompactJsmDecoder::ConductorEvent))
                return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "barId", bar }, { "events", events } };
            OptionalRaw(a, 3, "anchor", f);
            OptionalRaw(a, 4, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool StaffDef(const JArray &a, std::string &out)
        {
            std::string id;
            if (a.size() < 2 || !Id(a, 0, id)) return false;
            std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "number", JsonValue(*At(a, 1)) } };
            std::string name;
            if (At(a, 2) && !At(a, 2)->is<jsonxx::Null>()) {
                if (!Str(a, 2, name)) return false;
                f.push_back({ "name", name });
            }
            OptionalRaw(a, 3, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool VoiceLane(const JArray &a, std::string &out)
        {
            std::string id, staff;
            if (a.size() < 2 || !Id(a, 0, id) || !Id(a, 1, staff)) return false;
            std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "homeStaffId", staff } };
            std::string name;
            if (At(a, 2) && !At(a, 2)->is<jsonxx::Null>()) {
                if (!Str(a, 2, name)) return false;
                f.push_back({ "name", name });
            }
            OptionalRaw(a, 3, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool StaffContext(const JArray &a, std::string &out)
        {
            std::string id;
            if (a.size() < 2 || !Id(a, 0, id)) return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "staffId", id }, { "clef", JsonValue(*At(a, 1)) } };
            OptionalRaw(a, 2, "lines", f);
            OptionalRaw(a, 3, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool Context(const JArray &a, std::string &out)
        {
            std::string id, st;
            if (a.size() < 6 || !Id(a, 0, id) || !MapArray(At(a, 5), st, &CompactJsmDecoder::StaffContext))
                return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "contentHash", JsonValue(*At(a, 1)) }, { "time", JsonValue(*At(a, 2)) },
                      { "key", JsonValue(*At(a, 3)) } };
            OptionalRaw(a, 4, "transpose", f);
            f.push_back({ "staves", st });
            OptionalRaw(a, 6, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool Instrument(const JArray &a, std::string &out)
        {
            std::string id, sound;
            if (a.size() < 4 || !Id(a, 0, id) || !Str(a, 2, sound)) return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "sound", sound }, { "transposition", JsonValue(*At(a, 3)) } };
            std::string name;
            if (At(a, 1) && !At(a, 1)->is<jsonxx::Null>()) {
                if (!Str(a, 1, name)) return false;
                f.push_back({ "name", name });
            }
            const char *n[]
                = { "midiProgram", "midiChannel", "writtenRange", "concertRange", "percussion", "extensions" };
            for (size_t i = 0; i < 6; ++i) OptionalRaw(a, 4 + i, n[i], f);
            out = JsonObject(f);
            return true;
        }
        bool Endpoint(const JArray &a, std::string &out)
        {
            if (a.size() < 4 || !a.has<jsonxx::String>(0)) return false;
            std::string tag = a.get<jsonxx::String>(0), x;
            std::vector<std::pair<std::string, std::string>> f;
            if (tag == "e") {
                f.push_back({ "kind", "\"event\"" });
                if (!Id(a, 3, x)) return false;
                f.push_back({ "eventId", x });
                const char *n[] = { "partId", "measureId" };
                for (size_t i = 1; i <= 2; ++i)
                    if (At(a, i) && !At(a, i)->is<jsonxx::Null>()) {
                        if (!Id(a, i, x)) return false;
                        f.push_back({ n[i - 1], x });
                    }
                if (At(a, 4) && !At(a, 4)->is<jsonxx::Null>()) {
                    if (!Id(a, 4, x)) return false;
                    f.push_back({ "toneId", x });
                }
            }
            else if (tag == "p") {
                f.push_back({ "kind", "\"position\"" });
                if (!Id(a, 2, x)) return false;
                f.push_back({ "barId", x });
                f.push_back({ "onset", JsonValue(*At(a, 3)) });
                const char *n[] = { "partId", "staffId", "laneId" };
                const size_t p[] = { 1, 4, 5 };
                for (size_t i = 0; i < 3; ++i)
                    if (At(a, p[i]) && !At(a, p[i])->is<jsonxx::Null>()) {
                        if (!Id(a, p[i], x)) return false;
                        f.push_back({ n[i], x });
                    }
            }
            else {
                f.push_back({ "kind", "\"barline\"" });
                if (!Id(a, 2, x)) return false;
                f.push_back({ "barId", x });
                f.push_back({ "side", JsonValue(*At(a, 3)) });
                if (At(a, 1) && !At(a, 1)->is<jsonxx::Null>()) {
                    if (!Id(a, 1, x)) return false;
                    f.push_back({ "partId", x });
                }
                if (At(a, 4) && !At(a, 4)->is<jsonxx::Null>()) {
                    if (!Id(a, 4, x)) return false;
                    f.push_back({ "staffId", x });
                }
            }
            out = JsonObject(f);
            return true;
        }
        bool Spanner(const JArray &a, std::string &out)
        {
            std::string id, start, end;
            if (a.size() < 4 || !Id(a, 0, id) || !a.has<JArray>(2) || !a.has<JArray>(3)
                || !Endpoint(a.get<JArray>(2), start) || !Endpoint(a.get<JArray>(3), end))
                return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "kind", JsonValue(*At(a, 1)) }, { "start", start }, { "end", end } };
            const char *n[] = { "tuplet", "number", "placement", "properties", "anchor", "extensions" };
            for (size_t i = 0; i < 6; ++i) OptionalRaw(a, 4 + i, n[i], f);
            out = JsonObject(f);
            return true;
        }
        bool Event(const JArray &a, std::string &out)
        {
            if (a.size() < 5 || !a.has<jsonxx::String>(0)) return false;
            const std::string tag = a.get<jsonxx::String>(0);
            std::string id;
            if (!Id(a, 1, id)) return false;
            std::string type = tag == "n" ? "note"
                : tag == "c"              ? "chord"
                : tag == "r"              ? "rest"
                : tag == "s"              ? "spacer"
                                          : "forward";
            std::vector<std::pair<std::string, std::string>> f
                = { { "type", JsonString(type) }, { "id", id }, { "onset", JsonValue(*At(a, 2)) },
                      { "duration", JsonValue(*At(a, 3)) }, { "order", JsonValue(*At(a, 4)) } };
            size_t extra = 5;
            if (tag == "n") {
                std::string tone;
                if (!Tone(At(a, 5), tone)) return false;
                f.push_back({ "tone", tone });
                f.push_back({ "noteType", JsonValue(*At(a, 6)) });
                extra = 7;
            }
            else if (tag == "c") {
                if (!At(a, 5) || !At(a, 5)->is<JArray>()) return false;
                std::vector<std::string> tones;
                for (auto v : At(a, 5)->get<JArray>().values()) {
                    std::string t;
                    if (!Tone(v, t)) return false;
                    tones.push_back(t);
                }
                f.push_back({ "tones", JsonArray(tones) });
                f.push_back({ "noteType", JsonValue(*At(a, 6)) });
                extra = 7;
            }
            else if (tag == "r") {
                f.push_back({ "noteType", JsonValue(*At(a, 5)) });
                extra = 6;
            }
            const jsonxx::Value *e = At(a, extra);
            if (e && e->is<JObject>())
                for (const auto &v : e->get<JObject>().kv_map()) {
                    if (v.first == "staffId") {
                        std::string x;
                        if (!Ref(m_ids, v.second, x, "/compact/staffId")) return false;
                        f.push_back({ v.first, x });
                    }
                    else
                        f.push_back({ v.first, JsonValue(*v.second) });
                }
            out = JsonObject(f);
            return true;
        }
        bool Voice(const JArray &a, std::string &out)
        {
            std::string id, lane, events;
            if (a.size() < 3 || !Id(a, 0, id) || !Id(a, 1, lane)
                || !MapArray(At(a, 2), events, &CompactJsmDecoder::Event))
                return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "laneId", lane }, { "events", events } };
            OptionalRaw(a, 3, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool Staff(const JArray &a, std::string &out)
        {
            std::string id, voices;
            if (a.size() < 2 || !Id(a, 0, id) || !MapArray(At(a, 1), voices, &CompactJsmDecoder::Voice)) return false;
            std::vector<std::pair<std::string, std::string>> f = { { "staffId", id }, { "voices", voices } };
            OptionalRaw(a, 2, "anchor", f);
            OptionalRaw(a, 3, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool MeasureEvent(const JArray &a, std::string &out)
        {
            if (a.size() < 5 || !a.has<jsonxx::String>(0)) return false;
            std::string tag = a.get<jsonxx::String>(0), id, x;
            if (!Id(a, 1, id)) return false;
            std::vector<std::pair<std::string, std::string>> f;
            if (tag == "x") {
                if (!Id(a, 4, x)) return false;
                f = { { "type", "\"contextChange\"" }, { "id", id }, { "onset", JsonValue(*At(a, 2)) },
                    { "duration", "[0,1]" }, { "order", JsonValue(*At(a, 3)) }, { "contextRef", x } };
                if (At(a, 5) && !At(a, 5)->is<jsonxx::Null>()) {
                    std::string refs;
                    if (!RefArray(At(a, 5), m_ids, refs)) return false;
                    f.push_back({ "staffIds", refs });
                }
            }
            else if (tag == "d") {
                if (!Id(a, 5, x)) return false;
                f = { { "type", "\"directionRef\"" }, { "id", id }, { "onset", JsonValue(*At(a, 2)) },
                    { "duration", JsonValue(*At(a, 3)) }, { "order", JsonValue(*At(a, 4)) },
                    { "conductorEventRef", x } };
            }
            else {
                f = { { "type", "\"barline\"" }, { "id", id }, { "onset", JsonValue(*At(a, 2)) },
                    { "duration", "[0,1]" }, { "order", JsonValue(*At(a, 3)) }, { "side", JsonValue(*At(a, 4)) },
                    { "style", JsonValue(*At(a, 5)) } };
            }
            size_t ei = tag == "x" ? 6 : tag == "d" ? 6 : 6;
            const jsonxx::Value *e = At(a, ei);
            if (e && e->is<JObject>())
                for (const auto &v : e->get<JObject>().kv_map()) {
                    if (v.first == "staffId") {
                        if (!Ref(m_ids, v.second, x, "/compact/staffId")) return false;
                        f.push_back({ v.first, x });
                    }
                    else
                        f.push_back({ v.first, JsonValue(*v.second) });
                }
            out = JsonObject(f);
            return true;
        }
        bool Measure(const JArray &a, std::string &out)
        {
            std::string id, bar, ctx, refs, staves, events, spanners, active;
            if (a.size() < 9 || !Id(a, 0, id) || !Id(a, 1, bar) || !Id(a, 3, ctx) || !RefArray(At(a, 4), m_ids, refs)
                || !MapArray(At(a, 5), staves, &CompactJsmDecoder::Staff)
                || !MapArray(At(a, 6), events, &CompactJsmDecoder::MeasureEvent)
                || !MapArray(At(a, 7), spanners, &CompactJsmDecoder::Spanner))
                return false;
            if (!At(a, 8) || !At(a, 8)->is<JArray>()) return false;
            std::vector<std::string> acts;
            for (auto v : At(a, 8)->get<JArray>().values()) {
                if (!v->is<JArray>()) return false;
                const JArray &x = v->get<JArray>();
                std::string sid;
                if (!Id(x, 0, sid)) return false;
                acts.push_back(JsonObject({ { "spannerId", sid }, { "role", JsonValue(*At(x, 1)) } }));
            }
            active = JsonArray(acts);
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "barId", bar }, { "duration", JsonValue(*At(a, 2)) }, { "contextRef", ctx },
                      { "conductorEventRefs", refs }, { "staves", staves }, { "measureEvents", events },
                      { "spanners", spanners }, { "activeSpannerRefs", active } };
            const jsonxx::Value *e = At(a, 9);
            if (e && e->is<JObject>())
                for (const auto &v : e->get<JObject>().kv_map()) f.push_back({ v.first, JsonValue(*v.second) });
            out = JsonObject(f);
            return true;
        }
        bool Part(const JArray &a, std::string &out)
        {
            std::string id, name, instrument, staves, lanes, contexts, measures;
            if (a.size() < 9 || !Id(a, 0, id) || !Str(a, 1, name) || !a.has<JArray>(3)
                || !Instrument(a.get<JArray>(3), instrument)
                || !MapArray(At(a, 4), staves, &CompactJsmDecoder::StaffDef)
                || !MapArray(At(a, 5), lanes, &CompactJsmDecoder::VoiceLane)
                || !MapArray(At(a, 7), contexts, &CompactJsmDecoder::Context)
                || !MapArray(At(a, 8), measures, &CompactJsmDecoder::Measure))
                return false;
            std::vector<std::pair<std::string, std::string>> f
                = { { "id", id }, { "name", name }, { "instrument", instrument }, { "staves", staves },
                      { "voiceLanes", lanes }, { "contexts", contexts }, { "measures", measures } };
            std::string x;
            if (At(a, 2) && !At(a, 2)->is<jsonxx::Null>()) {
                if (!Str(a, 2, x)) return false;
                f.push_back({ "abbreviation", x });
            }
            if (At(a, 6) && !At(a, 6)->is<jsonxx::Null>()) {
                if (!RefArray(At(a, 6), m_ids, x)) return false;
                f.push_back({ "groups", x });
            }
            OptionalRaw(a, 9, "anchor", f);
            OptionalRaw(a, 10, "extensions", f);
            out = JsonObject(f);
            return true;
        }
        bool Extraction(const JArray &a, std::string &out)
        {
            if (a.size() < 8) return Fail("JSM_COMPACT_EXTRACTION", "/5/extraction", "tuple is too short");
            std::string sourceScore;
            std::string sourcePart;
            std::string sourceBars;
            if (!Id(a, 1, sourceScore) || !Id(a, 2, sourcePart) || !RefArray(At(a, 5), m_ids, sourceBars)) return false;
            if (!At(a, 7) || !At(a, 7)->is<JArray>())
                return Fail("JSM_COMPACT_EXTRACTION", "/5/extraction/7", "expected reference closure tuple");
            const JArray &closureTuple = At(a, 7)->get<JArray>();
            std::string included;
            if (closureTuple.empty() || !RefArray(At(closureTuple, 0), m_ids, included)) return false;
            std::vector<std::pair<std::string, std::string>> closure = { { "includedIds", included } };
            if (At(closureTuple, 1) && !At(closureTuple, 1)->is<jsonxx::Null>()) {
                std::string external;
                if (!RefArray(At(closureTuple, 1), m_ids, external)) return false;
                closure.push_back({ "externalRefs", external });
            }
            std::vector<std::pair<std::string, std::string>> fields
                = { { "kind", JsonValue(*At(a, 0)) }, { "sourceScoreId", sourceScore }, { "sourcePartId", sourcePart },
                      { "sourceHash", JsonValue(*At(a, 3)) }, { "sourceBarIds", sourceBars },
                      { "preservedVerbatim", JsonValue(*At(a, 6)) }, { "referenceClosure", JsonObject(closure) } };
            if (At(a, 4) && !At(a, 4)->is<jsonxx::Null>()) {
                std::string sourceUri;
                if (!Str(a, 4, sourceUri)) return false;
                fields.push_back({ "sourceUri", sourceUri });
            }
            if (At(a, 8) && !At(a, 8)->is<jsonxx::Null>()) {
                std::string incoming;
                if (!MapArray(At(a, 8), incoming, &CompactJsmDecoder::Spanner)) return false;
                fields.push_back({ "incomingSpanners", incoming });
            }
            if (At(a, 9) && !At(a, 9)->is<jsonxx::Null>()) {
                std::string extractedAt;
                if (!Str(a, 9, extractedAt)) return false;
                fields.push_back({ "extractedAt", extractedAt });
            }
            OptionalRaw(a, 10, "extensions", fields);
            out = JsonObject(fields);
            return true;
        }
        bool Score(const JArray &a, std::string &out)
        {
            std::string id, bars, parts;
            if (a.size() < 6 || !Id(a, 0, id) || !MapArray(At(a, 2), bars, &CompactJsmDecoder::LogicalBar) || !At(a, 3)
                || !At(a, 3)->is<JArray>() || !MapArray(At(a, 4), parts, &CompactJsmDecoder::Part))
                return false;
            const JArray &trackTuple = At(a, 3)->get<JArray>();
            std::string conductor;
            if (trackTuple.empty() || !MapArray(At(trackTuple, 0), conductor, &CompactJsmDecoder::ConductorMeasure))
                return false;
            std::vector<std::pair<std::string, std::string>> track = { { "measures", conductor } };
            OptionalRaw(trackTuple, 1, "extensions", track);
            std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "logicalBars", bars },
                { "conductorTrack", JsonObject(track) }, { "parts", parts }, { "views", JsonValue(*At(a, 5)) } };
            OptionalRaw(a, 1, "metadata", f);
            const char *n[] = { "performance", "analysis", "provenance", "validation", "extensions" };
            for (size_t i = 0; i < 5; ++i) OptionalRaw(a, 6 + i, n[i], f);
            out = JsonObject(f);
            return true;
        }

        const JArray &m_root;
        const JArray *m_strings = nullptr;
        const JArray *m_ids = nullptr;
        const JArray *m_tones = nullptr;
        size_t m_expandedReferenceBytes = 0;
    };

} // namespace

JsmInput::JsmInput(Doc *doc) : Input(doc) {}

bool JsmInput::Import(const std::string &data)
{
    if (data.size() > 64 * 1024 * 1024) return Fail("JSM_RESOURCE_LIMIT", "/", "input exceeds 64 MiB");

    const std::string::size_type first = data.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && data[first] == '[') {
        JArray compact;
        if (!compact.parse(data)) return Fail("JSM_PARSE_ERROR", "/", "invalid compact JSON");
        if (compact.size() >= 3 && compact.has<jsonxx::String>(0) && compact.get<jsonxx::String>(0) == "JSM") {
            std::string canonical;
            CompactJsmDecoder decoder(compact);
            return decoder.Decode(canonical) && Import(canonical);
        }
        return Fail("JSM_FORMAT", "/", "expected compact JSM envelope");
    }

    JObject root;
    if (!root.parse(data)) return Fail("JSM_PARSE_ERROR", "/", "invalid JSON");
    std::string format;
    std::string version;
    std::string profile;
    if (!StringAt(root, "format", "", format) || format != "JSM") return Fail("JSM_FORMAT", "/format", "expected JSM");
    if (!StringAt(root, "version", "", version) || version != "0.1.0") {
        return Fail("JSM_VERSION", "/version", "only JSM 0.1.0 is supported");
    }
    if (!StringAt(root, "profile", "", profile) || profile != "canonical") {
        return Fail("JSM_PROFILE", "/profile", "native importer currently requires canonical profile");
    }
    jsonxx::Value rootValue(root);
    std::set<std::string> ids;
    if (!ValidateIds(rootValue, "", ids)) return false;
    const JObject *scoreSource = ObjectAt(root, "score", "");
    if (!scoreSource) return false;
    const JArray *parts = ArrayAt(*scoreSource, "parts", "/score");
    const JArray *bars = ArrayAt(*scoreSource, "logicalBars", "/score");
    if (!parts || !bars || parts->empty() || bars->empty()) {
        return Fail("JSM_EMPTY_SCORE", "/score", "parts and logicalBars must not be empty");
    }
    if (parts->size() > 256 || bars->size() > 100000) {
        return Fail("JSM_RESOURCE_LIMIT", "/score", "score dimensions exceed importer limits");
    }

    m_doc->Reset();
    m_doc->SetType(Raw);
    AddDocumentHeader(m_doc, *scoreSource);
    int ppq = 4;
    jsonxx::Value scoreValue(*scoreSource);
    CollectPpq(scoreValue, ppq);
    Mdiv *mdiv = new Mdiv();
    mdiv->SetVisibility(Visible);
    if (scoreSource->has<jsonxx::String>("id")) mdiv->SetID(scoreSource->get<jsonxx::String>("id"));
    m_doc->AddChild(mdiv);
    Score *score = new Score();
    mdiv->AddChild(score);
    AddPageCredits(score, *scoreSource);
    Section *section = new Section();
    score->AddChild(section);
    StaffGrp *staffGrp = new StaffGrp();
    score->GetScoreDef()->AddChild(staffGrp);

    std::map<std::string, StaffBinding> staffBindings;
    bool meterAdded = false;
    int nextStaff = 1;
    for (unsigned int partIndex = 0; partIndex < parts->size(); ++partIndex) {
        const std::string partPath = "/score/parts/" + std::to_string(partIndex);
        if (!parts->has<JObject>(partIndex)) return Fail("JSM_INVALID_PART", partPath, "expected object");
        const JObject &part = parts->get<JObject>(partIndex);
        std::string partId;
        if (!StringAt(part, "id", partPath, partId)) return false;
        const JArray *staves = ArrayAt(part, "staves", partPath);
        if (!staves || staves->empty()) {
            return Fail("JSM_EMPTY_STAVES", partPath + "/staves", "part must declare a staff");
        }
        StaffGrp *partStaffGrp = (staves->size() > 1) ? new StaffGrp() : nullptr;
        if (partStaffGrp) {
            partStaffGrp->SetID(partId);
            partStaffGrp->SetBarThru(BOOLEAN_true);
            AddPartLabels(partStaffGrp, part);
            GrpSym *symbol = new GrpSym();
            symbol->SetSymbol(staffGroupingSym_SYMBOL_brace);
            partStaffGrp->AddChild(symbol);
        }
        for (unsigned int staffIndex = 0; staffIndex < staves->size(); ++staffIndex) {
            const std::string staffPath = partPath + "/staves/" + std::to_string(staffIndex);
            if (!staves->has<JObject>(staffIndex)) return Fail("JSM_INVALID_STAFF", staffPath, "expected object");
            const JObject &staffSource = staves->get<JObject>(staffIndex);
            std::string staffId;
            if (!StringAt(staffSource, "id", staffPath, staffId)) return false;
            if (staffBindings.contains(staffId)) return Fail("JSM_DUPLICATE_ID", staffPath + "/id", staffId);
            StaffDef *staffDef = new StaffDef();
            staffDef->SetN(nextStaff);
            staffDef->SetLines(5);
            staffDef->SetID(staffId);
            staffDef->m_unsupported.push_back({ "jsm-part-id", partId });
            if (!partStaffGrp) AddPartLabels(staffDef, part);
            AddTranspositionAndPpq(staffDef, part, ppq);
            (partStaffGrp ? static_cast<Object *>(partStaffGrp) : static_cast<Object *>(staffGrp))->AddChild(staffDef);
            if (!AddInitialContext(staffDef, part, staffId, meterAdded, score->GetScoreDef())) return false;
            staffBindings.emplace(
                staffId, StaffBinding{ nextStaff, partId, staffId, InitialFifths(part), InitialContextId(part) });
            ++nextStaff;
        }
        if (partStaffGrp) staffGrp->AddChild(partStaffGrp);
    }

    std::map<std::string, const JObject *> conductorEvents;
    const JObject *conductorTrack = ObjectAt(*scoreSource, "conductorTrack", "/score", false);
    const JArray *conductorMeasures
        = conductorTrack ? ArrayAt(*conductorTrack, "measures", "/score/conductorTrack", false) : nullptr;
    if (conductorMeasures) {
        for (unsigned int measureIndex = 0; measureIndex < conductorMeasures->size(); ++measureIndex) {
            if (!conductorMeasures->has<JObject>(measureIndex)) continue;
            const JArray *events = ArrayAt(conductorMeasures->get<JObject>(measureIndex), "events",
                "/score/conductorTrack/measures/" + std::to_string(measureIndex), false);
            if (!events) continue;
            for (unsigned int eventIndex = 0; eventIndex < events->size(); ++eventIndex) {
                if (!events->has<JObject>(eventIndex)) continue;
                const JObject &event = events->get<JObject>(eventIndex);
                if (event.has<jsonxx::String>("id")) conductorEvents[event.get<jsonxx::String>("id")] = &event;
            }
        }
    }
    std::set<std::string> renderedConductorEvents;

    std::map<std::string, std::string> eventTargets;
    std::map<std::string, std::string> toneTargets;
    std::map<std::string, std::pair<unsigned int, double>> eventTimes;
    std::map<std::string, double> eventEndTimes;
    std::vector<PendingSpanner> pendingSpanners;
    bool hasEncodedBreaks = false;
    for (unsigned int barIndex = 0; barIndex < bars->size(); ++barIndex) {
        hasEncodedBreaks = hasEncodedBreaks || HasPresentationBreak(*parts, barIndex, "newPage")
            || HasPresentationBreak(*parts, barIndex, "newSystem");
    }
    if (hasEncodedBreaks) {
        m_layoutInformation = LAYOUT_ENCODED;
        if (!HasPresentationBreak(*parts, 0, "newPage") && !HasPresentationBreak(*parts, 0, "newSystem")) {
            section->AddChild(new Pb());
        }
    }
    std::string activeMeter;
    bool activeMeterSet = false;
    for (unsigned int partIndex = 0; partIndex < parts->size(); ++partIndex) {
        const JObject *context = InitialContext(parts->get<JObject>(partIndex));
        const JObject *time = context ? ObjectAt(*context, "time", "/score/parts/contexts", false) : nullptr;
        const std::string timeJson = time ? JsonValue(jsonxx::Value(*time)) : std::string();
        if (!activeMeterSet) {
            activeMeter = timeJson;
            activeMeterSet = true;
        }
        else if (activeMeter != timeJson) {
            return Fail(
                "JSM_CONFLICTING_METERS", "/score/parts/contexts/time", "all parts must use the same initial meter");
        }
    }
    for (unsigned int barIndex = 0; barIndex < bars->size(); ++barIndex) {
        const std::string barPath = "/score/logicalBars/" + std::to_string(barIndex);
        if (!bars->has<JObject>(barIndex)) return Fail("JSM_INVALID_BAR", barPath, "expected object");
        const JObject &bar = bars->get<JObject>(barIndex);
        std::string barId;
        if (!StringAt(bar, "id", barPath, barId)) return false;
        const bool newPage = HasPresentationBreak(*parts, barIndex, "newPage");
        const bool newSystem = HasPresentationBreak(*parts, barIndex, "newSystem");
        if (newPage) section->AddChild(new Pb());
        if (newSystem) section->AddChild(new Sb());
        if (newPage || newSystem) m_layoutInformation = LAYOUT_ENCODED;

        struct ChangedStaff {
            StaffBinding *binding;
            const JObject *oldContext;
            const JObject *newContext;
            std::string staffId;
        };
        std::vector<ChangedStaff> changedStaves;
        const JObject *effectiveMeter = nullptr;
        std::string effectiveMeterJson;
        bool effectiveMeterSet = false;
        for (unsigned int partIndex = 0; partIndex < parts->size(); ++partIndex) {
            const JObject &part = parts->get<JObject>(partIndex);
            const JArray *partMeasures = ArrayAt(part, "measures", "/score/parts");
            if (!partMeasures || !partMeasures->has<JObject>(barIndex)) continue;
            const JObject &partMeasure = partMeasures->get<JObject>(barIndex);
            if (!partMeasure.has<jsonxx::String>("contextRef")) continue;
            const std::string contextRef = partMeasure.get<jsonxx::String>("contextRef");
            const JObject *context = ContextById(part, contextRef);
            if (!context) return Fail("JSM_UNKNOWN_CONTEXT", "/score/parts/measures/contextRef", contextRef);

            const JObject *time = ObjectAt(*context, "time", "/score/parts/contexts", false);
            const std::string timeJson = time ? JsonValue(jsonxx::Value(*time)) : std::string();
            if (!effectiveMeterSet) {
                effectiveMeter = time;
                effectiveMeterJson = timeJson;
                effectiveMeterSet = true;
            }
            else if (effectiveMeterJson != timeJson) {
                return Fail("JSM_CONFLICTING_METERS", "/score/parts/measures/contextRef",
                    "all parts must use the same meter at a measure boundary");
            }

            const JArray *measureStaves = ArrayAt(partMeasure, "staves", "/score/parts/measures", false);
            if (measureStaves) {
                for (unsigned int i = 0; i < measureStaves->size(); ++i) {
                    if (!measureStaves->has<JObject>(i)) continue;
                    const JObject &measureStaff = measureStaves->get<JObject>(i);
                    if (!measureStaff.has<jsonxx::String>("staffId")) continue;
                    const std::string staffId = measureStaff.get<jsonxx::String>("staffId");
                    auto binding = staffBindings.find(staffId);
                    if (binding == staffBindings.end()) {
                        return Fail("JSM_UNKNOWN_STAFF", "/score/parts/measures/staves/staffId", staffId);
                    }
                    if (binding->second.partId != part.get<jsonxx::String>("id")) {
                        return Fail("JSM_STAFF_OWNERSHIP", "/score/parts/measures/staves/staffId",
                            "staff belongs to another part");
                    }
                    if (binding->second.contextId != contextRef) {
                        changedStaves.push_back(
                            { &binding->second, ContextById(part, binding->second.contextId), context, staffId });
                    }
                }
            }
        }

        const bool meterChanged = !activeMeterSet || activeMeter != effectiveMeterJson;
        if (!changedStaves.empty() || meterChanged) {
            ScoreDef *contextChange = new ScoreDef();
            contextChange->SetPpq(ppq);
            if (meterChanged && effectiveMeter
                && !AddMeter(contextChange, *effectiveMeter, "/score/parts/contexts/time")) {
                delete contextChange;
                return false;
            }

            StaffGrp *changedStaffGrp = nullptr;
            bool hasGlobalKeyChange = false;
            for (const ChangedStaff &change : changedStaves) {
                const JObject *oldKey
                    = change.oldContext ? ObjectAt(*change.oldContext, "key", "/score/parts/contexts", false) : nullptr;
                const JObject *newKey = ObjectAt(*change.newContext, "key", "/score/parts/contexts", false);
                const auto clefForStaff = [&change](const JObject *context) -> const JObject * {
                    if (!context) return nullptr;
                    const JArray *staves = ArrayAt(*context, "staves", "/score/parts/contexts", false);
                    if (!staves) return nullptr;
                    for (unsigned int i = 0; i < staves->size(); ++i) {
                        if (!staves->has<JObject>(i)) continue;
                        const JObject &staff = staves->get<JObject>(i);
                        if (staff.get<jsonxx::String>("staffId", "") == change.staffId) {
                            return ObjectAt(staff, "clef", "/score/parts/contexts/staves", false);
                        }
                    }
                    return nullptr;
                };
                const JObject *oldClef = clefForStaff(change.oldContext);
                const JObject *newClef = clefForStaff(change.newContext);
                const bool keyChanged = (oldKey ? JsonValue(jsonxx::Value(*oldKey)) : std::string())
                    != (newKey ? JsonValue(jsonxx::Value(*newKey)) : std::string());
                const bool clefChanged = (oldClef ? JsonValue(jsonxx::Value(*oldClef)) : std::string())
                    != (newClef ? JsonValue(jsonxx::Value(*newClef)) : std::string());
                const bool globalKeyChange = keyChanged && (staffBindings.size() == 1);
                if (globalKeyChange && newKey && !AddKeySig(contextChange, *newKey, "/score/parts/contexts/key")) {
                    delete contextChange;
                    return false;
                }
                hasGlobalKeyChange = hasGlobalKeyChange || globalKeyChange;
                if ((keyChanged && !globalKeyChange) || clefChanged) {
                    if (!changedStaffGrp) {
                        changedStaffGrp = new StaffGrp();
                        contextChange->AddChild(changedStaffGrp);
                    }
                    StaffDef *staffDef = new StaffDef();
                    staffDef->SetN(change.binding->number);
                    if (clefChanged && newClef
                        && !AddClef(staffDef, *newClef, "/score/parts/contexts/staves/clef", true)) {
                        delete contextChange;
                        return false;
                    }
                    if (keyChanged && !globalKeyChange && newKey
                        && !AddKeySig(staffDef, *newKey, "/score/parts/contexts/key")) {
                        delete contextChange;
                        return false;
                    }
                    changedStaffGrp->AddChild(staffDef);
                }
            }
            if (meterChanged || changedStaffGrp || hasGlobalKeyChange)
                section->AddChild(contextChange);
            else
                delete contextChange;
        }
        activeMeter = effectiveMeterJson;
        activeMeterSet = true;
        for (const ChangedStaff &change : changedStaves) {
            const JObject *key = ObjectAt(*change.newContext, "key", "/score/parts/contexts", false);
            change.binding->contextId = change.newContext->get<jsonxx::String>("id");
            change.binding->fifths = key ? IntAt(*key, "fifths", 0) : 0;
        }

        Measure *measure = new Measure(MEASURED, static_cast<int>(barIndex + 1));
        std::string primaryMeasureId = barId;
        if (parts->has<JObject>(0)) {
            const JArray *firstMeasures = ArrayAt(parts->get<JObject>(0), "measures", "/score/parts/0", false);
            if (firstMeasures && firstMeasures->has<JObject>(barIndex)) {
                StringAt(
                    firstMeasures->get<JObject>(barIndex), "id", "/score/parts/0/measures", primaryMeasureId, false);
            }
        }
        measure->SetID(primaryMeasureId);
        measure->m_unsupported.push_back({ "jsm-bar-id", barId });
        measure->SetN(bar.get<jsonxx::String>("number", std::to_string(barIndex + 1)));
        section->AddChild(measure);

        bool repeatStart = false;
        bool repeatEnd = false;
        for (unsigned int partIndex = 0; partIndex < parts->size(); ++partIndex) {
            const JObject &part = parts->get<JObject>(partIndex);
            const JArray *partMeasures = ArrayAt(part, "measures", "/score/parts/" + std::to_string(partIndex));
            if (!partMeasures || partMeasures->size() != bars->size() || !partMeasures->has<JObject>(barIndex)) {
                return Fail("JSM_MEASURE_ALIGNMENT", "/score/parts/" + std::to_string(partIndex) + "/measures",
                    "part measures must align with logicalBars");
            }
            const JObject &partMeasure = partMeasures->get<JObject>(barIndex);
            std::string partMeasureId;
            std::string partId;
            if (!StringAt(partMeasure, "id", "/score/parts/measures", partMeasureId)
                || !StringAt(part, "id", "/score/parts", partId))
                return false;
            if (!partMeasure.has<jsonxx::String>("barId") || partMeasure.get<jsonxx::String>("barId") != barId) {
                return Fail("JSM_MEASURE_ALIGNMENT", "/score/parts/measures/barId", "barId does not match logical bar");
            }
            const JObject *navigation = ObjectAt(partMeasure, "navigation", "/score/parts/measures", false);
            if (navigation) {
                repeatStart = repeatStart || BoolAt(*navigation, "repeatStart", false);
                repeatEnd = repeatEnd || navigation->has<jsonxx::Number>("repeatEnd");
            }
            const JArray *measureStaves = ArrayAt(partMeasure, "staves", "/score/parts/measures");
            if (!measureStaves) return false;
            const JArray *partStaves = ArrayAt(part, "staves", "/score/parts");
            if (!partStaves || measureStaves->size() != partStaves->size()) {
                return Fail("JSM_STAFF_COVERAGE", "/score/parts/measures/staves",
                    "each measure must contain every declared part staff exactly once");
            }

            std::vector<PendingContextChange> contextChanges;
            const JArray *measureEvents = ArrayAt(partMeasure, "measureEvents", "/score/parts/measures", false);
            if (measureEvents) {
                for (unsigned int eventIndex = 0; eventIndex < measureEvents->size(); ++eventIndex) {
                    const std::string eventPath = "/score/parts/" + std::to_string(partIndex) + "/measures/"
                        + std::to_string(barIndex) + "/measureEvents/" + std::to_string(eventIndex);
                    if (!measureEvents->has<JObject>(eventIndex)) {
                        return Fail("JSM_INVALID_MEASURE_EVENT", eventPath, "expected object");
                    }
                    const JObject &event = measureEvents->get<JObject>(eventIndex);
                    if (event.get<jsonxx::String>("type", "") != "contextChange") continue;
                    long double onset = 0.0L;
                    long double duration = 0.0L;
                    if (!RationalAt(event, "onset", eventPath, onset)
                        || !RationalAt(event, "duration", eventPath, duration))
                        return false;
                    long double measureDuration = 0.0L;
                    if (!RationalAt(partMeasure, "duration", "/score/parts/measures", measureDuration)) return false;
                    if (onset < 0.0L || onset > measureDuration) {
                        return Fail("JSM_INVALID_CONTEXT_ONSET", eventPath + "/onset", "outside measure duration");
                    }
                    if (std::abs(duration) > 1e-12L) {
                        return Fail("JSM_INVALID_CONTEXT_DURATION", eventPath + "/duration", "expected zero");
                    }
                    std::string contextRef;
                    if (!StringAt(event, "contextRef", eventPath, contextRef)) return false;
                    const JObject *context = ContextById(part, contextRef);
                    if (!context) return Fail("JSM_UNKNOWN_CONTEXT", eventPath + "/contextRef", contextRef);
                    const JArray *contextStaves = ArrayAt(*context, "staves", "/score/parts/contexts", false);
                    if (!ObjectAt(*context, "key", "/score/parts/contexts", false)
                        || !ObjectAt(*context, "time", "/score/parts/contexts", false) || !contextStaves) {
                        return Fail("JSM_INCOMPLETE_CONTEXT", eventPath + "/contextRef",
                            "context changes require complete key, time, and staff snapshots");
                    }
                    std::set<std::string> snapshotStaffIds;
                    for (unsigned int i = 0; i < contextStaves->size(); ++i) {
                        if (!contextStaves->has<JObject>(i)
                            || !contextStaves->get<JObject>(i).has<jsonxx::String>("staffId")) {
                            return Fail("JSM_INCOMPLETE_CONTEXT", eventPath + "/contextRef/staves",
                                "expected staff snapshots with staffId");
                        }
                        snapshotStaffIds.insert(contextStaves->get<JObject>(i).get<jsonxx::String>("staffId"));
                    }
                    if (snapshotStaffIds.size() != partStaves->size()) {
                        return Fail("JSM_INCOMPLETE_CONTEXT", eventPath + "/contextRef/staves",
                            "snapshot must cover every part staff");
                    }
                    for (unsigned int i = 0; i < partStaves->size(); ++i) {
                        if (!partStaves->has<JObject>(i)
                            || !snapshotStaffIds.contains(partStaves->get<JObject>(i).get<jsonxx::String>("id", ""))) {
                            return Fail("JSM_INCOMPLETE_CONTEXT", eventPath + "/contextRef/staves",
                                "snapshot must cover every part staff");
                        }
                    }
                    PendingContextChange pending{ &event, context, eventPath, onset, {} };
                    const JArray *staffIds = ArrayAt(event, "staffIds", eventPath, false);
                    if (staffIds) {
                        if (staffIds->empty()) {
                            return Fail(
                                "JSM_INVALID_CONTEXT_STAVES", eventPath + "/staffIds", "expected non-empty array");
                        }
                        for (unsigned int i = 0; i < staffIds->size(); ++i) {
                            if (!staffIds->has<jsonxx::String>(i)) {
                                return Fail("JSM_INVALID_CONTEXT_STAVES", eventPath + "/staffIds", "expected IDs");
                            }
                            const std::string staffId = staffIds->get<jsonxx::String>(i);
                            const auto binding = staffBindings.find(staffId);
                            if (binding == staffBindings.end() || binding->second.partId != partId) {
                                return Fail("JSM_CONTEXT_STAFF_OWNERSHIP", eventPath + "/staffIds", staffId);
                            }
                            if (!pending.staffIds.insert(staffId).second) {
                                return Fail("JSM_DUPLICATE_CONTEXT_STAFF", eventPath + "/staffIds", staffId);
                            }
                        }
                    }
                    contextChanges.push_back(std::move(pending));
                }
                std::stable_sort(contextChanges.begin(), contextChanges.end(), [](const auto &left, const auto &right) {
                    if (left.onset != right.onset) return left.onset < right.onset;
                    return IntAt(*left.source, "order", 0) < IntAt(*right.source, "order", 0);
                });
            }
            std::set<std::string> seenMeasureStaves;
            for (unsigned int staffIndex = 0; staffIndex < measureStaves->size(); ++staffIndex) {
                if (!measureStaves->has<JObject>(staffIndex)) {
                    return Fail("JSM_INVALID_STAFF", "/score/parts/measures/staves", "expected object");
                }
                const JObject &measureStaff = measureStaves->get<JObject>(staffIndex);
                std::string staffId;
                if (!StringAt(measureStaff, "staffId", "/score/parts/measures/staves", staffId)) return false;
                if (!seenMeasureStaves.insert(staffId).second) {
                    return Fail("JSM_DUPLICATE_STAFF", "/score/parts/measures/staves/staffId", staffId);
                }
                auto binding = staffBindings.find(staffId);
                if (binding == staffBindings.end()) {
                    return Fail("JSM_UNKNOWN_STAFF", "/score/parts/measures/staves/staffId", staffId);
                }
                if (binding->second.partId != partId) {
                    return Fail(
                        "JSM_STAFF_OWNERSHIP", "/score/parts/measures/staves/staffId", "staff belongs to another part");
                }
                Staff *staff = new Staff(binding->second.number);
                staff->SetID(partMeasureId + "-staff-" + std::to_string(binding->second.number));
                staff->m_unsupported.push_back({ "jsm-staff-id", staffId });
                staff->m_unsupported.push_back({ "jsm-part-id", partId });
                staff->m_unsupported.push_back({ "jsm-measure-id", partMeasureId });
                measure->AddChild(staff);
                std::map<std::string, int> accidentalState;
                const std::string baseContextRef = partMeasure.get<jsonxx::String>("contextRef", "");
                const JObject *baseContext = ContextById(part, baseContextRef);
                if (!baseContext) {
                    return Fail("JSM_UNKNOWN_CONTEXT", "/score/parts/measures/contextRef", baseContextRef);
                }
                std::vector<const PendingContextChange *> staffContextChanges;
                for (const PendingContextChange &change : contextChanges) {
                    if (change.staffIds.empty() || change.staffIds.contains(staffId)) {
                        staffContextChanges.push_back(&change);
                    }
                }
                const JArray *voices = ArrayAt(measureStaff, "voices", "/score/parts/measures/staves");
                if (!voices) return false;
                if (voices->size() > 1) {
                    return Fail("JSM_UNSUPPORTED_POLYPHONY", "/score/parts/measures/staves/voices",
                        "multiple voices on one staff require onset-ordered accidental handling");
                }
                for (unsigned int voiceIndex = 0; voiceIndex < voices->size(); ++voiceIndex) {
                    if (!voices->has<JObject>(voiceIndex)) {
                        return Fail("JSM_INVALID_VOICE", "/score/parts/measures/staves/voices", "expected object");
                    }
                    const JObject &voice = voices->get<JObject>(voiceIndex);
                    Layer *layer = new Layer();
                    layer->SetN(static_cast<int>(voiceIndex + 1));
                    if (voice.has<jsonxx::String>("id")) layer->SetID(voice.get<jsonxx::String>("id"));
                    staff->AddChild(layer);
                    const JObject *activeContext = baseContext;
                    const JObject *initialKey = ObjectAt(*activeContext, "key", "/score/parts/contexts", false);
                    int activeFifths = initialKey ? IntAt(*initialKey, "fifths", 0) : 0;
                    size_t nextContextChange = 0;
                    auto applyContextChanges = [&](long double onset) -> bool {
                        while (nextContextChange < staffContextChanges.size()) {
                            const PendingContextChange &change = *staffContextChanges[nextContextChange];
                            if (change.onset > onset + 1e-12L) break;
                            if (change.onset < onset - 1e-12L) {
                                return Fail("JSM_UNALIGNED_CONTEXT_ONSET", change.path + "/onset",
                                    "context onset must align with a layer event boundary");
                            }
                            const JObject *oldKey = ObjectAt(*activeContext, "key", "/score/parts/contexts", false);
                            const JObject *newKey = ObjectAt(*change.context, "key", "/score/parts/contexts", false);
                            const JObject *oldTime = ObjectAt(*activeContext, "time", "/score/parts/contexts", false);
                            const JObject *newTime = ObjectAt(*change.context, "time", "/score/parts/contexts", false);
                            const JObject *oldClef = ContextClefForStaff(*activeContext, staffId);
                            const JObject *newClef = ContextClefForStaff(*change.context, staffId);
                            if (!newClef) {
                                return Fail("JSM_INCOMPLETE_CONTEXT", change.path + "/contextRef",
                                    "context snapshot does not cover the affected staff");
                            }
                            if (!change.staffIds.empty()
                                && (!SameObject(oldKey, newKey) || !SameObject(oldTime, newTime))) {
                                return Fail("JSM_SCOPED_CONTEXT_CONFLICT", change.path + "/staffIds",
                                    "staff-scoped changes may only alter staff notation");
                            }
                            if (!SameObject(oldClef, newClef)
                                && !AddClef(layer, *newClef, change.path + "/contextRef/staves", true))
                                return false;
                            if (change.staffIds.empty() && !SameObject(oldKey, newKey)) {
                                if (!AddKeySig(layer, *newKey, change.path + "/contextRef/key")) return false;
                                activeFifths = IntAt(*newKey, "fifths", 0);
                                accidentalState.clear();
                            }
                            if (change.staffIds.empty() && !SameObject(oldTime, newTime)
                                && !AddMeter(layer, *newTime, change.path + "/contextRef/time"))
                                return false;
                            activeContext = change.context;
                            ++nextContextChange;
                        }
                        return true;
                    };
                    const JArray *events = ArrayAt(voice, "events", "/score/parts/measures/staves/voices");
                    if (!events) return false;
                    long double expectedOnset = 0.0L;
                    Tuplet *activeTuplet = nullptr;
                    std::string tupletEndId;
                    Beam *activeBeam = nullptr;
                    for (unsigned int eventIndex = 0; eventIndex < events->size(); ++eventIndex) {
                        const std::string eventPath = "/score/parts/" + std::to_string(partIndex) + "/measures/"
                            + std::to_string(barIndex) + "/staves/" + std::to_string(staffIndex) + "/voices/"
                            + std::to_string(voiceIndex) + "/events/" + std::to_string(eventIndex);
                        if (!events->has<JObject>(eventIndex)) {
                            return Fail("JSM_INVALID_EVENT", eventPath, "expected object");
                        }
                        const JObject &event = events->get<JObject>(eventIndex);
                        long double onset = 0.0L;
                        long double eventDuration = 0.0L;
                        if (!RationalAt(event, "onset", eventPath, onset)
                            || !RationalAt(event, "duration", eventPath, eventDuration))
                            return false;
                        if (std::abs(onset - expectedOnset) > 1e-12L) {
                            return Fail("JSM_ONSET_GAP", eventPath + "/onset",
                                "events must be contiguous; encode gaps with spacer events");
                        }
                        if (!applyContextChanges(onset)) return false;
                        expectedOnset += eventDuration;
                        std::string type;
                        std::string eventId;
                        if (!StringAt(event, "type", eventPath, type) || !StringAt(event, "id", eventPath, eventId))
                            return false;
                        eventTimes[eventId]
                            = { barIndex, static_cast<double>(onset) * InitialBeatType(part) / 4.0 + 1.0 };
                        eventEndTimes[eventId]
                            = static_cast<double>(onset + eventDuration) * InitialBeatType(part) / 4.0 + 1.0;
                        const JObject *tupletSpanner = TupletStartingAt(partMeasure, eventId);
                        if (tupletSpanner) {
                            if (activeTuplet) {
                                return Fail(
                                    "JSM_UNSUPPORTED_NESTED_TUPLET", eventPath, "nested tuplets are not yet supported");
                            }
                            const JObject *spec = ObjectAt(*tupletSpanner, "tuplet", eventPath);
                            if (!spec) return false;
                            const int actual = IntAt(*spec, "actualNotes", 0);
                            const int normal = IntAt(*spec, "normalNotes", 0);
                            if (actual < 1 || normal < 1) {
                                return Fail("JSM_INVALID_TUPLET", eventPath, "positive ratio required");
                            }
                            activeTuplet = new Tuplet();
                            activeTuplet->SetNum(actual);
                            activeTuplet->SetNumbase(normal);
                            activeTuplet->SetBracketVisible(
                                BoolAt(*spec, "bracket", false) ? BOOLEAN_true : BOOLEAN_false);
                            const std::string showNumber = spec->get<jsonxx::String>("showNumber", "actual");
                            if (showNumber == "none")
                                activeTuplet->SetNumVisible(BOOLEAN_false);
                            else if (showNumber == "both")
                                activeTuplet->SetNumFormat(tupletVis_NUMFORMAT_ratio);
                            else
                                activeTuplet->SetNumFormat(tupletVis_NUMFORMAT_count);
                            activeTuplet->SetID(tupletSpanner->get<jsonxx::String>("id", ""));
                            layer->AddChild(activeTuplet);
                            tupletEndId = SpannerEventId(*tupletSpanner, "end");
                        }
                        Object *element = nullptr;
                        if (type == "note") {
                            const JObject *tone = ObjectAt(event, "tone", eventPath);
                            if (!tone) return false;
                            std::string toneId;
                            if (!StringAt(*tone, "id", eventPath + "/tone", toneId)) return false;
                            Note *note = new Note();
                            note->SetID(eventId);
                            if (IsCue(event)) note->SetCue(BOOLEAN_true);
                            note->m_unsupported.push_back({ "jsm-event-id", eventId });
                            note->m_unsupported.push_back({ "jsm-tone-id", toneId });
                            if (!SetDuration(note, event, eventPath, activeTuplet != nullptr)
                                || !AddTone(note, *tone, accidentalState, activeFifths, eventPath + "/tone")
                                || !SetEventAppearance(note, event, eventPath)) {
                                delete note;
                                return false;
                            }
                            element = note;
                            eventTargets[eventId] = eventId;
                            toneTargets[toneId] = eventId;
                            eventTimes[toneId] = eventTimes[eventId];
                            eventEndTimes[toneId] = eventEndTimes[eventId];
                        }
                        else if (type == "chord") {
                            const JArray *tones = ArrayAt(event, "tones", eventPath);
                            if (!tones || tones->empty()) {
                                return Fail("JSM_EMPTY_CHORD", eventPath + "/tones", "chord requires tones");
                            }
                            Chord *chord = new Chord();
                            chord->SetID(eventId);
                            if (IsCue(event)) chord->SetCue(BOOLEAN_true);
                            chord->m_unsupported.push_back({ "jsm-event-id", eventId });
                            if (!SetDuration(chord, event, eventPath, activeTuplet != nullptr)
                                || !SetEventAppearance(chord, event, eventPath)) {
                                delete chord;
                                return false;
                            }
                            for (unsigned int toneIndex = 0; toneIndex < tones->size(); ++toneIndex) {
                                if (!tones->has<JObject>(toneIndex)) {
                                    delete chord;
                                    return Fail("JSM_INVALID_TONE", eventPath + "/tones/" + std::to_string(toneIndex),
                                        "expected object");
                                }
                                const JObject &tone = tones->get<JObject>(toneIndex);
                                std::string toneId;
                                if (!StringAt(tone, "id", eventPath + "/tones", toneId)) {
                                    delete chord;
                                    return false;
                                }
                                Note *note = new Note();
                                note->SetID(toneId);
                                note->m_unsupported.push_back({ "jsm-tone-id", toneId });
                                if (!AddTone(note, tone, accidentalState, activeFifths,
                                        eventPath + "/tones/" + std::to_string(toneIndex))) {
                                    delete note;
                                    delete chord;
                                    return false;
                                }
                                chord->AddChild(note);
                                toneTargets[toneId] = toneId;
                                eventTimes[toneId] = eventTimes[eventId];
                                eventEndTimes[toneId] = eventEndTimes[eventId];
                            }
                            element = chord;
                            eventTargets[eventId] = eventId;
                        }
                        else if (type == "rest") {
                            DurationInterface *duration = nullptr;
                            if (BoolAt(event, "measureRest", false)) {
                                MRest *rest = new MRest();
                                rest->SetID(eventId);
                                if (IsCue(event)) rest->SetCue(BOOLEAN_true);
                                element = rest;
                            }
                            else {
                                Rest *rest = new Rest();
                                rest->SetID(eventId);
                                if (IsCue(event)) rest->SetCue(BOOLEAN_true);
                                duration = rest;
                                element = rest;
                            }
                            element->m_unsupported.push_back({ "jsm-event-id", eventId });
                            if (duration && !SetDuration(duration, event, eventPath, activeTuplet != nullptr)) {
                                delete element;
                                return false;
                            }
                            eventTargets[eventId] = eventId;
                        }
                        else if (type == "spacer") {
                            Space *space = new Space();
                            space->SetID(eventId);
                            space->m_unsupported.push_back({ "jsm-event-id", eventId });
                            if (!SetDuration(space, event, eventPath, activeTuplet != nullptr)) {
                                delete space;
                                return false;
                            }
                            element = space;
                            eventTargets[eventId] = eventId;
                        }
                        else {
                            return Fail("JSM_UNSUPPORTED_EVENT", eventPath + "/type", type);
                        }
                        if (!AddArticulations(
                                element, event, measure, eventPath, binding->second.number, InitialBeatType(part))) {
                            delete element;
                            return false;
                        }
                        const JArray *beams = ArrayAt(event, "beams", eventPath, false);
                        std::string primaryBeam;
                        if (beams && !beams->empty() && beams->has<JObject>(0)) {
                            primaryBeam = beams->get<JObject>(0).get<jsonxx::String>("value", "");
                        }
                        Object *eventParent
                            = activeTuplet ? static_cast<Object *>(activeTuplet) : static_cast<Object *>(layer);
                        if (primaryBeam == "begin") {
                            if (activeBeam) {
                                delete element;
                                return Fail("JSM_INVALID_BEAM", eventPath + "/beams", "nested beam begin");
                            }
                            activeBeam = new Beam();
                            eventParent->AddChild(activeBeam);
                        }
                        else if ((primaryBeam == "continue" || primaryBeam == "end") && !activeBeam) {
                            delete element;
                            return Fail("JSM_INVALID_BEAM", eventPath + "/beams", "beam continuation without begin");
                        }
                        if (activeBeam) eventParent = activeBeam;
                        eventParent->AddChild(element);
                        if (primaryBeam == "end") activeBeam = nullptr;
                        if (eventId == tupletEndId) {
                            if (activeBeam) {
                                return Fail("JSM_INVALID_TUPLET", eventPath, "tuplet ends inside an open beam");
                            }
                            activeTuplet = nullptr;
                            tupletEndId.clear();
                        }
                    }
                    if (activeBeam) return Fail("JSM_INVALID_BEAM", "/score/parts/measures", "unterminated beam");
                    if (activeTuplet) return Fail("JSM_INVALID_TUPLET", "/score/parts/measures", "unterminated tuplet");
                    if (!applyContextChanges(expectedOnset)) return false;
                    if (nextContextChange != staffContextChanges.size()) {
                        return Fail("JSM_UNALIGNED_CONTEXT_ONSET",
                            staffContextChanges[nextContextChange]->path + "/onset",
                            "context onset must align with a layer event boundary");
                    }
                    long double measureDuration = 0.0L;
                    if (!RationalAt(partMeasure, "duration", "/score/parts/measures", measureDuration)) return false;
                    if (std::abs(expectedOnset - measureDuration) > 1e-12L) {
                        return Fail("JSM_INCOMPLETE_VOICE", "/score/parts/measures/staves/voices/events",
                            "voice duration does not fill the measure");
                    }
                }
            }
            const JArray *spanners = ArrayAt(partMeasure, "spanners", "/score/parts/measures", false);
            if (spanners) {
                std::string defaultStaffId;
                if (!partStaves->has<JObject>(0)
                    || !StringAt(partStaves->get<JObject>(0), "id", "/score/parts/staves/0", defaultStaffId))
                    return false;
                const auto defaultStaff = staffBindings.find(defaultStaffId);
                if (defaultStaff == staffBindings.end()) {
                    return Fail("JSM_UNKNOWN_STAFF", "/score/parts/staves/0/id", defaultStaffId);
                }
                for (unsigned int i = 0; i < spanners->size(); ++i) {
                    if (!spanners->has<JObject>(i)) {
                        return Fail("JSM_INVALID_SPANNER", "/score/parts/measures/spanners", "expected object");
                    }
                    pendingSpanners.push_back({ &spanners->get<JObject>(i), measure,
                        "/score/parts/" + std::to_string(partIndex) + "/measures/" + std::to_string(barIndex)
                            + "/spanners/" + std::to_string(i),
                        partId, defaultStaff->second.number, InitialBeatType(part) });
                }
            }
            if (measureEvents) {
                for (unsigned int eventIndex = 0; eventIndex < measureEvents->size(); ++eventIndex) {
                    const std::string directionPath = "/score/parts/" + std::to_string(partIndex) + "/measures/"
                        + std::to_string(barIndex) + "/measureEvents/" + std::to_string(eventIndex);
                    if (!measureEvents->has<JObject>(eventIndex)) {
                        return Fail("JSM_INVALID_DIRECTION", directionPath, "expected object");
                    }
                    const JObject &direction = measureEvents->get<JObject>(eventIndex);
                    std::string type;
                    std::string conductorId;
                    if (!StringAt(direction, "type", directionPath, type)) return false;
                    if (type == "contextChange") continue;
                    if (type != "directionRef") {
                        return Fail("JSM_UNSUPPORTED_MEASURE_EVENT", directionPath + "/type", type);
                    }
                    if (!StringAt(direction, "conductorEventRef", directionPath, conductorId)) return false;
                    auto conductor = conductorEvents.find(conductorId);
                    if (conductor == conductorEvents.end()) {
                        return Fail("JSM_UNKNOWN_CONDUCTOR_EVENT", directionPath + "/conductorEventRef", conductorId);
                    }
                    std::string defaultStaffId;
                    if (!partStaves->has<JObject>(0)
                        || !StringAt(partStaves->get<JObject>(0), "id", "/score/parts/staves/0", defaultStaffId))
                        return false;
                    if (!AddDirectionRef(direction, *conductor->second, directionPath, defaultStaffId, staffBindings,
                            InitialBeatType(part), renderedConductorEvents, measure))
                        return false;
                }
            }
        }
        if (repeatStart) measure->SetLeft(BARRENDITION_rptstart);
        if (repeatEnd) measure->SetRight(BARRENDITION_rptend);
    }

    for (const PendingSpanner &pending : pendingSpanners) {
        std::string id;
        std::string kind;
        if (!StringAt(*pending.source, "id", pending.path, id)
            || !StringAt(*pending.source, "kind", pending.path, kind))
            return false;
        // Tuplets are layer containers and were materialized while their
        // member events were added above, rather than as control elements.
        if (kind == "tuplet") continue;
        const JObject *start = ObjectAt(*pending.source, "start", pending.path);
        const JObject *end = ObjectAt(*pending.source, "end", pending.path);
        if (!start || !end) return false;
        struct ResolvedEndpoint {
            bool isEvent = false;
            std::string id;
            unsigned int barIndex = 0;
            double timestamp = 0.0;
            double endTimestamp = 0.0;
        };
        auto endpoint = [&](const JObject &source, const std::string &path, ResolvedEndpoint &result) -> bool {
            const std::string endpointKind = source.get<jsonxx::String>("kind", "");
            if (endpointKind == "event") {
                if (source.has<jsonxx::String>("toneId")) {
                    auto iter = toneTargets.find(source.get<jsonxx::String>("toneId"));
                    if (iter != toneTargets.end()) {
                        result.isEvent = true;
                        result.id = iter->second;
                        const auto timing = eventTimes.find(source.get<jsonxx::String>("toneId"));
                        if (timing != eventTimes.end()) {
                            result.barIndex = timing->second.first;
                            result.timestamp = timing->second.second;
                        }
                        const auto ending = eventEndTimes.find(source.get<jsonxx::String>("toneId"));
                        if (ending != eventEndTimes.end()) result.endTimestamp = ending->second;
                        return true;
                    }
                }
                if (source.has<jsonxx::String>("eventId")) {
                    auto iter = eventTargets.find(source.get<jsonxx::String>("eventId"));
                    if (iter != eventTargets.end()) {
                        result.isEvent = true;
                        result.id = iter->second;
                        const auto timing = eventTimes.find(source.get<jsonxx::String>("eventId"));
                        if (timing != eventTimes.end()) {
                            result.barIndex = timing->second.first;
                            result.timestamp = timing->second.second;
                        }
                        const auto ending = eventEndTimes.find(source.get<jsonxx::String>("eventId"));
                        if (ending != eventEndTimes.end()) result.endTimestamp = ending->second;
                        return true;
                    }
                }
                return Fail("JSM_UNKNOWN_ENDPOINT", path, "event endpoint does not resolve");
            }
            if (endpointKind != "position") {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", path + "/kind", endpointKind);
            }
            if (source.get<jsonxx::String>("partId", "") != pending.partId) {
                return Fail("JSM_SPANNER_OWNERSHIP", path + "/partId", "position belongs to another part");
            }
            const std::string endpointBarId = source.get<jsonxx::String>("barId", "");
            bool foundBar = false;
            for (unsigned int i = 0; i < bars->size(); ++i) {
                if (bars->has<JObject>(i) && bars->get<JObject>(i).get<jsonxx::String>("id", "") == endpointBarId) {
                    result.barIndex = i;
                    foundBar = true;
                    break;
                }
            }
            if (!foundBar) return Fail("JSM_UNKNOWN_ENDPOINT", path + "/barId", endpointBarId);
            long double onset = 0.0L;
            if (!RationalAt(source, "onset", path, onset)) return false;
            if (onset < 0.0L) return Fail("JSM_INVALID_ENDPOINT", path + "/onset", "expected non-negative onset");
            result.timestamp = static_cast<double>(onset) * pending.beatType / 4.0 + 1.0;
            return true;
        };
        ResolvedEndpoint startEndpoint;
        ResolvedEndpoint endEndpoint;
        if (!endpoint(*start, pending.path + "/start", startEndpoint)
            || !endpoint(*end, pending.path + "/end", endEndpoint))
            return false;

        if (kind == "pedal") {
            if (startEndpoint.isEvent || endEndpoint.isEvent) {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "pedal requires position endpoints");
            }
            for (const auto &[resolved, direction] :
                { std::pair{ &startEndpoint, pedalLog_DIR_down }, std::pair{ &endEndpoint, pedalLog_DIR_up } }) {
                Pedal *pedal = new Pedal();
                pedal->SetID(id + (direction == pedalLog_DIR_down ? "-start" : "-stop"));
                pedal->SetDir(direction);
                pedal->SetStaff({ pending.staffNumber });
                pedal->SetVgrp(2000);
                pedal->SetTstamp(resolved->timestamp - (direction == pedalLog_DIR_up ? 0.1 : 0.0));
                pending.measure->AddChild(pedal);
            }
            continue;
        }

        ControlElement *spanner = nullptr;
        if (kind == "tie") {
            if (!startEndpoint.isEvent || !endEndpoint.isEvent) {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "tie requires event endpoints");
            }
            Tie *tie = new Tie();
            const std::string placement = pending.source->get<jsonxx::String>("placement", "auto");
            if (placement == "above")
                tie->SetCurvedir(curvature_CURVEDIR_above);
            else if (placement == "below")
                tie->SetCurvedir(curvature_CURVEDIR_below);
            spanner = tie;
        }
        else if (kind == "slur") {
            if (!startEndpoint.isEvent || !endEndpoint.isEvent) {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "slur requires event endpoints");
            }
            Slur *slur = new Slur();
            const std::string placement = pending.source->get<jsonxx::String>("placement", "auto");
            if (placement == "above")
                slur->SetCurvedir(curvature_CURVEDIR_above);
            else if (placement == "below")
                slur->SetCurvedir(curvature_CURVEDIR_below);
            spanner = slur;
        }
        else if (kind == "hairpin-crescendo" || kind == "hairpin-diminuendo") {
            Hairpin *hairpin = new Hairpin();
            hairpin->SetForm(kind == "hairpin-crescendo" ? hairpinLog_FORM_cres : hairpinLog_FORM_dim);
            hairpin->SetStaff({ pending.staffNumber });
            const std::string placement = pending.source->get<jsonxx::String>("placement", "auto");
            if (placement == "above")
                hairpin->SetPlace(STAFFREL_above);
            else if (placement == "below")
                hairpin->SetPlace(STAFFREL_below);
            spanner = hairpin;
        }
        else if (kind == "octave-shift") {
            Octave *octave = new Octave();
            const JObject *properties = ObjectAt(*pending.source, "properties", pending.path, false);
            const std::string direction = properties ? properties->get<jsonxx::String>("direction", "up") : "up";
            const int size = properties ? IntAt(*properties, "size", 8) : 8;
            if (size == 8)
                octave->SetDis(OCTAVE_DIS_8);
            else if (size == 15)
                octave->SetDis(OCTAVE_DIS_15);
            else if (size == 22)
                octave->SetDis(OCTAVE_DIS_22);
            else {
                delete octave;
                return Fail("JSM_UNSUPPORTED_SPANNER", pending.path + "/properties/size", std::to_string(size));
            }
            if (direction == "up")
                octave->SetDisPlace(STAFFREL_basic_below);
            else if (direction == "down")
                octave->SetDisPlace(STAFFREL_basic_above);
            else {
                delete octave;
                return Fail("JSM_UNSUPPORTED_SPANNER", pending.path + "/properties/direction", direction);
            }
            octave->SetN(std::to_string(IntAt(*pending.source, "number", 1)));
            octave->SetStaff({ pending.staffNumber });
            spanner = octave;
        }
        else if (kind == "glissando") {
            if (!startEndpoint.isEvent || !endEndpoint.isEvent) {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "glissando requires event endpoints");
            }
            Gliss *gliss = new Gliss();
            gliss->SetN(std::to_string(IntAt(*pending.source, "number", 1)));
            gliss->SetType("glissando");
            gliss->SetStaff({ pending.staffNumber });
            spanner = gliss;
        }
        else if (kind == "trill-extension") {
            if (!startEndpoint.isEvent || !endEndpoint.isEvent) {
                return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "trill extension requires event endpoints");
            }
            Trill *trill = new Trill();
            trill->SetExtender(BOOLEAN_true);
            trill->SetLstartsym(LINESTARTENDSYMBOL_none);
            trill->SetN(std::to_string(IntAt(*pending.source, "number", 1)));
            trill->SetStaff({ pending.staffNumber });
            spanner = trill;
        }
        else
            return Fail("JSM_UNSUPPORTED_SPANNER", pending.path + "/kind", kind);
        spanner->SetID(id);
        spanner->m_unsupported.push_back({ "jsm-spanner-id", id });
        TimeSpanningInterface *timeSpan = spanner->GetTimeSpanningInterface();
        if (!timeSpan) {
            delete spanner;
            return Fail("JSM_INTERNAL", pending.path, "spanner lacks time interface");
        }
        if (startEndpoint.isEvent && endEndpoint.isEvent) {
            timeSpan->SetStartid("#" + startEndpoint.id);
            if (kind == "trill-extension") {
                if (endEndpoint.barIndex < startEndpoint.barIndex) {
                    delete spanner;
                    return Fail("JSM_INVALID_ENDPOINT", pending.path, "end precedes start");
                }
                timeSpan->SetTstamp2(
                    { static_cast<int>(endEndpoint.barIndex - startEndpoint.barIndex), endEndpoint.endTimestamp });
            }
            else {
                timeSpan->SetEndid("#" + endEndpoint.id);
            }
        }
        else if (!startEndpoint.isEvent && !endEndpoint.isEvent) {
            if (endEndpoint.barIndex < startEndpoint.barIndex) {
                delete spanner;
                return Fail("JSM_INVALID_ENDPOINT", pending.path, "end precedes start");
            }
            timeSpan->SetTstamp(startEndpoint.timestamp);
            timeSpan->SetTstamp2(
                { static_cast<int>(endEndpoint.barIndex - startEndpoint.barIndex), endEndpoint.timestamp });
        }
        else {
            delete spanner;
            return Fail("JSM_UNSUPPORTED_ENDPOINT", pending.path, "mixed event and position endpoints");
        }
        pending.measure->AddChild(spanner);
    }

    m_doc->ConvertToPageBasedDoc();
    return true;
}

} // namespace vrv
