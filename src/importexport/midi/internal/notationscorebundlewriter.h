/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#pragma once

#include "project/inotationwriter.h"

#include "engraving/rendering/iscorerenderer.h"
#include "imidiconfiguration.h"
#include "modularity/ioc.h"

namespace mu::iex::midi {
class NotationScoreBundleWriter : public project::INotationWriter, public muse::Contextable
{
    muse::GlobalInject<engraving::rendering::IScoreRenderer> scoreRenderer;
    muse::GlobalInject<IMidiImportExportConfiguration> midiImportExportConfiguration;

public:
    explicit NotationScoreBundleWriter(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx) {}

    std::vector<UnitType> supportedUnitTypes() const override;
    bool supportsUnitType(UnitType unitType) const override;

    muse::Ret write(notation::INotationPtr notation, muse::io::IODevice& destinationDevice,
                    const Options& options = Options()) override;
    muse::Ret writeList(const notation::INotationPtrList& notations, muse::io::IODevice& destinationDevice,
                        const Options& options = Options()) override;
};
}
