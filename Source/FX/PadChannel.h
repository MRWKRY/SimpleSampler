#pragma once

#include "DspUtil.h"
#include "Eq3.h"
#include "Drive.h"
#include "Botanica.h"
#include "FutureBass.h"
#include "../ParamMath.h"
#include "../Modulation/TempoSync.h"

namespace fx
{
    /** Current (already modulated) values of one pad's tone / genre knobs, in parameter units. */
    struct PadValues
    {
        float v[ParamMath::id::numPadFx] = {};
        float operator[] (int index) const noexcept     { return v[index]; }
        float amount (int index) const noexcept         { return juce::jlimit (0.0f, 1.0f, v[index] * 0.01f); }
    };

    /** Settings shared by all pads for the tempo-synced genre knobs. */
    struct GenreSettings
    {
        double pumpBeats = 1.0, wobbleBeats = 0.5, gateStepBeats = 0.25;
        float pumpRelease = 0.6f;
        bool gateSteps[TempoSync::numSteps] = {};
    };

    /** Stereo buses the pads mix into. */
    struct MixBuses
    {
        float* mainL; float* mainR;
        float* reverbL; float* reverbR;
        float* delayL; float* delayR;
        float* bloomL; float* bloomR;
    };

    /** One pad's channel strip:
        Grain -> Crush -> Drive -> Warmth -> EQ -> Wobble -> Dew -> Width -> Gate/Pump -> Level/Pan -> sends.
        Any stage whose amount is 0 is skipped completely (true bypass, no CPU). */
    class PadChannel
    {
    public:
        void prepare (double sampleRate, int padIndex)
        {
            sr = sampleRate;
            drift = DriftGenerator ((juce::uint32) padIndex + 1u);
            eq.prepare (sr);
            grain.prepare (sr);
            warmth.prepare (sr);
            dew.prepare (sr);
            wobble.prepare (sr);
            width.prepare (sr);
            reset();
        }

        void reset() noexcept
        {
            eq.reset(); drive.reset(); grain.reset(); warmth.reset(); dew.reset();
            wobble.reset(); width.reset(); crush.reset();
            tail = 0;
            needsSnap = true;
            for (auto& on : stageOn) on = false;
        }

        /** Tells the strip whether voices are sounding; returns true while it has to be processed. */
        bool update (bool hasVoices, int numSamples) noexcept
        {
            if (hasVoices)
                tail = (int) (0.8 * sr);
            else if (tail > 0)
                tail = juce::jmax (0, tail - numSamples);

            const bool active = hasVoices || tail > 0;
            if (! active)
                needsSnap = true;
            return active;
        }

        /** Slow pitch / pan wander for this segment (call before the voices are rendered). */
        void advanceDrift (int numSamples, float amount01) noexcept
        {
            if (amount01 > 0.0f) drift.advance (numSamples, sr, amount01);
            else                 drift.pitchSemitones = drift.pan = 0.0f;
        }
        float getDriftSemitones() const noexcept        { return drift.pitchSemitones; }

