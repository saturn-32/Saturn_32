#pragma once
#include "config.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

extern Adafruit_SSD1306 display;
extern Preferences      prefs;

extern Track   tracks[TRACK_COUNT];
extern uint8_t activeTrack;

extern volatile bool    isPlaying;
extern volatile bool    isRecording;
extern bool    metronomeOn;
extern volatile uint8_t currentStep;
extern uint8_t halfStepCount;   // 0=début step, 1=milieu step
extern int     bpm;
extern volatile unsigned long lastStepUs;
extern volatile unsigned long stepIntervalUs;

extern bool    stepEditMode;
extern uint8_t editCursor;
extern int     tieAnchorStep; // step de départ du tie en stepEdit (-1 si inactif)
extern int     tieAnchorNote;    // note du tie en stepEdit
extern bool    tieAnchorWasExisting;
extern bool    stepEditGate;   // true = ENC2 modifie le gate/offset du step sélectionné
extern uint8_t stepViewOffset; // 0 = steps 0-15 visibles, 16 = steps 16-31 visibles
extern bool    presetMenuOpen;
extern int     presetCursor;
extern int     loadCursor;
extern bool    waveMenuOpen;
extern uint8_t waveMenuParam;   // 0..PARAM_COUNT-1
extern uint8_t waveMenuPage;    // 0=OSC 1=FLT 2=LFO 3=FX
extern bool    wavePickerOpen;  // wave picker temps réel (long press ENC1)
extern uint8_t wavePickerPage;  // 0=OSC1  1=OSC2
extern bool    wavePickerMixOpen; // true = barre de mix OSC1/OSC2 affichée
extern bool    projectMenuOpen;
extern uint8_t projectSlot;
extern bool    projectSaveMode;
extern uint8_t projectPage;    // 0=PROJET 1=PRESET
extern bool    chordMenuOpen;
extern bool    enc2ComboUsed;
extern TaskHandle_t audioTaskHandle;
extern TaskHandle_t seqTaskHandle;
// Popup confirmation save
extern bool    saveConfirmOpen;  // popup ouvert
extern bool    saveConfirmIsProject; // true=projet false=preset
extern bool    saveConfirmCursor;    // false=YES true=NO
extern bool    effectMenuOpen;
extern int8_t  effectCursor;   // 0-7 curseur dans le menu effet
extern int8_t  activeEffect;   // -1=aucun, 0-7=effet actif
extern uint8_t effectIntensity; // 0-8 intensité
extern bool    arpOctMenuOpen;  // menu octave range ARP

extern bool enc1BtnLast, enc2BtnLast;
extern unsigned long enc1BtnDown, enc2BtnDown;
extern bool enc1LongFired, enc2LongFired;

extern bool          keyState[5][5];
extern unsigned long keyDownMs[5][5];
extern unsigned long keyLastRelease[5][5];
extern bool          keyLongDone[5][5];

extern bool bpmHeld;
extern bool nextStepHeld;

extern UndoState undoStack[UNDO_STACK_SIZE];
extern UndoState redoStack[UNDO_STACK_SIZE];
extern int undoHead;
extern int undoCount;
extern int redoHead;
extern int redoCount;

extern bool    preCountActive;
extern uint8_t preCountVal;
extern unsigned long preCountLastMs;
extern bool    playJustStarted;

extern Voice       voices[MAX_VOICES];
extern DrumVoice   drumVoices[4];
extern SemaphoreHandle_t voiceMutex;
extern float       masterVolume;

// Delay/reverb synths (BASS/SYN1/SYN2) et drums — buffers séparés
extern float  eff_delay;      // reverb synths
extern float  eff_delay_drum; // reverb drum indépendante

// Buffers effets
extern float*  delayBuf;       // delay synths
extern int     delayBufIdx;
extern float*  delayBufDrum;   // delay drum
extern int     delayBufDrumIdx;
extern float*  stutterBuf;     // buffer stutter (PSRAM)
extern uint32_t stutterBufLen; // longueur allouee
extern float*  chorusBufs[TRACK_COUNT];   // un buffer par piste
extern int     chorusBufIdxs[TRACK_COUNT];
extern float   chorusLFOPhases[TRACK_COUNT];
// (reverbAcc supprimé — reverb remplacée par delay dans l'UI)



extern uint32_t    lfsrState;
extern QueueHandle_t noteQueue;

struct PendingOff {
  bool    active;
  uint8_t trackIdx;
  uint8_t noteIdx;
  uint8_t octave;
};
extern PendingOff pendingOffs[TRACK_COUNT * MAX_NOTES_PER_STEP];

// ---- Prototypes partagés ----
void liveNoteOn(uint8_t t, uint8_t noteIdx, uint8_t octave);
void liveNoteOff(uint8_t t, uint8_t noteIdx, uint8_t octave);
void liveNotesAdvance();
void clearLiveNotes();
void markDrumManualTrigger(uint8_t noteIdx);
void calcBpmInterval();
void updateEffects();
void triggerNote(uint8_t t, uint8_t noteIdx, uint8_t octave);
void triggerNoteWithDecay(uint8_t t, uint8_t noteIdx, uint8_t octave, float decayMult);
void triggerNoteSustained(uint8_t t, uint8_t noteIdx, uint8_t octave);
void triggerNoteSustainedSeq(uint8_t t, uint8_t noteIdx, uint8_t octave);
void triggerNoteOff(uint8_t t, uint8_t noteIdx, uint8_t octave);
void triggerMetro();
void triggerMetroAccent(bool accent);
void triggerDrum(uint8_t drumType);
void stopAllVoices();
void releaseAllSustainedForTrack(uint8_t t);
void initScratchBuffer();
extern float scratchPitchFactor; // 1.0=normal, >1=pitch haut, <1=pitch bas, 0=silence
void enterArpMode(uint8_t t);
void exitArpMode(uint8_t t);
void pushUndo();
void doUndo();
void doRedo();
void savePreset(uint8_t t, uint8_t p);
void loadPreset(uint8_t t, uint8_t p);
void saveProject(uint8_t slot);
void loadProject(uint8_t slot);
bool projectExists(uint8_t slot);
void checkPresetVersion();
void initDrumPresets();
void initDrumSamplesRAM();
void initAudio();
void audioTask(void* param);
void initMatrix();
void initEncoders();
void scanMatrix();
void handleEncoders();
void updateSequencer();
void seqTask(void*);
void drawUI();
void drawPresetSaveAnim();
void drawSaveNotif(const char* line1, const char* line2);
void drawSaveConfirm();
void drawWaveShape(int bx, int by, int bw, int bh, uint8_t w, uint16_t col);
void drawProjectMenu();
void drawChordMenu();
void drawArpOctMenu();
void _activateVoice(uint8_t ti, float freq, uint8_t wave, float amp,
                    float decayRate, bool sustained, uint8_t noteIdx, uint8_t octave);
void _releaseVoice(uint8_t ti, uint8_t noteIdx, uint8_t octave);
void _triggerDrumVoice(uint8_t noteIdx);
float synthSample();
float drumSample();
float getFreqFromNote(uint8_t noteIdx, uint8_t octave);
float calcDecayRate(uint8_t t);
void onPresetClick();
uint8_t buildChord(uint8_t rootNote, uint8_t rootOctave,
                   uint8_t chordType, uint8_t* notes, uint8_t* octaves);
