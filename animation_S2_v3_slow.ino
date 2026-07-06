// =============================================================================
// DRUMS.INO v12 — Samples copiés en PSRAM au boot pour lecture rapide
// pgm_read_word depuis flash = lent (cache miss) → copie en PSRAM = ×100 plus rapide
// =============================================================================

#include <Arduino.h>
#include <pgmspace.h>
#include "config.h"
#include "globals.h"
#include "drum_samples.h"

const char* DRUM_NAMES[13] = {
  "KICK","SNARE","HIHAT","CLAP","TOM1","TOM2","RIM",
  "COWBL","CYMBL","SHKR","CONGA","SNAP","CLHAT"
};
const char* DRUM_KIT_NAMES[4] = { "808", "LOFI", "ACOU", "INDU" };

// =============================================================================
// COPIE PSRAM — pointeurs vers copies RAM des samples PROGMEM
// =============================================================================
static int16_t* ram_KICK_808   = nullptr;
static int16_t* ram_KICK_INDU  = nullptr;
static int16_t* ram_KICK_LOFI  = nullptr;
static int16_t* ram_KICK_ACOU  = nullptr;
static int16_t* ram_SNARE_808  = nullptr;
static int16_t* ram_SNARE_LOFI = nullptr;
static int16_t* ram_SNARE_ACOU = nullptr;
static int16_t* ram_SNARE_INDU = nullptr;
static int16_t* ram_CLAP_808   = nullptr;
static int16_t* ram_CLAP_LOFI  = nullptr;
static int16_t* ram_CLAP_ACOU  = nullptr;
static int16_t* ram_CLAP_INDU  = nullptr;

// Copie un sample PROGMEM en PSRAM, retourne le pointeur RAM
static int16_t* copyToRAM(const int16_t* progmemPtr, uint16_t len) {
  int16_t* buf = (int16_t*)ps_malloc(len * sizeof(int16_t));
  if (!buf) {
    // Fallback sur heap normal si PSRAM indisponible
    buf = (int16_t*)malloc(len * sizeof(int16_t));
  }
  if (buf) memcpy_P(buf, progmemPtr, len * sizeof(int16_t));
  return buf;
}

void initDrumSamplesRAM() {
  ram_KICK_808   = copyToRAM(SAMPLE_KICK_808,   SAMPLE_KICK_808_LEN);
  ram_KICK_INDU  = copyToRAM(SAMPLE_KICK_INDU,  SAMPLE_KICK_INDU_LEN);
  ram_KICK_LOFI  = copyToRAM(SAMPLE_KICK_LOFI,  SAMPLE_KICK_LOFI_LEN);
  ram_KICK_ACOU  = copyToRAM(SAMPLE_KICK_ACOU,  SAMPLE_KICK_ACOU_LEN);
  ram_SNARE_808  = copyToRAM(SAMPLE_SNARE_808,  SAMPLE_SNARE_808_LEN);
  ram_SNARE_LOFI = copyToRAM(SAMPLE_SNARE_LOFI, SAMPLE_SNARE_LOFI_LEN);
  ram_SNARE_ACOU = copyToRAM(SAMPLE_SNARE_ACOU, SAMPLE_SNARE_ACOU_LEN);
  ram_SNARE_INDU = copyToRAM(SAMPLE_SNARE_INDU, SAMPLE_SNARE_INDU_LEN);
  ram_CLAP_808   = copyToRAM(SAMPLE_CLAP_808,   SAMPLE_CLAP_808_LEN);
  ram_CLAP_LOFI  = copyToRAM(SAMPLE_CLAP_LOFI,  SAMPLE_CLAP_LOFI_LEN);
  ram_CLAP_ACOU  = copyToRAM(SAMPLE_CLAP_ACOU,  SAMPLE_CLAP_ACOU_LEN);
  ram_CLAP_INDU  = copyToRAM(SAMPLE_CLAP_INDU,  SAMPLE_CLAP_INDU_LEN);
  Serial.printf("Drum samples en RAM: %dKB\n",
    (SAMPLE_KICK_808_LEN + SAMPLE_KICK_LOFI_LEN + SAMPLE_KICK_ACOU_LEN + SAMPLE_KICK_INDU_LEN +
     SAMPLE_SNARE_808_LEN + SAMPLE_SNARE_LOFI_LEN + SAMPLE_SNARE_ACOU_LEN + SAMPLE_SNARE_INDU_LEN +
     SAMPLE_CLAP_808_LEN + SAMPLE_CLAP_LOFI_LEN + SAMPLE_CLAP_ACOU_LEN + SAMPLE_CLAP_INDU_LEN) * 2 / 1024);
}

