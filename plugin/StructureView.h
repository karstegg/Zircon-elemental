#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

// Editable structure view of the patch: modules as boxes, wires as curves.
//  * drag from a port to another port to create a wire
//  * click a wire or module, press Delete to remove it
//  * drag a module to move it
//  * right-click empty space: add a module; right-click a module: delete / set input constants
// Every edit is serialised back to patch JSON and handed to `onEdit`, which
// recompiles and swaps the live engine (returns an error string, or empty).
struct StructureHooks {
    std::function<void()> onClose;
    std::function<juce::String(const juce::String&)> onEdit;        // json -> error ("" = ok)
    std::function<juce::StringArray()> macroNames;
    std::function<juce::String(const juce::String&)> macroText;     // macro name -> json
};

class StructureCanvas : public juce::Component {
public:
    explicit StructureCanvas(StructureHooks hooks) : hooks_(std::move(hooks)) { setWantsKeyboardFocus(true); }

    void setPatch(const juce::String& json);          // external change (preset switch etc.)
    juce::String currentText() const { return lastCommitted_; }
    std::function<void(const juce::String&)> onStatus;  // error text for the window

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Box {
        juce::String id, type, extra;
        std::vector<juce::String> ins, outs;
        int layer = 0;
        juce::Rectangle<int> r;
    };
    struct Wire { int from = -1, to = -1; juce::String fromPort, toPort; bool feedback = false; int jsonIdx = -1; };
    struct PortHit { int box = -1; juce::String port; bool isOut = false; };

    void rebuild();
    void commit();
    int findBox(const juce::String& id) const;
    int boxAt(juce::Point<int>) const;
    PortHit portAt(juce::Point<int>) const;
    int wireAt(juce::Point<int>) const;
    static int rowOf(const std::vector<juce::String>& v, const juce::String& n);
    juce::Point<float> outPoint(const Box&, const juce::String& port) const;
    juce::Point<float> inPoint(const Box&, const juce::String& port) const;
    void portsOfType(const juce::String& type, std::vector<juce::String>& ins, std::vector<juce::String>& outs) const;

    // edits (mutate patch_, then commit)
    void addWire(const PortHit& out, const PortHit& in);
    void removeWire(int wireIdx);
    void deleteBox(int box);
    void addModule(const juce::String& type, juce::Point<int> where);
    void setConstant(int box, const juce::String& port);
    void showMenu(juce::Point<int> at);

    StructureHooks hooks_;
    juce::var patch_;
    juce::String lastCommitted_, title_, error_;
    std::vector<Box> boxes_;
    std::vector<Wire> wires_;
    std::map<juce::String, juce::Point<int>> userPos_;   // module id -> moved/placed position
    int selected_ = -1, selectedWire_ = -1, dragging_ = -1;
    juce::Point<int> dragOffset_;
    struct { bool active = false; PortHit from; juce::Point<int> pos; } wireDrag_;
};

class StructurePanel;

class StructureWindow : public juce::DocumentWindow {
public:
    explicit StructureWindow(StructureHooks hooks);
    ~StructureWindow() override;
    void setPatch(const juce::String& patchName, const juce::String& json);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    std::function<void()> onClose_;
    std::unique_ptr<StructurePanel> panel_;
};
