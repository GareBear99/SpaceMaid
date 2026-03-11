#include "PluginEditor.h"

SpaceMaidAudioProcessorEditor::SpaceMaidAudioProcessorEditor (SpaceMaidAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (760, 420);

    setupKnob(blend, "Blend");
    setupKnob(size, "Size");
    setupKnob(clarity, "Clarity");
    setupKnob(motion, "Motion");

    setupKnob(mix, "Mix");
    setupKnob(output, "Output");

    keepPunch.setButtonText("Keep Punch");
    addAndMakeVisible(keepPunch);

    outLbl.setJustificationType(juce::Justification::centredLeft);
    duckLbl.setJustificationType(juce::Justification::centredLeft);
    tailLbl.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(outLbl);
    addAndMakeVisible(duckLbl);
    addAndMakeVisible(tailLbl);

    auto& apvts = audioProcessor.apvts;
    aBlend = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::blend, blend);
    aSize  = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::size, size);
    aClarity = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::clarity, clarity);
    aMotion  = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::motion, motion);

    aMix   = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::mix, mix);
    aOut   = std::make_unique<Attach>(apvts, SpaceMaidAudioProcessor::IDs::output, output);

    aPunch = std::make_unique<BAttach>(apvts, SpaceMaidAudioProcessor::IDs::keepPunch, keepPunch);

    startTimerHz(15);
}

SpaceMaidAudioProcessorEditor::~SpaceMaidAudioProcessorEditor() {}

void SpaceMaidAudioProcessorEditor::setupKnob(juce::Slider& s, const juce::String& name)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    s.setName(name);
    s.setMouseDragSensitivity(160);
    addAndMakeVisible(s);
}

void SpaceMaidAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour(0xff0b0b10));

    g.setColour(juce::Colour(0xffe8e8ff));
    g.setFont(18.0f);
    g.drawText("SpaceMaid", 16, 10, 300, 26, juce::Justification::left);

    g.setFont(12.0f);
    g.setColour(juce::Colour(0xffa8a8c8));
    g.drawText("Automatic Space Blending (Housekeeping for your space)", 16, 34, 600, 18, juce::Justification::left);

    auto label = [&](juce::Slider& s)
    {
        auto r = s.getBounds();
        g.setColour(juce::Colour(0xffc8c8e8));
        g.drawFittedText(s.getName(), r.withY(r.getY()-18).withHeight(18), juce::Justification::centred, 1);
    };

    for (auto* s : { &blend,&size,&clarity,&motion,&mix,&output })
        label(*s);
}

void SpaceMaidAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(14);
    r.removeFromTop(60);

    auto top = r.removeFromTop(220);
    const int big = 170;

    blend.setBounds(top.removeFromLeft(big).reduced(12));
    size.setBounds(top.removeFromLeft(big).reduced(12));
    clarity.setBounds(top.removeFromLeft(big).reduced(12));
    motion.setBounds(top.removeFromLeft(big).reduced(12));

    auto mid = r.removeFromTop(120);
    mix.setBounds(mid.removeFromLeft(170).reduced(12));
    output.setBounds(mid.removeFromLeft(170).reduced(12));

    keepPunch.setBounds(mid.removeFromLeft(220).reduced(12).withHeight(26));

    auto meters = r.removeFromTop(70);
    outLbl.setBounds(meters.removeFromTop(22));
    tailLbl.setBounds(meters.removeFromTop(22));
    duckLbl.setBounds(meters.removeFromTop(22));
}

void SpaceMaidAudioProcessorEditor::timerCallback()
{
    auto toDb = [](float rms){ return 20.0f * std::log10(std::max(rms, 1.0e-12f)); };

    const float outR = audioProcessor.meters.outRms.load(std::memory_order_relaxed);
    const float tailR = audioProcessor.meters.tailRms.load(std::memory_order_relaxed);
    const float duckDb = audioProcessor.meters.duckGrDb.load(std::memory_order_relaxed);

    outLbl.setText("Out RMS: " + juce::String(toDb(outR), 1) + " dB", juce::dontSendNotification);
    tailLbl.setText("Tail RMS: " + juce::String(toDb(tailR), 1) + " dB", juce::dontSendNotification);
    duckLbl.setText("Ducking Gain: " + juce::String(duckDb, 2) + " dB", juce::dontSendNotification);
}
