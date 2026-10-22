#include "sidplay.h"

/* libsidplayfp's installed headers (third_party/sidplay-<platform>/include). */
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidDatabase.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/sidplayfp.h>

#include <cstdio>
#include <cstring>
#include <new>

namespace {

uint8_t rom_kernal[8192], rom_basic[8192], rom_chargen[4096];
bool have_kernal, have_basic, have_chargen;

SidDatabase songlengths;
bool have_songlengths;

const int CYCLES = 5000;        // emulation step: ~5 ms of C64 time
const int PENDING = 4096;       // frames mixed but not yet handed out

void copy_string(char *dst, size_t cap, const char *src)
{
    std::snprintf(dst, cap, "%s", src ? src : "");
}

}

struct sidplay {
    ReSIDfpBuilder builder{"PS64"};     // (declared first: outlives the engine using its SIDs)
    sidplayfp      engine;
    SidTune       *tune = nullptr;
    int16_t        pending[PENDING * 2];
    int            npending = 0, at = 0;
    int            song = 1;
};

extern "C" void sidplay_set_roms(const uint8_t *kernal, const uint8_t *basic, const uint8_t *chargen)
{
    if ((have_kernal = kernal != nullptr))
        std::memcpy(rom_kernal, kernal, sizeof(rom_kernal));
    if ((have_basic = basic != nullptr))
        std::memcpy(rom_basic, basic, sizeof(rom_basic));
    if ((have_chargen = chargen != nullptr))
        std::memcpy(rom_chargen, chargen, sizeof(rom_chargen));
}

extern "C" int sidplay_songlengths(const char *path)
{
    if (have_songlengths)
        return 0;
    have_songlengths = songlengths.open(path);
    return have_songlengths ? 0 : -1;
}

static int start(sidplay *p, int song, char *err, size_t cap)
{
    p->song = (int)p->tune->selectSong((unsigned)song);
    if (!p->engine.load(p->tune)) {
        copy_string(err, cap, p->engine.error());
        return -1;
    }
    p->engine.initMixer(true);
    p->npending = p->at = 0;
    return 0;
}

extern "C" sidplay *sidplay_open(const void *data, size_t len, char *err, size_t cap)
{
    sidplay *p = new (std::nothrow) sidplay;
    if (!p) {
        copy_string(err, cap, "out of memory");
        return nullptr;
    }
    p->tune = new (std::nothrow) SidTune(static_cast<const uint_least8_t *>(data),
                                         static_cast<uint_least32_t>(len));
    if (!p->tune || !p->tune->getStatus()) {
        copy_string(err, cap, p->tune ? p->tune->statusString() : "out of memory");
        sidplay_close(p);
        return nullptr;
    }
    p->engine.setRoms(have_kernal ? rom_kernal : nullptr, have_basic ? rom_basic : nullptr,
                      have_chargen ? rom_chargen : nullptr);
    SidConfig cfg;
    cfg.frequency = SIDPLAY_RATE;
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    cfg.sidEmulation = &p->builder;
    if (!p->engine.config(cfg)) {
        copy_string(err, cap, p->engine.error());
        sidplay_close(p);
        return nullptr;
    }
    if (start(p, 0, err, cap) != 0) {
        sidplay_close(p);
        return nullptr;
    }
    return p;
}

extern "C" void sidplay_close(sidplay *p)
{
    if (!p)
        return;
    delete p->tune;
    delete p;
}

extern "C" int sidplay_select(sidplay *p, int song)
{
    char err[2];
    return p ? start(p, song, err, sizeof(err)) : -1;
}

extern "C" void sidplay_get_info(const sidplay *p, sidplay_info *info)
{
    std::memset(info, 0, sizeof(*info));
    const SidTuneInfo *ti = p->tune->getInfo();
    unsigned n = ti->numberOfInfoStrings();
    copy_string(info->title, sizeof(info->title), n > 0 ? ti->infoString(0) : "");
    copy_string(info->author, sizeof(info->author), n > 1 ? ti->infoString(1) : "");
    copy_string(info->released, sizeof(info->released), n > 2 ? ti->infoString(2) : "");
    info->songs = (int)ti->songs();
    info->start_song = (int)ti->startSong();
    info->song = (int)ti->currentSong();
    switch (ti->sidModel(0)) {
    case SidTuneInfo::SIDMODEL_6581: info->model = SIDPLAY_MODEL_6581; break;
    case SidTuneInfo::SIDMODEL_8580: info->model = SIDPLAY_MODEL_8580; break;
    case SidTuneInfo::SIDMODEL_ANY:  info->model = SIDPLAY_MODEL_ANY; break;
    default:                         info->model = SIDPLAY_MODEL_UNKNOWN; break;
    }
    info->ntsc = ti->clockSpeed() == SidTuneInfo::CLOCK_NTSC;
    info->sids = ti->sidChips();
    copy_string(info->format, sizeof(info->format), ti->formatString());
    char md5[SidTune::MD5_LENGTH + 1];
    copy_string(info->md5, sizeof(info->md5), p->tune->createMD5New(md5));
    info->length_ms = have_songlengths
        ? (int)songlengths.lengthMs(info->md5, (unsigned)info->song) : -1;
    if (info->length_ms <= 0)
        info->length_ms = -1;
}

extern "C" int sidplay_render(sidplay *p, int16_t *stereo, int frames)
{
    int done = 0;
    while (done < frames) {
        if (p->at == p->npending) {
            int samples = p->engine.play(CYCLES);
            if (samples < 0)
                return done ? done : -1;
            p->npending = (int)p->engine.mix(p->pending, (unsigned)samples) / 2;
            p->at = 0;
            if (!p->npending)
                break;
        }
        int n = p->npending - p->at;
        if (n > frames - done)
            n = frames - done;
        std::memcpy(stereo + done * 2, p->pending + p->at * 2, (size_t)n * 4);
        p->at += n;
        done += n;
    }
    return done;
}

extern "C" uint32_t sidplay_time_ms(const sidplay *p)
{
    return (uint32_t)p->engine.timeMs();
}

extern "C" int sidplay_regs(sidplay *p, int n, uint8_t regs[32])
{
    return p->engine.getSidStatus((unsigned)n, regs) ? 0 : -1;
}
