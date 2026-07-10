// =============================================================================
// AUDIO.INO — SATURN 32 — ESP32-S3 + PCM5102
// Driver I2S nouvelle API (ESP32-S3 Arduino Core >= 2.x)
// PCM5102 : DIN=GPIO40  BCK=GPIO41  LCK=GPIO42
// =============================================================================

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <math.h>
#include "config.h"
#include "globals.h"

// =============================================================================
// SCRATCH BUFFER — lecture audio à vitesse variable (effet vinyl)
// Buffer circulaire 1 seconde en PSRAM
// =============================================================================
#define SCRATCH_BUF_LEN  22050  // 1 seconde à 22050Hz
static float*    scratchBuf   = nullptr;
static uint32_t  scratchWrite = 0;       // position d'écriture (avance de 1 chaque sample)
static float     scratchRead  = 0.0f;    // position de lecture flottante (vitesse variable)
float scratchPitchFactor = 1.0f;           // facteur de pitch pour les scratches

// Buffers effets master (globaux — pas sur la stack)
static float    g_flangerBuf[512]  = {0};
static uint32_t g_flangerPos       = 0;
static float    g_vinylSpeed       = 1.0f;
static bool     g_vinylStopped     = false;
static float    g_stutterBufA[512] = {0};
static float    g_stutterBufB[512] = {0};
static bool     g_stutterFillA     = true;
static uint32_t g_stutterFillPos   = 0;

void initScratchBuffer() {
  scratchBuf = (float*)ps_malloc(SCRATCH_BUF_LEN * sizeof(float));
  if (scratchBuf) {
    memset(scratchBuf, 0, SCRATCH_BUF_LEN * sizeof(float));
    // scratchRead commence à SCRATCH_BUF_LEN/2 derrière scratchWrite
    // Ainsi le pointeur de lecture a toujours du contenu disponible derrière lui
    scratchWrite = SCRATCH_BUF_LEN / 2;
    scratchRead  = 0.0f;
  }
}

static i2s_chan_handle_t i2s_tx_handle = nullptr;

// =============================================================================
// FONCTIONS RAPIDES — évite sinf/expf dans la boucle audio (44100Hz × voix)
// =============================================================================
#define SIN_TBL_SIZE 512
static float sinTbl[SIN_TBL_SIZE];

static void buildSinTable() {
  for (int i = 0; i < SIN_TBL_SIZE; i++)
    sinTbl[i] = sinf((float)i * 6.2832f / (float)SIN_TBL_SIZE);
}

// sin rapide : phase 0..1 → [-1..1], lookup + interpolation linéaire
static inline float fastSin(float phase) {
  float p = phase - floorf(phase); // wrap 0..1
  float fi = p * SIN_TBL_SIZE;
  int   i0 = (int)fi & (SIN_TBL_SIZE - 1);
  int   i1 = (i0 + 1) & (SIN_TBL_SIZE - 1);
  float fr = fi - (int)fi;
  return sinTbl[i0] + fr * (sinTbl[i1] - sinTbl[i0]);
}

// expf approché — erreur < 0.2% pour x in [-10..0], parfait pour enveloppes
static inline float fastExp(float x) {
  // Clamper pour éviter overflow
  if (x < -10.0f) return 0.0f;
  if (x >   0.0f) return 1.0f;
  // Approximation polynomiale : exp(x) ≈ (1 + x/256)^256
  x = 1.0f + x * (1.0f / 256.0f);
  x *= x; x *= x; x *= x; x *= x;
  x *= x; x *= x; x *= x; x *= x; // ^256
  return x;
}

// Soft clip rapide sans expf
static inline float fastClip(float x) {
  if (x >  1.0f) return  1.0f - 0.25f / (x + 0.25f);
  if (x < -1.0f) return -1.0f + 0.25f / (-x + 0.25f);
  return x;
}

// ---- Wavetables ----
static float wtbl[4][64];

static void buildWavetables() {
  for (int i = 0; i < 64; i++) {
    float t = (float)i / 64.0f;
    wtbl[0][i] = 0.6f*sinf(t*6.2832f) + 0.3f*sinf(t*12.5664f) + 0.1f*sinf(t*18.8496f);
    wtbl[1][i] = 0.5f*sinf(t*6.2832f) + 0.3f*sinf(t*14.3f)    + 0.2f*sinf(t*23.7f);
    wtbl[2][i] = 0.4f*sinf(t*6.2832f)  + 0.3f*sinf(t*12.5664f)
               + 0.15f*sinf(t*18.8496f) + 0.1f*sinf(t*25.1f) + 0.05f*sinf(t*31.4f);
    wtbl[3][i] = 0.85f*sinf(t*6.2832f) + 0.15f*sinf(t*12.5664f);
  }
  for (int w = 0; w < 4; w++) {
    float mx = 0.0f;
    for (int i = 0; i < 64; i++) if (fabsf(wtbl[w][i]) > mx) mx = fabsf(wtbl[w][i]);
    if (mx > 0.0f) for (int i = 0; i < 64; i++) wtbl[w][i] /= mx;
  }
}

void initAudio() {
  buildSinTable();
  buildWavetables();

  // ── Nouvelle API I2S ESP32-S3 ──────────────────────────────────────────────
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  i2s_new_channel(&chan_cfg, &i2s_tx_handle, nullptr);

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCK_PIN,
      .ws   = (gpio_num_t)I2S_LRCK_PIN,
      .dout = (gpio_num_t)I2S_DIN_PIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };
  i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
  i2s_channel_enable(i2s_tx_handle);

  Serial.println("Audio PCM5102 I2S ESP32-S3 OK");
}

