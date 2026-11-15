#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Read-only structure view: draws the patch's modules as boxes and its wires
// as curves (left = signal sources, right = output). Parsed straight from the
// patch JSON, so macros appear as single boxes (their type is shown).
// A full editable node graph is still P2.
class StructureCanvas : public juce::Component {
public:
    void setPatch(const juce::String& json);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    struct Port { juce::String name; };
    struct Box {
        juce::String id, type, extra;      // extra: constants / param info
        std::vector<juce::String> ins, outs;
        int layer = 0;
        juce::Rectangle<int> r;
    };
    struct Wire { int from = -1, to = -1; juce::String fromPort, toPort; bool feedback = false; };

    int findBox(const juce::String& id) const;
    static int rowOf(const std::vector<juce::String>& v, const juce::String& n);
    juce::Point<float> outPoint(const Box&, const juce::String& port) const;
    juce::Point<float> inPoint(const Box&, const juce::String& port) const;

    std::vector<Box> boxes_;
    std::vector<Wire> wires_;
    int boxAt(juce::Point<int>) const;
    int selected_ = -1, dragging_ = -1;
    juce::Point<int> dragOffset_;
    juce::String title_, error_;
};

class StructureWindow : public juce::DocumentWindow {
public:
    explicit StructureWindow(std::function<void()> onClose);
    void setPatch(const juce::String& patchName, const juce::String& json);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    StructureCanvas canvas_;
    juce::Viewport view_;
    std::function<void()> onClose_;
};
