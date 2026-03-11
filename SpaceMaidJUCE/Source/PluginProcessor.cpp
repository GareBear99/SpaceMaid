#include "PluginProcessor.h"
#include "PluginEditor.h"

SpaceMaidAudioProcessor::SpaceMaidAudioProcessor()
: AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
  apvts(*this, nullptr, "PARAMS", createParams())
{
    for (auto* id : { IDs::blend, IDs::size, IDs::clarity, IDs::motion, IDs::keepPunch, IDs::mix, IDs::output })
        apvts.addParameterListener(id, this);

    revParams.roomSize = 0.5f;
    revParams.damping  = 0.5f;
    revParams.wetLevel = 0.35f;
    revParams.dryLevel = 0.0f;
    revParams.width    = 1.0f;
    revParams.freezeMode = 0.0f;
}

SpaceMaidAudioProcessor::~SpaceMaidAudioProcessor()
{
    for (auto* p : apvts.getParameterPointerList())
        apvts.removeParameterListener(p->paramID, this);
}

const juce::String SpaceMaidAudioProcessor::getName() const { return "SpaceMaid"; }
bool SpaceMaidAudioProcessor::acceptsMidi() const { return false; }
bool SpaceMaidAudioProcessor::producesMidi() const { return false; }
bool SpaceMaidAudioProcessor::isMidiEffect() const { return false; }
double SpaceMaidAudioProcessor::getTailLengthSeconds() const { return 1.0; }

int SpaceMaidAudioProcessor::getNumPrograms() { return 1; }
int SpaceMaidAudioProcessor::getCurrentProgram() { return 0; }
void SpaceMaidAudioProcessor::setCurrentProgram (int) {}
const juce::String SpaceMaidAudioProcessor::getProgramName (int) { return {}; }
void SpaceMaidAudioProcessor::changeProgramName (int, const juce::String&) {}

bool SpaceMaidAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SpaceMaidAudioProcessor::createEditor() { return new SpaceMaidAudioProcessorEditor (*this); }

bool SpaceMaidAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getChannelSet(true, 0)  == juce::AudioChannelSet::stereo()
        && layouts.getChannelSet(false, 0) == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorValueTreeState::ParameterLayout SpaceMaidAudioProcessor::createParams()
{
    using P = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<P>(IDs::blend,   "Blend",   juce::NormalisableRange<float>(0.f, 100.f, 0.01f), 35.f));
    layout.add(std::make_unique<P>(IDs::size,    "Size",    juce::NormalisableRange<float>(0.f, 100.f, 0.01f), 45.f));
    layout.add(std::make_unique<P>(IDs::clarity, "Clarity", juce::NormalisableRange<float>(0.f, 100.f, 0.01f), 55.f));
    layout.add(std::make_unique<P>(IDs::motion,  "Motion",  juce::NormalisableRange<float>(0.f, 100.f, 0.01f), 25.f));
    layout.add(std::make_unique<B>(IDs::keepPunch, "Keep Punch", true));

    layout.add(std::make_unique<P>(IDs::mix,     "Mix",     juce::NormalisableRange<float>(0.f, 100.f, 0.01f), 50.f));
    layout.add(std::make_unique<P>(IDs::output,  "Output",  juce::NormalisableRange<float>(-24.f, 24.f, 0.01f), 0.f));

    return layout;
}

void SpaceMaidAudioProcessor::parameterChanged (const juce::String&, float)
{
    dspDirty.store(true, std::memory_order_release);
}

void SpaceMaidAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max(64, samplesPerBlock);

    spec.sampleRate = sr;
    spec.maximumBlockSize = (juce::uint32) maxBlock;
    spec.numChannels = 2;

    reverb.reset();
    chorus.reset();
    chorus.prepare(spec);

    // Chorus defaults (will be controlled by Motion)
    chorus.setCentreDelay(7.0f);
    chorus.setFeedback(0.05f);
    chorus.setMix(0.45f);

    for (int ch=0; ch<2; ++ch)
    {
        hp[ch].reset();
        shelf[ch].reset();
        lp[ch].reset();
    }

    wetGain.prepare(spec);
    outGain.prepare(spec);
    wetGain.setRampDurationSeconds(0.02);
    outGain.setRampDurationSeconds(0.02);

    wetBuf.setSize(2, maxBlock, false, false, true);
    dryCopy.setSize(2, maxBlock, false, false, true);

    // Predelay buffer: up to 120ms
    const int maxDelaySamps = (int) std::ceil(0.12 * sr) + maxBlock + 4;
    preDelayBuf.setSize(2, maxDelaySamps, false, false, true);
    preDelayBuf.clear();
    preDelayWrite = 0;

    wetMixSm.reset(sr, 0.02);
    outputSm.reset(sr, 0.02);

    blendSm.reset(sr, 0.05);
    sizeSm.reset(sr, 0.05);
    claritySm.reset(sr, 0.05);
    motionSm.reset(sr, 0.05);
    predelaySm.reset(sr, 0.05);

    dryEnv = 0.0f;
    duckEnv = 0.0f;

    dspDirty.store(true, std::memory_order_release);
}

void SpaceMaidAudioProcessor::releaseResources() {}

void SpaceMaidAudioProcessor::analyzeBlock(const juce::AudioBuffer<float>& buffer)
{
    // Simple, stable metrics:
    // - rms
    // - crest (peak/rms)
    // - transient proxy (abs diff between short env and longer env)
    double sumSq = 0.0;
    float peak = 0.0f;

    const int n = buffer.getNumSamples();
    for (int ch=0; ch<2; ++ch)
    {
        const float* x = buffer.getReadPointer(ch);
        for (int i=0; i<n; ++i)
        {
            const float v = x[i];
            sumSq += (double)v * (double)v;
            peak = std::max(peak, std::abs(v));
        }
    }

    rms = std::sqrt((float)(sumSq / (double)(n * 2)));
    crest = peak / std::max(rms, 1.0e-6f);

    // transient: compare fast and slow envelope
    float fast = 0.0f, slow = 0.0f;
    const float fastC = std::exp(-1.0f / (0.005f * (float)sr)); // ~5ms
    const float slowC = std::exp(-1.0f / (0.050f * (float)sr)); // ~50ms
    for (int i=0; i<n; ++i)
    {
        float m = 0.5f * (std::abs(buffer.getSample(0, i)) + std::abs(buffer.getSample(1, i)));
        fast = m + fastC * (fast - m);
        slow = m + slowC * (slow - m);
    }
    transient = clamp01((fast - slow) * 6.0f); // scaled into 0..1-ish
}