// =============================================================================
// TABLE DES SAMPLES PAR KIT — pointe vers RAM après initDrumSamplesRAM()
// =============================================================================
struct SampleRef {
  int16_t** ramPtr;
  uint16_t  len;
  float     amplitude;
};

// Samples pour KICK, SNARE, CLAP par kit (index 0,1,2,3)
static const SampleRef KICK_SAMPLES[4] = {
  { &ram_KICK_ACOU, SAMPLE_KICK_ACOU_LEN,  1.00f },  // kit 1 (était 808)
  { &ram_KICK_LOFI, SAMPLE_KICK_LOFI_LEN,  0.96f },  // kit 2 (LOFI)
  { &ram_KICK_808,  SAMPLE_KICK_808_LEN,   1.00f },  // kit 3 (était ACOU)
  { &ram_KICK_INDU, SAMPLE_KICK_INDU_LEN,  1.00f },  // kit 4 (INDU)
};
static const SampleRef SNARE_SAMPLES[4] = {
  { &ram_SNARE_808,  SAMPLE_SNARE_808_LEN,  0.92f },
  { &ram_SNARE_LOFI, SAMPLE_SNARE_LOFI_LEN, 0.95f },
  { &ram_SNARE_ACOU, SAMPLE_SNARE_ACOU_LEN, 0.94f },
  { &ram_SNARE_INDU, SAMPLE_SNARE_INDU_LEN, 0.98f },
};
static const SampleRef CLAP_SAMPLES[4] = {
  { &ram_CLAP_808,  SAMPLE_CLAP_808_LEN,   0.88f },
  { &ram_CLAP_LOFI, SAMPLE_CLAP_LOFI_LEN,  0.80f },
  { &ram_CLAP_ACOU, SAMPLE_CLAP_ACOU_LEN,  0.84f },
  { &ram_CLAP_INDU, SAMPLE_CLAP_INDU_LEN,  0.92f },
};

// =============================================================================
// PARAMÈTRES SYNTHÈSE pour les autres sons (hihat, toms, etc.)
// =============================================================================
struct DrumParams {
  float   freqStart;
  float   freqEnd;
  float   decay;
  float   amplitude;
  float   noiseAmt;
  float   filterCoef;
  float   pitchMod;
  uint8_t synthType;
};

