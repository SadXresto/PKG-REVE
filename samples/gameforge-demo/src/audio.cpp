// Audio mixer for the NeonDrift demo, using SDL2's audio device. Synthetic.
#include <SDL.h>

#include <cstring>

namespace neondrift {

namespace {

void mixCallback(void* userdata, Uint8* stream, int length)
{
    auto* phase = static_cast<float*>(userdata);
    auto* out = reinterpret_cast<Sint16*>(stream);
    const int samples = length / static_cast<int>(sizeof(Sint16));
    for (int i = 0; i < samples; ++i) {
        *phase += 0.01f;
        out[i] = static_cast<Sint16>(8000.0f * (1.0f - 2.0f * (*phase - static_cast<int>(*phase))));
    }
}

} // namespace

bool startMixer()
{
    SDL_AudioSpec want;
    std::memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    static float phase = 0.0f;
    want.callback = mixCallback;
    want.userdata = &phase;
    return SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0) != 0;
}

} // namespace neondrift
