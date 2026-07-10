#pragma once
// =============================================================================
// CONFIG.H — SATURN 32 — ESP32-S3 WROOM-1 N16R8
// =============================================================================

// OLED SSD1306
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define I2C_SDA        39   // SDA cable sur GPIO39 (inverse a la soudure)
#define I2C_SCL        38   // SCL cable sur GPIO38 (inverse a la soudure)

// MATRICE TOUCHES 5x5
extern const int COL_PINS[5];   // GPIO  1  2  3  4  5
extern const int ROW_PINS[5];   // GPIO  6  7  8  9 10

// ENCODEURS
#define ENC1_CLK  11
#define ENC1_DT   12
#define ENC1_SW   13
#define ENC2_CLK  14
#define ENC2_DT   21
#define ENC2_SW   47

// AUDIO I2S -> PCM5102
#define I2S_DIN_PIN   40
#define I2S_BCK_PIN   41
#define I2S_LRCK_PIN  42
#define SAMPLE_RATE        22050  // 22050Hz : -50% CPU vs 44100Hz, fréq. max 11025Hz
#define AUDIO_BUF_SAMPLES    256  // 256 samples = 11.6ms budget/batch (était 5.8ms)
#define DELAY_BUF_LEN       4410  // 200ms à 22050Hz (était 8820 à 44100Hz)
#define CHORUS_BUF_LEN       882  //  40ms à 22050Hz (était 1764 à 44100Hz)

// SEQUENCEUR
#define STEPS_PER_TRACK     32
#define MAX_NOTES_PER_STEP   6
#define TRACK_COUNT          4
#define MAX_VOICES           8
#define UNDO_STACK_SIZE      6

#define TRACK_DRUM   0
#define TRACK_BASS   1
#define TRACK_SYNTH1 2
#define TRACK_SYNTH2 3

// TIMINGS
#define KEY_DEBOUNCE_MS      80
#define LONG_PRESS_MS      1500
#define STEP_EDIT_LONG_MS   500
#define ERASE_LONG_MS      2500
#define BTN_DEBOUNCE_MS      25

// ENCODEURS seuils
#define ENC_THRESHOLD_X10   30
#define ENC_STABLE_READS     1

// DRUMS
#define DRUM_COUNT 8

// ONDES
#define WAVE_SINE     0
#define WAVE_SQUARE   1
#define WAVE_SAW      2
#define WAVE_NOISE    3
#define WAVE_TRIANGLE 4
#define WAVE_PULSE    5
#define WAVE_FM       6
#define WAVE_WTBL     7
#define WAVE_HARM     8   // Harmonique additive (fond + harmoniques, morph = densité)
#define WAVE_FOLD     9   // Wave folder (sinus replié, morph = gain de pliage)
#define WAVE_WARM    10   // Saw band-limitée chaud (morph = brillance)
#define WAVE_SUB     11   // Sub bass (sinus fond + sinus -1 octave, morph = balance)
#define WAVE_CHEW    12   // Chebyshev (harmoniques musicales précises, morph = ordre)
#define WAVE_COUNT   13

// PARAMETRES MENU SYNTH
#define PARAM_WAVE     0
#define PARAM_DECAY    1
#define PARAM_REVERB   2
#define PARAM_DELAY    3
#define PARAM_ATTACK   4
#define PARAM_CHORUS   5
#define PARAM_DRIVE    6
#define PARAM_FILTER   7
#define PARAM_GLIDE    8
#define PARAM_LFO_DEP  9
#define PARAM_LFO_RAT 10
#define PARAM_LFO_DST 11
#define PARAM_SUB     12
#define PARAM_NOISE   13
#define PARAM_FILRES  14
#define PARAM_SUSTAIN 15
#define PARAM_RELEASE 16
#define PARAM_COUNT   17

// =============================================================================
// STRUCTURES
// =============================================================================

// Tie state par note
#define TIE_NONE  0  // note courte
#define TIE_START 1  // début d'une note longue (déclenche sustain)
#define TIE_MID   2  // milieu (silencieux, la voix continue)
#define TIE_END   3  // fin (déclenche noteOff)

// ─── Boutons encodeur — machine à états ─────────────────────────────────────
#define EVT_NONE  0
#define EVT_SHORT 1
#define EVT_LONG  2

struct BtnState {
  bool          last;
  bool          longFired;
  unsigned long downMs;
};

struct NoteSlot {
  uint8_t note;
  uint8_t octave;
  uint8_t tie;   // TIE_NONE, TIE_START, TIE_MID, TIE_END
};

struct Step {
  bool      active;
  uint8_t   noteCount;
  NoteSlot  slots[MAX_NOTES_PER_STEP];
  uint8_t   stepDecay;
  uint8_t   drumOffset; // 0=normal, 1=2ème demi-step, 2=1er demi-step seul, 3=deux fois
};