// =============================================================================
// TACHE AUDIO
// =============================================================================
void audioTask(void* param) {
  static int16_t buf[AUDIO_BUF_SAMPLES * 2];
  for (;;) {
    NoteEvent ev;
    while (xQueueReceive(noteQueue, &ev, 0) == pdTRUE) {
      if (ev.noteOff) {
        _releaseVoice(ev.trackIdx, ev.noteIdx, ev.octave);
      } else if (ev.isMetro) {
        if (ev.metroAccent) {
          // Temps fort : bip aigu court (style FL Studio)
          _activateVoice(0, 1500.0f, WAVE_SINE, 1.4f, 0.05f, false, 255, 3);
        } else {
          // Temps faible : bip grave court
          _activateVoice(0, 800.0f, WAVE_SINE, 1.0f, 0.04f, false, 255, 3);
        }
      } else if (ev.trackIdx == TRACK_DRUM) {
        _triggerDrumVoice(ev.noteIdx);
      } else {
        float freq  = getFreqFromNote(ev.noteIdx, ev.octave);
        float decay = calcDecayRate(ev.trackIdx);
        _activateVoice(ev.trackIdx, freq,
                       tracks[ev.trackIdx].waveform,
                       0.80f, decay, ev.sustained,
                       ev.noteIdx, ev.octave);
      }
    }

    for (int i = 0; i < AUDIO_BUF_SAMPLES; i++) {
      float s = synthSample();
      float d = drumSample();

      // ── REVERB SYNTHS — appliquée sur le signal synth seul ──────────────
      if (eff_delay > 0.01f && delayBuf) {
        float dl = delayBuf[delayBufIdx];
        delayBuf[delayBufIdx] = s + dl * 0.25f;
        if (++delayBufIdx >= DELAY_BUF_LEN) delayBufIdx = 0;
        s = s * (1.0f - eff_delay * 0.20f) + dl * eff_delay * 0.20f;
      }

      // ── REVERB DRUM — appliquée sur le signal drum seul ─────────────────
      if (eff_delay_drum > 0.01f && delayBufDrum) {
        float dl = delayBufDrum[delayBufDrumIdx];
        delayBufDrum[delayBufDrumIdx] = d + dl * 0.25f;
        if (++delayBufDrumIdx >= DELAY_BUF_LEN) delayBufDrumIdx = 0;
        d = d * (1.0f - eff_delay_drum * 0.20f) + dl * eff_delay_drum * 0.20f;
      }

      float mix = (s + d) * masterVolume;

      // ── EFFETS MASTER ────────────────────────────────────────────────────
      if (activeEffect >= 0) {
        static uint32_t effectPhase = 0;
        effectPhase++;
        float    intens      = (float)effectIntensity / 8.0f;
        float    bpmF        = (float)(bpm < 40 ? 40 : bpm);
        uint32_t stepSamples = (uint32_t)(SAMPLE_RATE * 15.0f / bpmF);
        if (stepSamples < 256) stepSamples = 256; // garde-fou absolu

        // Buffers statiques pour les effets (déclarés hors switch pour éviter crash C++)
        // Buffers effets : voir variables globales en haut du fichier

        // Reset vinyl speed quand l'effet n'est pas actif
        if (activeEffect != 4) { g_vinylSpeed = 1.0f; g_vinylStopped = false; }

                switch (activeEffect) {

          case 0: { // FILTER LP/HP
            // ei=0→3 : passe-bas  (0=fort, 1=moyen-fort, 2=moyen, 3=leger)
            // ei=4   : neutre (pas de filtre)
            // ei=5→8 : passe-haut (5=leger, 6=moyen, 7=fort, 8=maxi)
            static float filt0LP = 0.0f;
            static float filt0HP = 0.0f;
            int ei = (int)effectIntensity;
            if (ei == 4) {
              filt0LP = 0.0f; filt0HP = 0.0f;
              break;
            }
            if (ei < 4) {
              // LP : fc fixes selon exemple utilisateur
              // ei=0:0.03  ei=1:0.10  ei=2:0.20  ei=3:0.30
              const float lpFC[4] = { 0.03f, 0.10f, 0.20f, 0.30f };
              float fc = lpFC[ei];
              filt0LP += fc * (mix - filt0LP);
              filt0HP = 0.0f;
              mix = filt0LP;
            } else {
              // HP : ei=5→8
              const float hpFC[4] = { 0.05f, 0.15f, 0.30f, 0.50f }; // idx=ei-5
              float fc = hpFC[ei - 5];
              filt0HP += fc * (mix - filt0HP);
              filt0LP = 0.0f;
              mix = mix - filt0HP;
            }
            break;
          }

          case 1: { // GATE — gate LFO sinusoïdal lissé
            float ph   = (float)(effectPhase % stepSamples) / (float)stepSamples;
            float gate = 0.5f + 0.5f * sinf(ph * 2.0f * M_PI);
            mix *= (1.0f - intens) + intens * gate;
            break;
          }

          case 2: { // BIT CRUSH — reduction de bits (16→5) sur 8 niveaux
            int ei = (int)effectIntensity;
            if (ei == 0) break;
            // Level 8 = maxi (ce qu'etait le 7 avant)
            // t lineaire sur les 8 niveaux : 1/8 → 8/8
            float t      = (float)ei / 8.0f;            // 0.125 → 1.0
            float bits   = 16.0f - t * 11.0f;           // 14.6 → 5 bits
            if (bits < 5.0f) bits = 5.0f;
            float levels = powf(2.0f, floorf(bits));
            float q      = 2.0f / levels;
            mix = floorf(mix / q + 0.5f) * q;
            if (mix >  1.0f) mix =  1.0f;
            if (mix < -1.0f) mix = -1.0f;
            break;
          }

          case 3: { // PITCH SHIFT — transposition par octaves
            // ei=0 : oct-4 (x0.0625)   ei=4 : neutre (x1.0)   ei=8 : oct+4 (x16.0)
            // ei=1 : oct-3 (x0.125)    ei=5 : oct+1 (x2.0)
            // ei=2 : oct-2 (x0.25)     ei=6 : oct+2 (x4.0)
            // ei=3 : oct-1 (x0.5)      ei=7 : oct+3 (x8.0)
            // Applique via scratchPitchFactor (synthSample le lit pour activeEffect 2-5)
            static const float PITCH_FACTORS[9] = {
              0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f
            };
            int ei = (int)effectIntensity;
            if (ei < 0) ei = 0;
            if (ei > 8) ei = 8;
            scratchPitchFactor = PITCH_FACTORS[ei];
            // Pas de traitement audio supplémentaire — le pitch est appliqué
            // dans synthSample() via pitchFactor quand activeEffect==3 (range 2-5)
            break;
          }
          case 4: { // VINYL STOP — ralentit le pitch progressivement
            float decel = intens * 0.00005f + 0.000005f;
            if (!g_vinylStopped) {
              g_vinylSpeed -= decel;
              if (g_vinylSpeed < 0.02f) { g_vinylSpeed = 0.02f; g_vinylStopped = true; }
            }
            scratchPitchFactor = g_vinylSpeed;
            mix *= g_vinylSpeed;
            break;
          }

          case 5: { // FLANGER SYNC — flanger synchronisé au BPM
            float ph = (float)(effectPhase % stepSamples) / (float)stepSamples;
            float lfo = 0.5f + 0.5f * sinf(ph * 2.0f * M_PI);
            uint32_t delayLen = (uint32_t)(lfo * intens * 200.0f + 1.0f);
            if (delayLen >= 512) delayLen = 511;
            uint32_t readIdx = (g_flangerPos + 512 - delayLen) % 512;
            float delayed = g_flangerBuf[readIdx];
            g_flangerBuf[g_flangerPos] = mix + delayed * 0.4f;
            g_flangerPos = (g_flangerPos + 1) % 512;
            mix = mix * (1.0f - intens * 0.5f) + delayed * intens * 0.5f;
            break;
          }
          case 6: { // REWIND — séquenceur à l'envers (géré dans sequencer.ino)
            // Audio : volume légèrement pulsé pour l'effet "rewind"
            float ph  = (float)(effectPhase % stepSamples) / (float)stepSamples;
            float env = 1.0f - intens * 0.3f * sinf(ph * 2.0f * M_PI);
            mix *= env;
            break;
          }

          case 7: { // REVERSE — séquenceur à l'envers (géré dans sequencer.ino)
            // Pas de traitement audio supplémentaire
            break;
          }
        }
      }
      // ── FIN EFFETS ───────────────────────────────────────────────────────

      // ── True peak limiter ───────────────────────────────────────────────
      static float limiterGain = 0.80f;
      static float peakEnv     = 0.0f;
      float absMix = mix < 0.0f ? -mix : mix;
      // Décroissance peakEnv : ~300ms (0.9998) était trop lent → ~80ms maintenant
      peakEnv *= 0.9994f;
      if (absMix > peakEnv) peakEnv = absMix;
      float targetGain = (peakEnv > 0.01f) ? (0.80f / peakEnv) : 1.0f;
      if (targetGain > 1.0f) targetGain = 1.0f;
      // Descente rapide si on dépasse, montée agressive quand signal est bas
      if (targetGain < limiterGain)
        limiterGain += (targetGain - limiterGain) * 0.015f;  // descente ~3ms
      else
        limiterGain += (targetGain - limiterGain) * 0.002f;  // montée ~20ms
      // Plancher : jamais en-dessous de 0.15 pour éviter l'inaudible
      if (limiterGain < 0.15f) limiterGain = 0.15f;
      mix *= limiterGain;

      int16_t sample = (int16_t)(mix * 32700.0f);
      buf[i*2]   = sample;
      buf[i*2+1] = sample;
    }

    size_t written = 0;
    i2s_channel_write(i2s_tx_handle, buf, sizeof(buf), &written, portMAX_DELAY);
    // Nourrir le watchdog — évite le reboot si la tâche audio est trop chargée
    vTaskDelay(0);
  }
}

