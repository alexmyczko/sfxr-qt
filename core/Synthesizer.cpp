#include "Synthesizer.h"

#include <QDebug>

#include <math.h>

#include "Sound.h"

static const float PI = 3.14159265f;

static const float MASTER_VOL = 0.05f;

static const int NOISE_SAMPLE_COUNT = 32;

Synthesizer::SynthStrategy::~SynthStrategy() {
}

Synthesizer::Synthesizer()
    : mSound(new Sound)
    , mNoiseGenerator(NOISE_SAMPLE_COUNT) {
}

Synthesizer::~Synthesizer() {
}

void Synthesizer::init(const Sound* sound) {
    mSound->fromOther(sound);
    mNoiseGenerator.reset();
    start();
}

void Synthesizer::resetSample(bool restart) {
    if (!restart) {
        mNoiseGenerator.reset();
        phase = 0;
    }
    fperiod = 100.0 / (mSound->baseFrequency() * mSound->baseFrequency() + 0.001);
    fmaxperiod = 100.0 / (mSound->minFrequency() * mSound->minFrequency() + 0.001);
    fslide = 1.0 - pow((double)mSound->slide(), 3.0) * 0.01;
    fdslide = -pow((double)mSound->deltaSlide(), 3.0) * 0.000001;
    square_duty = 0.5f - mSound->squareDuty() * 0.5f;
    square_slide = -mSound->dutySweep() * 0.00005f;
    if (mSound->changeAmount() >= 0.0f) {
        arp_mod = 1.0 - pow((double)mSound->changeAmount(), 2.0) * 0.9;
    } else {
        arp_mod = 1.0 + pow((double)mSound->changeAmount(), 2.0) * 10.0;
    }
    arp_time = 0;
    arp_limit = (int)(pow(1.0f - mSound->changeSpeed(), 2.0f) * 20000 + 32);
    if (mSound->changeSpeed() == 1.0f) {
        arp_limit = 0;
    }
    if (!restart) {
        // reset filter
        fltp = 0.0f;
        fltdp = 0.0f;
        fltw = pow(mSound->lpFilterCutoff(), 3.0f) * 0.1f;
        fltw_d = 1.0f + mSound->lpFilterCutoffSweep() * 0.0001f;
        fltdmp = 5.0f / (1.0f + pow(mSound->lpFilterResonance(), 2.0f) * 20.0f) * (0.01f + fltw);
        if (fltdmp > 0.8f) {
            fltdmp = 0.8f;
        }
        fltphp = 0.0f;
        flthp = pow(mSound->hpFilterCutoff(), 2.0f) * 0.1f;
        flthp_d = 1.0 + mSound->hpFilterCutoffSweep() * 0.0003f;
        // reset vibrato
        vib_phase = 0.0f;
        vib_speed = pow(mSound->vibratoSpeed(), 2.0f) * 0.01f;
        vib_amp = mSound->vibratoDepth() * 0.5f;
        // reset envelope
        env_vol = 0.0f;
        env_stage = Attack;
        env_time = 0;
        env_length[Attack] = int(mSound->attackTime() * mSound->attackTime() * 100000.0f);
        env_length[Sustain] = int(mSound->sustainTime() * mSound->sustainTime() * 100000.0f);
        env_length[Decay] = int(mSound->decayTime() * mSound->decayTime() * 100000.0f);

        fphase = pow(mSound->phaserOffset(), 2.0f) * 1020.0f;
        if (mSound->phaserOffset() < 0.0f) {
            fphase = -fphase;
        }
        fdphase = pow(mSound->phaserSweep(), 2.0f) * 1.0f;
        if (mSound->phaserSweep() < 0.0f) {
            fdphase = -fdphase;
        }
        ipp = 0;
        for (int i = 0; i < PHASER_BUFFER_LENGTH; i++) {
            phaser_buffer[i] = 0.0f;
        }

        rep_time = 0;
        rep_limit = (int)(pow(1.0f - mSound->repeatSpeed(), 2.0f) * 20000 + 32);
        if (mSound->repeatSpeed() == 0.0f) {
            rep_limit = 0;
        }
    }
}

