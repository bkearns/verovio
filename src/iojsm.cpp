/////////////////////////////////////////////////////////////////////////////
// Name:        iojsm.cpp
// Purpose:     Native JSM input adapter
/////////////////////////////////////////////////////////////////////////////

#include "iojsm.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "accid.h"
#include "artic.h"
#include "chord.h"
#include "clef.h"
#include "doc.h"
#include "fermata.h"
#include "hairpin.h"
#include "jsonxx.h"
#include "keysig.h"
#include "layer.h"
#include "mdiv.h"
#include "measure.h"
#include "metersig.h"
#include "mrest.h"
#include "note.h"
#include "rest.h"
#include "score.h"
#include "section.h"
#include "slur.h"
#include "space.h"
#include "staff.h"
#include "staffdef.h"
#include "staffgrp.h"
#include "tie.h"
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

void Warn(const std::string &code, const std::string &path, const std::string &message)
{
    LogWarning("%s path=%s message=%s", code.c_str(), path.c_str(), message.c_str());
}

const JObject *ObjectAt(const JObject &parent, const std::string &key, const std::string &path, bool required = true)
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
        return Fail("JSM_INVALID_RATIONAL", path + "/" + key, "expected integral numerator and positive denominator");
    }
    value = numerator / denominator;
    return true;
}

