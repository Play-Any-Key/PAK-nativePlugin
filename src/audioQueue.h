#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

typedef int16_t MY_TYPE;

struct AudioBlock {
    double streamTime;
    double chartTimeMs;
    double chartTimeScale;
    unsigned int frames;
    std::vector<MY_TYPE> samples;
};

struct AudioSpscQueue {
    std::vector<AudioBlock> blocks;
    std::atomic<unsigned int> readIndex;
    std::atomic<unsigned int> writeIndex;
};

void prepareAudioQueue(AudioSpscQueue *queue, unsigned int blockCount, unsigned int sampleCount);
bool pushAudioBlock(AudioSpscQueue *queue,
                    MY_TYPE *samples,
                    unsigned int frames,
                    unsigned int sampleCount,
                    double streamTime,
                    double chartTimeMs,
                    double chartTimeScale);
AudioBlock *frontAudioBlock(AudioSpscQueue *queue);
void popAudioBlock(AudioSpscQueue *queue);
