//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================
// [sonneveld]
// TODO:
// sound caching ([IKM] please, not right here in this backend module)
// slot id generation id
// pre-determine music sizes
// safer slot look ups (with gen id)
// generate/load mod/midi offsets

#include <math.h>
#include "media/audio/audio_core.h"
#include <unordered_map>
#include "debug/out.h"
#include "media/audio/openaldecoder.h"
#include "util/memory_compat.h"

namespace ags = AGS::Common;
namespace agsdbg = AGS::Common::Debug;

const auto GlobalGainScaling = 0.7f;

static void audio_core_entry();

struct AudioCoreSlot
{
    AudioCoreSlot(int handle, ALuint source, OpenALDecoder decoder) : handle_(handle), source_(source), decoder_(std::move(decoder)) {}

    int handle_ = -1;
    std::atomic<ALuint> source_ {0};

    OpenALDecoder decoder_;
};



static struct 
{
    // Device handle (could be a real hardware, or a service/server)
    ALCdevice *alcDevice = nullptr;
    // Context handle (all OpenAL operations are performed using the current context)
    ALCcontext *alcContext = nullptr;

    // Audio thread: polls sound decoders, feeds OpenAL sources
    std::thread audio_core_thread;
    bool audio_core_thread_running = false;

    // Sound slot id counter
    int nextId = 0;

    // One mutex to lock them all... any operation on individual decoders
    // is done under this only mutex, which means that they are currently
    // polled one by one, any action like pause/resume is also synced.
    std::mutex mixer_mutex_m;
    std::condition_variable mixer_cv;
    std::unordered_map<int, std::unique_ptr<AudioCoreSlot>> slots_;
} g_acore;


// -------------------------------------------------------------------------------------------------
// INIT / SHUTDOWN
// -------------------------------------------------------------------------------------------------

void audio_core_init() 
{
    /* InitAL opens a device and sets up a context using default attributes, making
     * the program ready to call OpenAL functions. */

    /* Open and initialize a device */
    g_acore.alcDevice = alcOpenDevice(nullptr);
    if (!g_acore.alcDevice) { throw std::runtime_error("AudioCore: error opening device"); }

    g_acore.alcContext = alcCreateContext(g_acore.alcDevice, nullptr);
    if (!g_acore.alcContext) { throw std::runtime_error("AudioCore: error creating context"); }

    if (alcMakeContextCurrent(g_acore.alcContext) == ALC_FALSE) { throw std::runtime_error("AudioCore: error setting context"); }

    const ALCchar *name = nullptr;
    if (alcIsExtensionPresent(g_acore.alcDevice, "ALC_ENUMERATE_ALL_EXT"))
        name = alcGetString(g_acore.alcDevice, ALC_ALL_DEVICES_SPECIFIER);
    if (!name || alcGetError(g_acore.alcDevice) != AL_NO_ERROR)
        name = alcGetString(g_acore.alcDevice, ALC_DEVICE_SPECIFIER);
    agsdbg::Printf(ags::kDbgMsg_Info, "AudioCore: opened device \"%s\"\n", name);

    // SDL_Sound
    Sound_Init();

    g_acore.audio_core_thread_running = true;
#if !defined(AGS_DISABLE_THREADS)
    g_acore.audio_core_thread = std::thread(audio_core_entry);
#endif
}

void audio_core_shutdown()
{
    g_acore.audio_core_thread_running = false;
#if !defined(AGS_DISABLE_THREADS)
    if (g_acore.audio_core_thread.joinable())
        g_acore.audio_core_thread.join();
#endif

    // dispose all the active slots
    g_acore.slots_.clear();

    // SDL_Sound
    Sound_Quit();

    alcMakeContextCurrent(nullptr);
    if(g_acore.alcContext) {
        alcDestroyContext(g_acore.alcContext);
        g_acore.alcContext = nullptr;
    }

    if (g_acore.alcDevice) {
        alcCloseDevice(g_acore.alcDevice);
        g_acore.alcDevice = nullptr;
    }
}


// -------------------------------------------------------------------------------------------------
// SLOTS
// -------------------------------------------------------------------------------------------------

static int avail_slot_id()
{
    auto result = g_acore.nextId;
    g_acore.nextId += 1;
    return result;
}

int audio_core_slot_init(const std::vector<char> &data, const ags::String &extension_hint, bool repeat)
{
    // TODO: move source gen to OpenALDecoder?
    ALuint source_;
    alGenSources(1, &source_);
    dump_al_errors();

    auto decoder = OpenALDecoder(source_, data, extension_hint, repeat);
    if (!decoder.Init())
        return -1;

    auto handle = avail_slot_id();
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    g_acore.slots_[handle] = std::make_unique<AudioCoreSlot>(handle, source_, std::move(decoder));
    g_acore.mixer_cv.notify_all();

    return handle;
}