// =============================================================================
// SYNTHESE MELODIQUE
// =============================================================================
float synthSample() {
  float out = 0.0f;
  // Timeout 2ms : _activateVoice depuis seqTask (Core 1) tient le mutex ~10µs.
  // L'ancien timeout de 1 tick (1ms) pouvait expirer sur contention cross-core
  // → synthSample retournait 0.0f → clic/grésil audible.
  if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(2)) != pdTRUE) return 0.0f;

  // Compteur sub-rate : LFO mis à jour tous les 8 samples, chorus tous les 4
  static uint8_t subRateCtr = 0;
  subRateCtr++;

  for (int v = 0; v < MAX_VOICES; v++) {
    if (!voices[v].active) continue;

    uint8_t ti    = voices[v].trackIdx;
    float   ph    = voices[v].phase;
    float   morph = (ti < TRACK_COUNT) ? tracks[ti].waveMorph : 0.5f;
    float   s     = 0.0f;

    // ── GLIDE : utilise le coef précalculé à l'activation ────────────────
    if (voices[v].glideCoef > 0.0001f) {
      voices[v].freq = voices[v].freq * voices[v].glideCoef
                     + voices[v].targetFreq * (1.0f - voices[v].glideCoef);
    } else {
      voices[v].freq = voices[v].targetFreq;
    }
    float freq = voices[v].freq;

    // ── LFO vibrato (sub-rate: toutes les 8 samples) ────────────────────
    float lfoOut = 0.0f;
    if (ti < TRACK_COUNT && tracks[ti].lfoDepth > 0.001f) {
      if ((subRateCtr & 7) == (uint8_t)(v & 7)) { // décalé par voix pour éviter burst
        float lfoHz = 0.05f + tracks[ti].lfoRate * tracks[ti].lfoRate * 11.95f;
        voices[v].lfoPhase += lfoHz * 8.0f / (float)SAMPLE_RATE;
        if (voices[v].lfoPhase >= 1.0f) voices[v].lfoPhase -= 1.0f;
      }
      lfoOut = fastSin(voices[v].lfoPhase) * tracks[ti].lfoDepth;
      if (tracks[ti].lfoDest == 2)
        freq = freq * (1.0f + lfoOut * 0.0595f);
    }

    switch (voices[v].waveform) {
      case WAVE_SINE: {
        float mult = 1.0f + fabsf(morph - 0.5f) * 14.0f;
        float mph  = fmodf(ph * mult, 1.0f);
        float x    = (mph < 0.5f) ? (mph*4.0f-1.0f) : (3.0f-mph*4.0f);
        s = x*(1.0f - 0.25f*(x < 0 ? -x : x));
        break;
      }
      case WAVE_SQUARE: {
        float pw = 0.05f + morph * 0.9f;
        s = (ph < pw) ? 0.9f : -0.9f;
        break;
      }
      case WAVE_SAW: {
        if (morph < 0.5f) {
          float t = morph * 2.0f;
          float saw    =  ph * 2.0f - 1.0f;
          float sawInv = -ph * 2.0f + 1.0f;
          s = sawInv * (1.0f-t) + saw * t;
        } else {
          float t = (morph - 0.5f) * 2.0f;
          float saw = ph * 2.0f - 1.0f;
          float tri = (ph < 0.5f) ? (ph*4.0f-1.0f) : (3.0f-ph*4.0f);
          s = saw * (1.0f-t) + tri * t;
        }
        break;
      }
      case WAVE_NOISE: {
        lfsrState ^= lfsrState<<13; lfsrState ^= lfsrState>>17; lfsrState ^= lfsrState<<5;
        float raw = (float)(int32_t)lfsrState * (1.0f/2147483648.0f);
        static float lpState = 0.0f;
        float lpCoef = 0.05f + morph * 0.9f;
        lpState = lpState*(1.0f-lpCoef) + raw*lpCoef;
        float hpState = raw - lpState;
        if (morph < 0.5f) { float t = morph*2.0f; s = lpState*(1.0f-t) + raw*t; }
        else               { float t = (morph-0.5f)*2.0f; s = raw*(1.0f-t) + hpState*t; }
        break;
      }
      case WAVE_TRIANGLE: {
        float tri    = (ph < 0.5f) ? (ph*4.0f-1.0f) : (3.0f-ph*4.0f);
        float rampUp = ph * 2.0f - 1.0f;
        float rampDn = 1.0f - ph * 2.0f;
        if (morph < 0.5f) { float t = morph*2.0f; s = rampUp*(1.0f-t) + tri*t; }
        else               { float t = (morph-0.5f)*2.0f; s = tri*(1.0f-t) + rampDn*t; }
        break;
      }
      case WAVE_PULSE: {
        float pw = 0.05f + morph * 0.9f;
        s = (ph < pw) ? 0.9f : -0.9f;
        break;
      }
      case WAVE_FM: {
        float ratio  = 0.5f + morph * 13.5f;
        float depth  = (ti < TRACK_COUNT) ? tracks[ti].fmDepth : 0.5f;
        float modSin = fastSin(voices[v].phaseFM);
        float modPh  = ph + depth * modSin;
        modPh = modPh - floorf(modPh);
        float x = (modPh < 0.5f) ? (modPh*4.0f-1.0f) : (3.0f-modPh*4.0f);
        s = x*(1.0f-0.25f*(x<0?-x:x));
        voices[v].phaseFM += freq * ratio / (float)SAMPLE_RATE;
        if (voices[v].phaseFM >= 1.0f) voices[v].phaseFM -= 1.0f;
        break;
      }
      case WAVE_WTBL: {
        float pos  = morph * 3.0f;
        int   wi0  = (int)pos;
        int   wi1  = wi0 + 1;
        float frac = pos - (float)wi0;
        if (wi0 >= 4) { wi0 = 3; wi1 = 3; frac = 0.0f; }
        if (wi1 >= 4) wi1 = 3;
        float idx  = ph * 64.0f;
        int   i0   = (int)idx & 63;
        int   i1   = (i0+1) & 63;
        float phFr = idx - floorf(idx);
        float s0   = wtbl[wi0][i0]*(1.0f-phFr) + wtbl[wi0][i1]*phFr;
        float s1   = wtbl[wi1][i0]*(1.0f-phFr) + wtbl[wi1][i1]*phFr;
        s = s0*(1.0f-frac) + s1*frac;
        break;
      }

      case WAVE_HARM: {
        // Harmonique additive : fondamentale + 3 harmoniques
        // morph 0→1 : ajoute progressivement octave(H2), quinte(H3), double-octave(H4)
        float h2 = morph > 0.0f ? morph         * 0.50f : 0.0f;
        float h3 = morph > 0.33f ? (morph-0.33f)*1.50f * 0.25f : 0.0f;
        float h4 = morph > 0.66f ? (morph-0.66f)*3.00f * 0.12f : 0.0f;
        s = fastSin(ph)
          + h2 * fastSin(ph * 2.0f - (float)(int)(ph * 2.0f))
          + h3 * fastSin(ph * 3.0f - (float)(int)(ph * 3.0f))
          + h4 * fastSin(ph * 4.0f - (float)(int)(ph * 4.0f));
        float norm = 1.0f / (1.0f + h2 + h3 + h4);
        s *= norm;
        break;
      }

      case WAVE_FOLD: {
        // Wave folder : sinus amplifié puis replié sur lui-même
        // morph 0 = sinus pur, morph 1 = très replié (riche en harmoniques impaires)
        float gain = 1.0f + morph * 5.0f;
        float raw  = fastSin(ph) * gain;
        // Pliage : réflexion entre -1 et +1
        raw = raw - 4.0f * floorf((raw + 1.0f) * 0.25f);
        if (raw > 1.0f)  raw =  2.0f - raw;
        if (raw < -1.0f) raw = -2.0f - raw;
        s = raw;
        break;
      }

      case WAVE_WARM: {
        // Saw band-limitée "chaude" : série harmonique de SAW tronquée
        // morph 0 = quasi-sinus (1 harmonique), morph 1 = SAW riche (5 harmoniques)
        int nHarm = 1 + (int)(morph * 4.0f); // 1..5 harmoniques
        float acc = 0.0f;
        float sign = 1.0f;
        for (int h = 1; h <= nHarm; h++) {
          float hph = ph * (float)h - (float)((int)(ph * (float)h));
          acc += sign * fastSin(hph) / (float)h;
          sign = -sign;
        }
        s = acc * (2.0f / 3.14159f); // normalisation
        break;
      }

      case WAVE_SUB: {
        // Sub bass : sinus fondamental + sinus -1 octave
        // morph 0 = fundamental seul, morph 0.5 = 50/50, morph 1 = sub seul
        float subPh2 = voices[v].subPhase; // déjà avancé à freq*0.5
        float sFund  = fastSin(ph);
        float sSub   = fastSin(subPh2);
        s = sFund * (1.0f - morph) + sSub * morph;
        break;
      }

      case WAVE_CHEW: {
        // Chebyshev : polynôme d'ordre variable sur entrée sinusoïdale
        // morph 0 = T1 (sinus), morph 0.33 = T2 (2e harm), morph 0.66 = T3, morph 1 = T4
        float x  = fastSin(ph); // entrée [-1..1]
        float t1 = x;
        float t2 = 2.0f*x*x - 1.0f;
        float t3 = x*(4.0f*x*x - 3.0f);
        float t4 = 8.0f*x*x*x*x - 8.0f*x*x + 1.0f;
        float m  = morph * 3.0f;
        float w1 = 1.0f;
        float w2 = constrain(m,       0.0f, 1.0f);
        float w3 = constrain(m-1.0f,  0.0f, 1.0f);
        float w4 = constrain(m-2.0f,  0.0f, 1.0f);
        s = (t1*w1*(1.0f-w2) + t2*w2*(1.0f-w3) + t3*w3*(1.0f-w4) + t4*w4);
        s = s * 0.85f; // légère atténuation anti-clip
        break;
      }
    }

    // REWIND (6) : pitch bas. SCRATCH (2-5) : pitch variable via scratchPitchFactor
    float pitchFactor = 1.0f;
    if (activeEffect == 6)                         pitchFactor = 0.5f;
    else if (activeEffect >= 2 && activeEffect <= 5) pitchFactor = scratchPitchFactor;
    ph += freq * pitchFactor / (float)SAMPLE_RATE;
    if (ph >= 1.0f) ph -= 1.0f;
    voices[v].phase = ph;

    // ── SOUS-OSCILLATEUR (octave -1) ──────────────────────────────────────
    // subPhase avancé si subOscAmt > 0 OU si la forme d'onde est WAVE_SUB
    if (ti < TRACK_COUNT && (tracks[ti].subOscAmt > 0.001f || voices[v].waveform == WAVE_SUB)) {
      voices[v].subPhase += freq * 0.5f / (float)SAMPLE_RATE;
      if (voices[v].subPhase >= 1.0f) voices[v].subPhase -= 1.0f;
    }
    if (ti < TRACK_COUNT && tracks[ti].subOscAmt > 0.001f && voices[v].waveform != WAVE_SUB) {
      float subSq = (voices[v].subPhase < 0.5f) ? 0.9f : -0.9f;
      s = s * (1.0f - tracks[ti].subOscAmt * 0.5f) + subSq * tracks[ti].subOscAmt * 0.5f;
    }

    // ── OSC2 — second oscillateur superposé ───────────────────────────────
    if (ti < TRACK_COUNT && ti != TRACK_DRUM &&
        tracks[ti].waveform2 != 255 && tracks[ti].osc2Mix > 0.001f) {
      float ph2   = voices[v].osc2Phase;
      float s2    = 0.0f;
      float mix2  = tracks[ti].osc2Mix;
      float morph2 = tracks[ti].waveMorph2; // morph indépendant d'OSC1

      switch (tracks[ti].waveform2) {
        case WAVE_SINE: {
          float mult = 1.0f + fabsf(morph2 - 0.5f) * 14.0f;
          float mph  = fmodf(ph2 * mult, 1.0f);
          float x    = (mph < 0.5f) ? (mph*4.0f-1.0f) : (3.0f-mph*4.0f);
          s2 = x*(1.0f - 0.25f*(x < 0 ? -x : x));
          break;
        }
        case WAVE_SQUARE: {
          float pw = 0.05f + morph2 * 0.9f;
          s2 = (ph2 < pw) ? 0.9f : -0.9f;
          break;
        }
        case WAVE_SAW: {
          if (morph2 < 0.5f) {
            float t = morph2 * 2.0f;
            s2 = (-ph2*2.0f+1.0f)*(1.0f-t) + (ph2*2.0f-1.0f)*t;
          } else {
            float t = (morph2-0.5f)*2.0f;
            float saw = ph2*2.0f-1.0f;
            float tri = (ph2<0.5f)?(ph2*4.0f-1.0f):(3.0f-ph2*4.0f);
            s2 = saw*(1.0f-t)+tri*t;
          }
          break;
        }
        case WAVE_NOISE: {
          lfsrState ^= lfsrState<<13; lfsrState ^= lfsrState>>17; lfsrState ^= lfsrState<<5;
          s2 = (float)(int32_t)lfsrState * (1.0f/2147483648.0f);
          break;
        }
        case WAVE_TRIANGLE: {
          float tri = (ph2<0.5f)?(ph2*4.0f-1.0f):(3.0f-ph2*4.0f);
          float rampUp = ph2*2.0f-1.0f;
          float rampDn = 1.0f-ph2*2.0f;
          if (morph2<0.5f) { float t=morph2*2.0f; s2=rampUp*(1.0f-t)+tri*t; }
          else              { float t=(morph2-0.5f)*2.0f; s2=tri*(1.0f-t)+rampDn*t; }
          break;
        }
        case WAVE_PULSE: {
          float pw = 0.05f + morph2 * 0.9f;
          s2 = (ph2 < pw) ? 0.9f : -0.9f;
          break;
        }
        case WAVE_FM: {
          float ratio  = 0.5f + morph2 * 13.5f;
          float modSin = fastSin(ph2 * ratio);
          float modPh  = ph2 + 0.5f * modSin;
          modPh -= floorf(modPh);
          float x = (modPh<0.5f)?(modPh*4.0f-1.0f):(3.0f-modPh*4.0f);
          s2 = x*(1.0f-0.25f*(x<0?-x:x));
          break;
        }
        case WAVE_WTBL: {
          float pos  = morph2 * 3.0f;
          int   wi0  = (int)pos; if (wi0 >= 4) wi0 = 3;
          int   wi1  = wi0+1;   if (wi1 >= 4) wi1 = 3;
          float frac = pos - (float)wi0;
          float idx2 = ph2 * 64.0f;
          int   i0   = (int)idx2 & 63;
          int   i1   = (i0+1) & 63;
          float fr2  = idx2 - floorf(idx2);
          float sw0  = wtbl[wi0][i0]*(1.0f-fr2) + wtbl[wi0][i1]*fr2;
          float sw1  = wtbl[wi1][i0]*(1.0f-fr2) + wtbl[wi1][i1]*fr2;
          s2 = sw0*(1.0f-frac)+sw1*frac;
          break;
        }
        case WAVE_HARM: {
          float h2 = morph2 > 0.0f    ? morph2            * 0.50f : 0.0f;
          float h3 = morph2 > 0.33f   ? (morph2-0.33f)*1.50f*0.25f : 0.0f;
          float h4 = morph2 > 0.66f   ? (morph2-0.66f)*3.00f*0.12f : 0.0f;
          s2 = fastSin(ph2)
             + h2*fastSin(ph2*2.0f-(float)(int)(ph2*2.0f))
             + h3*fastSin(ph2*3.0f-(float)(int)(ph2*3.0f))
             + h4*fastSin(ph2*4.0f-(float)(int)(ph2*4.0f));
          s2 /= (1.0f+h2+h3+h4);
          break;
        }
        case WAVE_FOLD: {
          float raw = fastSin(ph2) * (1.0f + morph2 * 5.0f);
          raw = raw - 4.0f * floorf((raw+1.0f)*0.25f);
          if (raw >  1.0f) raw =  2.0f - raw;
          if (raw < -1.0f) raw = -2.0f - raw;
          s2 = raw;
          break;
        }
        case WAVE_WARM: {
          int nHarm = 1 + (int)(morph2 * 4.0f);
          float acc = 0.0f; float sign = 1.0f;
          for (int h = 1; h <= nHarm; h++) {
            float hph = ph2*(float)h - (float)((int)(ph2*(float)h));
            acc += sign * fastSin(hph) / (float)h;
            sign = -sign;
          }
          s2 = acc * (2.0f / 3.14159f);
          break;
        }
        case WAVE_SUB: {
          // Pour OSC2 : sub dérivé de ph2 à demi-fréquence
          float subPh2osc = ph2 * 0.5f;
          subPh2osc -= floorf(subPh2osc);
          s2 = fastSin(ph2)*(1.0f-morph2) + fastSin(subPh2osc)*morph2;
          break;
        }
        case WAVE_CHEW: {
          float x = fastSin(ph2);
          float t1=x, t2=2.0f*x*x-1.0f, t3=x*(4.0f*x*x-3.0f), t4=8.0f*x*x*x*x-8.0f*x*x+1.0f;
          float m=morph2*3.0f;
          float w1=1.0f, w2=constrain(m,0.0f,1.0f), w3=constrain(m-1.0f,0.0f,1.0f), w4=constrain(m-2.0f,0.0f,1.0f);
          s2 = (t1*w1*(1.0f-w2)+t2*w2*(1.0f-w3)+t3*w3*(1.0f-w4)+t4*w4)*0.85f;
          break;
        }
        default: s2 = 0.0f; break;
      }

      // Avancer la phase OSC2 (même fréquence qu'OSC1)
      ph2 += freq * pitchFactor / (float)SAMPLE_RATE;
      if (ph2 >= 1.0f) ph2 -= 1.0f;
      voices[v].osc2Phase = ph2;

      // Mix : s = OSC1*(1-mix2) + OSC2*mix2
      s = s * (1.0f - mix2) + s2 * mix2;
    }

    // ── NOISE mélangé ─────────────────────────────────────────────────────
    if (ti < TRACK_COUNT && tracks[ti].noiseAmt > 0.001f) {
      lfsrState ^= lfsrState<<13; lfsrState ^= lfsrState>>17; lfsrState ^= lfsrState<<5;
      float nz = (float)(int32_t)lfsrState * (1.0f/2147483648.0f);
      s = s * (1.0f - tracks[ti].noiseAmt) + nz * tracks[ti].noiseAmt;
    }

    // ── LFO wah et tremolo (lfoOut déjà calculé avant la phase) ─────────
    if (ti < TRACK_COUNT && tracks[ti].lfoDepth > 0.001f) {
      switch (tracks[ti].lfoDest) {
        case 1: // Tremolo volume
          s *= (1.0f - tracks[ti].lfoDepth * 0.5f * (1.0f + fastSin(voices[v].lfoPhase)));
          break;
        // case 2 vibrato : déjà appliqué sur freq avant la phase
        // case 0 wah : appliqué dans le filtre ci-dessous
      }
    }

    // ── ENVELOPPE ADSR ────────────────────────────────────────────────────
    float env = voices[v].envelope;
    if (voices[v].attacking) {
      env += voices[v].attackRate;
      if (env >= 1.0f) { env = 1.0f; voices[v].attacking = false; }
      voices[v].envelope = env;
    } else if (voices[v].releasing) {
      // Phase release : descendre vers 0
      env -= voices[v].releaseRate;
      if (env < 0.0f) env = 0.0f;
      voices[v].envelope = env;
      if (env == 0.0f) { voices[v].active = false; continue; }
    } else if (voices[v].sustained) {
      // Phase sustain : maintenir au niveau sustainLevel
      float target = (ti < TRACK_COUNT) ? tracks[ti].sustainLevel : 1.0f;
      if (env > target) {
        env -= voices[v].decayRate;
        if (env < target) env = target;
        voices[v].envelope = env;
      }
      // ── WATCHDOG voix sustain : basé sur longueur max du pattern ────────
      // Longueur max = 32 steps. À 40 BPM = 32 * (60/40/4) = 12 secondes max
      // On utilise 16 secondes comme limite absolue de sécurité
      voices[v].sustainSamples++;
      if (voices[v].sustained && voices[v].sustainSamples > (SAMPLE_RATE * 16UL)) {
        voices[v].sustained      = false;
        voices[v].releasing      = true;
        voices[v].sustainSamples = 0;
      }
    } else {
      // Phase decay libre (note non sustained)
      env -= voices[v].decayRate;
      if (env < 0.0f) env = 0.0f;
      voices[v].envelope = env;
      if (env == 0.0f) { voices[v].active = false; continue; }
    }

    float vol = (ti < TRACK_COUNT) ? tracks[ti].volume : 0.5f;
    float vs  = s * env * voices[v].amplitude * vol;

    // ---- Effets par voix ----
    if (ti < TRACK_COUNT) {
      // DRIVE — saturation parallèle (sec + saturé)
      // Ancienne version : fastClip * (1/gain) sur-compensait → plus silencieux que l'entrée
      float drv = tracks[ti].driveAmt;
      if (drv > 0.01f) {
        float driven = fastClip(vs * (1.0f + drv * 8.0f));
        vs = vs * (1.0f - drv * 0.6f) + driven * (drv * 0.6f);
      }
      // FILTRE RÉSONANT 2 PÔLES (Moog-like)
      float fi  = tracks[ti].filterCutoff;
      float res = tracks[ti].filterRes;
      if (tracks[ti].lfoDest == 0 && tracks[ti].lfoDepth > 0.001f)
        fi = constrain(fi + lfoOut * 0.4f, 0.0f, 0.99f);
      if (fi < 0.98f || res > 0.01f) {
        float fc  = 0.005f + fi * fi * 0.35f;  // légèrement réduit
        // fb limité à 0.95 max pour éviter l'auto-oscillation incontrôlée
        float fb  = res * 0.95f;
        float in  = vs - voices[v].filterState2 * fb;
        voices[v].filterState  += fc * (in  - voices[v].filterState);
        voices[v].filterState2 += fc * (voices[v].filterState - voices[v].filterState2);
        // Clamper les états pour éviter la divergence numérique
        if (voices[v].filterState  >  2.0f) voices[v].filterState  =  2.0f;
        if (voices[v].filterState  < -2.0f) voices[v].filterState  = -2.0f;
        if (voices[v].filterState2 >  2.0f) voices[v].filterState2 =  2.0f;
        if (voices[v].filterState2 < -2.0f) voices[v].filterState2 = -2.0f;
        vs = voices[v].filterState2;
      }
      // CHORUS — LFO sub-rate toutes les 4 samples
      float ch = tracks[ti].chorusAmt;
      if (ch > 0.01f && chorusBufs[ti]) {
        if ((subRateCtr & 3) == 0) {
          chorusLFOPhases[ti] += 0.8f * 4.0f / (float)SAMPLE_RATE;
          if (chorusLFOPhases[ti] >= 1.0f) chorusLFOPhases[ti] -= 1.0f;
        }
        float lfoC = fastSin(chorusLFOPhases[ti]);
        int delaySamples = (int)((0.010f + lfoC*0.008f) * (float)SAMPLE_RATE);
        if (delaySamples < 1) delaySamples = 1;
        if (delaySamples >= CHORUS_BUF_LEN) delaySamples = CHORUS_BUF_LEN - 1;
        int readIdx = (chorusBufIdxs[ti] - delaySamples + CHORUS_BUF_LEN) % CHORUS_BUF_LEN;
        chorusBufs[ti][chorusBufIdxs[ti]] = vs;
        chorusBufIdxs[ti] = (chorusBufIdxs[ti] + 1) % CHORUS_BUF_LEN;
        vs = vs*(1.0f - ch*0.5f) + chorusBufs[ti][readIdx]*ch*0.5f;
      }
    }
    out += vs;
  }
  xSemaphoreGive(voiceMutex);

  // Normalisation RMS : évite la saturation avec N voix simultanées
  return out * 0.154f; // normalisation 8 voix + effets
}

