#include "StructureView.h"
#include <algorithm>

namespace {
const juce::Colour kBg{0xff111318}, kPanel{0xff1a1d24}, kEdge{0xff2a2f3a}, kText{0xffe6e8ee},
    kDim{0xff8a90a0}, kAccent{0xffff8a3d}, kOk{0xff5ad19a}, kErr{0xffff5a6a};
constexpr int kBoxW = 150, kGapX = 90, kGapY = 16, kHead = 24, kRow = 16, kPad = 20;

void splitRef(const juce::String& s, juce::String& mod, juce::String& port, const juce::String& defPort) {
    int dot = s.indexOfChar('.');
    mod = dot < 0 ? s : s.substring(0, dot);
    port = dot < 0 ? defPort : s.substring(dot + 1);
}
void addUnique(std::vector<juce::String>& v, const juce::String& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}
} // namespace

int StructureCanvas::findBox(const juce::String& id) const {
    for (size_t i = 0; i < boxes_.size(); ++i) if (boxes_[i].id == id) return (int)i;
    return -1;
}
int StructureCanvas::rowOf(const std::vector<juce::String>& v, const juce::String& n) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == n) return (int)i;
    return 0;
}
juce::Point<float> StructureCanvas::outPoint(const Box& b, const juce::String& port) const {
    return {(float)b.r.getRight(), (float)(b.r.getY() + kHead + rowOf(b.outs, port) * kRow + kRow / 2)};
}
juce::Point<float> StructureCanvas::inPoint(const Box& b, const juce::String& port) const {
    return {(float)b.r.getX(), (float)(b.r.getY() + kHead + rowOf(b.ins, port) * kRow + kRow / 2)};
}

void StructureCanvas::setPatch(const juce::String& json) {
    boxes_.clear(); wires_.clear(); error_.clear(); title_.clear(); selected_ = dragging_ = -1;
    auto root = juce::JSON::parse(json);
    auto* obj = root.getDynamicObject();
    if (obj == nullptr) { error_ = "Patch JSON could not be read."; setSize(400, 120); repaint(); return; }
    title_ = obj->getProperty("name").toString();

    if (auto* mods = obj->getProperty("modules").getArray()) {
        for (auto& m : *mods) {
            Box b;
            b.id = m.getProperty("id", {}).toString();
            b.type = m.getProperty("type", {}).toString();
            if (auto* ps = m.getProperty("params", {}).getDynamicObject()) {
                if (ps->hasProperty("name")) b.extra = "\"" + ps->getProperty("name").toString() + "\"";
            }
            if (auto* in = m.getProperty("in", {}).getDynamicObject())
                for (auto& kv : in->getProperties()) {
                    addUnique(b.ins, kv.name.toString());
                    b.extra << (b.extra.isEmpty() ? "" : "  ") << kv.name.toString() << "=" << kv.value.toString();
                }
            boxes_.push_back(std::move(b));
        }
    }
    // synthetic output sink
    Box out; out.id = "__out"; out.type = "OUT"; out.ins = {"in"};
    boxes_.push_back(out);

    auto addWire = [&](const juce::String& src, const juce::String& dst) {
        juce::String sm, sp, dm, dp;
        splitRef(src, sm, sp, "out"); splitRef(dst, dm, dp, "in");
        int a = findBox(sm), b = findBox(dm);
        if (a < 0 || b < 0) return;
        addUnique(boxes_[(size_t)a].outs, sp);
        addUnique(boxes_[(size_t)b].ins, dp);
        wires_.push_back({a, b, sp, dp, false});
    };
    if (auto* ws = obj->getProperty("wires").getArray())
        for (auto& w : *ws)
            if (auto* pair = w.getArray(); pair != nullptr && pair->size() >= 2)
                addWire((*pair)[0].toString(), (*pair)[1].toString());
    addWire(obj->getProperty("out").toString(), "__out.in");
    for (auto& b : boxes_) {
        if (b.outs.empty()) b.outs.push_back("out");
        if (b.ins.empty() && b.id == "__out") b.ins.push_back("in");
    }

    // Layering: longest path from sources. Edges into z1/delay break loops.
    for (auto& w : wires_) {
        const auto& t = boxes_[(size_t)w.to].type;
        w.feedback = (t == "z1" || t == "delay") ;
    }
    const int n = (int)boxes_.size();
    for (int round = 0; round < n; ++round) {
        bool changed = false;
        for (auto& w : wires_) {
            if (w.feedback) continue;
            int want = std::min(boxes_[(size_t)w.from].layer + 1, n);
            if (boxes_[(size_t)w.to].layer < want) { boxes_[(size_t)w.to].layer = want; changed = true; }
        }
        if (!changed) break;
    }
    // The output sink goes one column past everything.
    int maxLayer = 0;
    for (auto& b : boxes_) if (b.id != "__out") maxLayer = std::max(maxLayer, b.layer);
    boxes_.back().layer = maxLayer + 1;

    // Positions: stack boxes within each layer in patch order.
    std::vector<int> yNext((size_t)maxLayer + 2, kPad + 28);
    int maxY = 0;
    for (auto& b : boxes_) {
        int rows = std::max({(int)b.ins.size(), (int)b.outs.size(), 1});
        int h = kHead + rows * kRow + (b.extra.isEmpty() ? 4 : 18);
        int x = kPad + b.layer * (kBoxW + kGapX);
        b.r = {x, yNext[(size_t)b.layer], kBoxW, h};
        yNext[(size_t)b.layer] += h + kGapY;
        maxY = std::max(maxY, b.r.getBottom());
    }
    setSize(kPad * 2 + (maxLayer + 2) * (kBoxW + kGapX) - kGapX, maxY + kPad);
    repaint();
}

