#include <libultraship.h>

#include <Fast3D/interpreter.h>
#include "Engine.h"
#include "port/coop/sync/lockstep_game.h" // COOP: a frame waits for every player's input (#20)
#include "port/coop/test/stop_signal.h" // COOP: clean exit on SIGTERM in test runs (#71)

extern "C" {
#include <sf64mesg.h>
    void Main_SetVIMode(void);
    void Main_Initialize(void);
    void Main_ThreadEntry(void* arg);
    void Lib_FillScreen(u8 setFill);
    void Graphics_ThreadUpdate();
    void AudioThread_CreateTask();
}

extern "C"
void Graphics_PushFrame(Gfx* data) {
    GameEngine::ProcessGfxCommands(data);
}

extern "C" void Timer_Update();

void push_frame() {
    if (CoopSyncGame_HoldFrame()) { // COOP: no game frame and no audio frame without every player's input (#20)
        return;
    }
    Graphics_ThreadUpdate();
    GameEngine::StartAudioFrame();
    GameEngine::Instance->StartFrame();
    Timer_Update();
    // thread5_iteration();
    GameEngine::EndAudioFrame();
}

#ifdef _WIN32
int SDL_main(int argc, char **argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
int main(int argc, char *argv[]) {
#endif
    GameEngine::Create();
    Main_SetVIMode();
    Lib_FillScreen(1);
    Main_Initialize();
    Main_ThreadEntry(NULL);
    // COOP: in a test run a stop signal ends the loop as closing the window does, see stop_signal.h (#71)
    while (WindowIsRunning() && !CoopStopSignal_Requested()) {
        push_frame();
    }
    GameEngine::Instance->Destroy();
    return 0;
}