// =============================================================================
// HELPERS
// =============================================================================
float calcDecayRate(uint8_t t) {
  float dur = 0.05f + tracks[t].decayTime*2.95f;
  return 1.0f/(dur*(float)SAMPLE_RATE);
}

void _activateVoice(uint8_t ti, float freq, uint8_t wave, float amp,
                    float decayRate, bool sustained, uint8_t noteIdx, uint8_t octave) {
  if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(3)) != pdTRUE) return;

  int vIdx = -1;

  // 1. Chercher une voix inactive
  for (int v = 0; v < MAX_VOICES; v++) {
    if (!voices[v].active) { vIdx = v; break; }
  }

  // 2. Voler la voix non-sustained avec enveloppe la plus faible
  if (vIdx < 0) {
    float minEnv = 999.0f;
    for (int v = 0; v < MAX_VOICES; v++) {
      if (!voices[v].sustained && voices[v].envelope < minEnv) {
        minEnv = voices[v].envelope; vIdx = v;
      }
    }
  }

  // 3. En dernier recours : si la nouvelle note est aussi sustained,
  //    voler la voix sustain la PLUS ANCIENNE (startTime le plus petit).
  //    Critère startTime et NON envelope : toutes les voix sustain ont
  //    envelope≈1.0, donc l'envelope ne distingue pas → on volait toujours
  //    la même voix (slot 0) et les notes 2 et 3 d'un accord écrasaient
  //    la note 1 → une seule note sonnait.
  //    Si la nouvelle note n'est PAS sustained (ex: ARP), on abandonne.
  if (vIdx < 0) {
    if (!sustained) {
      xSemaphoreGive(voiceMutex);
      return;
    }
    // Voler la voix sustain la plus ancienne — même piste en priorité
    unsigned long oldestMs = 0xFFFFFFFF;
    for (int v = 0; v < MAX_VOICES; v++) {
      if (voices[v].sustained && voices[v].trackIdx == ti &&
          voices[v].startTime < oldestMs) {
        oldestMs = voices[v].startTime; vIdx = v;
      }
    }
    // Sinon voler n'importe quelle voix sustain la plus ancienne
    if (vIdx < 0) {
      oldestMs = 0xFFFFFFFF;
      for (int v = 0; v < MAX_VOICES; v++) {
        if (voices[v].active && voices[v].startTime < oldestMs) {
          oldestMs = voices[v].startTime; vIdx = v;
        }
      }
    }
  }
  if (vIdx < 0) vIdx = 0;
  // Calculer l'attack depuis la piste
  float attackAmt = (ti < TRACK_COUNT) ? tracks[ti].attackAmt : 0.0f;
  float attackDur = attackAmt * 0.5f;
  float aRate = (attackDur > 0.001f) ? (1.0f / (attackDur * (float)SAMPLE_RATE)) : 0.0f;
  float initEnv = (aRate > 0.0f) ? 0.0f : 1.0f;

  // Release rate
  float relAmt = (ti < TRACK_COUNT) ? tracks[ti].releaseTime : 0.1f;
  float relDur = 0.01f + relAmt * relAmt * 2.0f; // 10ms..2s exponentiel
  float rRate  = 1.0f / (relDur * (float)SAMPLE_RATE);

  // Glide coef précalculé — fastExp une seule fois au lieu de chaque sample
  float startFreq  = freq;
  float glideCoef  = 0.0f; // 0 = pas de glide (freq = targetFreq directement)
  if (ti < TRACK_COUNT && tracks[ti].glideAmt > 0.001f) {
    float glideTime = tracks[ti].glideAmt * tracks[ti].glideAmt * 2.0f;
    glideCoef = (glideTime > 0.0001f)
                ? fastExp(-1.0f / (glideTime * (float)SAMPLE_RATE))
                : 0.0f;
    for (int v = 0; v < MAX_VOICES; v++) {
      if (v == vIdx) continue;
      if (voices[v].active && voices[v].trackIdx == ti) {
        startFreq = voices[v].freq;
        break;
      }
    }
  }

  //            active  freq       targetFreq phase  phaseFM subPh  osc2Ph amp     env      decay     aRate  rRate  glideCoef     atk           rel    sust
  voices[vIdx] = {true, startFreq, freq,      0.0f,  0.0f,   0.0f,  0.0f,  amp,    initEnv, decayRate, aRate, rRate, glideCoef, (aRate>0.0f), false, sustained,
                  ti, wave, noteIdx, octave, millis(), 0UL, 0.0f, 0.0f, 0.0f};
  xSemaphoreGive(voiceMutex);
}

