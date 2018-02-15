#include "noisegenerator.h"

#include <stdlib.h>

NoiseGenerator::NoiseGenerator(int sampleCount)
    : mSampleCount(sampleCount) {
}

void NoiseGenerator::reset() {
    mRandomSeed = 0;
    mLastIndex = -1;
}

float NoiseGenerator::get(float alpha) {
    int index = mSampleCount * alpha;
    if (index != mLastIndex) {
        mLastIndex = index;
        mLastValue = frnd(2.0f) - 1.0f;
    }
    return mLastValue;
}

float NoiseGenerator::frnd(float range) {
    return rand_r(&mRandomSeed) / float(RAND_MAX) * range;
}