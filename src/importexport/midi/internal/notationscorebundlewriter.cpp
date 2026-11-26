/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#include "notationscorebundlewriter.h"

#include <QBuffer>
#include <QCryptographicHash>

#include "draw/painter.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurerepeat.h"
#include "engraving/dom/note.h"
#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/system.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/synthesizerstate.h"
#include "engraving/rw/rwregister.h"
#include "global/io/buffer.h"
#include "global/serialization/json.h"
#include "global/serialization/zipwriter.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h"

#include "midiexport/exportmidi.h"
#include "scorebundle/displaylistpaintprovider.h"
#include "log.h"

using namespace muse;
using namespace muse::draw;
using namespace mu::engraving;
using namespace mu::notation;
using namespace mu::project;

namespace mu::iex::midi {
namespace {
ByteArray eventMapJson(const MidiExportTrace& trace, const std::string& midiSha256,
                       const DisplayListPaintProvider& provider)
{
    JsonObject root;
    root["schemaVersion"] = 1;
    root["midiSha256"] = midiSha256;
    root["format"] = trace.format;
    root["division"] = trace.division;

    JsonArray events;
    for (const MidiExportEventTrace& source : trace.events) {
        JsonObject event;
        event["track"] = static_cast<int>(source.track);
        event["ordinal"] = static_cast<int>(source.ordinal);
        event["tick"] = source.tick;
        event["type"] = source.type;
        event["channel"] = source.channel;
        event["dataA"] = source.dataA;
        event["dataB"] = source.dataB;
        if (source.type == ME_META) {
            event["metaType"] = source.metaType;
        }
        JsonArray elementIds;
        for (const std::string& elementId : source.sourceElementIds) {
            if (provider.hasElementId(elementId)) {
                elementIds.append(elementId);
            }
        }
        if (!elementIds.empty()) {
            event["sourceElementIds"] = elementIds;
        }
        events.append(event);
    }
    root["events"] = events;
    return JsonDocument(root).toJson(JsonDocument::Format::Indented);
}

ByteArray activationsJson(const MidiExportTrace& trace, const std::string& midiSha256,
                          const DisplayListPaintProvider& provider)
{
    JsonObject root;
    root["schemaVersion"] = 1;
    root["midiSha256"] = midiSha256;
    root["division"] = trace.division;

    JsonArray activations;
    for (const MidiExportActivation& source : trace.activations) {
        if (!provider.hasElementId(source.elementId)) {
            continue;
        }
        JsonObject activation;
        activation["elementId"] = source.elementId;
        activation["occurrence"] = static_cast<int>(source.occurrence);
        activation["startTick"] = source.startTick;
        activation["endTick"] = source.endTick;
        activation["channel"] = source.channel;
        activation["pitch"] = source.pitch;
        JsonArray visualElementIds;
        for (const std::string& elementId : source.visualElementIds) {
            if (provider.hasElementId(elementId)) {
                visualElementIds.append(elementId);
            }
        }
        if (!visualElementIds.empty()) {
            activation["visualElementIds"] = visualElementIds;
        }
        activations.append(activation);
    }
    root["activations"] = activations;
    return JsonDocument(root).toJson(JsonDocument::Format::Indented);
}

std::string notationElementId(const EngravingItem* item)
{
    const EID existing = item->eid();
    return (existing.isValid() ? existing : item->assignNewEID()).toStdString();
}

void appendNotationElements(JsonArray& elements, const ChordRest* chordRest, int tick, int measureId,
                            const Chord* graceParent = nullptr)
{
    JsonObject position;
    position["measureId"] = measureId;
    position["tick"] = tick;
    position["duration"] = chordRest->actualTicks().ticks();
    position["staff"] = static_cast<int>(chordRest->staffIdx()) + 1;
    position["displayStaff"] = static_cast<int>(chordRest->vStaffIdx()) + 1;
    position["voice"] = static_cast<int>(chordRest->voice()) + 1;

    if (chordRest->isChord()) {
        const Chord* chord = toChord(chordRest);
        position["chordId"] = notationElementId(chord);
        if (graceParent) {
            position["graceParentId"] = notationElementId(graceParent);
            position["graceType"] = static_cast<int>(chord->noteType());
        }
        for (const Note* note : chord->notes()) {
            JsonObject event = position;
            event["type"] = "note";
            event["elementId"] = notationElementId(note);
            event["visible"] = note->visible();
            event["x"] = note->pagePos().x() + note->headWidth() / 2.0;
            event["y"] = note->pagePos().y();
            event["pitch"] = note->pitch();
            event["soundingPitch"] = note->ppitch();
            event["tpc"] = note->tpc();
            event["play"] = note->play();
            if (const Tie* tie = note->tieBack()) {
                if (const Note* start = tie->startNote()) {
                    event["tiedFrom"] = notationElementId(start);
                }
            }
            elements.append(event);
        }
        for (const Chord* grace : chord->graceNotes()) {
            appendNotationElements(elements, grace, tick, measureId, chord);
        }
        return;
    }

    position["elementId"] = notationElementId(chordRest);
    position["visible"] = chordRest->visible();
    position["x"] = chordRest->pagePos().x();
    position["y"] = chordRest->pagePos().y();
    if (chordRest->isMeasureRepeat()) {
        position["type"] = "measureRepeat";
        position["measures"] = toMeasureRepeat(chordRest)->numMeasures();
    } else {
        position["type"] = "rest";
    }
    elements.append(position);
}

// Export notation objects in their written order, with layout references and
// the repeat traversal already used by the standard MIDI exporter.
ByteArray notationJson(Score* score, const MidiExportTrace& trace)
{
    JsonObject root;
    root["schemaVersion"] = 1;
    root["division"] = trace.division;
    root["staffCount"] = static_cast<int>(score->nstaves());
    root["partCount"] = static_cast<int>(score->parts().size());

    JsonArray systems;
    JsonArray measures;
    JsonArray elements;
    int systemId = 0;
    int measureId = 0;
    int pageIndex = -1;
    for (const Page* page : score->pages()) {
        ++pageIndex;
        for (const System* system : page->systems()) {
            bool hasMeasures = false;
            for (const MeasureBase* base : system->measures()) {
                if (!base->isMeasure()) {
                    continue;
                }
                hasMeasures = true;
                const Measure* measure = toMeasure(base);
                JsonObject measureJson;
                measureJson["id"] = ++measureId;
                measureJson["systemId"] = systemId + 1;
                measureJson["tick"] = measure->tick().ticks();
                measureJson["duration"] = measure->ticks().ticks();
                measureJson["x"] = measure->pagePos().x();
                measureJson["width"] = measure->width();
                measureJson["beats"] = measure->timesig().numerator();
                measureJson["beatType"] = measure->timesig().denominator();
                measureJson["repeatStart"] = measure->repeatStart();
                measureJson["repeatEnd"] = measure->repeatEnd();
                measureJson["repeatCount"] = measure->repeatCount();
                measures.append(measureJson);

                for (const Segment* segment = measure->first(SegmentType::ChordRest);
                     segment; segment = segment->next(SegmentType::ChordRest)) {
                    for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                        const EngravingItem* item = segment->element(track);
                        if (item && (item->isChord() || item->isRest() || item->isMeasureRepeat())) {
                            appendNotationElements(elements, toChordRest(item), segment->tick().ticks(), measureId);
                        }
                    }
                }
            }
            if (hasMeasures) {
                JsonObject systemJson;
                systemJson["id"] = ++systemId;
                systemJson["pageIndex"] = pageIndex;
                systemJson["y"] = system->pagePos().y();
                JsonArray staves;
                for (staff_idx_t staff = 0; staff < score->nstaves(); ++staff) {
                    JsonObject staffJson;
                    staffJson["y"] = system->staffYpage(staff);
                    staffJson["lineSpacing"] = score->staff(staff)->spatium(system->firstMeasure()->tick());
                    staves.append(staffJson);
                }
                systemJson["staves"] = staves;
                systems.append(systemJson);
            }
        }
    }
    root["systems"] = systems;
    root["measures"] = measures;
    root["elements"] = elements;
    JsonArray segments;
    for (const RepeatSegment* segment : score->repeatList()) {
        JsonObject entry;
        entry["tick"] = segment->tick;
        entry["duration"] = segment->len();
        entry["playTick"] = segment->utick;
        segments.append(entry);
    }
    root["segments"] = segments;
    return JsonDocument(root).toJson(JsonDocument::Format::Compact);
}

ByteArray manifestJson(bool expandRepeats, bool exportRpns, const std::string& midiSha256)
{
    JsonObject root;
    root["schemaVersion"] = 1;
    root["format"] = "musescore-score-bundle";

    JsonObject render;
    render["displayList"] = "render/display-list.json";
    render["elementMap"] = "render/element-map.json";
    root["render"] = render;

    JsonObject playback;
    playback["midi"] = "playback/score.mid";
    playback["eventMap"] = "playback/event-map.json";
    playback["activations"] = "playback/activations.json";
    playback["midiSha256"] = midiSha256;
    root["playback"] = playback;

    JsonObject options;
    options["expandRepeats"] = expandRepeats;
    options["exportRPNs"] = exportRpns;
    root["midiExportOptions"] = options;

    JsonObject notation;
    notation["index"] = "notation/index.json";
    notation["source"] = "notation/score.mscx";
    root["notation"] = notation;

    return JsonDocument(root).toJson(JsonDocument::Format::Indented);
}
}

