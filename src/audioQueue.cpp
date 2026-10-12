#include "audioQueue.h"

#include <cstring>

void prepareAudioQueue(AudioSpscQueue *queue, unsigned int blockCount, unsigned int sampleCount) {
    // Prepares fixed audio blocks for callback-to-judge transfer.
    queue->blocks.resize(blockCount);
    for (unsigned int i = 0; i < blockCount; i++) {
        queue->blocks[i].streamTime = 0.0;
        queue->blocks[i].chartTimeMs = 0.0;
        queue->blocks[i].chartTimeScale = 1.0;
        queue->blocks[i].frames = 0;
        queue->blocks[i].samples.assign(sampleCount, 0);
    }
    queue->readIndex.store(0);
    queue->writeIndex.store(0);
}

bool pushAudioBlock(AudioSpscQueue *queue,
                    MY_TYPE *samples,
                    unsigned int frames,
                    unsigned int sampleCount,
                    double streamTime,
                    double chartTimeMs,
                    double chartTimeScale) {
    // Pushes one input buffer from the audio callback to the judge thread.
    unsigned int write = queue->writeIndex.load(std::memory_order_relaxed);
    unsigned int next = (write + 1) % (unsigned int)queue->blocks.size();
    if (next == queue->readIndex.load(std::memory_order_acquire))
        return false;

    AudioBlock &block = queue->blocks[write];
    block.streamTime = streamTime;
    block.chartTimeMs = chartTimeMs;
    block.chartTimeScale = chartTimeScale;
    block.frames = frames;
    memcpy(block.samples.data(), samples, sampleCount * sizeof(MY_TYPE));
    queue->writeIndex.store(next, std::memory_order_release);
    return true;
}

AudioBlock *frontAudioBlock(AudioSpscQueue *queue) {
    // Returns the next readable audio block for the single judge thread.
    unsigned int read = queue->readIndex.load(std::memory_order_relaxed);
    if (read == queue->writeIndex.load(std::memory_order_acquire))
        return nullptr;
    return &queue->blocks[read];
}

void popAudioBlock(AudioSpscQueue *queue) {
    // Releases the current audio block after judgment work is complete.
    unsigned int read = queue->readIndex.load(std::memory_order_relaxed);
    queue->readIndex.store((read + 1) % (unsigned int)queue->blocks.size(),
                           std::memory_order_release);
}
