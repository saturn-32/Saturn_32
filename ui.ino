// =============================================================================
// GROOVEBOX ESP32 v8
// =============================================================================

#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include "config.h"
#include "globals.h"

// =============================================================================
// CONSTANTES
// =============================================================================
const int COL_PINS[5] = { 1,  2,  3,  4,  5};
const int ROW_PINS[5] = { 6,  7,  8,  9, 10};

const char* TRACK_NAMES[4]     = {"DRUM","BASS","SYN1","SYN2"};
const char* PRESET_PREFIXES[4] = {"KIT", "BAS", "SY1", "SY2"};
const char* NOTE_NAMES[13]     = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B","C'"};
const char* WAVE_NAMES[WAVE_COUNT] = {
  "SIN","SQR","SAW","NOI","TRI","PLS","FM","WTBL",
  "HARM","FOLD","WARM","SUB","CHEW"
};
const char* PARAM_NAMES[PARAM_COUNT] = {
  "FORME","DECAY","-----","REVERB","ATTK","CHORU","DRIVE",
  "FILTR","GLIDE","LFODP","LFORT","LFDST","SUB","NOISE","FILRS","SUST","REL"
};
const char* WTBL_NAMES[4] = {"ORGUE","CLOCH","VIOLO","FLUTE"};

const float NOTE_FREQS_BASE[13] = {
  261.63f,277.18f,293.66f,311.13f,329.63f,
  349.23f,369.99f,392.00f,415.30f,440.00f,
  466.16f,493.88f,523.25f
};

const int KEY_NOTE_MAP[3][5] = {
  { 0, 1, 2, 3, 4},
  { 5, 6, 7, 8, 9},
  {10,11,12,-1,-1}
};

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences      prefs;

Track   tracks[TRACK_COUNT];
uint8_t activeTrack = 0;

volatile bool    isPlaying     = false;
volatile bool    isRecording   = false;
bool    metronomeOn   = false;
volatile uint8_t currentStep   = 0;
int     bpm           = 120;
volatile unsigned long lastStepUs     = 0;
volatile unsigned long stepIntervalUs = 0;

bool    stepEditMode   = false;
uint8_t editCursor     = 0;
int     tieAnchorStep  = -1;
int     tieAnchorNote  = -1;
bool    tieAnchorWasExisting = false;
bool    stepEditGate   = false;
uint8_t stepViewOffset = 0;   // page visible : 0=steps 0-15, 16=steps 16-31
bool    presetMenuOpen = false;
int     presetCursor   = 0;
int     loadCursor     = -1;
bool    waveMenuOpen   = false;
uint8_t waveMenuParam  = 0;
uint8_t waveMenuPage   = 0;    // 0=OSC 1=FLT 2=LFO 3=FX
bool    wavePickerOpen  = false;
uint8_t wavePickerPage  = 0;     // 0=OSC1  1=OSC2
bool    wavePickerMixOpen = false;
bool    projectMenuOpen  = false;
uint8_t projectSlot      = 0;
bool    projectSaveMode  = true;
uint8_t projectPage      = 0;    // 0=PROJET 1=PRESET
bool    chordMenuOpen    = false;
bool    effectMenuOpen   = false;
int8_t  effectCursor     = 0;
int8_t  activeEffect     = -1;  // -1=aucun
uint8_t effectIntensity  = 4;   // 0-8
bool    arpOctMenuOpen   = false;

bool enc2ComboUsed  = false; // ENC1+ENC2 combo utilisé → bloquer clic ENC2
TaskHandle_t audioTaskHandle = nullptr;
TaskHandle_t seqTaskHandle   = nullptr;
bool saveConfirmOpen      = false;
bool saveConfirmIsProject = false;
bool saveConfirmCursor    = false;

bool          keyState[5][5]       = {};
unsigned long keyDownMs[5][5]      = {};
unsigned long keyLastRelease[5][5] = {};
bool          keyLongDone[5][5]    = {};

bool bpmHeld      = false;
bool nextStepHeld = false;

UndoState undoStack[UNDO_STACK_SIZE];
int undoHead  = -1;
int undoCount =  0;
UndoState redoStack[UNDO_STACK_SIZE];
int redoHead  = -1;
int redoCount =  0;
bool playJustStarted = false;

bool    preCountActive   = false;
uint8_t preCountVal      = 4;
unsigned long preCountLastMs = 0;

Voice      voices[MAX_VOICES];
DrumVoice  drumVoices[4];
SemaphoreHandle_t voiceMutex = nullptr;
float      masterVolume = 0.75f;

float  eff_delay      = 0.0f;  // reverb synths
float  eff_delay_drum = 0.0f;  // reverb drum

