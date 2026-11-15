#include "StructureView.h"
#include "zircon/ops.hpp"
#include <algorithm>
#include <set>

namespace {
const juce::Colour kBg{0xff111318}, kPanel{0xff1a1d24}, kEdge{0xff2a2f3a}, kText{0xffe6e8ee},
    kDim{0xff8a90a0}, kAccent{0xffff8a3d}, kOk{0xff5ad19a}, kErr{0xffff5a6a};
constexpr int kBoxW = 150, kGapX = 90, kGapY = 16, kHead = 24, kRow = 16, kPad = 20, kPortR = 8;

void splitRef(const juce::String& s, juce::String& mod, juce::String& port, const juce::String& defPort) {
    int dot = s.indexOfChar('.');
    mod = dot < 0 ? s : s.substring(0, dot);
    port = dot < 0 ? defPort : s.substring(dot + 1);
}
void addUnique(std::vector<juce::String>& v, const juce::String& s) {
    if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}
juce::DynamicObject* obj(const juce::var& v) { return v.getDynamicObject(); }
} // namespace

// ------------------------------------------------------------------ model helpers
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

void StructureCanvas::portsOfType(const juce::String& type, std::vector<juce::String>& ins,
                                  std::vector<juce::String>& outs) const {
    if (const auto* spec = zircon::findOp(type.toStdString())) {
        for (auto& p : spec->inputs) ins.push_back(p.name);
        for (auto& o : spec->outputs) outs.push_back(o);
        return;
    }
    auto fromDef = [&](const juce::var& def) {
        if (auto* arr = def.getProperty("inputs", {}).getArray())
            for (auto& i : *arr) ins.push_back(i.getProperty("name", {}).toString());
        if (auto* o = def.getProperty("outputs", {}).getDynamicObject())
            for (auto& kv : o->getProperties()) outs.push_back(kv.name.toString());
    };
    auto inlineMacros = patch_.getProperty("macros", {});
    if (auto* mo = inlineMacros.getDynamicObject(); mo != nullptr && mo->hasProperty(type)) {
        fromDef(mo->getProperty(type));
    } else if (hooks_.macroText) {
        auto text = hooks_.macroText(type);
        if (text.isNotEmpty()) fromDef(juce::JSON::parse(text));
    }
}

// ------------------------------------------------------------------ load / rebuild
void StructureCanvas::setPatch(const juce::String& json) {
    if (json == lastCommitted_ && !patch_.isVoid()) return;   // our own edit coming back
    userPos_.clear();
    selected_ = selectedWire_ = dragging_ = -1;
    lastCommitted_ = json;
    patch_ = juce::JSON::parse(json);
    error_.clear();
    rebuild();
}