void Synthesizer::start() {
    resetSample(false);
}

bool Synthesizer::synthSample(int length, SynthStrategy* strategy) {
    for (int i = 0; i < length; i++) {
        rep_time++;
        if (rep_limit != 0 && rep_time >= rep_limit) {
            rep_time = 0;
            resetSample(true);
        }

        // frequency envelopes/arpeggios
        arp_time++;
        if (arp_limit != 0 && arp_time >= arp_limit) {
            arp_limit = 0;
            fperiod *= arp_mod;
        }
        fslide += fdslide;
        fperiod *= fslide;
        if (fperiod > fmaxperiod) {
            fperiod = fmaxperiod;
            if (mSound->minFrequency() > 0.0f) {
                return false;
            }
        }
        float rfperiod = fperiod;
        if (vib_amp > 0.0f) {
            vib_phase += vib_speed;
            rfperiod = fperiod * (1.0 + sin(vib_phase) * vib_amp);
        }
        int period = std::max((int)rfperiod, 8);
        square_duty = qBound(0.0f, square_duty + square_slide, 0.5f);
        // volume envelope
        env_time++;
        if (env_time > env_length[env_stage]) {
            if (env_stage == Decay) {
                return false;
            }
            env_time = 0;
            env_stage = EnvelopStage(int(env_stage) + 1);
        }
        switch (env_stage) {
        case Attack:
            env_vol = (float)env_time / env_length[Attack];
            break;
        case Sustain:
            env_vol = 1.0f + pow(1.0f - (float)env_time / env_length[Sustain], 1.0f) * 2.0f * mSound->sustainPunch();
            break;
        case Decay:
            env_vol = 1.0f - (float)env_time / env_length[Decay];
            break;
        }

        // phaser step
        fphase += fdphase;
        int iphase = std::min(abs((int)fphase), PHASER_BUFFER_LENGTH - 1);

        if (flthp_d != 0.0f) {
            flthp *= flthp_d;
            if (flthp < 0.00001f) {
                flthp = 0.00001f;
            }
            if (flthp > 0.1f) {
                flthp = 0.1f;
            }
        }

        float ssample = 0.0f;
        for (int si = 0; si < 8; si++) { // 8x supersampling
            float sample = 0.0f;
            phase++;
            if (phase >= period) {
                phase %= period;
            }
            // base waveform
            float fp = (float)phase / period;
            switch (mSound->waveType()) {
            case 0: // square
                if (fp < square_duty) {
                    sample = 0.5f;
                } else {
                    sample = -0.5f;
                }
                break;
            case 1: // sawtooth
                sample = 1.0f - fp * 2;
                break;
            case 2: // sine
                sample = (float)sin(fp * 2 * PI);
                break;
            case 3: // noise
                sample = mNoiseGenerator.get(fp);
                break;
            }
            // lp filter
            float pp = fltp;
            fltw = qBound(0.0f, fltw * fltw_d, 0.1f);
            if (mSound->lpFilterCutoff() != 1.0f) {
                fltdp += (sample - fltp) * fltw;
                fltdp -= fltdp * fltdmp;
            } else {
                fltp = sample;
                fltdp = 0.0f;
            }
            fltp += fltdp;
            // hp filter
            fltphp += fltp - pp;
            fltphp -= fltphp * flthp;
            sample = fltphp;
            // phaser
            phaser_buffer[ipp % PHASER_BUFFER_LENGTH] = sample;
            sample += phaser_buffer[(ipp - iphase + PHASER_BUFFER_LENGTH) % PHASER_BUFFER_LENGTH];
            ipp = (ipp + 1) % PHASER_BUFFER_LENGTH;
            // final accumulation and envelope application
            ssample += sample * env_vol;
        }
        ssample = ssample / 8 * MASTER_VOL;

        ssample *= 2.0f * mSound->volume();

        strategy->write(ssample);
    }
    return true;
}