        void process (float* left, float* right, int n, const PadValues& p, const GenreSettings& g,
                      double beat, const MixBuses& out) noexcept
        {
            using namespace ParamMath::id;
            const float slow = smoothCoef (n, 0.02f, sr), fast = smoothCoef (n, 0.008f, sr), dyn = smoothCoef (n, 0.003f, sr);

            // ---- targets for this segment
            const float level = dbToGain (p[fxLevel]);
            const float pan = juce::jlimit (-1.0f, 1.0f, p[fxPan] * 0.01f + drift.pan);
            const float angle = (pan + 1.0f) * (pi * 0.25f);
            const float sqrt2 = 1.41421356f;

            float dynamics = 1.0f;
            const float pumpAmount = p.amount (fxPump), gateAmount = p.amount (fxGate);
            if (pumpAmount > 0.0f)
                dynamics *= TempoSync::pumpGain (TempoSync::fraction (beat / g.pumpBeats), pumpAmount, g.pumpRelease);
            if (gateAmount > 0.0f && ! g.gateSteps[TempoSync::gateStepIndex (beat, g.gateStepBeats)])
                dynamics *= 1.0f - gateAmount;

            const float targets[numRamps] =
            {
                p.amount (fxGrain), p.amount (fxCrush), p.amount (fxDrive), p.amount (fxWarmth),
                p.amount (fxWobble), p.amount (fxDew), p.amount (fxWidth),
                dynamics,
                level * std::cos (angle) * sqrt2, level * std::sin (angle) * sqrt2,
                p.amount (fxSendRev), p.amount (fxSendDly), p.amount (fxBloom),
                p[fxEqLow], p[fxEqMid], p[fxEqHigh]
            };

            if (needsSnap)
            {
                for (int i = 0; i < numRamps; ++i) ramps[i].snap (targets[i]);
                needsSnap = false;
            }
            for (int i = 0; i < numRamps; ++i)
                ramps[i].setTarget (targets[i], n, i == rDynamics ? dyn : (i == rGainL || i == rGainR ? fast : slow));

            // ---- insert effects (each one is skipped while its amount is exactly 0)
            if (stage (rGrain))   grain.process (left, right, n, ramps[rGrain]);
            if (stage (rCrush))   crush.process (left, right, n, ramps[rCrush]);
            if (stage (rDrive))   drive.process (left, right, n, ramps[rDrive]);
            if (stage (rWarmth))  warmth.process (left, right, n, ramps[rWarmth]);

            // EQ: gains move once per segment
            const float eqLow = ramps[rEqLow].skip (n), eqMid = ramps[rEqMid].skip (n), eqHigh = ramps[rEqHigh].skip (n);
            const bool eqOn = ! Eq3::isFlat (eqLow, eqMid, eqHigh);
            if (eqOn)
            {
                if (! eqWasOn) eq.reset();
                eq.setGains (eqLow, eqMid, p[fxEqMidFreq], eqHigh);
                eq.process (left, right, n);
            }
            eqWasOn = eqOn;

            if (stage (rWobble))  wobble.process (left, right, n, ramps[rWobble], TempoSync::fraction (beat / g.wobbleBeats));
            if (stage (rDew))     dew.process (left, right, n, ramps[rDew]);
            if (stage (rWidth))   width.process (left, right, n, ramps[rWidth]);

            // ---- gate / pump, fader, pan and sends
            auto& rd = ramps[rDynamics]; auto& rl = ramps[rGainL]; auto& rr = ramps[rGainR];
            auto& rRev = ramps[rSendRev]; auto& rDly = ramps[rSendDly]; auto& bloomRamp = ramps[rBloom];
            const bool sendRev = rRev.current > 0.0f || rRev.step != 0.0f;
            const bool sendDly = rDly.current > 0.0f || rDly.step != 0.0f;
            const bool sendBloom = bloomRamp.current > 0.0f || bloomRamp.step != 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float d = rd.next();
                const float l = left[i] * d * rl.next();
                const float r = right[i] * d * rr.next();
                out.mainL[i] += l;
                out.mainR[i] += r;

                if (sendRev)   { const float s = rRev.next();   out.reverbL[i] += l * s; out.reverbR[i] += r * s; }
                if (sendDly)   { const float s = rDly.next();   out.delayL[i]  += l * s; out.delayR[i]  += r * s; }
                if (sendBloom) { const float s = bloomRamp.next(); out.bloomL[i]  += l * s; out.bloomR[i]  += r * s; }
            }
        }

    private:
        enum RampIndex
        {
            rGrain = 0, rCrush, rDrive, rWarmth, rWobble, rDew, rWidth,
            rDynamics, rGainL, rGainR, rSendRev, rSendDly, rBloom,
            rEqLow, rEqMid, rEqHigh, numRamps
        };

        /** True while the stage must run; clears the effect's memory when it switches on. */
        bool stage (int index) noexcept
        {
            const bool on = ramps[index].current > 0.0f || ramps[index].step != 0.0f;
            if (on && ! stageOn[index])
            {
                switch (index)
                {
                    case rGrain:  grain.reset(); break;
                    case rCrush:  crush.reset(); break;
                    case rDrive:  drive.reset(); break;
                    case rWarmth: warmth.reset(); break;
                    case rWobble: wobble.reset(); break;
                    case rDew:    dew.reset(); break;
                    case rWidth:  width.reset(); break;
                    default: break;
                }
            }
            stageOn[index] = on;
            return on;
        }

        double sr = 44100.0;
        Ramp ramps[numRamps];
        bool stageOn[numRamps] = {};
        bool needsSnap = true, eqWasOn = false;
        int tail = 0;

        Eq3 eq;
        Drive drive;
        Grain grain;
        Warmth warmth;
        Dew dew;
        Wobble wobble;
        Width width;
        Crush crush;
        DriftGenerator drift;
    };
}