void StructureCanvas::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setColour(kText);
    g.setFont(juce::FontOptions(16.f, juce::Font::bold));
    g.drawText(title_.isEmpty() ? "Patch structure" : title_ + "  -  structure", kPad, 6, getWidth() - 2 * kPad, 22,
               juce::Justification::centredLeft);
    if (error_.isNotEmpty()) {
        g.setColour(kErr);
        g.setFont(juce::FontOptions(14.f));
        g.drawText(error_, getLocalBounds(), juce::Justification::centred);
        return;
    }
    // wires first, boxes on top
    for (auto& w : wires_) {
        auto p0 = outPoint(boxes_[(size_t)w.from], w.fromPort);
        auto p1 = inPoint(boxes_[(size_t)w.to], w.toPort);
        float dx = std::max(40.f, std::abs(p1.x - p0.x) * 0.5f);
        juce::Path p;
        p.startNewSubPath(p0);
        p.cubicTo(p0.x + dx, p0.y, p1.x - dx, p1.y, p1.x, p1.y);
        const bool hot = selected_ >= 0 && (w.from == selected_ || w.to == selected_);
        const float alpha = selected_ < 0 ? 0.75f : (hot ? 1.0f : 0.15f);
        g.setColour((w.feedback ? kErr : kAccent).withAlpha(alpha));
        g.strokePath(p, juce::PathStrokeType(hot ? 2.8f : 1.6f));
        g.fillEllipse(p0.x - 3, p0.y - 3, 6, 6);
        g.fillEllipse(p1.x - 3, p1.y - 3, 6, 6);
    }
    for (size_t bi = 0; bi < boxes_.size(); ++bi) {
        auto& b = boxes_[bi];
        auto r = b.r.toFloat();
        const bool sink = b.id == "__out";
        bool linked = false;
        for (auto& w : wires_) if (selected_ >= 0 && ((w.from == selected_ && w.to == (int)bi) || (w.to == selected_ && w.from == (int)bi))) linked = true;
        const bool sel = (int)bi == selected_;
        g.setColour(sel ? kPanel.brighter(0.12f) : kPanel);
        g.fillRoundedRectangle(r, 6.f);
        g.setColour(sel ? kAccent : linked ? kAccent.withAlpha(0.6f) : (sink ? kOk : kEdge));
        g.drawRoundedRectangle(r, 6.f, sel ? 2.2f : 1.2f);
        g.setColour(kText);
        g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        g.drawText(sink ? "OUT" : b.id, b.r.getX() + 8, b.r.getY() + 3, kBoxW - 16, 18, juce::Justification::centredLeft);
        if (!sink) {
            g.setColour(kDim);
            g.setFont(juce::FontOptions(11.f));
            g.drawText(b.type, b.r.getX() + 8, b.r.getY() + 3, kBoxW - 16, 18, juce::Justification::centredRight);
        }
        g.setFont(juce::FontOptions(11.f));
        for (size_t i = 0; i < b.ins.size(); ++i) {
            g.setColour(kDim);
            g.drawText(b.ins[i], b.r.getX() + 8, b.r.getY() + kHead + (int)i * kRow, kBoxW / 2 - 8, kRow,
                       juce::Justification::centredLeft);
        }
        for (size_t i = 0; i < b.outs.size(); ++i) {
            if (sink) break;
            g.setColour(kDim);
            g.drawText(b.outs[i], b.r.getCentreX(), b.r.getY() + kHead + (int)i * kRow, kBoxW / 2 - 8, kRow,
                       juce::Justification::centredRight);
        }
        if (b.extra.isNotEmpty()) {
            g.setColour(kAccent.withAlpha(0.85f));
            g.drawText(b.extra, b.r.getX() + 8, b.r.getBottom() - 16, kBoxW - 16, 14, juce::Justification::centredLeft, true);
        }
    }
    g.setColour(kDim);
    g.setFont(juce::FontOptions(11.f));
    g.drawText("Click a module to highlight its wiring, drag to move.  Left to right = signal flow.  Red wires = feedback (through z1/delay).  Macros are shown as one box.",
               kPad, getHeight() - 16, getWidth() - 2 * kPad, 14, juce::Justification::centredLeft);
}