void StructureCanvas::rebuild() {
    boxes_.clear(); wires_.clear(); title_.clear();
    auto* root = obj(patch_);
    if (root == nullptr) { error_ = "Patch JSON could not be read."; setSize(400, 120); repaint(); return; }
    title_ = root->getProperty("name").toString();
    std::vector<int> jsonIdxOfBox;

    if (auto* mods = root->getProperty("modules").getArray()) {
        for (auto& m : *mods) {
            Box b;
            b.id = m.getProperty("id", {}).toString();
            b.type = m.getProperty("type", {}).toString();
            portsOfType(b.type, b.ins, b.outs);
            if (auto* ps = m.getProperty("params", {}).getDynamicObject())
                if (ps->hasProperty("name")) b.extra = "\"" + ps->getProperty("name").toString() + "\"";
            if (auto* in = m.getProperty("in", {}).getDynamicObject())
                for (auto& kv : in->getProperties()) {
                    addUnique(b.ins, kv.name.toString());
                    b.extra << (b.extra.isEmpty() ? "" : "  ") << kv.name.toString() << "=" << kv.value.toString();
                }
            boxes_.push_back(std::move(b));
        }
    }
    Box out; out.id = "__out"; out.type = "OUT"; out.ins = {"in"};
    boxes_.push_back(out);

    auto addW = [&](const juce::String& src, const juce::String& dst, int jsonIdx) {
        juce::String sm, sp, dm, dp;
        splitRef(src, sm, sp, "out"); splitRef(dst, dm, dp, "");
        int a = findBox(sm), b = findBox(dm);
        if (a < 0 || b < 0) return;
        if (dp.isEmpty()) dp = boxes_[(size_t)b].ins.empty() ? "in" : boxes_[(size_t)b].ins[0];
        addUnique(boxes_[(size_t)a].outs, sp);
        addUnique(boxes_[(size_t)b].ins, dp);
        wires_.push_back({a, b, sp, dp, false, jsonIdx});
    };
    if (auto* ws = root->getProperty("wires").getArray()) {
        int i = 0;
        for (auto& w : *ws) {
            if (auto* pair = w.getArray(); pair != nullptr && pair->size() >= 2)
                addW((*pair)[0].toString(), (*pair)[1].toString(), i);
            ++i;
        }
    }
    addW(root->getProperty("out").toString(), "__out.in", -1);
    for (auto& b : boxes_) if (b.id != "__out" && b.outs.empty()) b.outs.push_back("out");

    for (auto& w : wires_) {
        const auto& t = boxes_[(size_t)w.to].type;
        w.feedback = (t == "z1" || t == "delay");
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
    int maxLayer = 0;
    for (auto& b : boxes_) if (b.id != "__out") maxLayer = std::max(maxLayer, b.layer);
    boxes_.back().layer = maxLayer + 1;

    std::vector<int> yNext((size_t)maxLayer + 2, kPad + 28);
    int maxX = 0, maxY = 0;
    for (auto& b : boxes_) {
        int rows = std::max({(int)b.ins.size(), (int)b.outs.size(), 1});
        int h = kHead + rows * kRow + (b.extra.isEmpty() ? 4 : 18);
        int x = kPad + b.layer * (kBoxW + kGapX);
        b.r = {x, yNext[(size_t)b.layer], kBoxW, h};
        yNext[(size_t)b.layer] += h + kGapY;
        auto it = userPos_.find(b.id);
        if (it != userPos_.end()) b.r.setPosition(it->second);
        maxX = std::max(maxX, b.r.getRight());
        maxY = std::max(maxY, b.r.getBottom());
    }
    setSize(std::max(maxX + kPad * 2, 600), std::max(maxY + kPad * 2, 300));
    selected_ = selectedWire_ = dragging_ = -1;
    repaint();
}

void StructureCanvas::commit() {
    lastCommitted_ = juce::JSON::toString(patch_, false);
    error_.clear();
    if (hooks_.onEdit) error_ = hooks_.onEdit(lastCommitted_);
    rebuild();
    if (onStatus) onStatus(error_);
}

// ------------------------------------------------------------------ edits
void StructureCanvas::addWire(const PortHit& out, const PortHit& in) {
    auto* root = obj(patch_);
    if (root == nullptr) return;
    const juce::String src = boxes_[(size_t)out.box].id + "." + out.port;
    if (boxes_[(size_t)in.box].id == "__out") {
        root->setProperty("out", src);
    } else {
        if (root->getProperty("wires").getArray() == nullptr) root->setProperty("wires", juce::Array<juce::var>());
        juce::Array<juce::var> pair;
        pair.add(src); pair.add(boxes_[(size_t)in.box].id + "." + in.port);
        root->getProperty("wires").getArray()->add(juce::var(pair));
    }
    commit();
}

void StructureCanvas::removeWire(int wi) {
    if (wi < 0 || wi >= (int)wires_.size()) return;
    int idx = wires_[(size_t)wi].jsonIdx;
    if (idx < 0) {
        error_ = "The wire into OUT can't be deleted. Wire a different module to OUT instead.";
        if (onStatus) onStatus(error_);
        repaint();
        return;
    }
    if (auto* ws = obj(patch_)->getProperty("wires").getArray()) ws->remove(idx);
    commit();
}

void StructureCanvas::deleteBox(int bi) {
    if (bi < 0 || bi >= (int)boxes_.size() || boxes_[(size_t)bi].id == "__out") return;
    const juce::String id = boxes_[(size_t)bi].id;
    auto* root = obj(patch_);
    if (auto* mods = root->getProperty("modules").getArray())
        for (int i = mods->size(); --i >= 0;)
            if ((*mods)[i].getProperty("id", {}).toString() == id) mods->remove(i);
    if (auto* ws = root->getProperty("wires").getArray())
        for (int i = ws->size(); --i >= 0;) {
            auto* pair = (*ws)[i].getArray();
            if (pair == nullptr || pair->size() < 2) continue;
            juce::String m1, p1, m2, p2;
            splitRef((*pair)[0].toString(), m1, p1, "out"); splitRef((*pair)[1].toString(), m2, p2, "");
            if (m1 == id || m2 == id) ws->remove(i);
        }
    juce::String om, op;
    splitRef(root->getProperty("out").toString(), om, op, "out");
    if (om == id) root->setProperty("out", "");
    userPos_.erase(id);
    commit();
}

void StructureCanvas::addModule(const juce::String& type, juce::Point<int> where) {
    auto* root = obj(patch_);
    if (root == nullptr) return;
    if (root->getProperty("modules").getArray() == nullptr) root->setProperty("modules", juce::Array<juce::var>());
    juce::String id = type; int k = 1;
    while (findBox(id) >= 0) id = type + juce::String(++k);
    auto* m = new juce::DynamicObject();
    m->setProperty("id", id);
    m->setProperty("type", type);
    if (type == "param") {
        auto* p = new juce::DynamicObject();
        p->setProperty("name", "Knob"); p->setProperty("min", 0.0); p->setProperty("max", 1.0); p->setProperty("default", 0.5);
        m->setProperty("params", juce::var(p));
    } else if (type == "delay") {
        auto* p = new juce::DynamicObject();
        p->setProperty("max", 0.05);
        m->setProperty("params", juce::var(p));
    }
    root->getProperty("modules").getArray()->add(juce::var(m));
    userPos_[id] = {std::max(0, where.x), std::max(30, where.y)};
    commit();
}

void StructureCanvas::setConstant(int bi, const juce::String& port) {
    if (bi < 0 || bi >= (int)boxes_.size()) return;
    const juce::String id = boxes_[(size_t)bi].id;
    juce::String current;
    if (auto* mods = obj(patch_)->getProperty("modules").getArray())
        for (auto& m : *mods)
            if (m.getProperty("id", {}).toString() == id)
                if (auto* in = m.getProperty("in", {}).getDynamicObject()) current = in->getProperty(port).toString();

    auto* aw = new juce::AlertWindow("Set input constant", id + "." + port + "  (empty = remove)",
                                     juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("v", current);
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<StructureCanvas> self(this);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([self, aw, id, port](int r) {
        if (r != 1 || self == nullptr) return;
        const auto text = aw->getTextEditorContents("v").trim();
        auto* mods = obj(self->patch_)->getProperty("modules").getArray();
        if (mods == nullptr) return;
        for (auto& m : *mods) {
            if (m.getProperty("id", {}).toString() != id) continue;
            auto* mo = obj(m);
            if (mo->getProperty("in").getDynamicObject() == nullptr) mo->setProperty("in", juce::var(new juce::DynamicObject()));
            auto* in = mo->getProperty("in").getDynamicObject();
            if (text.isEmpty()) in->removeProperty(port); else in->setProperty(port, text.getDoubleValue());
            if (in->getProperties().size() == 0) mo->removeProperty("in");
        }
        self->commit();
    }), true);
}

void StructureCanvas::showMenu(juce::Point<int> at) {
    auto actions = std::make_shared<std::vector<std::function<void()>>>();
    juce::Component::SafePointer<StructureCanvas> self(this);
    auto add = [&](juce::PopupMenu& m, const juce::String& text, std::function<void()> fn) {
        actions->push_back(std::move(fn));
        m.addItem((int)actions->size(), text);
    };
    juce::PopupMenu menu;
    int hit = boxAt(at);
    if (hit >= 0 && boxes_[(size_t)hit].id != "__out") {
        selected_ = hit; repaint();
        add(menu, "Delete module '" + boxes_[(size_t)hit].id + "'", [self, hit] { if (self) self->deleteBox(hit); });
        menu.addSeparator();
        for (auto& port : boxes_[(size_t)hit].ins)
            add(menu, "Set constant: " + port + "...", [self, hit, port] { if (self) self->setConstant(hit, port); });
    } else {
        std::map<juce::String, juce::PopupMenu> cats;
        for (auto& spec : zircon::opCatalogue()) {
            actions->push_back([self, t = juce::String(spec.type), at] { if (self) self->addModule(t, at); });
            cats[juce::String(spec.category)].addItem((int)actions->size(), juce::String(spec.type) + "   " + juce::String(spec.doc).substring(0, 48));
        }
        juce::PopupMenu addMenu;
        for (auto& [name, sub] : cats) addMenu.addSubMenu(name, sub);
        if (hooks_.macroNames) {
            juce::PopupMenu macros;
            for (auto& n : hooks_.macroNames()) {
                actions->push_back([self, n, at] { if (self) self->addModule(n, at); });
                macros.addItem((int)actions->size(), n);
            }
            addMenu.addSubMenu("macros", macros);
        }
        menu.addSubMenu("Add module", addMenu);
    }
    menu.showMenuAsync(juce::PopupMenu::Options(), [actions](int r) {
        if (r > 0 && r <= (int)actions->size()) (*actions)[(size_t)r - 1]();
    });
}

// ------------------------------------------------------------------ hit testing / mouse
int StructureCanvas::boxAt(juce::Point<int> p) const {
    for (int i = (int)boxes_.size() - 1; i >= 0; --i)
        if (boxes_[(size_t)i].r.contains(p)) return i;
    return -1;
}

StructureCanvas::PortHit StructureCanvas::portAt(juce::Point<int> p) const {
    const auto pf = p.toFloat();
    for (size_t i = 0; i < boxes_.size(); ++i) {
        const auto& b = boxes_[i];
        for (auto& port : b.ins)
            if (inPoint(b, port).getDistanceFrom(pf) <= kPortR) return {(int)i, port, false};
        if (b.id != "__out")
            for (auto& port : b.outs)
                if (outPoint(b, port).getDistanceFrom(pf) <= kPortR) return {(int)i, port, true};
    }
    return {};
}

int StructureCanvas::wireAt(juce::Point<int> p) const {
    const auto pf = p.toFloat();
    for (size_t i = 0; i < wires_.size(); ++i) {
        const auto& w = wires_[i];
        auto p0 = outPoint(boxes_[(size_t)w.from], w.fromPort);
        auto p1 = inPoint(boxes_[(size_t)w.to], w.toPort);
        float dx = std::max(40.f, std::abs(p1.x - p0.x) * 0.5f);
        for (int s = 0; s <= 24; ++s) {
            float t = s / 24.f, u = 1.f - t;
            juce::Point<float> c1{p0.x + dx, p0.y}, c2{p1.x - dx, p1.y};
            juce::Point<float> q = p0 * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + p1 * (t * t * t);
            if (q.getDistanceFrom(pf) < 6.f) return (int)i;
        }
    }
    return -1;
}

void StructureCanvas::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    const auto p = e.getPosition();
    if (e.mods.isPopupMenu()) { showMenu(p); return; }
    auto ph = portAt(p);
    if (ph.box >= 0) {
        wireDrag_ = {true, ph, p};
        selected_ = selectedWire_ = -1;
        repaint();
        return;
    }
    int b = boxAt(p);
    if (b >= 0) {
        selected_ = b; selectedWire_ = -1; dragging_ = b;
        dragOffset_ = p - boxes_[(size_t)b].r.getPosition();
    } else {
        selected_ = -1; selectedWire_ = wireAt(p); dragging_ = -1;
    }
    repaint();
}

