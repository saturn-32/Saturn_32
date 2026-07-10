// =============================================================================
// STORAGE.INO v1
// =============================================================================
// UNDO: clic court UNDO
// REDO: long press UNDO 1.5s (géré dans matrix.ino)
// activeTrack jamais modifié par undo/redo

#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "globals.h"

// =============================================================================
// STRUCT SAUVEGARDE PROJET
// Définie EN PREMIER pour que sizeof(SavedTrack) soit disponible
// dans checkPresetVersion() ci-dessous.
// RÈGLE : n'ajouter des champs qu'EN FIN de struct — ne jamais réordonner.
// =============================================================================
struct SavedTrack {
  Step    steps[STEPS_PER_TRACK];
  // Paramètres track
  uint8_t octave;
  uint8_t drumKit;
  uint8_t trackLen;
  uint8_t waveform;
  uint8_t chordMode;
  uint8_t lfoDest;
  uint8_t wtblIdx;
  float   volume;
  float   decayTime;
  float   waveMorph;
  float   fmRatio;
  float   fmDepth;
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
  float   sustainLevel;
  float   releaseTime;
  float   subOscAmt;
  float   noiseAmt;
  // ARP
  bool    arpEnabled;
  uint8_t arpDiv;
  uint8_t arpOctaveRange;
  bool    arpLatch;
  uint8_t arpNoteCount;
  uint8_t arpNotes[6];
  uint8_t arpOctaves[6];
  // v9 : champs ajoutés en fin de struct
  float   pwmWidth;
  bool    muted;
  uint8_t activePreset;
  // v10 : OSC2
  uint8_t waveform2;
  float   osc2Mix;
  float   waveMorph2;
};

// =============================================================================
// VERSION CHECKS — presets ET projets
// =============================================================================
#define PRESET_STRUCT_VERSION  ((uint16_t)sizeof(Preset))
#define PROJECT_STRUCT_VERSION ((uint16_t)sizeof(SavedTrack))

// Appelé depuis setup() dans Saturn_32.ino.
// Si la taille de Preset ou SavedTrack a changé depuis le dernier flash,
// les entrées NVS ont une taille différente et prefs.getBytes() retourne
// l'ancienne taille ≠ sizeof(struct), ce qui fait sauter le "if (read!=sizeof)"
// et rien ne se charge. La solution : effacer les entrées obsolètes au démarrage.
void checkPresetVersion() {
  // --- Presets synth ---
  uint16_t storedPr = prefs.getUShort("prVer", 0);
  if (storedPr != PRESET_STRUCT_VERSION) {
    for (uint8_t t = 0; t < TRACK_COUNT; t++)
      for (uint8_t p = 0; p < 4; p++) {
        char key[12]; snprintf(key, sizeof(key), "t%dp%d", (int)t, (int)p);
        prefs.remove(key);
      }
    prefs.putUShort("prVer", PRESET_STRUCT_VERSION);
    Serial.printf("Presets reinitialises (struct=%d bytes)\n", PRESET_STRUCT_VERSION);
  }

  // --- Projets ---
  // Même mécanisme : si sizeof(SavedTrack) a changé, effacer tous les slots.
  // Les projets devront être re-sauvegardés après un flash avec struct modifiée.
  uint16_t storedPj = prefs.getUShort("prjVer", 0);
  if (storedPj != PROJECT_STRUCT_VERSION) {
    for (uint8_t s = 0; s < 4; s++) {
      char key[12];
      snprintf(key, sizeof(key), "s%dbpm", s); prefs.remove(key);
      for (uint8_t t = 0; t < TRACK_COUNT; t++) {
        snprintf(key, sizeof(key), "s%dt%d", s, t); prefs.remove(key);
      }
    }
    prefs.putUShort("prjVer", PROJECT_STRUCT_VERSION);
    Serial.printf("Projets reinitialises (struct=%d bytes)\n", PROJECT_STRUCT_VERSION);
  }
}