void SpaceMaidAudioProcessor::updateDSPIfNeeded()
{
    // Even if no knob changes, we update macro targets per block from analysis.
    // Coefficients/structural changes are gated by dspDirty.
    const bool dirty = dspDirty.exchange(false, std::memory_order_acq_rel);

    const float blend01   = clamp01(apvts.getRawParameterValue(IDs::blend)->load() / 100.0f);
    const float size01    = clamp01(apvts.getRawParameterValue(IDs::size)->load() / 100.0f);
    const float clarity01 = clamp01(apvts.getRawParameterValue(IDs::clarity)->load() / 100.0f);
    const float motion01  = clamp01(apvts.getRawParameterValue(IDs::motion)->load() / 100.0f);
    const bool keepPunch  = apvts.getRawParameterValue(IDs::keepPunch)->load() > 0.5f;

    blendSm.setTargetValue(blend01);
    sizeSm.setTargetValue(size01);
    claritySm.setTargetValue(clarity01);
    motionSm.setTargetValue(motion01);

    // Auto predelay: more for transient sources and "keep punch"
    // 5..65ms, plus transient bias
    const float baseMs = 5.0f + 45.0f * size01;
    const float tBias  = 30.0f * transient * (keepPunch ? 1.0f : 0.6f);
    predelaySm.setTargetValue(baseMs + tBias);

    // Chorus settings follow Motion (small, wet-only width)
    chorus.setRate(0.08f + 0.35f * motion01);
    chorus.setDepth(0.08f + 0.28f * motion01);
    chorus.setCentreDelay(7.0f + 8.0f * motion01);
    chorus.setFeedback(0.02f + 0.08f * motion01);
    chorus.setMix(0.25f + 0.40f * motion01);

    // Reverb macro from analysis:
    // - sustained sources can take more decay
    // - transient sources use more predelay and stronger ducking
    const float sustain01 = clamp01(1.0f - transient);
    const float density = clamp01(juce::jmap(crest, 1.0f, 8.0f, 0.0f, 1.0f)); // higher crest = more transient

    // Room size and damping
    revParams.roomSize = juce::jlimit(0.05f, 0.98f, 0.20f + 0.70f * size01);
    revParams.damping  = juce::jlimit(0.05f, 0.98f, 0.20f + 0.60f * (1.0f - clarity01)); // cleaner = less damping? (more HF control)
    revParams.width    = juce::jlimit(0.0f, 1.0f, 0.70f + 0.30f * motion01);

    // WetLevel internal (we still do external Mix)
    revParams.wetLevel = juce::jlimit(0.0f, 1.0f, 0.15f + 0.60f * blend01);
    revParams.dryLevel = 0.0f;

    reverb.setParameters(revParams);

    if (dirty)
    {
        // Tail EQ coefficients (safe updates on changes)
        // Highpass stays constant-ish; shelf/LP depend on clarity and blend
        const float hpHz = 120.0f + 120.0f * (1.0f - clarity01); // cleaner -> higher HP on tail
        const float lpHz = 6500.0f + 6500.0f * clarity01;        // lush -> lower LP? actually clarity->higher LP

        // Low shelf cut around 300 Hz to remove mud; stronger when clarity is high
        const float shelfHz = 320.0f;
        const float shelfGainDb = -2.0f - 10.0f * clarity01 * (0.4f + 0.6f * blend01);

        auto hpC = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, hpHz, 0.707f);
        auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, lpHz, 0.707f);
        auto shC = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, shelfHz, 0.707f, dbToLin(shelfGainDb));

        for (int ch=0; ch<2; ++ch)
        {
            *hp[ch].state = *hpC;
            *lp[ch].state = *lpC;
            *shelf[ch].state = *shC;
        }
    }
}

void SpaceMaidAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // Copy dry for mixing + ducking driver
    dryCopy.makeCopyOf(buffer, true);

    analyzeBlock(dryCopy);
    updateDSPIfNeeded();

    const float mix01   = clamp01(apvts.getRawParameterValue(IDs::mix)->load() / 100.0f);
    const float outDb   = apvts.getRawParameterValue(IDs::output)->load();

    wetMixSm.setTargetValue(mix01);
    outputSm.setTargetValue(dbToLin(outDb));

    // Build wet buffer from dry
    wetBuf.makeCopyOf(dryCopy, true);

    // Predelay (wet path only)
    const float pdMs = predelaySm.getCurrentValue();
    const int delaySamps = (int) juce::jlimit(0.0, (double)preDelayBuf.getNumSamples() - 1.0, (pdMs * 0.001) * sr);

    for (int ch=0; ch<2; ++ch)
    {
        float* w = wetBuf.getWritePointer(ch);
        float* dBuf = preDelayBuf.getWritePointer(ch);
        const int bufN = preDelayBuf.getNumSamples();

        int wr = preDelayWrite;
        for (int i=0; i<numSamples; ++i)
        {
            dBuf[wr] = w[i];

            int rd = wr - delaySamps;
            if (rd < 0) rd += bufN;

            w[i] = dBuf[rd];

            if (++wr >= bufN) wr = 0;
        }
        preDelayWrite = wr;
    }

    // Reverb (in-place)
    reverb.processStereo(wetBuf.getWritePointer(0), wetBuf.getWritePointer(1), numSamples);

    // Wet-only chorus/modulation
    {
        juce::dsp::AudioBlock<float> blk(wetBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        chorus.process(ctx);
    }

    // Tail EQ (wet-only)
    for (int ch=0; ch<2; ++ch)
    {
        float* w = wetBuf.getWritePointer(ch);
        for (int i=0; i<numSamples; ++i)
        {
            float x = w[i];
            x = hp[ch].processSample(x);
            x = shelf[ch].processSample(x);
            x = lp[ch].processSample(x);
            w[i] = x;
        }
    }

    // Auto-ducking the wet tail by dry envelope (internal sidechain)
    const float blend01   = blendSm.getCurrentValue();
    const float clarity01 = claritySm.getCurrentValue();
    const bool keepPunch  = apvts.getRawParameterValue(IDs::keepPunch)->load() > 0.5f;

    // Duck strength: stronger when clarity high, blend high, transient high, and keepPunch
    const float duckAmt = clamp01((0.25f + 0.55f * clarity01) * (0.35f + 0.65f * blend01) * (0.35f + 0.65f * transient) * (keepPunch ? 1.0f : 0.75f));

    const float atkMs = 6.0f;
    const float relMs = 120.0f + 120.0f * (1.0f - clarity01);
    const float atkC = std::exp(-1.0f / (0.001f * atkMs * (float)sr));
    const float relC = std::exp(-1.0f / (0.001f * relMs * (float)sr));

    float dryE = dryEnv;
    float duckE = duckEnv;

    double tailSq = 0.0;

    for (int i=0; i<numSamples; ++i)
    {
        const float d = 0.5f * (std::abs(dryCopy.getSample(0, i)) + std::abs(dryCopy.getSample(1, i)));
        // envelope
        const float coeff = (d > dryE) ? atkC : relC;
        dryE = d + coeff * (dryE - d);

        // map envelope to duck gain: louder dry -> lower wet
        // 0..1 env => 1..(1-duckAmt) gain
        const float g = 1.0f - duckAmt * clamp01(dryE * 3.0f); // scale env
        // smooth ducking
        const float dcoeff = (g < duckE) ? atkC : relC;
        duckE = g + dcoeff * (duckE - g);

        for (int ch=0; ch<2; ++ch)
        {
            float* w = wetBuf.getWritePointer(ch);
            w[i] *= duckE;
            tailSq += (double)w[i] * (double)w[i];
        }
    }

    dryEnv = dryE;
    duckEnv = duckE;

    const float tailRms = std::sqrt((float)(tailSq / (double)(numSamples * 2)));
    meters.tailRms.store(tailRms, std::memory_order_relaxed);
    meters.duckGrDb.store(linToDb(std::max(duckE, 1.0e-6f)), std::memory_order_relaxed);

    // Wet/dry mix
    const float mix = wetMixSm.getCurrentValue();
    for (int ch=0; ch<2; ++ch)
    {
        const float* dry = dryCopy.getReadPointer(ch);
        float* wet = wetBuf.getWritePointer(ch);
        for (int i=0; i<numSamples; ++i)
            wet[i] = wet[i] * mix + dry[i] * (1.0f - mix);
    }

    // Output gain
    outGain.setGainLinear(outputSm.getCurrentValue());
    {
        juce::dsp::AudioBlock<float> blk(wetBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        outGain.process(ctx);
    }

    buffer.makeCopyOf(wetBuf, true);

    // Output RMS meter
    double outSq = 0.0;
    for (int ch=0; ch<2; ++ch)
    {
        const float* y = buffer.getReadPointer(ch);
        for (int i=0; i<numSamples; ++i)
            outSq += (double)y[i] * (double)y[i];
    }
    const float outRms = std::sqrt((float)(outSq / (double)(numSamples * 2)));
    meters.outRms.store(outRms, std::memory_order_relaxed);
}

void SpaceMaidAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpaceMaidAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    dspDirty.store(true, std::memory_order_release);
}
