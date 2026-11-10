#pragma once

#include <stdint.h>

void partyBegin();
// Draws the next frame of the party pattern when one is due; true when the
// canvas changed. Does nothing while g_party is PARTY_OFF.
bool partyFrame(uint32_t now);