// =============================================================================
// DRUM PRESETS
// =============================================================================
void initDrumPresets() {
  uint8_t ver = prefs.getUChar("drumVer", 0);
  if (ver < 3) {
    for (uint8_t p = 0; p < 4; p++) {
      char key[12]; snprintf(key, sizeof(key), "t%dp%d", (int)TRACK_DRUM, (int)p);
      prefs.remove(key);
    }
    prefs.putUChar("drumVer", 3);
  }

  for (uint8_t p = 0; p < 4; p++) {
    char key[12]; snprintf(key, sizeof(key), "t%dp%d", (int)TRACK_DRUM, (int)p);
    Preset pr;
    if (prefs.getBytes(key, &pr, sizeof(pr)) != sizeof(pr)) {
      pr.waveform    = 0;
      pr.decayTime   = 0.10f;
      pr.waveMorph   = 0.0f;
      pr.pwmWidth    = 0.5f;
      pr.fmRatio     = 0.5f;
      pr.fmDepth     = 0.5f;
      pr.wtblIdx     = 0;
      pr.reverbAmt   = 0.0f;
      pr.delayAmt    = 0.0f;
      pr.attackAmt   = 0.0f;
      pr.chorusAmt   = 0.0f;
      pr.driveAmt    = 0.0f;
      pr.lfoDepth    = 0.0f;
      pr.lfoRate     = 0.3f;
      pr.filterCutoff= 1.0f;
      pr.glideAmt    = 0.0f;
      pr.filterRes   = 0.0f;
      pr.lfoDest     = 0;
      pr.subOscAmt   = 0.0f;
      pr.noiseAmt    = 0.0f;
      pr.sustainLevel= 1.0f;
      pr.releaseTime = 0.1f;
      pr.chordMode   = 0;
      pr.drumKit     = p;
      pr.waveform2   = 255;
      pr.osc2Mix     = 0.5f;
      pr.waveMorph2  = 0.5f;
      prefs.putBytes(key, &pr, sizeof(pr));
    }
  }
}

// =============================================================================
// UNDO / REDO
// =============================================================================
void pushUndo() {
  undoHead = (undoHead + 1) % UNDO_STACK_SIZE;
  memcpy(undoStack[undoHead].tracks, tracks, sizeof(tracks));
  undoStack[undoHead].activeTrack = activeTrack;
  if (undoCount < UNDO_STACK_SIZE) undoCount++;
  redoHead  = -1;
  redoCount =  0;
}

void doUndo() {
  if (undoCount <= 0 || undoHead < 0) return;
  redoHead = (redoHead + 1) % UNDO_STACK_SIZE;
  memcpy(redoStack[redoHead].tracks, tracks, sizeof(tracks));
  redoStack[redoHead].activeTrack = activeTrack;
  if (redoCount < UNDO_STACK_SIZE) redoCount++;
  uint8_t save = activeTrack;
  memcpy(tracks, undoStack[undoHead].tracks, sizeof(tracks));
  activeTrack = save;
  undoHead = (undoHead - 1 + UNDO_STACK_SIZE) % UNDO_STACK_SIZE;
  if (undoCount > 0) undoCount--;
  updateEffects();
  Serial.println("UNDO");
}

void doRedo() {
  if (redoCount <= 0 || redoHead < 0) return;
  undoHead = (undoHead + 1) % UNDO_STACK_SIZE;
  memcpy(undoStack[undoHead].tracks, tracks, sizeof(tracks));
  undoStack[undoHead].activeTrack = activeTrack;
  if (undoCount < UNDO_STACK_SIZE) undoCount++;
  uint8_t save = activeTrack;
  memcpy(tracks, redoStack[redoHead].tracks, sizeof(tracks));
  activeTrack = save;
  redoHead = (redoHead - 1 + UNDO_STACK_SIZE) % UNDO_STACK_SIZE;
  if (redoCount > 0) redoCount--;
  updateEffects();
  Serial.println("REDO");
}