// Helpers step-level pour compatibilité affichage/séquenceur
inline bool stepHasTieStart(const Step& s) {
  for (int i=0;i<s.noteCount;i++) if (s.slots[i].tie==TIE_START) return true; return false;
}
inline bool stepHasTieMid(const Step& s) {
  for (int i=0;i<s.noteCount;i++) if (s.slots[i].tie==TIE_MID) return true; return false;
}
inline bool stepHasTieEnd(const Step& s) {
  for (int i=0;i<s.noteCount;i++) if (s.slots[i].tie==TIE_END) return true; return false;
}
inline bool stepIsAllMid(const Step& s) {
  if (!s.active || s.noteCount==0) return false;
  for (int i=0;i<s.noteCount;i++) if (s.slots[i].tie!=TIE_MID) return false; return true;
}
inline bool stepHasShort(const Step& s) {
  for (int i=0;i<s.noteCount;i++) if (s.slots[i].tie==TIE_NONE) return true; return false;
}

struct ArpData {
  bool    enabled;
  uint8_t noteCount;
  uint8_t notes[6];
  uint8_t octaves[6];
  uint8_t div;
  uint8_t pos;
  uint8_t stepCount;
  bool    latch;
  uint8_t octaveRange;  // 1=1 octave, 2=2 octaves, 3=3 octaves
  uint8_t octaveShift;  // octave courante dans le cycle (0..octaveRange-1)
  uint8_t noteLocked;   // bitmask : bit i=1 -> note permanente (REC), bit i=0 -> temporaire
};

struct Track {
  Step    steps[STEPS_PER_TRACK];
  Step    arpSteps[STEPS_PER_TRACK];
  ArpData arp;
  uint8_t octave;
  bool    muted;
  uint8_t drumKit;
  float   volume;
  uint8_t activePreset;
  uint8_t trackLen;
  uint8_t waveform;
  float   decayTime;
  float   waveMorph;
  float   pwmWidth;
  float   fmRatio;
  float   fmDepth;
  uint8_t wtblIdx;
  float   reverbAmt;
  float   delayAmt;
  float   attackAmt;
  float   chorusAmt;
  float   driveAmt;
  float   filterCutoff;
  float   filterRes;
  float   glideAmt;
  float   lfoDepth;
  float   lfoRate;
  uint8_t lfoDest;
  float   subOscAmt;
  float   noiseAmt;
  float   sustainLevel;
  float   releaseTime;
  uint8_t chordMode;     // 0=off 1=maj 2=min 3=dom7 4=min7
  // OSC2 — second oscillateur superposé (255 = désactivé)
  uint8_t waveform2;   // 0..WAVE_COUNT-1, 255 = NONE
  float   osc2Mix;     // 0.0=OSC1 seul  0.5=50/50  1.0=OSC2 seul
  float   waveMorph2;  // morph indépendant pour OSC2
};

struct Preset {
  uint8_t waveform;
  float   decayTime;
  float   waveMorph;
  float   pwmWidth;
  float   fmRatio;
  float   fmDepth;
  uint8_t wtblIdx;
  float   reverbAmt;
  float   delayAmt;
  float   attackAmt;  // attack time 0..1
  float   chorusAmt;
  float   driveAmt;
  float   lfoDepth;
  float   lfoRate;
  uint8_t lfoDest;
  float   filterCutoff;
  float   filterRes;
  float   glideAmt;
  float   subOscAmt;
  float   noiseAmt;
  float   sustainLevel;
  float   releaseTime;
  uint8_t chordMode;
  uint8_t drumKit;
  uint8_t trackLen;
  // OSC2
  uint8_t waveform2;
  float   osc2Mix;
  float   waveMorph2;
};

struct Voice {
  bool          active;
  float         freq;          // fréquence courante (glide)
  float         targetFreq;    // fréquence cible
  float         phase;
  float         phaseFM;
  float         subPhase;      // phase sous-oscillateur
  float         osc2Phase;     // phase OSC2
  float         amplitude;
  float         envelope;
  float         decayRate;
  float         attackRate;
  float         releaseRate;   // taux de release
  float         glideCoef;     // précalculé : exp(-1/(glideTime*SR))
  bool          attacking;
  bool          releasing;     // phase de release
  bool          sustained;
  uint8_t       trackIdx;
  uint8_t       waveform;
  uint8_t       noteIdx;
  uint8_t       octave;
  unsigned long startTime;
  uint32_t      sustainSamples; // compteur samples pour watchdog (évite millis() dans boucle audio)
  float         filterState;
  float         filterState2;  // 2ème pôle filtre résonant
  float         lfoPhase;      // phase LFO par voix
};

struct DrumVoice {
  bool    active;
  uint8_t drumType;
  float   phase;
  float   phase2;
  float   envelope;
  float   decayRate;
  float   freq;
  float   freq2;
  float   amplitude;
  float   filterState;
};

struct NoteEvent {
  uint8_t trackIdx;
  uint8_t noteIdx;
  uint8_t octave;
  bool    isMetro;
  bool    metroAccent;
  bool    noteOff;
  bool    sustained;
  bool    fromSeq;    // true = tieStart séquenceur, false = live
};

struct UndoState {
  Track   tracks[TRACK_COUNT];
  uint8_t activeTrack;
};

extern const char* TRACK_NAMES[4];
extern const char* PRESET_PREFIXES[4];
extern const char* NOTE_NAMES[13];
extern const char* WAVE_NAMES[WAVE_COUNT];
extern const char* DRUM_NAMES[13];
extern const char* DRUM_KIT_NAMES[4];
extern const char* PARAM_NAMES[PARAM_COUNT];
extern const char* WTBL_NAMES[4];
extern const float NOTE_FREQS_BASE[13];
extern const int   KEY_NOTE_MAP[3][5];
