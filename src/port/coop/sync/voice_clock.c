// The sound thread's voice line, as the radio reads it, see voice_clock.h.

#include "voice_clock.h"

#include <string.h>

#include "global.h"
#include "sf64audio_provisional.h"

// The channel of the voice sequence that plays a line: Audio_UpdateVoice and Audio_GetCurrentVoice
// (src/audio/audio_general.c).
#define VOICE_LINE_CHANNEL 15

bool CoopVoiceClock_Read(CoopVoiceClock* out) {
    const SequencePlayer* player = &gSeqPlayers[SEQ_PLAYER_VOICE];
    const SequenceChannel* channel = player->channels[VOICE_LINE_CHANNEL];

    memset(out, 0, sizeof(*out));
    if (!player->enabled) {
        return false;
    }
    out->tempo = player->tempo;
    out->tempoAcc = player->tempoAcc;
    out->delay = player->delay;
    if (IS_SEQUENCE_CHANNEL_VALID(channel)) {
        out->channelDelay = channel->delay;
        out->lineIo = channel->seqScriptIO[1];
    } else {
        out->lineIo = -2;
    }
    return true;
}

uint8_t* CoopVoiceClock_ProbeByte(void) {
    SequencePlayer* player = &gSeqPlayers[SEQ_PLAYER_VOICE];

    return player->enabled ? (uint8_t*) &player->tempoAcc : NULL;
}
