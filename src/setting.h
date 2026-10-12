#pragma once

#include "plugin_state.h"

class RtAudio;

extern RtAudio *g_adac;
extern PluginState g_state;

// Closes the ASIO driver currently selected through SelectAsioDriver.
void releaseSelectedAsioDriver(void);
