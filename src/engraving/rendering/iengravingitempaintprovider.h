/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#pragma once

#include <string>

namespace mu::engraving {
class EngravingItem;

namespace rendering {
class IEngravingItemPaintProvider
{
public:
    virtual ~IEngravingItemPaintProvider() = default;

    virtual std::string objectNameForItem(const EngravingItem* item) const = 0;
};
}
}
