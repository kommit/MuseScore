/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#include "displaylistpaintprovider.h"

#include <algorithm>

#include "draw/fontmetrics.h"
#include "draw/painter.h"
#include "draw/types/drawdata.h"
#include "engraving/dom/engravingitem.h"

#ifndef NO_QT_SUPPORT
#include <QByteArray>
#endif

using namespace muse;
using namespace muse::draw;

namespace {
JsonObject rectObject(const RectF& rect)
{
    JsonObject result;
    result["x"] = rect.x();
    result["y"] = rect.y();
    result["width"] = rect.width();
    result["height"] = rect.height();
    return result;
}

JsonObject matrixObject(const Transform& transform)
{
    JsonObject result;
    result["m11"] = transform.m11();
    result["m12"] = transform.m12();
    result["m13"] = transform.m13();
    result["m21"] = transform.m21();
    result["m22"] = transform.m22();
    result["m23"] = transform.m23();
    result["m31"] = transform.m31();
    result["m32"] = transform.m32();
    result["m33"] = transform.m33();
    return result;
}

JsonObject colorObject(const Color& color)
{
    JsonObject result;
    const std::string source = color.toString();
    result["source"] = source;
    result["css"] = "rgb(" + std::to_string(color.red()) + " "
                    + std::to_string(color.green()) + " " + std::to_string(color.blue())
                    + " / " + std::to_string(static_cast<double>(color.alpha()) / 255.0) + ")";
    return result;
}

JsonObject penObject(const Pen& pen)
{
    JsonObject result;
    result["style"] = static_cast<int>(pen.style());
    result["color"] = colorObject(pen.color());
    result["width"] = pen.widthF();
    result["capStyle"] = static_cast<int>(pen.capStyle());
    result["joinStyle"] = static_cast<int>(pen.joinStyle());
    result["miterLimit"] = pen.miterLimit();
    JsonArray dash;
    for (double value : pen.dashPattern()) {
        // Some distributed short-line patterns contain a negative computed
        // gap. QPainter effectively collapses that gap; v1 keeps dash entries
        // portable by serializing the collapsed gap as zero.
        dash.append(std::max(0.0, value));
    }
    result["dashPattern"] = dash;
    return result;
}

JsonObject brushObject(const Brush& brush)
{
    JsonObject result;
    result["style"] = static_cast<int>(brush.style());
    result["color"] = colorObject(brush.color());
    return result;
}

JsonObject fontObject(const Font& font)
{
    JsonObject result;
    result["family"] = font.family().id();
    result["type"] = static_cast<int>(font.type());
    result["pointSize"] = font.pointSizeF();
    result["weight"] = static_cast<int>(font.weight());
    result["italic"] = font.italic();
    result["hinting"] = static_cast<int>(font.hinting());
    result["noMerging"] = font.noFontMerging();
    return result;
}

JsonObject stateObject(const DisplayListPaintProvider::State& state)
{
    JsonObject result;
    result["pen"] = penObject(state.pen);
    result["brush"] = brushObject(state.brush);
    result["font"] = fontObject(state.font);
    result["antialiasing"] = state.antialiasing;
    result["compositionMode"] = state.compositionMode == CompositionMode::HardLight ? "hard-light" : "source-over";
    result["globalAlpha"] = 1.0;
    result["transform"] = matrixObject(state.transform);
    return result;
}

JsonObject pathObject(const PainterPath& path, bool transformGeometry = false, const Transform& transform = Transform())
{
    JsonObject result;
    result["fillRule"] = static_cast<int>(path.fillRule());
    JsonArray elements;
    for (size_t index = 0; index < path.elementCount(); ++index) {
        const PainterPath::Element element = path.elementAt(index);
        PointF point(element.x, element.y);
        if (transformGeometry) {
            point = transform.map(point);
        }
        JsonObject value;
        switch (element.type) {
        case PainterPath::ElementType::MoveToElement:
            value["op"] = "moveTo";
            break;
        case PainterPath::ElementType::LineToElement:
            value["op"] = "lineTo";
            break;
        case PainterPath::ElementType::CurveToElement: {
            if (index + 2 >= path.elementCount()) {
                continue;
            }
            PointF control1(element.x, element.y);
            PointF control2(path.elementAt(index + 1).x, path.elementAt(index + 1).y);
            PointF endpoint(path.elementAt(index + 2).x, path.elementAt(index + 2).y);
            if (transformGeometry) {
                control1 = transform.map(control1);
                control2 = transform.map(control2);
                endpoint = transform.map(endpoint);
            }
            value["op"] = "cubicTo";
            value["c1x"] = control1.x();
            value["c1y"] = control1.y();
            value["c2x"] = control2.x();
            value["c2y"] = control2.y();
            value["x"] = endpoint.x();
            value["y"] = endpoint.y();
            elements.append(value);
            index += 2;
            continue;
        }
        case PainterPath::ElementType::CurveToDataElement:
            continue;
        }
        value["x"] = point.x();
        value["y"] = point.y();
        elements.append(value);
    }
    result["elements"] = elements;
    return result;
}

JsonArray pointArray(const PointF& point)
{
    return JsonArray { point.x(), point.y() };
}
}