void StructureCanvas::mouseDrag(const juce::MouseEvent& e) {
    if (wireDrag_.active) { wireDrag_.pos = e.getPosition(); repaint(); return; }
    if (dragging_ < 0) return;
    auto& b = boxes_[(size_t)dragging_];
    auto pos = e.getPosition() - dragOffset_;
    b.r.setPosition(std::max(0, pos.x), std::max(30, pos.y));
    userPos_[b.id] = b.r.getPosition();
    int w = std::max(getWidth(), b.r.getRight() + kPad), h = std::max(getHeight(), b.r.getBottom() + kPad);
    if (w != getWidth() || h != getHeight()) setSize(w, h);
    repaint();
}

void StructureCanvas::mouseUp(const juce::MouseEvent& e) {
    dragging_ = -1;
    if (!wireDrag_.active) return;
    auto from = wireDrag_.from;
    wireDrag_.active = false;
    auto to = portAt(e.getPosition());
    if (to.box >= 0 && to.box != from.box && to.isOut != from.isOut)
        addWire(from.isOut ? from : to, from.isOut ? to : from);
    else
        repaint();
}

bool StructureCanvas::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
        if (selectedWire_ >= 0) { removeWire(selectedWire_); return true; }
        if (selected_ >= 0) { deleteBox(selected_); return true; }
    }
    return false;
}

