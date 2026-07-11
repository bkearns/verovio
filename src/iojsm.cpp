/////////////////////////////////////////////////////////////////////////////
// Name:        iojsm.cpp
// Purpose:     Native JSM input adapter
/////////////////////////////////////////////////////////////////////////////

#include "iojsm.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "accid.h"
#include "artic.h"
#include "chord.h"
#include "clef.h"
#include "doc.h"
#include "dynam.h"
#include "fermata.h"
#include "grpsym.h"
#include "hairpin.h"
#include "jsonxx.h"
#include "keysig.h"
#include "layer.h"
#include "label.h"
#include "labelabbr.h"
#include "lb.h"
#include "mdiv.h"
#include "measure.h"
#include "metersig.h"
#include "mrest.h"
#include "note.h"
#include "pb.h"
#include "reh.h"
#include "rend.h"
#include "rest.h"
#include "score.h"
#include "section.h"
#include "sb.h"
#include "slur.h"
#include "space.h"
#include "staff.h"
#include "staffdef.h"
#include "staffgrp.h"
#include "tempo.h"
#include "text.h"
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
                        const long long next = static_cast<long long>(ppq) / GreatestCommonDivisor(ppq, denominator)
                            * denominator;
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
    const std::vector<int> staffNumbers { binding->second.number };
    ControlElement *control = nullptr;

    // MusicXML projects global tempo and rehearsal events only on the first
    // referring staff, while dynamics remain part-local.
    if ((kind == "tempo" || kind == "rehearsal") && renderedOnce.contains(conductorId)) return true;
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
        const JArray *bpm = tempoSource ? ArrayAt(*tempoSource, "bpm", "/score/conductorTrack/events/tempo", false)
                                        : nullptr;
        if (bpm) tempo->SetMidiBpm(RationalValue(*bpm));
        control = tempo;
        renderedOnce.insert(conductorId);
    }
    else if (kind == "rehearsal") {
        if (!conductor.has<jsonxx::String>("value")) {
            return Fail("JSM_INVALID_DIRECTION", "/score/conductorTrack/events/value", "rehearsal value must be text");
        }
        Reh *rehearsal = new Reh();
        rehearsal->SetPlace(Placement(reference));
        rehearsal->SetStaff(staffNumbers);
        Rend *rend = new Rend();
        rend->SetRend(TEXTRENDITION_box);
        Text *text = new Text();
        text->SetText(UTF8to32(conductor.get<jsonxx::String>("value")));
        rend->AddChild(text);
        rehearsal->AddChild(rend);
        control = rehearsal;
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
                else out << static_cast<char>(ch);
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
        std::vector<std::pair<std::string, std::string>> fields = { { "format", "\"JSM\"" }, { "version", "\"0.1.0\"" },
            { "profile", "\"canonical\"" }, { "score", score } };
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
        if (!reference || !reference->is<jsonxx::Number>()) return Fail("JSM_COMPACT_REF", path, "expected reference");
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
    bool Str(const JArray &a, size_t i, std::string &out) { return Ref(m_strings, At(a, i), out, "/compact/string"); }
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
        std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "onset", JsonValue(*At(a, 1)) },
            { "duration", JsonValue(*At(a, 2)) }, { "order", JsonValue(*At(a, 3)) }, { "kind", JsonValue(*At(a, 4)) } };
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
        std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "barId", bar }, { "events", events } };
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
        std::vector<std::pair<std::string, std::string>> f = { { "staffId", id }, { "clef", JsonValue(*At(a, 1)) } };
        OptionalRaw(a, 2, "lines", f);
        OptionalRaw(a, 3, "extensions", f);
        out = JsonObject(f);
        return true;
    }
    bool Context(const JArray &a, std::string &out)
    {
        std::string id, st;
        if (a.size() < 6 || !Id(a, 0, id) || !MapArray(At(a, 5), st, &CompactJsmDecoder::StaffContext)) return false;
        std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "contentHash", JsonValue(*At(a, 1)) },
            { "time", JsonValue(*At(a, 2)) }, { "key", JsonValue(*At(a, 3)) } };
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
        const char *n[] = { "midiProgram", "midiChannel", "writtenRange", "concertRange", "percussion", "extensions" };
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
        if (a.size() < 3 || !Id(a, 0, id) || !Id(a, 1, lane) || !MapArray(At(a, 2), events, &CompactJsmDecoder::Event))
            return false;
        std::vector<std::pair<std::string, std::string>> f = { { "id", id }, { "laneId", lane }, { "events", events } };
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
                { "duration", JsonValue(*At(a, 3)) }, { "order", JsonValue(*At(a, 4)) }, { "conductorEventRef", x } };
        }
        else {
            f = { { "type", "\"barline\"" }, { "id", id }, { "onset", JsonValue(*At(a, 2)) }, { "duration", "[0,1]" },
                { "order", JsonValue(*At(a, 3)) }, { "side", JsonValue(*At(a, 4)) },
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
            || !Instrument(a.get<JArray>(3), instrument) || !MapArray(At(a, 4), staves, &CompactJsmDecoder::StaffDef)
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
            AddInitialContext(staffDef, part, staffId, meterAdded, score->GetScoreDef());
            staffBindings.emplace(
                staffId, StaffBinding { nextStaff, partId, staffId, InitialFifths(part), InitialContextId(part) });
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
        Measure *measure = new Measure(MEASURED, static_cast<int>(barIndex + 1));
        std::string primaryMeasureId = barId;
        if (parts->has<JObject>(0)) {
            const JArray *firstMeasures = ArrayAt(parts->get<JObject>(0), "measures", "/score/parts/0", false);
            if (firstMeasures && firstMeasures->has<JObject>(barIndex)) {
                StringAt(firstMeasures->get<JObject>(barIndex), "id", "/score/parts/0/measures", primaryMeasureId,
                    false);
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