static const DrumParams SYNTH_PARAMS[4][10] = {
  // KIT 0 808 — amplitudes ×0.65 (-35%) sauf KICK/SNARE/CLAP (samplés, non concernés)
  {
    {8500.f,   0.f, 0.07f, 0.455f, 1.00f, 0.92f, 0.0f, 2 }, // HIHAT
    { 130.f,  48.f, 0.30f, 0.598f, 0.05f, 0.15f, 5.0f, 4 }, // TOM1
    { 200.f,  72.f, 0.24f, 0.572f, 0.05f, 0.15f, 5.0f, 4 }, // TOM2
    { 400.f,   0.f, 0.06f, 0.507f, 0.20f, 0.60f, 0.0f, 6 }, // RIM
    { 820.f, 560.f, 0.55f, 0.423f, 0.15f, 0.50f, 0.0f, 5 }, // COWBL
    {1050.f, 720.f, 0.80f, 0.377f, 0.40f, 0.85f, 0.0f, 5 }, // CYMBL
    {6000.f,   0.f, 0.10f, 0.338f, 1.00f, 0.30f, 0.0f, 7 }, // SHKR
    { 180.f,  75.f, 0.22f, 0.585f, 0.04f, 0.12f, 4.5f, 4 }, // CONGA
    {3200.f,   0.f, 0.03f, 0.488f, 0.10f, 0.80f, 0.0f, 6 }, // SNAP
    {8000.f,   0.f, 0.045f,0.403f, 1.00f, 0.95f, 0.0f, 2 }, // CLHAT
  },
  // KIT 1 LOFI — amplitudes ×0.65 (-35%)
  {
    {3200.f,   0.f, 0.12f, 0.377f, 1.00f, 0.55f, 0.0f, 2 }, // HIHAT
    {  88.f,  35.f, 0.38f, 0.559f, 0.12f, 0.10f, 3.5f, 4 }, // TOM1
    { 130.f,  55.f, 0.30f, 0.533f, 0.12f, 0.10f, 3.5f, 4 }, // TOM2
    { 260.f,   0.f, 0.08f, 0.442f, 0.35f, 0.45f, 0.0f, 6 }, // RIM
    { 480.f, 330.f, 0.42f, 0.377f, 0.25f, 0.40f, 0.0f, 5 }, // COWBL
    { 580.f, 400.f, 0.55f, 0.338f, 0.50f, 0.70f, 0.0f, 5 }, // CYMBL
    {2200.f,   0.f, 0.14f, 0.312f, 1.00f, 0.25f, 0.0f, 7 }, // SHKR
    { 115.f,  50.f, 0.32f, 0.533f, 0.08f, 0.08f, 3.5f, 4 }, // CONGA
    {1600.f,   0.f, 0.05f, 0.442f, 0.20f, 0.70f, 0.0f, 6 }, // SNAP
    {2800.f,   0.f, 0.07f, 0.338f, 1.00f, 0.60f, 0.0f, 2 }, // CLHAT
  },
  // KIT 2 ACOU — amplitudes ×0.65 (-35%)
  {
    {9200.f,   0.f, 0.055f,0.423f, 1.00f, 0.96f, 0.0f, 2 }, // HIHAT
    { 125.f,  58.f, 0.34f, 0.598f, 0.08f, 0.20f, 6.0f, 4 }, // TOM1
    { 195.f,  88.f, 0.28f, 0.572f, 0.08f, 0.20f, 6.0f, 4 }, // TOM2
    { 370.f,   0.f, 0.065f,0.481f, 0.15f, 0.55f, 0.0f, 6 }, // RIM
    { 560.f, 420.f, 0.48f, 0.390f, 0.20f, 0.48f, 0.0f, 5 }, // COWBL
    {1200.f, 880.f, 0.75f, 0.351f, 0.50f, 0.88f, 0.0f, 5 }, // CYMBL
    {5500.f,   0.f, 0.12f, 0.325f, 1.00f, 0.28f, 0.0f, 7 }, // SHKR
    { 155.f,  68.f, 0.28f, 0.572f, 0.06f, 0.16f, 5.5f, 4 }, // CONGA
    {2900.f,   0.f, 0.04f, 0.455f, 0.08f, 0.75f, 0.0f, 6 }, // SNAP
    {7200.f,   0.f, 0.04f, 0.377f, 1.00f, 0.96f, 0.0f, 2 }, // CLHAT
  },
  // KIT 3 INDU — amplitudes ×0.65 (-35%)
  {
    {9800.f,   0.f, 0.09f, 0.533f, 1.00f, 0.98f, 0.0f, 2 }, // HIHAT
    {  95.f,  32.f, 0.45f, 0.624f, 0.25f, 0.22f, 4.0f, 4 }, // TOM1
    { 150.f,  55.f, 0.36f, 0.598f, 0.25f, 0.22f, 4.0f, 4 }, // TOM2
    { 650.f,   0.f, 0.09f, 0.572f, 0.40f, 0.70f, 0.0f, 6 }, // RIM
    {1250.f, 820.f, 0.68f, 0.468f, 0.30f, 0.60f, 0.0f, 5 }, // COWBL
    {1600.f, 980.f, 0.95f, 0.442f, 0.60f, 0.90f, 0.0f, 5 }, // CYMBL
    {7500.f,   0.f, 0.16f, 0.403f, 1.00f, 0.35f, 0.0f, 7 }, // SHKR
    { 200.f,  82.f, 0.38f, 0.611f, 0.20f, 0.18f, 4.0f, 4 }, // CONGA
    {4200.f,   0.f, 0.04f, 0.533f, 0.15f, 0.85f, 0.0f, 6 }, // SNAP
    {9500.f,   0.f, 0.065f,0.468f, 1.00f, 0.98f, 0.0f, 2 }, // CLHAT
  },
};