// =============================================================================
// SAVE / LOAD PRESET
// =============================================================================
void savePreset(uint8_t t, uint8_t p) {
  char key[12]; snprintf(key, sizeof(key), "t%dp%d", t, p);
  Preset pr;
  pr.waveform    = tracks[t].waveform;
  pr.decayTime   = tracks[t].decayTime;
  pr.waveMorph   = tracks[t].waveMorph;
  pr.pwmWidth    = tracks[t].pwmWidth;
  pr.fmRatio     = tracks[t].fmRatio;
  pr.fmDepth     = tracks[t].fmDepth;
  pr.wtblIdx     = tracks[t].wtblIdx;
  pr.reverbAmt   = tracks[t].reverbAmt;
  pr.delayAmt    = tracks[t].delayAmt;
  pr.attackAmt   = tracks[t].attackAmt;
  pr.chorusAmt   = tracks[t].chorusAmt;
  pr.driveAmt    = tracks[t].driveAmt;
  pr.lfoDepth    = tracks[t].lfoDepth;
  pr.lfoRate     = tracks[t].lfoRate;
  pr.filterCutoff= tracks[t].filterCutoff;
  pr.glideAmt    = tracks[t].glideAmt;
  pr.filterRes   = tracks[t].filterRes;
  pr.lfoDest     = tracks[t].lfoDest;
  pr.subOscAmt   = tracks[t].subOscAmt;
  pr.noiseAmt    = tracks[t].noiseAmt;
  pr.sustainLevel= tracks[t].sustainLevel;
  pr.releaseTime = tracks[t].releaseTime;
  pr.chordMode   = tracks[t].chordMode;
  pr.drumKit     = (t == TRACK_DRUM) ? tracks[t].drumKit : 0;
  pr.trackLen    = tracks[t].trackLen;
  pr.waveform2   = tracks[t].waveform2;
  pr.osc2Mix     = tracks[t].osc2Mix;
  pr.waveMorph2  = tracks[t].waveMorph2;
  esp_task_wdt_reset();
  prefs.putBytes(key, &pr, sizeof(pr));
}

void loadPreset(uint8_t t, uint8_t p) {
  char key[12]; snprintf(key, sizeof(key), "t%dp%d", t, p);
  Preset pr;
  if (prefs.getBytes(key, &pr, sizeof(pr)) == sizeof(pr)) {
    tracks[t].waveform     = pr.waveform;
    tracks[t].decayTime    = pr.decayTime;
    tracks[t].waveMorph    = pr.waveMorph;
    tracks[t].pwmWidth     = pr.pwmWidth;
    tracks[t].fmRatio      = pr.fmRatio;
    tracks[t].fmDepth      = pr.fmDepth;
    tracks[t].wtblIdx      = pr.wtblIdx;
    tracks[t].reverbAmt    = pr.reverbAmt;
    tracks[t].delayAmt     = pr.delayAmt;
    tracks[t].attackAmt    = pr.attackAmt;
    tracks[t].chorusAmt    = pr.chorusAmt;
    tracks[t].driveAmt     = pr.driveAmt;
    tracks[t].lfoDepth     = pr.lfoDepth;
    tracks[t].lfoRate      = pr.lfoRate;
    tracks[t].filterCutoff = pr.filterCutoff;
    tracks[t].glideAmt     = pr.glideAmt;
    tracks[t].filterRes    = pr.filterRes;
    tracks[t].lfoDest      = pr.lfoDest;
    tracks[t].subOscAmt    = pr.subOscAmt;
    tracks[t].noiseAmt     = pr.noiseAmt;
    tracks[t].sustainLevel = pr.sustainLevel;
    tracks[t].releaseTime  = pr.releaseTime;
    tracks[t].chordMode    = pr.chordMode;
    if (t == TRACK_DRUM) tracks[t].drumKit = pr.drumKit % 4;
    tracks[t].trackLen  = (pr.trackLen == 32) ? 32 : 16;
    tracks[t].waveform2 = pr.waveform2;
    tracks[t].osc2Mix   = constrain(pr.osc2Mix, 0.0f, 1.0f);
    tracks[t].waveMorph2 = constrain(pr.waveMorph2, 0.0f, 1.0f);
    updateEffects();
  }
}

