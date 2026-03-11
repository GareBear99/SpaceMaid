#pragma once
#include <JuceHeader.h>

class SpaceMaidAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    SpaceMaidAudioProcessor();
    ~SpaceMaidAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    struct IDs
    {
        static constexpr const char* blend     = "blend";
        static constexpr const char* size      = "size";
        static constexpr const char* clarity   = "clarity";
        static constexpr const char* motion    = "motion";
        static constexpr const char* keepPunch = "keepPunch";
        static constexpr const char* mix       = "mix";
        static constexpr const char* output    = "output";
    };

    struct MeterState
    {
        std::atomic<float> outRms { 0.0f };
        std::atomic<float> duckGrDb { 0.0f };
        std::atomic<float> tailRms { 0.0f };
    } meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    static inline float clamp01(float x) { return juce::jlimit(0.0f, 1.0f, x); }
    static inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
    static inline float linToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1.0e-12f)); }

    void updateDSPIfNeeded();
    void analyzeBlock(const juce::AudioBuffer<float>& buffer);

    double sr = 44100.0;
    int maxBlock = 512;
    juce::dsp::ProcessSpec spec;

    // Core reverb
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters revParams;

    // Wet-only FX: gentle mod (chorus-ish)
    juce::dsp::Chorus<float> chorus;

    // Wet tail EQ (simple, safe): HP + low-shelf cut + LP
    juce::dsp::IIR::Filter<float> hp[2];
    juce::dsp::IIR::Filter<float> shelf[2];
    juce::dsp::IIR::Filter<float> lp[2];

    // Ducking (wet gain) driven by dry envelope
    float dryEnv = 0.0f;
    float duckEnv = 0.0f;

    juce::dsp::Gain<float> wetGain;
    juce::dsp::Gain<float> outGain;

    juce::AudioBuffer<float> wetBuf;
    juce::AudioBuffer<float> dryCopy;

    // Smoothed macro controls
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMixSm, outputSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> blendSm, sizeSm, claritySm, motionSm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> predelaySm;

    // Predelay implemented via circular buffer (RT-safe)
    juce::AudioBuffer<float> preDelayBuf;
    int preDelayWrite = 0;

    // Analysis metrics (updated per block)
    float rms = 0.0f;
    float crest = 0.0f;
    float transient = 0.0f;

    std::atomic<bool> dspDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceMaidAudioProcessor)
};
