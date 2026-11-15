#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Pure helper functions (no audio / GUI state) so they can be unit-tested.
namespace ParamMath
{
    constexpr int   numPads        = 16;
    constexpr int   maxVoices      = 32;

    constexpr float minBpm         = 20.0f;
    constexpr float maxBpm         = 400.0f;
    constexpr int   maxSemitones   = 24;

    constexpr float minAttackMs    = 1.0f;
    constexpr float minDecayMs     = 1.0f;
    constexpr float minReleaseMs   = 5.0f;
    constexpr float maxEnvMs       = 5000.0f;

    // ---- BPM ---------------------------------------------------------------
    /** Playback speed multiplier: result BPM / start BPM (1 when equal or invalid). */
    inline double stretchRatio (double startBpm, double resultBpm) noexcept
    {
        if (startBpm <= 0.0 || resultBpm <= 0.0)
            return 1.0;
        return resultBpm / startBpm;
    }

    inline double clampBpm (double bpm) noexcept
    {
        return juce::jlimit ((double) minBpm, (double) maxBpm, bpm);
    }

    /** "Diff" entry -> result BPM (clamped to the valid BPM range). */
    inline double resultBpmFromDiff (double startBpm, double diff) noexcept
    {
        return clampBpm (startBpm + diff);
    }

    /** Result BPM entry -> "Diff". */
    inline double diffFromResultBpm (double startBpm, double resultBpm) noexcept
    {
        return resultBpm - startBpm;
    }

    // ---- Pitch -------------------------------------------------------------
    inline int clampSemitones (int st) noexcept
    {
        return juce::jlimit (-maxSemitones, maxSemitones, st);
    }

    /** Result note entry -> semitone shift (clamped to +-24). */
    inline int semitonesFromNotes (int startNote, int resultNote) noexcept
    {
        return clampSemitones (resultNote - startNote);
    }

    /** Semitone entry -> result note. */
    inline int resultNoteFromSemitones (int startNote, int semitones) noexcept
    {
        return startNote + clampSemitones (semitones);
    }

    inline double pitchRatio (double semitones) noexcept
    {
        return std::pow (2.0, semitones / 12.0);
    }