// =============================================================================
// SAVE / LOAD PROJECT
// Clés NVS : "s{slot}bpm" = BPM, "s{slot}t{t}" = données piste
// =============================================================================
void saveProject(uint8_t slot) {
  if (slot >= 4) return;
  char key[12];

  // esp_task_wdt_reset() avant chaque prefs.putBytes() :
  // chaque write NVS peut bloquer ~200 ms (erase page flash).
  // Sans ce reset, la loop() bloque assez longtemps pour déclencher le WDT.
  esp_task_wdt_reset();
  snprintf(key, sizeof(key), "s%dbpm", slot);
  prefs.putInt(key, bpm);

  for (uint8_t t = 0; t < TRACK_COUNT; t++) {
    SavedTrack st;
    memset(&st, 0, sizeof(st));  // zéro-init les octets de padding
    memcpy(st.steps, tracks[t].steps, sizeof(tracks[t].steps));
    st.octave         = tracks[t].octave;
    st.drumKit        = tracks[t].drumKit;
    st.trackLen       = tracks[t].trackLen;
    st.waveform       = tracks[t].waveform;
    st.chordMode      = tracks[t].chordMode;
    st.lfoDest        = tracks[t].lfoDest;
    st.wtblIdx        = tracks[t].wtblIdx;
    st.volume         = tracks[t].volume;
    st.decayTime      = tracks[t].decayTime;
    st.waveMorph      = tracks[t].waveMorph;
    st.fmRatio        = tracks[t].fmRatio;
    st.fmDepth        = tracks[t].fmDepth;
    st.reverbAmt      = tracks[t].reverbAmt;
    st.delayAmt       = tracks[t].delayAmt;
    st.attackAmt      = tracks[t].attackAmt;
    st.chorusAmt      = tracks[t].chorusAmt;
    st.driveAmt       = tracks[t].driveAmt;
    st.filterCutoff   = tracks[t].filterCutoff;
    st.filterRes      = tracks[t].filterRes;
    st.glideAmt       = tracks[t].glideAmt;
    st.lfoDepth       = tracks[t].lfoDepth;
    st.lfoRate        = tracks[t].lfoRate;
    st.sustainLevel   = tracks[t].sustainLevel;
    st.releaseTime    = tracks[t].releaseTime;
    st.subOscAmt      = tracks[t].subOscAmt;
    st.noiseAmt       = tracks[t].noiseAmt;
    st.arpEnabled     = tracks[t].arp.enabled;
    st.arpDiv         = tracks[t].arp.div;
    st.arpOctaveRange = tracks[t].arp.octaveRange;
    st.arpLatch       = tracks[t].arp.latch;
    st.arpNoteCount   = tracks[t].arp.noteCount;
    memcpy(st.arpNotes,   tracks[t].arp.notes,   6);
    memcpy(st.arpOctaves, tracks[t].arp.octaves, 6);
    st.pwmWidth       = tracks[t].pwmWidth;
    st.muted          = tracks[t].muted;
    st.activePreset   = tracks[t].activePreset;
    st.waveform2      = tracks[t].waveform2;
    st.osc2Mix        = tracks[t].osc2Mix;
    st.waveMorph2     = tracks[t].waveMorph2;

    snprintf(key, sizeof(key), "s%dt%d", slot, t);
    esp_task_wdt_reset();
    bool ok = prefs.putBytes(key, &st, sizeof(st));
    Serial.printf("saveProject slot=%d track=%d: %s (%d bytes)\n",
                  slot, t, ok ? "OK" : "FAIL", (int)sizeof(st));
  }
  Serial.printf("Projet %d sauvegarde termine\n", slot);
}