// Index dans SYNTH_PARAMS pour les sons non-samplés
// noteIdx: 2=HIHAT→0  4=TOM1→1  5=TOM2→2  6=RIM→3  7=COWBL→4
//          8=CYMBL→5  9=SHKR→6  10=CONGA→7  11=SNAP→8  12=CLHAT→9
static int synthParamIdx(uint8_t noteIdx) {
  switch(noteIdx) {
    case 2: return 0;  // HIHAT
    case 4: return 1;  // TOM1
    case 5: return 2;  // TOM2
    case 6: return 3;  // RIM
    case 7: return 4;  // COWBL
    case 8: return 5;  // CYMBL
    case 9: return 6;  // SHKR
    case 10: return 7; // CONGA
    case 11: return 8; // SNAP
    case 12: return 9; // CLHAT
    default: return 0;
  }
}

// =============================================================================
// VOIX DRUM — champ sampleData pour les samples PROGMEM
// =============================================================================
// On réutilise DrumVoice en ajoutant un mode sample :
// dv.freq2 = position flottante dans le sample (quand synthType == 99)
// dv.freq  = amplitude du sample

// =============================================================================
// DÉCLENCHER
// =============================================================================
void _triggerDrumVoice(uint8_t noteIdx) {
  if (noteIdx >= 13) noteIdx = 0;
  uint8_t kit = tracks[TRACK_DRUM].drumKit % 4;

  int vIdx = -1; float minEnv = 999.0f;
  for (int v = 0; v < 4; v++) {
    if (!drumVoices[v].active) { vIdx = v; break; }
    if (drumVoices[v].envelope < minEnv) { minEnv = drumVoices[v].envelope; vIdx = v; }
  }
  if (vIdx < 0) vIdx = 0;

  DrumVoice& dv = drumVoices[vIdx];
  dv.active      = true;
  dv.drumType    = noteIdx;
  dv.phase       = 0.0f;   // position sample (float index)
  dv.phase2      = 0.0f;
  dv.envelope    = 1.0f;
  dv.filterState = 0.0f;

  if (noteIdx == 0) { // KICK — sample
    dv.freq      = KICK_SAMPLES[kit].amplitude;
    dv.freq2     = (float)KICK_SAMPLES[kit].len;
    dv.decayRate = 0.0f; // géré par la longueur du sample
    dv.amplitude = KICK_SAMPLES[kit].amplitude;
    // Layer 150Hz : phase2 = enveloppe (1.0 → 0), filterState = phase sinus

  } else if (noteIdx == 1) { // SNARE — sample
    dv.freq      = SNARE_SAMPLES[kit].amplitude;
    dv.freq2     = (float)SNARE_SAMPLES[kit].len;
    dv.decayRate = 0.0f;
    dv.amplitude = SNARE_SAMPLES[kit].amplitude;
  } else if (noteIdx == 3) { // CLAP — sample
    dv.freq      = CLAP_SAMPLES[kit].amplitude;
    dv.freq2     = (float)CLAP_SAMPLES[kit].len;
    dv.decayRate = 0.0f;
    dv.amplitude = CLAP_SAMPLES[kit].amplitude;
  } else { // Autres sons — synthèse
    int pi = synthParamIdx(noteIdx);
    const DrumParams& p = SYNTH_PARAMS[kit][pi];
    dv.freq      = p.freqStart;
    dv.freq2     = p.freqEnd;
    dv.decayRate = 1.0f / (p.decay * (float)SAMPLE_RATE);
    dv.amplitude = p.amplitude;
  }
}

