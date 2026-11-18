// The little CD player on the build screen: plays the game's own CD music (tracks 2 and 3, looped).
#pragma once
#include "util.h"
#include <array>

namespace hb {
class CdPlayer {
public:
    CdPlayer();
    ~CdPlayer();
    void set_folder(const fs::path& audio);  // work/data/audio; empty until the CD has been read
    bool has_disc() const;
    void play();
    void stop();
    bool playing() const;
    void volume(int delta);                  // 0..20
    int volume() const;
    int track() const;                       // 2 or 3
    double seconds() const;                  // position in the current track
    std::array<float, 5> levels();           // 0..1 per band (60 Hz, 250 Hz, 1 kHz, 4 kHz, 12 kHz)
    void shutdown();                         // stop and release the audio device
    struct State;
private:
    State* s;
};
}