void _releaseVoice(uint8_t ti, uint8_t noteIdx, uint8_t octave) {
  if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(3)) != pdTRUE) return;
  // Libérer la voix sustained la plus ancienne (startTime le plus petit)
  // pour ne jamais tuer une voix qui vient juste de démarrer
  int           best      = -1;
  unsigned long oldestMs  = 0xFFFFFFFF;
  for (int v = 0; v < MAX_VOICES; v++) {
    if (voices[v].active && voices[v].sustained &&
        voices[v].trackIdx == ti &&
        voices[v].noteIdx  == noteIdx &&
        voices[v].octave   == octave) {
      if (voices[v].startTime < oldestMs) {
        oldestMs = voices[v].startTime;
        best     = v;
      }
    }
  }
  if (best >= 0) {
    voices[best].sustained      = false;
    voices[best].releasing      = true;
    voices[best].sustainSamples = 0;
    if (voices[best].releaseRate < 0.000001f)
      voices[best].releaseRate = calcDecayRate(ti) * 3.0f;
  }
  xSemaphoreGive(voiceMutex);
}

void _releaseAllSustainedForTrack(uint8_t ti) {
  if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(3)) != pdTRUE) return;
  float decay = calcDecayRate(ti) * 3.0f;
  for (int v = 0; v < MAX_VOICES; v++) {
    if (voices[v].active && voices[v].sustained && voices[v].trackIdx == ti) {
      voices[v].sustained = false;
      voices[v].decayRate = decay;
    }
  }
  xSemaphoreGive(voiceMutex);
}