std::vector<INotationWriter::UnitType> NotationScoreBundleWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool NotationScoreBundleWriter::supportsUnitType(UnitType unitType) const
{
    return unitType == UnitType::PER_PART;
}

Ret NotationScoreBundleWriter::write(INotationPtr notation, io::IODevice& destinationDevice, const Options&)
{
    IF_ASSERT_FAILED(notation) {
        return make_ret(Ret::Code::UnknownError);
    }

    Score* score = notation->elements()->msScore();
    IF_ASSERT_FAILED(score) {
        return make_ret(Ret::Code::UnknownError);
    }

    score->doLayout();

    std::shared_ptr<DisplayListPaintProvider> provider = std::make_shared<DisplayListPaintProvider>();
    {
        Painter painter(provider, "DisplayList");
        engraving::rendering::IScoreRenderer::ScorePaintOptions paintOptions;
        paintOptions.isMultiPage = true;
        paintOptions.deviceDpi = 360;
        paintOptions.printPageBackground = true;
        paintOptions.isSetViewport = true;
        paintOptions.isPrinting = true;
        scoreRenderer()->paintScore(&painter, score, paintOptions);
    }

    const bool expandRepeats = midiImportExportConfiguration()->isExpandRepeats();
    const bool exportRpns = midiImportExportConfiguration()->isMidiExportRpns();
    const SynthesizerState synthesizerState = score->synthesizerState();

    QByteArray midiBytes;
    QBuffer midiBuffer(&midiBytes);
    if (!midiBuffer.open(QIODevice::WriteOnly)) {
        return make_ret(Ret::Code::InternalError);
    }

    MidiExportTrace trace;
    ExportMidi exportMidi(score);
    if (!exportMidi.write(&midiBuffer, expandRepeats, exportRpns, synthesizerState, &trace)) {
        return make_ret(Ret::Code::InternalError);
    }
    midiBuffer.close();

    const std::string midiSha256 = QCryptographicHash::hash(midiBytes, QCryptographicHash::Sha256).toHex().toStdString();
    const ByteArray midiData = ByteArray::fromQByteArray(midiBytes);

    const ByteArray notationData = notationJson(score, trace);
    ByteArray scoreData;
    auto scoreBuffer = io::Buffer::opened(io::IODevice::WriteOnly, &scoreData);
    if (!rw::RWRegister::writer()->writeScore(score, &scoreBuffer)) {
        return make_ret(Ret::Code::InternalError);
    }

    ZipWriter zip(&destinationDevice);
    if (zip.hasError()) {
        return make_ret(Ret::Code::InternalError);
    }
    zip.addFile("manifest.json", manifestJson(expandRepeats, exportRpns, midiSha256));
    zip.addFile("render/display-list.json", provider->toJson());
    zip.addFile("render/element-map.json", provider->elementMapToJson());
    zip.addFile("playback/score.mid", midiData);
    zip.addFile("playback/event-map.json", eventMapJson(trace, midiSha256, *provider));
    zip.addFile("playback/activations.json", activationsJson(trace, midiSha256, *provider));
    zip.addFile("notation/index.json", notationData);
    zip.addFile("notation/score.mscx", scoreData);
    zip.close();

    return zip.hasError() ? make_ret(Ret::Code::InternalError) : make_ret(Ret::Code::Ok);
}

Ret NotationScoreBundleWriter::writeList(const INotationPtrList&, io::IODevice&, const Options&)
{
    NOT_SUPPORTED;
    return Ret(Ret::Code::NotSupported);
}
}
