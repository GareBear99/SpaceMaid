#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpaceMaidAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit SpaceMaidAudioProcessorEditor (SpaceMaidAudioProcessor&);
    ~SpaceMaidAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SpaceMaidAudioProcessor& audioProcessor;

    juce::Slider blend, size, clarity, motion, mix, output;
    juce::ToggleButton keepPunch;

    juce::Label outLbl, duckLbl, tailLbl;

    using Attach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<Attach> aBlend, aSize, aClarity, aMotion, aMix, aOut;
    std::unique_ptr<BAttach> aPunch;

    void setupKnob(juce::Slider& s, const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceMaidAudioProcessorEditor)
};