// ------------------------------------------------------------------ paint
void StructureCanvas::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setColour(kText);
    g.setFont(juce::FontOptions(16.f, juce::Font::bold));
    g.drawText(title_.isEmpty() ? "Patch structure" : title_, kPad, 6, getWidth() - 2 * kPad, 22,
               juce::Justification::centredLeft);
    if (boxes_.empty()) return;

    for (size_t wi = 0; wi < wires_.size(); ++wi) {
        auto& w = wires_[wi];
        auto p0 = outPoint(boxes_[(size_t)w.from], w.fromPort);
        auto p1 = inPoint(boxes_[(size_t)w.to], w.toPort);
        float dx = std::max(40.f, std::abs(p1.x - p0.x) * 0.5f);
        juce::Path p;
        p.startNewSubPath(p0);
        p.cubicTo(p0.x + dx, p0.y, p1.x - dx, p1.y, p1.x, p1.y);
        const bool isSel = (int)wi == selectedWire_;
        const bool hot = selected_ >= 0 && (w.from == selected_ || w.to == selected_);
        const float alpha = (selected_ < 0 && selectedWire_ < 0) ? 0.75f : (hot || isSel ? 1.0f : 0.15f);
        g.setColour(isSel ? kText : (w.feedback ? kErr : kAccent).withAlpha(alpha));
        g.strokePath(p, juce::PathStrokeType(hot || isSel ? 2.8f : 1.6f));
        g.setColour((w.feedback ? kErr : kAccent).withAlpha(alpha));
        g.fillEllipse(p0.x - 3, p0.y - 3, 6, 6);
        g.fillEllipse(p1.x - 3, p1.y - 3, 6, 6);
    }
    if (wireDrag_.active) {
        const auto& fb = boxes_[(size_t)wireDrag_.from.box];
        auto a = wireDrag_.from.isOut ? outPoint(fb, wireDrag_.from.port) : inPoint(fb, wireDrag_.from.port);
        g.setColour(kOk);
        g.drawLine({a, wireDrag_.pos.toFloat()}, 2.f);
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
            auto pt = inPoint(b, b.ins[i]);
            g.setColour(kEdge.brighter(0.4f));
            g.fillEllipse(pt.x - 3, pt.y - 3, 6, 6);
            g.setColour(kDim);
            g.drawText(b.ins[i], b.r.getX() + 8, b.r.getY() + kHead + (int)i * kRow, kBoxW / 2 - 8, kRow,
                       juce::Justification::centredLeft);
        }
        if (!sink)
            for (size_t i = 0; i < b.outs.size(); ++i) {
                auto pt = outPoint(b, b.outs[i]);
                g.setColour(kEdge.brighter(0.4f));
                g.fillEllipse(pt.x - 3, pt.y - 3, 6, 6);
                g.setColour(kDim);
                g.drawText(b.outs[i], b.r.getCentreX(), b.r.getY() + kHead + (int)i * kRow, kBoxW / 2 - 8, kRow,
                           juce::Justification::centredRight);
            }
        if (b.extra.isNotEmpty()) {
            g.setColour(kAccent.withAlpha(0.85f));
            g.drawText(b.extra, b.r.getX() + 8, b.r.getBottom() - 16, kBoxW - 16, 14, juce::Justification::centredLeft, true);
        }
    }
}