void stopAllVoices() {
  if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (int v = 0; v < MAX_VOICES; v++) voices[v].active = false;
    xSemaphoreGive(voiceMutex);
  }
  for (int v = 0; v < 4; v++) drumVoices[v].active = false;
}

float getFreqFromNote(uint8_t noteIdx, uint8_t octave) {
  if (noteIdx >= 13) noteIdx = 0;
  float f = NOTE_FREQS_BASE[noteIdx];
  int shift = (int)octave - 3;
  for (int i = 0; i < shift;  i++) f *= 2.0f;
  for (int i = 0; i > shift; i--) f *= 0.5f;
  return f;
}

void triggerNote(uint8_t t, uint8_t noteIdx, uint8_t octave) {
  NoteEvent ev = {}; ev.trackIdx=t; ev.noteIdx=noteIdx; ev.octave=octave;
  xQueueSend(noteQueue, &ev, 0);
}
void triggerNoteWithDecay(uint8_t t, uint8_t noteIdx, uint8_t octave, float decayMult) {
  float freq  = getFreqFromNote(noteIdx, octave);
  float decay = calcDecayRate(t) / decayMult;
  if (decay < 0.000001f) decay = 0.000001f;
  _activateVoice(t, freq, tracks[t].waveform, 0.80f, decay, false, noteIdx, octave);
}
void triggerNoteSustained(uint8_t t, uint8_t noteIdx, uint8_t octave) {
  // Live : fromSeq=false → polyphonie préservée
  NoteEvent ev = {}; ev.trackIdx=t; ev.noteIdx=noteIdx; ev.octave=octave;
  ev.sustained=true; ev.fromSeq=false;
  xQueueSend(noteQueue, &ev, 0);
}
void triggerNoteSustainedSeq(uint8_t t, uint8_t noteIdx, uint8_t octave) {
  // Les sustains précédents ont déjà été libérés par flushPendingOffs
  // On active directement la nouvelle voix sustain sans libération supplémentaire
  NoteEvent ev = {}; ev.trackIdx=t; ev.noteIdx=noteIdx; ev.octave=octave;
  ev.sustained=true; ev.fromSeq=false; // fromSeq=false : pas de double libération
  xQueueSend(noteQueue, &ev, 0);
}
void triggerNoteOff(uint8_t t, uint8_t noteIdx, uint8_t octave) {
  NoteEvent ev = {}; ev.trackIdx=t; ev.noteIdx=noteIdx; ev.octave=octave; ev.noteOff=true;
  xQueueSend(noteQueue, &ev, 0);
}
void releaseAllSustainedForTrack(uint8_t t) {
  _releaseAllSustainedForTrack(t);
}
void triggerMetro() {
  NoteEvent ev = {}; ev.isMetro=true; ev.metroAccent=false; xQueueSend(noteQueue, &ev, 0);
}
void triggerMetroAccent(bool accent) {
  NoteEvent ev = {}; ev.isMetro=true; ev.metroAccent=accent; xQueueSend(noteQueue, &ev, 0);
}
void triggerDrum(uint8_t drumType) {
  NoteEvent ev = {}; ev.trackIdx=TRACK_DRUM; ev.noteIdx=drumType;
  xQueueSend(noteQueue, &ev, 0);
}

void updateEffects() {
  // Synths : BASS, SYN1, SYN2 seulement
  float dl = 0;
  for (int t = 1; t < TRACK_COUNT; t++) {
    if (tracks[t].delayAmt > dl) dl = tracks[t].delayAmt;
  }
  eff_delay = dl;
  // Drum : buffer séparé → reverb indépendante
  eff_delay_drum = tracks[TRACK_DRUM].delayAmt;
}