void loadProject(uint8_t slot) {
  if (slot >= 4) return;
  char key[12];

  snprintf(key, sizeof(key), "s%dbpm", slot);
  int savedBpm = prefs.getInt(key, 0);
  if (savedBpm > 0) { bpm = savedBpm; calcBpmInterval(); }

  for (uint8_t t = 0; t < TRACK_COUNT; t++) {
    snprintf(key, sizeof(key), "s%dt%d", slot, t);
    SavedTrack st;
    size_t read = prefs.getBytes(key, &st, sizeof(st));
    Serial.printf("loadProject slot=%d track=%d: read=%d expected=%d\n",
                  slot, t, (int)read, (int)sizeof(st));
    if (read != sizeof(st)) continue;

    memcpy(tracks[t].steps, st.steps, sizeof(tracks[t].steps));
    tracks[t].octave       = st.octave;
    tracks[t].drumKit      = st.drumKit;
    tracks[t].trackLen     = st.trackLen;
    tracks[t].waveform     = st.waveform;
    tracks[t].chordMode    = st.chordMode;
    tracks[t].lfoDest      = st.lfoDest;
    tracks[t].wtblIdx      = st.wtblIdx;
    tracks[t].volume       = st.volume;
    tracks[t].decayTime    = st.decayTime;
    tracks[t].waveMorph    = st.waveMorph;
    tracks[t].fmRatio      = st.fmRatio;
    tracks[t].fmDepth      = st.fmDepth;
    tracks[t].reverbAmt    = st.reverbAmt;
    tracks[t].delayAmt     = st.delayAmt;
    tracks[t].attackAmt    = st.attackAmt;
    tracks[t].chorusAmt    = st.chorusAmt;
    tracks[t].driveAmt     = st.driveAmt;
    tracks[t].filterCutoff = st.filterCutoff;
    tracks[t].filterRes    = st.filterRes;
    tracks[t].glideAmt     = st.glideAmt;
    tracks[t].lfoDepth     = st.lfoDepth;
    tracks[t].lfoRate      = st.lfoRate;
    tracks[t].sustainLevel = st.sustainLevel;
    tracks[t].releaseTime  = st.releaseTime;
    tracks[t].subOscAmt    = st.subOscAmt;
    tracks[t].noiseAmt     = st.noiseAmt;
    tracks[t].arp.enabled     = st.arpEnabled;
    tracks[t].arp.div         = st.arpDiv;
    tracks[t].arp.octaveRange = st.arpOctaveRange;
    tracks[t].arp.latch       = st.arpLatch;
    tracks[t].arp.noteCount   = st.arpNoteCount;
    tracks[t].arp.pos         = 0;
    tracks[t].arp.octaveShift = 0;
    memcpy(tracks[t].arp.notes,   st.arpNotes,   6);
    memcpy(tracks[t].arp.octaves, st.arpOctaves, 6);
    tracks[t].pwmWidth     = st.pwmWidth;
    tracks[t].muted        = st.muted;
    tracks[t].activePreset = st.activePreset % 4;
    tracks[t].waveform2    = st.waveform2;
    tracks[t].osc2Mix      = constrain(st.osc2Mix, 0.0f, 1.0f);
    tracks[t].waveMorph2   = constrain(st.waveMorph2, 0.0f, 1.0f);
  }
  stopAllVoices();
  updateEffects();
  Serial.printf("Projet %d charge\n", slot);
}

bool projectExists(uint8_t slot) {
  if (slot >= 4) return false;
  char key[12];
  snprintf(key, sizeof(key), "s%dbpm", slot);
  return prefs.isKey(key);
}