// =============================================================================
// SYNTHÈSE — 1 sample par appel
// =============================================================================
float drumSample() {
  float out = 0.0f;

  for (int v = 0; v < 4; v++) {
    DrumVoice& dv = drumVoices[v];
    if (!dv.active) continue;

    float s   = 0.0f;
    float vol = tracks[TRACK_DRUM].volume;
    uint8_t kit = tracks[TRACK_DRUM].drumKit % 4;
    uint8_t ni  = dv.drumType;

    // ── SONS SAMPLÉS (kick=0, snare=1, clap=3) ──────────────────────────
    if (ni == 0 || ni == 1 || ni == 3) {
      const SampleRef& sr = (ni==0) ? KICK_SAMPLES[kit]
                          : (ni==1) ? SNARE_SAMPLES[kit]
                          :           CLAP_SAMPLES[kit];
      const int16_t* data = *sr.ramPtr;  // lecture RAM directe
      uint16_t       len  = sr.len;

      if (!data) { dv.active = false; continue; } // sécurité si RAM non init
      uint16_t pos = (uint16_t)dv.phase;
      if (pos >= len) { dv.active = false; continue; }

      // Lecture RAM — pas de pgm_read_word
      float s0 = (float)data[pos]   / 32767.0f;
      float frac = dv.phase - (float)pos;
      if (pos + 1 < len)
        s = s0 * (1.0f - frac) + (float)data[pos+1] / 32767.0f * frac;
      else
        s = s0;

      // Fade out fin de sample
      float remaining = ((float)len - dv.phase) / (float)len;
      if (remaining < 0.05f) s *= remaining / 0.05f;

      dv.phase += 1.0f;
      if (dv.phase >= (float)len) dv.active = false;

      // ── Layer synthétique 150Hz sur KICK (kits 808, LOFI, INDU) ──────────
      // Le sample original n'a pas de contenu mid (0.9%) audible sur un
      // petit HP 4 ohms. On superpose une sinusoïde à 150Hz avec une
      // enveloppe rapide — technique utilisée par Roland/Akai/Elektron.
      // ACOU (kit 2) sonne déjà bien → non touché.


      out += s * dv.amplitude * sr.amplitude * vol;
      continue;
    }

    // ── SONS SYNTHÉTISÉS ─────────────────────────────────────────────────
    int pi = synthParamIdx(ni);
    const DrumParams& p = SYNTH_PARAMS[kit][pi];
    float env = dv.envelope;

    lfsrState ^= lfsrState << 13;
    lfsrState ^= lfsrState >> 17;
    lfsrState ^= lfsrState << 5;
    float noise = (float)(int32_t)lfsrState * (1.0f / 2147483648.0f);

    switch (p.synthType) {
      case 2: { // HIHAT : bruit HP double étage
        float fc  = p.filterCoef;
        float hp1 = noise - dv.filterState;
        dv.filterState = dv.filterState*(1.0f-fc*0.5f) + noise*fc*0.5f;
        float hp2 = hp1 - dv.phase2;
        dv.phase2 = dv.phase2*(1.0f-fc*0.3f) + hp1*fc*0.3f;
        s = hp2;
        break;
      }
      case 4: { // TOM/CONGA : sweep exponentiel + harmonique
        float t    = 1.0f - env;
        float sw   = expf(-p.pitchMod * t);
        float fCur = p.freqEnd + (p.freqStart - p.freqEnd) * sw;
        dv.phase  += fCur / (float)SAMPLE_RATE;
        if (dv.phase >= 1.0f) dv.phase -= 1.0f;
        dv.phase2 += fCur * 2.0f / (float)SAMPLE_RATE;
        if (dv.phase2 >= 1.0f) dv.phase2 -= 1.0f;
        float x1 = dv.phase*2.0f-1.0f, x2 = dv.phase2*2.0f-1.0f;
        float tone = x1*(1.6f-0.6f*fabsf(x1))*0.85f + x2*(1.6f-0.6f*fabsf(x2))*0.15f;
        s = tone*(1.0f-p.noiseAmt) + noise*p.noiseAmt*env;
        break;
      }
      case 5: { // COWBELL/CYMBAL : double osc + noise
        dv.phase  += p.freqStart / (float)SAMPLE_RATE;
        dv.phase2 += p.freqEnd   / (float)SAMPLE_RATE;
        if (dv.phase  >= 1.0f) dv.phase  -= 1.0f;
        if (dv.phase2 >= 1.0f) dv.phase2 -= 1.0f;
        float sq1 = (dv.phase  < 0.5f) ? 0.55f : -0.55f;
        float sq2 = (dv.phase2 < 0.5f) ? 0.55f : -0.55f;
        float fc  = p.filterCoef;
        float hp  = noise - dv.filterState;
        dv.filterState = dv.filterState*(1.0f-fc*0.4f) + noise*fc*0.4f;
        s = (sq1+sq2)*(1.0f-p.noiseAmt) + hp*p.noiseAmt;
        break;
      }
      case 6: { // RIM/SNAP : transient court
        dv.phase += p.freqStart / (float)SAMPLE_RATE;
        if (dv.phase >= 1.0f) dv.phase -= 1.0f;
        float sq    = (dv.phase < 0.5f) ? 0.8f : -0.8f;
        float burst = (env > 0.80f) ? noise*0.5f : 0.0f;
        s = sq*(1.0f-p.noiseAmt) + burst + noise*p.noiseAmt*env;
        break;
      }
      case 7: { // SHAKER : bruit LP granulaire
        dv.phase += 22.0f / (float)SAMPLE_RATE;
        if (dv.phase >= 1.0f) dv.phase -= 1.0f;
        float grain = (dv.phase < 0.5f) ? 1.0f : 0.4f;
        float fc    = p.filterCoef;
        dv.filterState = dv.filterState*(1.0f-fc) + noise*fc;
        s = dv.filterState * grain;
        break;
      }
      default: {
        dv.phase += p.freqStart / (float)SAMPLE_RATE;
        if (dv.phase >= 1.0f) dv.phase -= 1.0f;
        float x = dv.phase*2.0f-1.0f;
        s = x*(1.6f-0.6f*fabsf(x));
        break;
      }
    }

    env -= dv.decayRate;
    if (env < 0.0f) { env = 0.0f; dv.active = false; }
    dv.envelope = env;
    out += s * env * env * dv.amplitude * vol;
  }

  // ── DRIVE sur le mix drum ────────────────────────────────────────────────
  float drv = tracks[TRACK_DRUM].driveAmt;
  if (drv > 0.01f) {
    float driven = (out > 1.0f)  ?  1.0f - 0.25f / (out  + 0.25f)
                 : (out < -1.0f) ? -1.0f + 0.25f / (-out + 0.25f) : out;
    driven = (driven > 1.0f)  ?  1.0f - 0.25f / (driven  + 0.25f)
           : (driven < -1.0f) ? -1.0f + 0.25f / (-driven + 0.25f) : driven;
    out = out * (1.0f - drv * 0.6f) + driven * (1.0f + drv * 8.0f) * (drv * 0.6f);
    // re-clip
    if (out >  1.0f) out =  1.0f;
    if (out < -1.0f) out = -1.0f;
  }

  // ── FILTRE passe-bas sur le mix drum ──────────────────────────────────────
  float fi = tracks[TRACK_DRUM].filterCutoff;
  if (fi < 0.98f) {
    static float drumFilterState = 0.0f;
    float fc = 0.005f + fi * fi * 0.35f;
    drumFilterState += fc * (out - drumFilterState);
    out = drumFilterState;
  }

  return out;  // masterVolume appliqué dans audioTask : mix = (synth + drum) * masterVolume
}