StructureWindow::StructureWindow(std::function<void()> onClose)
    : DocumentWindow("Zircon - patch structure", kBg, DocumentWindow::closeButton), onClose_(std::move(onClose)) {
    setUsingNativeTitleBar(true);
    view_.setViewedComponent(&canvas_, false);
    view_.setScrollBarsShown(true, true);
    setContentNonOwned(&view_, false);
    setResizable(true, true);
    centreWithSize(900, 520);
}

void StructureWindow::setPatch(const juce::String& patchName, const juce::String& json) {
    setName("Zircon - structure: " + patchName);
    canvas_.setPatch(json);
}

int StructureCanvas::boxAt(juce::Point<int> p) const {
    for (int i = (int)boxes_.size() - 1; i >= 0; --i)
        if (boxes_[(size_t)i].r.contains(p)) return i;
    return -1;
}

void StructureCanvas::mouseDown(const juce::MouseEvent& e) {
    selected_ = boxAt(e.getPosition());
    dragging_ = selected_;
    if (dragging_ >= 0) dragOffset_ = e.getPosition() - boxes_[(size_t)dragging_].r.getPosition();
    repaint();
}

void StructureCanvas::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ < 0) return;
    auto& r = boxes_[(size_t)dragging_].r;
    auto pos = e.getPosition() - dragOffset_;
    r.setPosition(std::max(0, pos.x), std::max(30, pos.y));
    int w = std::max(getWidth(), r.getRight() + kPad), h = std::max(getHeight(), r.getBottom() + kPad);
    if (w != getWidth() || h != getHeight()) setSize(w, h);
    repaint();
}

void StructureCanvas::mouseUp(const juce::MouseEvent&) { dragging_ = -1; }
