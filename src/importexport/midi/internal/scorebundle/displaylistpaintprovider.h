/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#pragma once

#include <map>
#include <vector>

#include "draw/ipaintprovider.h"
#include "engraving/rendering/iengravingitempaintprovider.h"
#include "global/serialization/json.h"

namespace mu::iex::midi {
class DisplayListPaintProvider : public muse::draw::IPaintProvider,
    public engraving::rendering::IEngravingItemPaintProvider
{
public:
    DisplayListPaintProvider();

    bool isActive() const override;
    void beginTarget(const std::string& name) override;
    void beforeEndTargetHook(muse::draw::Painter*) override;
    bool endTarget(bool endDraw = false) override;
    void beginObject(const std::string& name) override;
    void endObject() override;

    void setAntialiasing(bool arg) override;
    void setCompositionMode(muse::draw::CompositionMode mode) override;
    void setWindow(const muse::RectF& window) override;
    void setViewport(const muse::RectF& viewport) override;
    void setFont(const muse::draw::Font& font) override;
    const muse::draw::Font& font() const override;
    void setPen(const muse::draw::Pen& pen) override;
    void setNoPen() override;
    const muse::draw::Pen& pen() const override;
    void setBrush(const muse::draw::Brush& brush) override;
    const muse::draw::Brush& brush() const override;
    void save() override;
    void restore() override;
    double deviceLogicalDpi() const override;
    void setTransform(const muse::draw::Transform& transform) override;
    const muse::draw::Transform& transform() const override;

    void drawPath(const muse::draw::PainterPath& path) override;
    void drawPolygon(const muse::PointF* points, size_t pointCount, muse::draw::PolygonMode mode) override;
    void drawText(const muse::PointF& point, const muse::String& text) override;
    void drawText(const muse::RectF& rect, muse::draw::Alignment alignment,
                  muse::draw::TextFlags textFlags, const muse::String& text) override;
    void drawSymbol(const muse::PointF& point, char32_t ucs4Code) override;
    void drawPixmap(const muse::PointF& point, const muse::draw::Pixmap& pm) override;
    void drawTiledPixmap(const muse::RectF& rect, const muse::draw::Pixmap& pm,
                         const muse::PointF& offset = muse::PointF()) override;
#ifndef NO_QT_SUPPORT
    void drawPixmap(const muse::PointF& point, const QPixmap& pm) override;
    void drawTiledPixmap(const muse::RectF& rect, const QPixmap& pm,
                         const muse::PointF& offset = muse::PointF()) override;
#endif

    bool hasClipping() const override;
    void setClipRect(const muse::RectF& rect) override;
    void setMask(const muse::RectF& background, const std::vector<muse::RectF>& maskRects) override;
    void setClipping(bool enable) override;

    std::string objectNameForItem(const engraving::EngravingItem* item) const override;
    muse::ByteArray toJson(bool prettify = true) const;
    muse::ByteArray elementMapToJson(bool prettify = true) const;
    bool hasElementId(const std::string& elementId) const;

    struct State {
        muse::draw::Pen pen;
        muse::draw::Brush brush;
        muse::draw::Font font;
        muse::draw::Transform transform;
        bool antialiasing = false;
        muse::draw::CompositionMode compositionMode = muse::draw::CompositionMode::SourceOver;

        bool operator==(const State& other) const;
    };

private:
    struct Page {
        std::string id;
        std::string name;
        muse::RectF viewport;
        muse::PointF origin;
        bool hasOrigin = false;
        muse::JsonArray commands;
    };

    Page& page();
    void append(const muse::JsonObject& command);
    void ensureState();
    int internState();
    std::string nextObjectId(const char* kind);
    muse::JsonObject metadata(const char* kind, const muse::RectF& localBounds);
    std::string imageResource(const muse::draw::Pixmap& pixmap);
    void drawPixmapImpl(const muse::RectF& destination, const muse::draw::Pixmap& pixmap,
                        bool tiled, const muse::PointF& offset);

    bool m_active = false;
    bool m_clipping = false;
    bool m_stateDirty = true;
    std::string m_name;
    muse::RectF m_viewport;
    State m_state;
    std::vector<State> m_stateStack;
    std::vector<State> m_states;
    int m_lastStateId = -1;
    std::vector<Page> m_pages;
    int m_currentPage = -1;
    std::vector<std::string> m_objects;
    size_t m_objectSequence = 0;
    std::map<std::string, std::vector<std::string>> m_elementObjects;
    muse::JsonArray m_images;
    std::map<unsigned int, std::string> m_imageIds;
};
}