// ------------------------------------------------------------------ window
class StructurePanel : public juce::Component {
public:
    explicit StructurePanel(StructureHooks hooks) : canvas_(std::move(hooks)) {
        view_.setViewedComponent(&canvas_, false);
        view_.setScrollBarsShown(true, true);
        addAndMakeVisible(view_);
        save_.onClick = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Save patch as JSON", juce::File(), "*.json");
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                      juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this](const juce::FileChooser& fc) {
                                      auto f = fc.getResult();
                                      if (f != juce::File()) f.replaceWithText(canvas_.currentText());
                                  });
        };
        addAndMakeVisible(save_);
        hint_.setText("Drag port to port to wire  |  click + Delete removes  |  right-click: add module / set constant",
                      juce::dontSendNotification);
        hint_.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(hint_);
        status_.setColour(juce::Label::textColourId, kErr);
        addAndMakeVisible(status_);
        canvas_.onStatus = [this](const juce::String& e) { status_.setText(e, juce::dontSendNotification); };
    }
    void resized() override {
        auto b = getLocalBounds();
        auto bar = b.removeFromTop(30).reduced(6, 3);
        save_.setBounds(bar.removeFromRight(110));
        bar.removeFromRight(8);
        hint_.setBounds(bar);
        status_.setBounds(b.removeFromBottom(22).reduced(8, 0));
        view_.setBounds(b);
    }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); }
    StructureCanvas canvas_;
    juce::Viewport view_;
    juce::TextButton save_{"Save patch..."};
    juce::Label hint_, status_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

StructureWindow::StructureWindow(StructureHooks hooks)
    : DocumentWindow("Zircon - patch structure", kBg, DocumentWindow::closeButton), onClose_(hooks.onClose) {
    panel_ = std::make_unique<StructurePanel>(std::move(hooks));
    setUsingNativeTitleBar(true);
    setContentNonOwned(panel_.get(), false);
    setResizable(true, true);
    centreWithSize(960, 600);
}

StructureWindow::~StructureWindow() { clearContentComponent(); }

void StructureWindow::setPatch(const juce::String& patchName, const juce::String& json) {
    setName("Zircon - structure: " + patchName);
    panel_->canvas_.setPatch(json);
}