namespace mu::iex::midi {
DisplayListPaintProvider::DisplayListPaintProvider() = default;

std::string DisplayListPaintProvider::objectNameForItem(const engraving::EngravingItem* item) const
{
    engraving::EID eid = item->eid();
    if (!eid.isValid()) {
        eid = item->assignNewEID();
    }
    const engraving::Fraction tick = item->tick();
    return "element-id=" + eid.toStdString()
           + ";" + std::string(item->typeName())
           + "-track-" + std::to_string(item->track())
           + "-tick-" + std::to_string(tick.numerator())
           + "-" + std::to_string(tick.denominator());
}

bool DisplayListPaintProvider::State::operator==(const State& other) const
{
    return pen == other.pen && brush == other.brush && font == other.font
           && transform == other.transform && antialiasing == other.antialiasing
           && compositionMode == other.compositionMode;
}

bool DisplayListPaintProvider::isActive() const
{
    return m_active;
}

void DisplayListPaintProvider::beginTarget(const std::string& name)
{
    m_active = true;
    m_name = name;
    m_pages.clear();
    m_states.clear();
    m_stateStack.clear();
    m_objects.clear();
    m_images = JsonArray();
    m_imageIds.clear();
    m_state = State();
    m_stateDirty = true;
    m_lastStateId = -1;
    m_currentPage = -1;
    m_objectSequence = 0;
    m_elementObjects.clear();
}

void DisplayListPaintProvider::beforeEndTargetHook(Painter*)
{
}

bool DisplayListPaintProvider::endTarget(bool)
{
    m_active = false;
    return true;
}

void DisplayListPaintProvider::beginObject(const std::string& name)
{
    if (name.rfind("page_", 0) == 0) {
        Page next;
        next.id = "page-" + name.substr(5);
        next.name = name;
        next.viewport = m_viewport;
        m_pages.push_back(std::move(next));
        m_currentPage = static_cast<int>(m_pages.size()) - 1;
        m_objects.clear();
        // Every page is independently replayable, so its first stateful
        // operation must emit a state even when it matches the prior page.
        m_stateDirty = true;
        m_lastStateId = -1;
        return;
    }
    m_objects.push_back(name);
}

void DisplayListPaintProvider::endObject()
{
    if (!m_objects.empty()) {
        m_objects.pop_back();
        return;
    }
    m_currentPage = -1;
}

void DisplayListPaintProvider::setAntialiasing(bool arg)
{
    m_state.antialiasing = arg;
    m_stateDirty = true;
}

void DisplayListPaintProvider::setCompositionMode(CompositionMode mode)
{
    m_state.compositionMode = mode;
    m_stateDirty = true;
}

void DisplayListPaintProvider::setWindow(const RectF&)
{
}

void DisplayListPaintProvider::setViewport(const RectF& viewport)
{
    m_viewport = viewport;
    if (m_currentPage >= 0) {
        page().viewport = viewport;
    }
}

void DisplayListPaintProvider::setFont(const Font& font)
{
    m_state.font = font;
    m_stateDirty = true;
}

const Font& DisplayListPaintProvider::font() const
{
    return m_state.font;
}

void DisplayListPaintProvider::setPen(const Pen& pen)
{
    m_state.pen = pen;
    m_stateDirty = true;
}

void DisplayListPaintProvider::setNoPen()
{
    m_state.pen = Pen(PenStyle::NoPen);
    m_stateDirty = true;
}

const Pen& DisplayListPaintProvider::pen() const
{
    return m_state.pen;
}

void DisplayListPaintProvider::setBrush(const Brush& brush)
{
    m_state.brush = brush;
    m_stateDirty = true;
}

const Brush& DisplayListPaintProvider::brush() const
{
    return m_state.brush;
}

void DisplayListPaintProvider::save()
{
    if (m_currentPage >= 0) {
        ensureState();
        JsonObject command;
        command["op"] = "save";
        append(command);
    }
    m_stateStack.push_back(m_state);
}

void DisplayListPaintProvider::restore()
{
    if (!m_stateStack.empty()) {
        m_state = m_stateStack.back();
        m_stateStack.pop_back();
        m_stateDirty = true;
    }
    if (m_currentPage >= 0) {
        JsonObject command;
        command["op"] = "restore";
        append(command);
    }
}

double DisplayListPaintProvider::deviceLogicalDpi() const
{
    return 360.0;
}

void DisplayListPaintProvider::setTransform(const Transform& transform)
{
    m_state.transform = transform;
    m_stateDirty = true;
}

const Transform& DisplayListPaintProvider::transform() const
{
    return m_state.transform;
}

DisplayListPaintProvider::Page& DisplayListPaintProvider::page()
{
    return m_pages.at(static_cast<size_t>(m_currentPage));
}

void DisplayListPaintProvider::append(const JsonObject& command)
{
    if (m_currentPage >= 0) {
        page().commands.append(command);
    }
}

int DisplayListPaintProvider::internState()
{
    State normalized = m_state;
    if (m_currentPage >= 0) {
        Page& currentPage = page();
        if (!currentPage.hasOrigin) {
            currentPage.origin = PointF(m_state.transform.m31(), m_state.transform.m32());
            currentPage.hasOrigin = true;
        }
        const Transform& transform = m_state.transform;
        normalized.transform.setMatrix(
            transform.m11(), transform.m12(), transform.m13(),
            transform.m21(), transform.m22(), transform.m23(),
            transform.m31() - currentPage.origin.x(),
            transform.m32() - currentPage.origin.y(),
            transform.m33());
    }
    auto found = std::find(m_states.cbegin(), m_states.cend(), normalized);
    if (found != m_states.cend()) {
        return static_cast<int>(std::distance(m_states.cbegin(), found));
    }
    m_states.push_back(normalized);
    return static_cast<int>(m_states.size()) - 1;
}

void DisplayListPaintProvider::ensureState()
{
    const int stateId = internState();
    if (!m_stateDirty && stateId == m_lastStateId) {
        return;
    }
    JsonObject command;
    command["op"] = "setState";
    command["stateId"] = stateId;
    append(command);
    m_lastStateId = stateId;
    m_stateDirty = false;
}

std::string DisplayListPaintProvider::nextObjectId(const char* kind)
{
    const std::string object = m_objects.empty() ? kind : m_objects.back();
    return page().id + ":" + object + ":" + std::to_string(m_objectSequence++);
}

JsonObject DisplayListPaintProvider::metadata(const char* kind, const RectF& localBounds)
{
    RectF bounds = m_state.transform.map(localBounds);
    if (m_currentPage >= 0 && page().hasOrigin) {
        bounds.translate(-page().origin.x(), -page().origin.y());
    }
    JsonObject result;
    const std::string objectId = nextObjectId(kind);
    result["objectId"] = objectId;
    if (!m_objects.empty()) {
        static constexpr std::string_view ELEMENT_ID_PREFIX = "element-id=";
        const std::string& objectName = m_objects.back();
        if (objectName.rfind(ELEMENT_ID_PREFIX, 0) == 0) {
            const size_t separator = objectName.find(';', ELEMENT_ID_PREFIX.size());
            if (separator != std::string::npos) {
                const std::string elementId = objectName.substr(ELEMENT_ID_PREFIX.size(),
                                                                separator - ELEMENT_ID_PREFIX.size());
                m_elementObjects[elementId].push_back(objectId);
            }
        }
    }
    result["bbox"] = rectObject(bounds);
    JsonObject hitShape;
    hitShape["kind"] = "rect";
    hitShape["rect"] = rectObject(bounds);
    result["hitShape"] = hitShape;
    result["layer"] = "notation";
    return result;
}

void DisplayListPaintProvider::drawPath(const PainterPath& path)
{
    ensureState();
    JsonObject command = metadata("path", path.boundingRect());
    command["op"] = "drawPath";
    command["path"] = pathObject(path);
    int mode = static_cast<int>(DrawMode::StrokeAndFill);
    if (m_state.pen.style() == PenStyle::NoPen) {
        mode = static_cast<int>(DrawMode::Fill);
    } else if (m_state.brush.style() == BrushStyle::NoBrush) {
        mode = static_cast<int>(DrawMode::Stroke);
    }
    command["mode"] = mode;
    append(command);
}

void DisplayListPaintProvider::drawPolygon(const PointF* points, size_t pointCount, PolygonMode mode)
{
    ensureState();
    PolygonF polygon(pointCount);
    JsonArray values;
    for (size_t index = 0; index < pointCount; ++index) {
        polygon[index] = points[index];
        values.append(pointArray(points[index]));
    }
    JsonObject geometry;
    geometry["points"] = values;
    geometry["mode"] = static_cast<int>(mode);
    JsonObject command = metadata("polygon", polygon.boundingRect());
    command["op"] = "drawPolygon";
    command["polygon"] = geometry;
    append(command);
}

void DisplayListPaintProvider::drawText(const PointF& point, const String& text)
{
    ensureState();
    RectF bounds = FontMetrics(m_state.font).boundingRect(text);
    bounds.translate(point);
    JsonObject geometry;
    geometry["kind"] = "point";
    geometry["x"] = point.x();
    geometry["y"] = point.y();
    geometry["alignment"] = 0;
    geometry["textFlags"] = 0;
    geometry["text"] = text;
    JsonObject command = metadata("text", bounds);
    command["op"] = "drawText";
    command["text"] = geometry;
    append(command);
}

void DisplayListPaintProvider::drawText(const RectF& rect, Alignment alignment, TextFlags textFlags, const String& text)
{
    ensureState();
    JsonObject geometry;
    geometry["kind"] = "rect";
    geometry["x"] = rect.x();
    geometry["y"] = rect.y();
    geometry["width"] = rect.width();
    geometry["height"] = rect.height();
    geometry["alignment"] = static_cast<int>(alignment);
    geometry["textFlags"] = static_cast<int>(textFlags);
    geometry["text"] = text;
    JsonObject command = metadata("text", rect);
    command["op"] = "drawText";
    command["text"] = geometry;
    append(command);
}

void DisplayListPaintProvider::drawSymbol(const PointF& point, char32_t ucs4Code)
{
    drawText(point, String::fromUcs4(&ucs4Code, 1));
}

std::string DisplayListPaintProvider::imageResource(const Pixmap& pixmap)
{
    const auto found = m_imageIds.find(pixmap.key());
    if (found != m_imageIds.cend()) {
        return found->second;
    }
    const std::string id = "image-" + std::to_string(m_imageIds.size());
    JsonObject resource;
    resource["id"] = id;
    resource["mimeType"] = "image/png";
    resource["width"] = pixmap.width();
    resource["height"] = pixmap.height();
#ifndef NO_QT_SUPPORT
    resource["source"] = "data:image/png;base64," + pixmap.data().toQByteArray().toBase64().toStdString();
#else
    resource["source"] = "";
#endif
    m_images.append(resource);
    m_imageIds[pixmap.key()] = id;
    return id;
}

void DisplayListPaintProvider::drawPixmapImpl(const RectF& destination, const Pixmap& pixmap,
                                              bool tiled, const PointF& offset)
{
    ensureState();
    JsonObject command = metadata("image", destination);
    command["op"] = "drawImage";
    command["imageId"] = imageResource(pixmap);
    command["source"] = rectObject(RectF(0.0, 0.0, pixmap.width(), pixmap.height()));
    command["destination"] = rectObject(destination);
    command["tiled"] = tiled;
    command["offsetX"] = offset.x();
    command["offsetY"] = offset.y();
    append(command);
}

void DisplayListPaintProvider::drawPixmap(const PointF& point, const Pixmap& pixmap)
{
    drawPixmapImpl(RectF(point, SizeF(pixmap.width(), pixmap.height())), pixmap, false, PointF());
}

void DisplayListPaintProvider::drawTiledPixmap(const RectF& rect, const Pixmap& pixmap, const PointF& offset)
{
    drawPixmapImpl(rect, pixmap, true, offset);
}

#ifndef NO_QT_SUPPORT
void DisplayListPaintProvider::drawPixmap(const PointF& point, const QPixmap& pixmap)
{
    drawPixmap(point, Pixmap::fromQPixmap(pixmap));
}

void DisplayListPaintProvider::drawTiledPixmap(const RectF& rect, const QPixmap& pixmap, const PointF& offset)
{
    drawTiledPixmap(rect, Pixmap::fromQPixmap(pixmap), offset);
}
#endif

bool DisplayListPaintProvider::hasClipping() const
{
    return m_clipping;
}

void DisplayListPaintProvider::setClipRect(const RectF& rect)
{
    ensureState();
    JsonObject command;
    command["op"] = "clipRect";
    command["rect"] = rectObject(rect);
    append(command);
}

void DisplayListPaintProvider::setMask(const RectF& background, const std::vector<RectF>& maskRects)
{
    ensureState();
    JsonObject command;
    command["op"] = "setMask";
    command["bounds"] = rectObject(background);
    JsonArray holes;
    for (const RectF& rect : maskRects) {
        holes.append(rectObject(rect));
    }
    command["holes"] = holes;
    append(command);
}

void DisplayListPaintProvider::setClipping(bool enable)
{
    if (enable == m_clipping) {
        return;
    }
    m_clipping = enable;
    if (enable) {
        save();
    } else {
        restore();
    }
}

ByteArray DisplayListPaintProvider::toJson(bool prettify) const
{
    JsonObject root;
    root["schemaVersion"] = 1;
    root["dpi"] = 360;
    JsonObject source;
    source["format"] = "display-list-v1";
    source["name"] = m_name;
    root["source"] = source;
    JsonObject resources;
    resources["images"] = m_images;
    resources["fontHashes"] = JsonObject();
    root["resources"] = resources;

    JsonArray states;
    for (const State& state : m_states) {
        states.append(stateObject(state));
    }
    root["states"] = states;

    JsonArray pages;
    for (const Page& page : m_pages) {
        JsonObject value;
        value["id"] = page.id;
        value["name"] = page.name;
        value["width"] = page.viewport.width();
        value["height"] = page.viewport.height();
        value["revision"] = std::to_string(page.commands.size());
        value["commands"] = page.commands;
        pages.append(value);
    }
    root["pages"] = pages;
    return JsonDocument(root).toJson(prettify ? JsonDocument::Format::Indented : JsonDocument::Format::Compact);
}

ByteArray DisplayListPaintProvider::elementMapToJson(bool prettify) const
{
    JsonObject root;
    root["schemaVersion"] = 1;

    JsonArray elements;
    for (const auto& [elementId, objectIds] : m_elementObjects) {
        JsonObject element;
        element["elementId"] = elementId;
        JsonArray objects;
        for (const std::string& objectId : objectIds) {
            objects.append(objectId);
        }
        element["objectIds"] = objects;
        elements.append(element);
    }
    root["elements"] = elements;

    return JsonDocument(root).toJson(prettify ? JsonDocument::Format::Indented : JsonDocument::Format::Compact);
}

bool DisplayListPaintProvider::hasElementId(const std::string& elementId) const
{
    return m_elementObjects.find(elementId) != m_elementObjects.cend();
}
}
