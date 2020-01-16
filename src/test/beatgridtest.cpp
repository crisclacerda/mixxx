#include <gtest/gtest.h>
#include <QtDebug>

#include "track/beatgrid.h"
#include "util/memory.h"

namespace {

const double kMaxBeatError = 1e-9;

class BeatGridTest : public testing::Test {
  protected:

    BeatGridTest()
            : m_pTrack(Track::newTemporary()),
              m_iSampleRate(44100),
              m_iFrameSize(2),
              m_iBpm(60),
              m_pGrid(nullptr) {

        m_pTrack->setBpm(m_iBpm);
        m_pTrack->setSampleRate(m_iSampleRate);
        m_pGrid = std::make_unique<BeatGrid>(*m_pTrack, 0);
        m_pGrid->setBpm(m_iBpm);
    }

    TrackPointer m_pTrack;
    int m_iSampleRate;
    int m_iFrameSize;
    double m_iBpm;
    std::unique_ptr<BeatGrid> m_pGrid;
};

TEST_F(BeatGridTest, Scale) {
    TrackPointer pTrack(Track::newTemporary());

    int sampleRate = 44100;
    double bpm = 60.0;
    pTrack->setBpm(bpm);
    pTrack->setSampleRate(sampleRate);

    auto pGrid = std::make_unique<BeatGrid>(*pTrack, 0);
    pGrid->setBpm(bpm);

    EXPECT_DOUBLE_EQ(bpm, pGrid->getBpm());
    pGrid->scale(Beats::DOUBLE);
    EXPECT_DOUBLE_EQ(2 * bpm, pGrid->getBpm());

    pGrid->scale(Beats::HALVE);
    EXPECT_DOUBLE_EQ(bpm, pGrid->getBpm());

    pGrid->scale(Beats::TWOTHIRDS);
    EXPECT_DOUBLE_EQ(bpm * 2 / 3, pGrid->getBpm());

    pGrid->scale(Beats::THREEHALVES);
    EXPECT_DOUBLE_EQ(bpm, pGrid->getBpm());

    pGrid->scale(Beats::THREEFOURTHS);
    EXPECT_DOUBLE_EQ(bpm * 3 / 4, pGrid->getBpm());

    pGrid->scale(Beats::FOURTHIRDS);
    EXPECT_DOUBLE_EQ(bpm, pGrid->getBpm());
}

TEST_F(BeatGridTest, TestNthBeatWhenOnBeat) {
    TrackPointer pTrack(Track::newTemporary());

    int sampleRate = 44100;
    double bpm = 60.1;
    const int kFrameSize = 2;
    pTrack->setBpm(bpm);
    pTrack->setSampleRate(sampleRate);
    double beatLength = (60.0 * sampleRate / bpm) * kFrameSize;

    auto pGrid = std::make_unique<BeatGrid>(*pTrack, 0);
    pGrid->setBpm(bpm);
    // Pretend we're on the 20th beat;
    double position = beatLength * 20;

    // The spec dictates that a value of 0 is always invalid and returns -1
    EXPECT_EQ(-1, pGrid->findNthBeat(position, 0));

    // findNthBeat should return exactly the current beat if we ask for 1 or
    // -1. For all other values, it should return n times the beat length.
    for (int i = 1; i < 20; ++i) {
        EXPECT_NEAR(position + beatLength * (i - 1), pGrid->findNthBeat(position, i), kMaxBeatError);
        EXPECT_NEAR(position + beatLength * (-i + 1), pGrid->findNthBeat(position, -i), kMaxBeatError);
    }

    // Also test prev/next beat calculation.
    double prevBeat, nextBeat;
    pGrid->findPrevNextBeats(position, &prevBeat, &nextBeat);
    EXPECT_NEAR(position, prevBeat, kMaxBeatError);
    EXPECT_NEAR(position + beatLength, nextBeat, kMaxBeatError);

    // Both previous and next beat should return the current position.
    EXPECT_NEAR(position, pGrid->findNextBeat(position), kMaxBeatError);
    EXPECT_NEAR(position, pGrid->findPrevBeat(position), kMaxBeatError);
}

TEST_F(BeatGridTest, TestNthBeatWhenOnBeat_BeforeEpsilon) {
    TrackPointer pTrack(Track::newTemporary());

    int sampleRate = 44100;
    double bpm = 60.1;
    const int kFrameSize = 2;
    pTrack->setBpm(bpm);
    pTrack->setSampleRate(sampleRate);
    double beatLength = (60.0 * sampleRate / bpm) * kFrameSize;

    auto pGrid = std::make_unique<BeatGrid>(*pTrack, 0);
    pGrid->setBpm(bpm);

    // Pretend we're just before the 20th beat.
    const double kClosestBeat = 20 * beatLength;
    double position = kClosestBeat - beatLength * 0.005;

    // The spec dictates that a value of 0 is always invalid and returns -1
    EXPECT_EQ(-1, pGrid->findNthBeat(position, 0));

    // findNthBeat should return exactly the current beat if we ask for 1 or
    // -1. For all other values, it should return n times the beat length.
    for (int i = 1; i < 20; ++i) {
        EXPECT_NEAR(kClosestBeat + beatLength * (i - 1), pGrid->findNthBeat(position, i), kMaxBeatError);
        EXPECT_NEAR(kClosestBeat + beatLength * (-i + 1), pGrid->findNthBeat(position, -i), kMaxBeatError);
    }

    // Also test prev/next beat calculation.
    double prevBeat, nextBeat;
    pGrid->findPrevNextBeats(position, &prevBeat, &nextBeat);
    EXPECT_NEAR(kClosestBeat, prevBeat, kMaxBeatError);
    EXPECT_NEAR(kClosestBeat + beatLength, nextBeat, kMaxBeatError);

    // Both previous and next beat should return the closest beat.
    EXPECT_NEAR(kClosestBeat, pGrid->findNextBeat(position), kMaxBeatError);
    EXPECT_NEAR(kClosestBeat, pGrid->findPrevBeat(position), kMaxBeatError);
}

TEST_F(BeatGridTest, TestNthBeatWhenOnBeat_AfterEpsilon) {
    TrackPointer pTrack(Track::newTemporary());

    int sampleRate = 44100;
    double bpm = 60.1;
    const int kFrameSize = 2;
    pTrack->setBpm(bpm);
    pTrack->setSampleRate(sampleRate);
    double beatLength = (60.0 * sampleRate / bpm) * kFrameSize;

    auto pGrid = std::make_unique<BeatGrid>(*pTrack, 0);
    pGrid->setBpm(bpm);

    // Pretend we're just before the 20th beat.
    const double kClosestBeat = 20 * beatLength;
    double position = kClosestBeat + beatLength * 0.005;

    // The spec dictates that a value of 0 is always invalid and returns -1
    EXPECT_EQ(-1, pGrid->findNthBeat(position, 0));

    // findNthBeat should return exactly the current beat if we ask for 1 or
    // -1. For all other values, it should return n times the beat length.
    for (int i = 1; i < 20; ++i) {
        EXPECT_NEAR(kClosestBeat + beatLength * (i - 1), pGrid->findNthBeat(position, i), kMaxBeatError);
        EXPECT_NEAR(kClosestBeat + beatLength * (-i + 1), pGrid->findNthBeat(position, -i), kMaxBeatError);
    }

    // Also test prev/next beat calculation.
    double prevBeat, nextBeat;
    pGrid->findPrevNextBeats(position, &prevBeat, &nextBeat);
    EXPECT_NEAR(kClosestBeat, prevBeat, kMaxBeatError);
    EXPECT_NEAR(kClosestBeat + beatLength, nextBeat, kMaxBeatError);

    // Both previous and next beat should return the closest beat.
    EXPECT_NEAR(kClosestBeat, pGrid->findNextBeat(position), kMaxBeatError);
    EXPECT_NEAR(kClosestBeat, pGrid->findPrevBeat(position), kMaxBeatError);
}

TEST_F(BeatGridTest, TestNthBeatWhenNotOnBeat) {
    TrackPointer pTrack(Track::newTemporary());
    int sampleRate = 44100;
    double bpm = 60.1;
    const int kFrameSize = 2;
    pTrack->setBpm(bpm);
    pTrack->setSampleRate(sampleRate);
    double beatLength = (60.0 * sampleRate / bpm) * kFrameSize;

    auto pGrid = std::make_unique<BeatGrid>(*pTrack, 0);
    pGrid->setBpm(bpm);

    // Pretend we're half way between the 20th and 21st beat
    double previousBeat = beatLength * 20.0;
    double nextBeat = beatLength * 21.0;
    double position = (nextBeat + previousBeat) / 2.0;

    // The spec dictates that a value of 0 is always invalid and returns -1
    EXPECT_EQ(-1, pGrid->findNthBeat(position, 0));

    // findNthBeat should return multiples of beats starting from the next or
    // previous beat, depending on whether N is positive or negative.
    for (int i = 1; i < 20; ++i) {
        EXPECT_NEAR(nextBeat + beatLength*(i-1), pGrid->findNthBeat(position, i), kMaxBeatError);
        EXPECT_NEAR(previousBeat + beatLength*(-i+1), pGrid->findNthBeat(position, -i), kMaxBeatError);
    }

    // Also test prev/next beat calculation
    double foundPrevBeat, foundNextBeat;
    pGrid->findPrevNextBeats(position, &foundPrevBeat, &foundNextBeat);
    EXPECT_NEAR(previousBeat, foundPrevBeat, kMaxBeatError);
    EXPECT_NEAR(nextBeat, foundNextBeat, kMaxBeatError);
}

TEST_F(BeatGridTest, TestSignature) {
    // Undefined signature must be 4/4
    EXPECT_TRUE(m_pGrid->getSignature() == mixxx::Signature(4,4)) << "If no signature defined, signature must be 4/4";

    // Add signatures and test change
    m_pGrid->setSignature(mixxx::Signature(3,4));
    EXPECT_TRUE(m_pGrid->getSignature() == mixxx::Signature(3,4)) << "Signature must be 3/4";
    m_pGrid->setSignature(mixxx::Signature(5,3));
    EXPECT_TRUE(m_pGrid->getSignature() == mixxx::Signature(5,3)) << "Signature must be 3/4";
}

}  // namespace