    // ---- Note names (middle C = 60 = "C3", so 36 = "C1") --------------------
    inline juce::String noteName (int midiNote)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = ((midiNote % 12) + 12) % 12;
        const int octave = (int) std::floor (midiNote / 12.0) - 2;
        return juce::String (names[n]) + juce::String (octave);
    }

    /** Parses "C3", "c#3", "Db2", "F#-1" or a plain MIDI number. Returns -1000 if invalid. */
    inline int parseNote (const juce::String& textIn)
    {
        constexpr int invalid = -1000;
        const auto text = textIn.trim().toUpperCase();
        if (text.isEmpty())
            return invalid;

        if (text.containsOnly ("0123456789"))
            return text.getIntValue();

        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const auto letter = text[0];
        if (letter < 'A' || letter > 'G')
            return invalid;

        int semitone = base[letter - 'A'];
        int i = 1;
        while (i < text.length() && (text[i] == '#' || text[i] == 'B'))
        {
            // 'B' right after the letter means "flat" (text is upper-cased)
            semitone += (text[i] == '#') ? 1 : -1;
            ++i;
        }

        const auto octaveText = text.substring (i).trim();
        if (octaveText.isEmpty() || ! octaveText.containsOnly ("-0123456789"))
            return invalid;

        return (octaveText.getIntValue() + 2) * 12 + semitone;
    }

    inline juce::String signedText (double v, int decimals = 0)
    {
        const auto body = decimals > 0 ? juce::String (std::abs (v), decimals)
                                       : juce::String (juce::roundToInt (std::abs (v)));
        if (decimals == 0 && juce::roundToInt (v) == 0) return "0";
        return (v < 0 ? "-" : "+") + body;
    }

    /** Formats a BPM value without trailing zeros (123, 123.5, 99.75). */
    inline juce::String bpmText (double bpm)
    {
        auto s = juce::String (bpm, 2);
        if (s.containsChar ('.'))
        {
            while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
            if (s.endsWithChar ('.'))    s = s.dropLastCharacters (1);
        }
        return s;
    }

    inline juce::String signedBpmText (double diff)
    {
        if (std::abs (diff) < 0.005) return "0";
        return (diff < 0 ? "-" : "+") + bpmText (std::abs (diff));
    }

    // ---- Parameter IDs -----------------------------------------------------
    namespace id
    {
        inline juce::String pad (int padIndex, const char* name)
        {
            return "p" + juce::String (padIndex + 1).paddedLeft ('0', 2) + "_" + name;
        }

        static constexpr const char* note      = "note";
        static constexpr const char* srcBpm    = "srcBpm";
        static constexpr const char* dstBpm    = "dstBpm";
        static constexpr const char* srcPitch  = "srcPitch";
        static constexpr const char* semitones = "semitones";
        static constexpr const char* loop      = "loop";
        static constexpr const char* loopStart = "loopStart";
        static constexpr const char* loopEnd   = "loopEnd";
        static constexpr const char* attack    = "attack";
        static constexpr const char* decay     = "decay";
        static constexpr const char* sustain   = "sustain";
        static constexpr const char* release   = "release";
        static constexpr const char* master    = "masterGain";

        // ---- version 2: per-pad mixer / tone / genre (all 0..100 % unless noted) ----
        static constexpr const char* level     = "level";       // dB, -60 = silent
        static constexpr const char* pan       = "pan";         // -100..100
        static constexpr const char* eqLow     = "eqLow";       // dB
        static constexpr const char* eqMid     = "eqMid";       // dB
        static constexpr const char* eqMidFreq = "eqMidFreq";   // Hz
        static constexpr const char* eqHigh    = "eqHigh";      // dB
        static constexpr const char* drive     = "drive";
        static constexpr const char* sendRev   = "sendRev";
        static constexpr const char* sendDly   = "sendDly";
        static constexpr const char* bloom     = "bloom";
        static constexpr const char* grain     = "grain";
        static constexpr const char* drift     = "drift";
        static constexpr const char* warmth    = "warmth";
        static constexpr const char* dew       = "dew";
        static constexpr const char* pump      = "pump";
        static constexpr const char* wobble    = "wobble";
        static constexpr const char* gate      = "gate";
        static constexpr const char* width     = "width";
        static constexpr const char* crush     = "crush";
        static constexpr const char* dive      = "dive";
        static constexpr const char* formant   = "formant";

        /** The per-pad parameters added in version 2, in the order used everywhere (PadValues, UI, tests). */
        constexpr int numPadFx = 21;
        inline const char* const* padFxNames() noexcept
        {
            static const char* const names[numPadFx] =
            {
                level, pan, eqLow, eqMid, eqMidFreq, eqHigh, drive, sendRev, sendDly,
                bloom, grain, drift, warmth, dew,
                pump, wobble, gate, width, crush, dive, formant
            };
            return names;
        }
        enum PadFx
        {
            fxLevel = 0, fxPan, fxEqLow, fxEqMid, fxEqMidFreq, fxEqHigh, fxDrive, fxSendRev, fxSendDly,
            fxBloom, fxGrain, fxDrift, fxWarmth, fxDew,
            fxPump, fxWobble, fxGate, fxWidth, fxCrush, fxDive, fxFormant
        };

        // ---- version 2: global effects ----
        static constexpr const char* rvSize     = "rvSize";
        static constexpr const char* rvDecay    = "rvDecay";
        static constexpr const char* rvPreDelay = "rvPreDelay";
        static constexpr const char* rvLowCut   = "rvLowCut";
        static constexpr const char* rvHighCut  = "rvHighCut";
        static constexpr const char* rvMix      = "rvMix";
        static constexpr const char* dlSync     = "dlSync";
        static constexpr const char* dlDivision = "dlDivision";
        static constexpr const char* dlTime     = "dlTime";
        static constexpr const char* dlFeedback = "dlFeedback";
        static constexpr const char* dlLowCut   = "dlLowCut";
        static constexpr const char* dlHighCut  = "dlHighCut";
        static constexpr const char* dlPingPong = "dlPingPong";
        static constexpr const char* dlMix      = "dlMix";

        // ---- version 2: shared settings of the genre knobs ----
        static constexpr const char* pumpRate     = "pumpRate";
        static constexpr const char* pumpRelease  = "pumpRelease";
        static constexpr const char* wobbleRate   = "wobbleRate";
        static constexpr const char* gateRate     = "gateRate";
        static constexpr const char* diveRange    = "diveRange";
        static constexpr const char* diveTime     = "diveTime";
        static constexpr const char* formantRange = "formantRange";
        inline juce::String gateStep (int step)   { return "gateStep" + juce::String (step + 1).paddedLeft ('0', 2); }

        // ---- version 2: tempo, modulators, randomiser ----
        static constexpr const char* tempo       = "manualTempo";
        static constexpr const char* rndAmount   = "rndAmount";
        static constexpr const char* rndScope    = "rndScope";
        static constexpr const char* rndAuto     = "rndAuto";
        static constexpr const char* rndInterval = "rndInterval";

        inline juce::String mod (int modIndex, const char* name)   { return "m" + juce::String (modIndex + 1) + "_" + name; }
        inline juce::String modStep (int modIndex, int step)       { return mod (modIndex, "s") + juce::String (step + 1).paddedLeft ('0', 2); }
        static constexpr const char* modType = "type";
        static constexpr const char* modSync = "sync";
        static constexpr const char* modRate = "rate";
        static constexpr const char* modHz   = "hz";
        static constexpr const char* modMode = "mode";
    }

    constexpr int numMods = 4;
}