data_DURATION DurationFromType(const std::string &type)
{
    static const std::map<std::string, data_DURATION> durations = { { "maxima", DURATION_maxima },
        { "long", DURATION_long }, { "breve", DURATION_breve }, { "whole", DURATION_1 },
        { "half", DURATION_2 }, { "quarter", DURATION_4 }, { "eighth", DURATION_8 },
        { "16th", DURATION_16 }, { "32nd", DURATION_32 }, { "64th", DURATION_64 },
        { "128th", DURATION_128 }, { "256th", DURATION_256 }, { "512th", DURATION_512 },
        { "1024th", DURATION_1024 }, { "2048th", DURATION_2048 } };
    auto iter = durations.find(type);
    return (iter == durations.end()) ? DURATION_NONE : iter->second;
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
    if (name == "marcato") return ARTICULATION_marc;
    if (name == "staccatissimo") return ARTICULATION_stacciss;
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

bool AddArticulations(Object *event, const JObject &source, Measure *measure, const std::string &path)
{
    const JArray *values = ArrayAt(source, "articulations", path, false);
    if (!values) return true;
    std::vector<data_ARTICULATION> result;
    for (unsigned int i = 0; i < values->size(); ++i) {
        if (!values->has<jsonxx::String>(i)) {
            return Fail("JSM_INVALID_ARTICULATION", path + "/articulations/" + std::to_string(i), "expected string");
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
        if (event->Is(REST) || event->Is(MREST)) {
            return Fail("JSM_UNSUPPORTED_REST_ARTICULATION", path + "/articulations/" + std::to_string(i), name);
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

bool SetDuration(DurationInterface *duration, const JObject &event, const std::string &path)
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
        static const std::vector<std::pair<long double, data_DURATION>> bases = { { 8.0L, DURATION_breve },
            { 4.0L, DURATION_1 }, { 2.0L, DURATION_2 }, { 1.0L, DURATION_4 }, { 0.5L, DURATION_8 },
            { 0.25L, DURATION_16 }, { 0.125L, DURATION_32 }, { 0.0625L, DURATION_64 } };
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
    static const std::map<data_DURATION, long double> quarterValues = { { DURATION_breve, 8.0L },
        { DURATION_1, 4.0L }, { DURATION_2, 2.0L }, { DURATION_4, 1.0L }, { DURATION_8, 0.5L },
        { DURATION_16, 0.25L }, { DURATION_32, 0.125L }, { DURATION_64, 0.0625L },
        { DURATION_128, 0.03125L }, { DURATION_256, 0.015625L }, { DURATION_512, 0.0078125L },
        { DURATION_1024, 0.00390625L }, { DURATION_2048, 0.001953125L } };
    auto quarterValue = quarterValues.find(value);
    if (quarterValue != quarterValues.end()) {
        long double dotFactor = 1.0L;
        for (int i = 0; i < dots; ++i) dotFactor += 1.0L / std::pow(2.0L, i + 1);
        if (std::abs(encodedDuration - quarterValue->second * dotFactor) > 1e-12L) {
            return Fail("JSM_DURATION_MISMATCH", path + "/duration",
                "rational duration does not match noteType and dots; tuplets are not yet supported");
        }
    }
    return true;
}

bool AddTone(Note *note, const JObject &tone, std::map<std::string, int> &accidentalState, int fifths,
    const std::string &path)
{
    const JObject *pitch = ObjectAt(tone, "pitch", path);
    if (!pitch || !AddPitch(note, *pitch, tone, accidentalState, fifths, path + "/pitch")) return false;
    if (tone.has<jsonxx::String>("notehead")) {
        static const std::map<std::string, data_HEADSHAPE_list> noteheads = { { "circle", HEADSHAPE_list_circle },
            { "cross", HEADSHAPE_list_plus }, { "diamond", HEADSHAPE_list_diamond },
            { "triangle", HEADSHAPE_list_rtriangle }, { "slash", HEADSHAPE_list_slash },
            { "square", HEADSHAPE_list_square }, { "x", HEADSHAPE_list_x } };
        const std::string value = tone.get<jsonxx::String>("notehead");
        auto iter = noteheads.find(value);
        if (iter == noteheads.end()) return Fail("JSM_UNSUPPORTED_NOTEHEAD", path + "/notehead", value);
        data_HEADSHAPE shape;
        shape.SetHeadShapeList(iter->second);
        note->SetHeadShape(shape);
    }
    return true;
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
};

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

void AddInitialContext(StaffDef *staffDef, const JObject &part, const std::string &staffId, bool &meterAdded,
    ScoreDef *scoreDef)
{
    const JObject *initial = InitialContext(part);
    if (!initial) return;
    const JObject &context = *initial;
    const JArray *contextStaves = ArrayAt(context, "staves", "/score/parts/contexts", false);
    if (contextStaves) {
        for (unsigned int i = 0; i < contextStaves->size(); ++i) {
            if (!contextStaves->has<JObject>(i)) continue;
            const JObject &contextStaff = contextStaves->get<JObject>(i);
            if (!contextStaff.has<jsonxx::String>("staffId") || contextStaff.get<jsonxx::String>("staffId") != staffId)
                continue;
            const JObject *clefSource = ObjectAt(contextStaff, "clef", "/score/parts/contexts/staves", false);
            if (!clefSource) break;
            const std::string sign = clefSource->get<jsonxx::String>("sign", "G");
            Clef *clef = new Clef();
            clef->SetShape(sign == "F" ? CLEFSHAPE_F : (sign == "C" ? CLEFSHAPE_C : CLEFSHAPE_G));
            clef->SetLine(IntAt(*clefSource, "line", sign == "F" ? 4 : 2));
            clef->IsAttribute(true);
            staffDef->AddChild(clef);
            break;
        }
    }
    const JObject *key = ObjectAt(context, "key", "/score/parts/contexts", false);
    if (key) {
        const int fifths = IntAt(*key, "fifths", 0);
        KeySig *keySig = new KeySig();
        keySig->SetSig({ std::abs(fifths), fifths < 0 ? ACCIDENTAL_WRITTEN_f : ACCIDENTAL_WRITTEN_s });
        keySig->IsAttribute(true);
        staffDef->AddChild(keySig);
    }
    if (!meterAdded) {
        const JObject *time = ObjectAt(context, "time", "/score/parts/contexts", false);
        const JArray *beats = time ? ArrayAt(*time, "beats", "/score/parts/contexts/time", false) : nullptr;
        if (time && beats && !beats->empty() && beats->has<jsonxx::Number>(0)) {
            std::vector<int> count;
            for (unsigned int i = 0; i < beats->size(); ++i) {
                if (beats->has<jsonxx::Number>(i)) count.push_back(static_cast<int>(beats->get<jsonxx::Number>(i)));
            }
            MeterSig *meter = new MeterSig();
            meter->SetCount({ count, MeterCountSign::None });
            meter->SetUnit(IntAt(*time, "beatType", 4));
            meter->IsAttribute(true);
            scoreDef->AddChild(meter);
            meterAdded = true;
        }
    }
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
            return Fail("JSM_PROFILE", "/2", "compact profile decoding is not yet implemented in the native adapter");
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
    Mdiv *mdiv = new Mdiv();
    mdiv->SetVisibility(Visible);
    if (scoreSource->has<jsonxx::String>("id")) mdiv->SetID(scoreSource->get<jsonxx::String>("id"));
    m_doc->AddChild(mdiv);
    Score *score = new Score();
    mdiv->AddChild(score);
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
            staffGrp->AddChild(staffDef);
            AddInitialContext(staffDef, part, staffId, meterAdded, score->GetScoreDef());
            staffBindings.emplace(
                staffId, StaffBinding { nextStaff, partId, staffId, InitialFifths(part), InitialContextId(part) });
            ++nextStaff;
        }
    }

    std::map<std::string, std::string> eventTargets;
    std::map<std::string, std::string> toneTargets;
    std::vector<PendingSpanner> pendingSpanners;
    for (unsigned int barIndex = 0; barIndex < bars->size(); ++barIndex) {
        const std::string barPath = "/score/logicalBars/" + std::to_string(barIndex);
        if (!bars->has<JObject>(barIndex)) return Fail("JSM_INVALID_BAR", barPath, "expected object");
        const JObject &bar = bars->get<JObject>(barIndex);
        std::string barId;
        if (!StringAt(bar, "id", barPath, barId)) return false;
        Measure *measure = new Measure(MEASURED, static_cast<int>(barIndex + 1));
        measure->SetID(barId);
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
            if (partMeasure.has<jsonxx::String>("contextRef")) {
                const std::string contextRef = partMeasure.get<jsonxx::String>("contextRef");
                const JArray *declaredStaves = ArrayAt(partMeasure, "staves", "/score/parts/measures", false);
                if (declaredStaves) {
                    for (unsigned int i = 0; i < declaredStaves->size(); ++i) {
                        if (!declaredStaves->has<JObject>(i)) continue;
                        const JObject &declaredStaff = declaredStaves->get<JObject>(i);
                        if (!declaredStaff.has<jsonxx::String>("staffId")) continue;
                        auto declaredBinding = staffBindings.find(declaredStaff.get<jsonxx::String>("staffId"));
                        if (declaredBinding != staffBindings.end() && !declaredBinding->second.contextId.empty()
                            && contextRef != declaredBinding->second.contextId) {
                            return Fail("JSM_UNSUPPORTED_CONTEXT_CHANGE", "/score/parts/measures/contextRef",
                                "measure-boundary context changes are not yet supported");
                        }
                    }
                }
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
                    return Fail("JSM_STAFF_OWNERSHIP", "/score/parts/measures/staves/staffId",
                        "staff belongs to another part");
                }
                Staff *staff = new Staff(binding->second.number);
                staff->SetID(partMeasureId + "-staff-" + std::to_string(binding->second.number));
                staff->m_unsupported.push_back({ "jsm-staff-id", staffId });
                staff->m_unsupported.push_back({ "jsm-part-id", partId });
                staff->m_unsupported.push_back({ "jsm-measure-id", partMeasureId });
                measure->AddChild(staff);
                std::map<std::string, int> accidentalState;
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
                    const JArray *events = ArrayAt(voice, "events", "/score/parts/measures/staves/voices");
                    if (!events) return false;
                    long double expectedOnset = 0.0L;
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
                        expectedOnset += eventDuration;
                        std::string type;
                        std::string eventId;
                        if (!StringAt(event, "type", eventPath, type)
                            || !StringAt(event, "id", eventPath, eventId))
                            return false;
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
                            if (!SetDuration(note, event, eventPath)
                                || !AddTone(
                                    note, *tone, accidentalState, binding->second.fifths, eventPath + "/tone")) {
                                delete note;
                                return false;
                            }
                            element = note;
                            eventTargets[eventId] = eventId;
                            toneTargets[toneId] = eventId;
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
                            if (!SetDuration(chord, event, eventPath)) {
                                delete chord;
                                return false;
                            }
                            for (unsigned int toneIndex = 0; toneIndex < tones->size(); ++toneIndex) {
                                if (!tones->has<JObject>(toneIndex)) {
                                    delete chord;
                                    return Fail("JSM_INVALID_TONE",
                                        eventPath + "/tones/" + std::to_string(toneIndex), "expected object");
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
                                if (!AddTone(note, tone, accidentalState, binding->second.fifths,
                                        eventPath + "/tones/" + std::to_string(toneIndex))) {
                                    delete note;
                                    delete chord;
                                    return false;
                                }
                                chord->AddChild(note);
                                toneTargets[toneId] = toneId;
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
                            if (duration && !SetDuration(duration, event, eventPath)) {
                                delete element;
                                return false;
                            }
                            eventTargets[eventId] = eventId;
                        }
                        else if (type == "spacer") {
                            Space *space = new Space();
                            space->SetID(eventId);
                            space->m_unsupported.push_back({ "jsm-event-id", eventId });
                            if (!SetDuration(space, event, eventPath)) {
                                delete space;
                                return false;
                            }
                            element = space;
                            eventTargets[eventId] = eventId;
                        }
                        else {
                            return Fail("JSM_UNSUPPORTED_EVENT", eventPath + "/type", type);
                        }
                        if (!AddArticulations(element, event, measure, eventPath)) {
                            delete element;
                            return false;
                        }
                        layer->AddChild(element);
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
                for (unsigned int i = 0; i < spanners->size(); ++i) {
                    if (!spanners->has<JObject>(i)) {
                        return Fail("JSM_INVALID_SPANNER", "/score/parts/measures/spanners", "expected object");
                    }
                    pendingSpanners.push_back({ &spanners->get<JObject>(i), measure,
                        "/score/parts/" + std::to_string(partIndex) + "/measures/" + std::to_string(barIndex)
                            + "/spanners/" + std::to_string(i) });
                }
            }
            const JArray *measureEvents = ArrayAt(partMeasure, "measureEvents", "/score/parts/measures", false);
            if (measureEvents && !measureEvents->empty()) {
                Warn("JSM_UNSUPPORTED_VISIBLE", "/score/parts/measures/measureEvents",
                    "directions are retained in JSM but not yet engraved by the native adapter");
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
        const JObject *start = ObjectAt(*pending.source, "start", pending.path);
        const JObject *end = ObjectAt(*pending.source, "end", pending.path);
        if (!start || !end) return false;
        auto endpoint = [&](const JObject &source, const std::string &path) -> std::string {
            if (source.has<jsonxx::String>("toneId")) {
                auto iter = toneTargets.find(source.get<jsonxx::String>("toneId"));
                if (iter != toneTargets.end()) return iter->second;
            }
            if (source.has<jsonxx::String>("eventId")) {
                auto iter = eventTargets.find(source.get<jsonxx::String>("eventId"));
                if (iter != eventTargets.end()) return iter->second;
            }
            Fail("JSM_UNKNOWN_ENDPOINT", path, "spanner endpoint does not resolve");
            return {};
        };
        const std::string startId = endpoint(*start, pending.path + "/start");
        const std::string endId = endpoint(*end, pending.path + "/end");
        if (startId.empty() || endId.empty()) return false;
        ControlElement *spanner = nullptr;
        if (kind == "tie") spanner = new Tie();
        else if (kind == "slur") spanner = new Slur();
        else if (kind == "hairpin-crescendo" || kind == "hairpin-diminuendo") {
            Hairpin *hairpin = new Hairpin();
            hairpin->SetForm(kind == "hairpin-crescendo" ? hairpinLog_FORM_cres : hairpinLog_FORM_dim);
            spanner = hairpin;
        }
        else return Fail("JSM_UNSUPPORTED_SPANNER", pending.path + "/kind", kind);
        spanner->SetID(id);
        spanner->m_unsupported.push_back({ "jsm-spanner-id", id });
        TimeSpanningInterface *timeSpan = spanner->GetTimeSpanningInterface();
        if (!timeSpan) {
            delete spanner;
            return Fail("JSM_INTERNAL", pending.path, "spanner lacks time interface");
        }
        timeSpan->SetStartid("#" + startId);
        timeSpan->SetEndid("#" + endId);
        pending.measure->AddChild(spanner);
    }

    m_doc->ConvertToPageBasedDoc();
    return true;
}

} // namespace vrv
