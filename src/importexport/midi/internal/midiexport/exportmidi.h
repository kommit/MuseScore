/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef EXPORTMIDI_H
#define EXPORTMIDI_H

#include <cstddef>
#include <string>
#include <vector>

#include <QFile>

#include "../midishared/midifile.h"
#include "engraving/compat/midi/pausemap.h"
#include "engraving/compat/midi/compatmidirenderinternal.h"

namespace mu::engraving {
class Score;
class SynthesizerState;
}

namespace mu::iex::midi {
struct MidiExportEventTrace
{
    size_t track = 0;
    size_t ordinal = 0;
    int tick = 0;
    int type = 0;
    int channel = 0;
    int dataA = 0;
    int dataB = 0;
    int metaType = 0;
    std::vector<std::string> sourceElementIds;
};

struct MidiExportActivation
{
    std::string elementId;
    std::vector<std::string> visualElementIds;
    size_t occurrence = 0;
    int startTick = 0;
    int endTick = 0;
    int channel = 0;
    int pitch = 0;
};

struct MidiExportTrace
{
    int format = 1;
    int division = 0;
    std::vector<MidiExportEventTrace> events;
    std::vector<MidiExportActivation> activations;

    void clear()
    {
        format = 1;
        division = 0;
        events.clear();
        activations.clear();
    }
};

//---------------------------------------------------------
//   ExportMidi
//---------------------------------------------------------

class ExportMidi
{
public:
    ExportMidi(engraving::Score* s) { m_score = s; }
    bool write(const QString& name, bool midiExpandRepeats, bool exportRPNs);
    bool write(QIODevice* device, bool midiExpandRepeats, bool exportRPNs);
    bool write(const QString& name, bool midiExpandRepeats, bool exportRPNs, const engraving::SynthesizerState& synthState);
    bool write(QIODevice* device, bool midiExpandRepeats, bool exportRPNs, const engraving::SynthesizerState& synthState,
               MidiExportTrace* trace = nullptr);

private:
    void writeHeader(const engraving::CompatMidiRendererInternal::Context& context);

    QFile m_file;
    MidiFile m_midiFile;
    engraving::Score* m_score = nullptr;
};
}
#endif // EXPORTMIDI_H