float*  delayBuf     = nullptr;
int     delayBufIdx  = 0;
float*  delayBufDrum = nullptr;
int     delayBufDrumIdx = 0;
float*   stutterBuf    = nullptr; // buffer stutter PSRAM
uint32_t stutterBufLen = 0;
float*  chorusBufs[TRACK_COUNT]    = {nullptr, nullptr, nullptr, nullptr};
int     chorusBufIdxs[TRACK_COUNT] = {0, 0, 0, 0};
float   chorusLFOPhases[TRACK_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
uint32_t   lfsrState = 0xACE1u;
QueueHandle_t noteQueue = nullptr;

// =============================================================================
// SETUP
// =============================================================================
void seqTask(void*) {
  for (;;) {
    updateSequencer();
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);  // SDA=39 SCL=38 (inverse a la soudure)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED KO"); while (true) delay(1000);
  }
  // ── Animation de boot ─────────────────────────────────────────────────────
  playBootAnimation(); // défini dans animation.ino

  delayBuf     = (float*)calloc(DELAY_BUF_LEN, sizeof(float));
  delayBufDrum = (float*)calloc(DELAY_BUF_LEN, sizeof(float));
  stutterBufLen = 2048; // ~93ms a 22050Hz
  stutterBuf = (float*)ps_malloc(stutterBufLen * sizeof(float));
  if (stutterBuf) memset(stutterBuf, 0, stutterBufLen * sizeof(float));
  // stutterBuf optionnel : si PSRAM insuffisante, le stutter est desactive (guard dans audio.ino)
  bool heapOk = (delayBuf != nullptr && delayBufDrum != nullptr);
  for (int t = 0; t < TRACK_COUNT; t++) {
    chorusBufs[t] = (float*)calloc(CHORUS_BUF_LEN, sizeof(float));
    if (!chorusBufs[t]) heapOk = false;
  }
  if (!heapOk) {
    display.clearDisplay(); display.setCursor(0,0);
    display.println("ERR HEAP"); display.display();
    while (true) delay(1000);
  }

  initTracks();
  initMatrix();
  initDrumSamplesRAM(); // copie samples PROGMEM → PSRAM pour lecture rapide
  initScratchBuffer();  // buffer audio pour effets scratch

  initEncoders();

  prefs.begin("gbox", false);
  checkPresetVersion();   // efface les presets si la struct a changé
  initDrumPresets();      // pré-initialise les 4 kits drum si flash vierge
  for (int t = 0; t < TRACK_COUNT; t++) {
    loadPreset(t, 0);
    // Forcer le decay à 10% — écrase les vieilles valeurs sauvegardées en flash
    if (tracks[t].decayTime > 0.10f) tracks[t].decayTime = 0.10f;
  }

  calcBpmInterval();

  voiceMutex = xSemaphoreCreateMutex();
  noteQueue  = xQueueCreate(128, sizeof(NoteEvent));

  for (int v = 0; v < 4; v++) drumVoices[v].active = false;

  initAudio();
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 5, nullptr, 0);
  // Tâche séquenceur : priorité 4 sur Core 1
  xTaskCreatePinnedToCore(seqTask, "seq", 4096, nullptr, 4, nullptr, 1);

  delay(400);
  display.clearDisplay(); display.display();
  Serial.println("v8 OK");
}

void loop() {
  static bool lastPlaying = false;
  if (isPlaying != lastPlaying) {
    Serial.printf("isPlaying changé: %d->%d\n", lastPlaying, (bool)isPlaying);
    if (!isPlaying) {
      // Afficher la call stack simulée via un flag
      Serial.printf("  currentStep=%d halfStep=%d\n", currentStep, halfStepCount);
    }
    lastPlaying = isPlaying;
  }
  scanMatrix();
  handleEncoders();
  // updateSequencer() est maintenant dans seqTask — ne plus l'appeler ici

  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 40) {
    lastDraw = millis();
    drawUI();
  }
}

// =============================================================================
// INIT PISTES
// =============================================================================
void initTracks() {
  for (int t = 0; t < TRACK_COUNT; t++) {
    tracks[t].octave        = 3;
    tracks[t].volume        = 0.75f;
    tracks[t].drumKit       = 0;
    tracks[t].muted         = false;
    tracks[t].activePreset  = 0;
    tracks[t].waveform      = (t == TRACK_DRUM) ? WAVE_NOISE :
                              (t == TRACK_BASS) ? WAVE_SAW   :
                              (t == TRACK_SYNTH1)? WAVE_SINE  : WAVE_SQUARE;
    tracks[t].decayTime     = (t == TRACK_DRUM) ? 0.10f : 0.10f;
    tracks[t].waveMorph     = 0.5f;  // centre = forme neutre
    tracks[t].pwmWidth      = 0.5f;
    tracks[t].fmRatio       = 0.3f;
    tracks[t].fmDepth       = 0.3f;
    tracks[t].wtblIdx       = 0;
    tracks[t].reverbAmt     = 0.0f;
    tracks[t].delayAmt      = 0.0f;
    tracks[t].attackAmt   = 0.0f;
    tracks[t].chorusAmt     = 0.0f;
    tracks[t].driveAmt      = 0.0f;
    tracks[t].lfoDepth      = 0.0f;
    tracks[t].lfoRate       = 0.3f;
    tracks[t].filterCutoff  = 1.0f;
    tracks[t].filterRes     = 0.0f;
    tracks[t].glideAmt      = 0.0f;
    // lfoDepth et lfoRate définis ci-dessus — pas de doublon
    tracks[t].lfoDest       = 0;
    tracks[t].subOscAmt     = 0.0f;
    tracks[t].noiseAmt      = 0.0f;
    tracks[t].sustainLevel  = 1.0f;
    tracks[t].releaseTime   = 0.1f;
    tracks[t].chordMode     = 0;
    tracks[t].trackLen      = 16;   // 16 steps par défaut
    tracks[t].waveform2     = 255;  // OSC2 désactivé par défaut
    tracks[t].osc2Mix       = 0.5f; // 50/50 prêt à l'emploi
    tracks[t].waveMorph2    = 0.5f; // morph OSC2 neutre
    tracks[t].arp.enabled   = false;
    tracks[t].arp.noteCount = 0;
    for (int s = 0; s < STEPS_PER_TRACK; s++) {
      tracks[t].steps[s].active    = false;
      tracks[t].steps[s].noteCount = 0;
      tracks[t].steps[s].stepDecay = 1;
      tracks[t].steps[s].drumOffset= false;
      tracks[t].arpSteps[s].active    = false;
      tracks[t].arpSteps[s].noteCount = 0;
      tracks[t].arpSteps[s].stepDecay = 1;
      tracks[t].arpSteps[s].drumOffset= false;
    }
  }
  for (int v = 0; v < MAX_VOICES; v++) voices[v].active = false;
}
