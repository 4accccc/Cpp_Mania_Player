#pragma once
#include <cstdint>
#include <string>

// Original osu! object type for conversion
enum class ObjectType {
    HitCircle,
    Slider,
    Spinner
};

enum class NoteState {
    Waiting,
    Holding,
    Released,  // released mid-hold, can recover
    Hit,
    Missed
};

enum class Judgement {
    None,
    Marvelous,
    Perfect,
    Great,
    Good,
    Bad,
    Miss
};

// osu! SampleSet for hitsounds
enum class SampleSet {
    None = 0,
    Normal = 1,
    Soft = 2,
    Drum = 3
};

struct Note {
    int lane;
    int64_t time;
    bool isHold;
    int64_t endTime;
    NoteState state;
    int64_t nextTickTime;  // for hold note ticks
    int64_t headHitError;  // for hold note combined judgement
    bool hadComboBreak;    // true if released mid-hold or head missed
    bool headHit;          // true if head was hit (pressed in time)
    float x;               // original X coordinate (for conversion)
    ObjectType objectType; // original object type (HitCircle/Slider/Spinner)
    int spanCount;         // for Slider: number of spans
    int segmentDuration;   // for Slider: duration per span in ms
    bool hasClap;          // HIT_CLAP sound
    bool hasFinish;        // HIT_FINISH sound
    bool isFakeNote;       // SV map fake note (NaN time) - visual only, no judgement
    bool fakeNoteShouldFix; // For fake notes: should position be fixed (extreme SV after endTime)
    float fakeNoteFixedY;  // For fake notes: fixed Y position once appeared
    bool fakeNoteHasFixedY; // Whether fakeNoteFixedY has been set

    // Key sound data (from extras field)
    SampleSet sampleSet;       // Main SampleSet (0=default, 1=Normal, 2=Soft, 3=Drum)
    SampleSet additions;       // Additional SampleSet
    int customIndex;           // Custom sample index
    int volume;                // Volume (0-100, 0 means use timing point volume)
    std::string filename;      // Custom sample filename
    int sampleHandle;          // Audio handle (-1 = no custom sample)

    // For hold notes: tail sound data
    SampleSet tailSampleSet;
    SampleSet tailAdditions;
    int tailCustomIndex;
    int tailVolume;
    std::string tailFilename;
    int tailSampleHandle;

    Note(int lane, int64_t time, bool isHold = false, int64_t endTime = 0)
        : lane(lane), time(time), isHold(isHold), endTime(endTime),
          state(NoteState::Waiting), nextTickTime(0), headHitError(0),
          hadComboBreak(false), headHit(false), x(0),
          objectType(ObjectType::HitCircle), spanCount(1), segmentDuration(0),
          hasClap(false), hasFinish(false), isFakeNote(false),
          fakeNoteShouldFix(false), fakeNoteFixedY(0), fakeNoteHasFixedY(false),
          sampleSet(SampleSet::None), additions(SampleSet::None),
          customIndex(0), volume(0), sampleHandle(-1),
          tailSampleSet(SampleSet::None), tailAdditions(SampleSet::None),
          tailCustomIndex(0), tailVolume(0), tailSampleHandle(-1) {}
};
