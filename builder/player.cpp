#include "player.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>
#include "dr_flac.h"

namespace hb {
struct CdPlayer::State {
    fs::path folder;
    std::vector<fs::path> tracks;      // track2.flac, track3.flac
    std::vector<int> numbers;
    std::mutex lock;
    std::vector<int16_t> pcm;          // decoded current track, stereo 44.1 kHz
    size_t frame = 0;                  // playback position in frames
    int current = 0;                   // index into tracks
    std::atomic<bool> playing{false}, loading{false};
    int volume = 12;
    SDL_AudioStream* stream = nullptr;
    std::thread loader;
    std::array<float, 5> shown{};
};

static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* s = static_cast<CdPlayer::State*>(user);
    std::lock_guard g(s->lock);
    std::vector<int16_t> out(size_t(std::max(0, additional)) / 2, 0);
    size_t at = 0;
    while (s->playing && at < out.size() && !s->pcm.empty()) {
        size_t frames = std::min((out.size() - at) / 2, s->pcm.size() / 2 - s->frame);
        std::memcpy(&out[at], &s->pcm[s->frame * 2], frames * 4);
        at += frames * 2;
        s->frame += frames;
        if (s->frame * 2 >= s->pcm.size()) break;  // end of track: the UI thread loads the next one
    }
    SDL_PutAudioStreamData(stream, out.data(), int(out.size() * 2));
}

CdPlayer::CdPlayer() : s(new State) {}
CdPlayer::~CdPlayer() { shutdown(); delete s; }

void CdPlayer::set_folder(const fs::path& audio) {
    if (audio == s->folder && !s->tracks.empty()) return;
    s->folder = audio;
    s->tracks.clear();
    s->numbers.clear();
    try {
        auto manifest = read_json(audio / "manifest.json");
        for (auto& t : manifest.at("tracks")) {
            if (t.value("pregap", false)) continue;
            auto file = audio / t.at("path").get<std::string>();
            if (fs::exists(file)) { s->tracks.push_back(file); s->numbers.push_back(t.at("track").get<int>()); }
        }
    } catch (...) {}
}

bool CdPlayer::has_disc() const { return !s->tracks.empty(); }

static void load(CdPlayer::State* s, int index) {
    s->loading = true;
    if (s->loader.joinable()) s->loader.join();
    s->loader = std::thread([s, index] {
        unsigned channels = 0, rate = 0;
        drflac_uint64 frames = 0;
        drflac_int16* data = drflac_open_file_and_read_pcm_frames_s16(s->tracks[index].string().c_str(), &channels, &rate, &frames, nullptr);
        std::vector<int16_t> pcm;
        if (data && channels == 2) pcm.assign(data, data + frames * 2);
        drflac_free(data, nullptr);
        std::lock_guard g(s->lock);
        s->pcm = std::move(pcm);
        s->frame = 0;
        s->current = index;
        s->loading = false;
    });
}

void CdPlayer::play() {
    if (s->tracks.empty() || s->playing) return;
    if (!s->stream) {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 44100};
        s->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, s);
        if (!s->stream) return;
        SDL_SetAudioStreamGain(s->stream, float(s->volume * s->volume) / 400.0f);
        SDL_ResumeAudioStreamDevice(s->stream);
    }
    if (s->pcm.empty() && !s->loading) load(s, s->current < int(s->tracks.size()) ? s->current : 0);
    s->playing = true;
}

void CdPlayer::stop() {
    s->playing = false;
    std::lock_guard g(s->lock);
    s->frame = 0;
    if (s->stream) SDL_ClearAudioStream(s->stream);
}

bool CdPlayer::playing() const { return s->playing; }

void CdPlayer::volume(int delta) {
    s->volume = std::clamp(s->volume + delta, 0, 20);
    if (s->stream) SDL_SetAudioStreamGain(s->stream, float(s->volume * s->volume) / 400.0f);
}
int CdPlayer::volume() const { return s->volume; }
int CdPlayer::track() const { return s->numbers.empty() ? 2 : s->numbers[size_t(s->current) % s->numbers.size()]; }

double CdPlayer::seconds() const {
    std::lock_guard g(s->lock);
    return s->frame / 44100.0;
}

std::array<float, 5> CdPlayer::levels() {
    std::array<float, 5> raw{};
    bool next = false;
    {
        std::lock_guard g(s->lock);
        if (s->playing && !s->pcm.empty()) {
            next = s->frame * 2 >= s->pcm.size();
            // Goertzel over the next 1024 frames (what is about to be heard).
            const int n = 1024;
            const double freqs[5] = {60, 250, 1000, 4000, 12000};
            size_t start = std::min(s->frame, s->pcm.size() / 2 > size_t(n) ? s->pcm.size() / 2 - n : 0);
            for (int b = 0; b < 5; ++b) {
                double w = 2 * 3.14159265358979 * freqs[b] / 44100, coeff = 2 * std::cos(w), q1 = 0, q2 = 0;
                for (int i = 0; i < n && (start + i) * 2 + 1 < s->pcm.size(); ++i) {
                    double hann = 0.5 - 0.5 * std::cos(2 * 3.14159265358979 * i / (n - 1));
                    double x = (s->pcm[(start + i) * 2] + s->pcm[(start + i) * 2 + 1]) / 65536.0 * hann;
                    double q0 = coeff * q1 - q2 + x;
                    q2 = q1; q1 = q0;
                }
                double power = std::sqrt(std::max(0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2)) / (n / 4.0);
                raw[b] = float(std::clamp((20 * std::log10(power + 1e-9) + 54) / 48, 0.0, 1.0));  // about -54..-6 dB
            }
        }
    }
    if (next && !s->loading) load(s, (s->current + 1) % int(s->tracks.size()));
    for (int b = 0; b < 5; ++b) s->shown[b] = raw[b] > s->shown[b] ? raw[b] : s->shown[b] * 0.86f;  // bars fall slowly
    return s->shown;
}

void CdPlayer::shutdown() {
    s->playing = false;
    if (s->loader.joinable()) s->loader.join();
    if (s->stream) { SDL_DestroyAudioStream(s->stream); s->stream = nullptr; }
    std::lock_guard g(s->lock);
    s->pcm.clear();
    s->pcm.shrink_to_fit();
}
}