// -------------------------------------------------------------------------------------------------
// SLOT CONTROL
// -------------------------------------------------------------------------------------------------

PlaybackState audio_core_slot_play(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    g_acore.slots_[slot_handle]->decoder_.Play();
    auto state = g_acore.slots_[slot_handle]->decoder_.GetPlayState();
    g_acore.mixer_cv.notify_all();
    return state;
}

PlaybackState audio_core_slot_pause(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    g_acore.slots_[slot_handle]->decoder_.Pause();
    auto state = g_acore.slots_[slot_handle]->decoder_.GetPlayState();
    g_acore.mixer_cv.notify_all();
    return state;
}

void audio_core_slot_stop(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    g_acore.slots_[slot_handle]->decoder_.Stop();
    g_acore.slots_.erase(slot_handle);
    g_acore.mixer_cv.notify_all();
}

void audio_core_slot_seek_ms(int slot_handle, float pos_ms)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    g_acore.slots_[slot_handle]->decoder_.Seek(pos_ms);
    g_acore.mixer_cv.notify_all();
}


// -------------------------------------------------------------------------------------------------
// SLOT CONFIG
// -------------------------------------------------------------------------------------------------

void audio_core_set_master_volume(float newvol) 
{
    alListenerf(AL_GAIN, newvol*GlobalGainScaling);
    dump_al_errors();
}

void audio_core_slot_configure(int slot_handle, float volume, float speed, float panning)
{
    ALuint source_ = g_acore.slots_[slot_handle]->source_;
    auto &player = g_acore.slots_[slot_handle]->decoder_;

    alSourcef(source_, AL_GAIN, volume*0.7f);
    dump_al_errors();

    player.SetSpeed(speed);

    if (panning != 0.0f) {
        // https://github.com/kcat/openal-soft/issues/194
        alSourcei(source_, AL_SOURCE_RELATIVE, AL_TRUE);
        dump_al_errors();
        alSource3f(source_, AL_POSITION, panning, 0.0f, -sqrtf(1.0f - panning*panning));
        dump_al_errors();
    }
    else {
        alSourcei(source_, AL_SOURCE_RELATIVE, AL_FALSE);
        dump_al_errors();
        alSource3f(source_, AL_POSITION, 0.0f, 0.0f, 0.0f);
        dump_al_errors();
    }
}

// -------------------------------------------------------------------------------------------------
// SLOT STATUS
// -------------------------------------------------------------------------------------------------

float audio_core_slot_get_pos_ms(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    auto pos = g_acore.slots_[slot_handle]->decoder_.GetPositionMs();
    g_acore.mixer_cv.notify_all();
    return pos;
}

float audio_core_slot_get_duration(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    auto dur = g_acore.slots_[slot_handle]->decoder_.GetDurationMs();
    g_acore.mixer_cv.notify_all();
    return dur;
}

PlaybackState audio_core_slot_get_play_state(int slot_handle)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    auto state = g_acore.slots_[slot_handle]->decoder_.GetPlayState();
    g_acore.mixer_cv.notify_all();
    return state;
}

PlaybackState audio_core_slot_get_play_state(int slot_handle, float &pos, float &pos_ms)
{
    std::lock_guard<std::mutex> lk(g_acore.mixer_mutex_m);
    auto state = g_acore.slots_[slot_handle]->decoder_.GetPlayState();
    pos_ms = g_acore.slots_[slot_handle]->decoder_.GetPositionMs();
    pos = pos_ms; // TODO: separate pos definition per sound type
    g_acore.mixer_cv.notify_all();
    return state;
}


// -------------------------------------------------------------------------------------------------
// AUDIO PROCESSING
// -------------------------------------------------------------------------------------------------

void audio_core_entry_poll()
{
    // burn off any errors for new loop
    dump_al_errors();

    for (auto &entry : g_acore.slots_) {
        auto &slot = entry.second;

        try {
            slot->decoder_.Poll();
        } catch (const std::exception& e) {
            agsdbg::Printf(ags::kDbgMsg_Error, "OpenALDecoder poll exception %s", e.what());
        }
    }
}

#if !defined(AGS_DISABLE_THREADS)
static void audio_core_entry()
{
    std::unique_lock<std::mutex> lk(g_acore.mixer_mutex_m);

    while (g_acore.audio_core_thread_running) {

        audio_core_entry_poll();

        g_acore.mixer_cv.wait_for(lk, std::chrono::milliseconds(50));
    }
}
#endif
