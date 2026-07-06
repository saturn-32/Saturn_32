// =============================================================================
// MATRIX.INO v8
// =============================================================================
// Debounce: fenêtre aveugle KEY_DEBOUNCE_MS sur front montant uniquement.
//
// BOUTON UNDO (r=4,c=2):
//   - Clic court  -> UNDO
//   - Long press  -> REDO
//
// ERASE (r=4,c=1):
//   - Clic court  -> efface piste active (action au RELÂCHEMENT si pas long press)
//   - Long press  -> master reset toutes pistes
//
// REC (r=3,c=2), NEXT STEP (r=3,c=3): inchangés (action au relâchement)

#include <Arduino.h>
#include "config.h"
#include "globals.h"
// Prototypes internes
void handleKeyPress(int r, int c);
void handleKeyRelease(int r, int c, unsigned long held);
void checkLongPress(int r, int c, unsigned long held);


static bool arpComboFired  = false;
static bool muteComboFired = false;

// =============================================================================
// CHORD MODE — intervalles en demi-tons depuis la note fondamentale
// =============================================================================
// noteIdx = 0..12 (chromatique), les accords wrap dans l'octave
static const int8_t CHORD_INTERVALS[4][3] = {
  { 4,  7,  0 }, // Majeur     : +4  +7
  { 3,  7,  0 }, // Mineur     : +3  +7
  { 4,  7, 10 }, // Dom7       : +4  +7  +10
  { 3,  7, 10 }, // Mineur7    : +3  +7  +10
};
static const uint8_t CHORD_SIZES[4] = { 2, 2, 3, 3 };

// Remplit notes[]/octaves[] avec les notes de l'accord, retourne le nombre total
uint8_t buildChord(uint8_t rootNote, uint8_t rootOctave,
                           uint8_t chordType,
                           uint8_t* notes, uint8_t* octaves) {
  notes[0]   = rootNote;
  octaves[0] = rootOctave;
  uint8_t n = 1;
  uint8_t sz = CHORD_SIZES[chordType - 1]; // chordType 1..4
  for (int i = 0; i < sz && n < MAX_NOTES_PER_STEP; i++) {
    int8_t iv = CHORD_INTERVALS[chordType - 1][i];
    if (iv == 0) continue;
    int noteAbs = (int)rootNote + iv;
    uint8_t oct = rootOctave;
    while (noteAbs >= 13) { noteAbs -= 13; oct++; }
    notes[n]   = (uint8_t)noteAbs;
    octaves[n] = (oct < 7) ? oct : 6;
    n++;
  }
  return n;
}

void initMatrix() {
  for (int c = 0; c < 5; c++) {
    pinMode(COL_PINS[c], OUTPUT);
    digitalWrite(COL_PINS[c], HIGH);
  }
  for (int r = 0; r < 5; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLUP);
  }
}

// =============================================================================
// SCAN
// =============================================================================
void scanMatrix() {
  unsigned long now = millis();

  for (int c = 0; c < 5; c++) {
    digitalWrite(COL_PINS[c], LOW);
    delayMicroseconds(10);

    for (int r = 0; r < 5; r++) {
      bool hw = (digitalRead(ROW_PINS[r]) == LOW);

      if (hw && !keyState[r][c]) {
        // Front montant — fenêtre aveugle depuis dernière pression ET dernier relâchement
        unsigned long sinceDown    = now - keyDownMs[r][c];
        unsigned long sinceRelease = now - keyLastRelease[r][c];
        if (sinceDown >= KEY_DEBOUNCE_MS && sinceRelease >= KEY_DEBOUNCE_MS) {
          keyState[r][c]    = true;
          keyDownMs[r][c]   = now;
          keyLongDone[r][c] = false;
          handleKeyPress(r, c);
        }
      } else if (!hw && keyState[r][c]) {
        keyState[r][c]       = false;
        keyLastRelease[r][c] = now;
        handleKeyRelease(r, c, now - keyDownMs[r][c]);
      }

      if (keyState[r][c] && !keyLongDone[r][c]) {
        checkLongPress(r, c, now - keyDownMs[r][c]);
      }
    }

    digitalWrite(COL_PINS[c], HIGH);
  }
}

// =============================================================================
// APPUI — actions immédiates uniquement (pas ERASE/REC/NEXT STEP/UNDO)
// =============================================================================
void handleKeyPress(int r, int c) {

  // ---- Notes lignes 0-2 ----
  if (r < 3) {
    int ni = KEY_NOTE_MAP[r][c];
    if (ni < 0) return;

    if (activeTrack == TRACK_DRUM) {
      triggerDrum((uint8_t)ni);
      if (isRecording && isPlaying) {
        // Marquer ce drum comme déclenché manuellement
        // → le séquenceur ne le rejouera pas pendant 1 step
        markDrumManualTrigger((uint8_t)ni);
        uint8_t drumStep = currentStep % tracks[TRACK_DRUM].trackLen;
        Step& s = tracks[TRACK_DRUM].steps[drumStep];
        // Vérifier que cette note n'est pas déjà sur ce step
        bool already = false;
        for (int n = 0; n < s.noteCount; n++)
          if (s.slots[n].note == (uint8_t)ni) { already = true; break; }
        if (!already) {
          s.active = true;
          if (s.noteCount < MAX_NOTES_PER_STEP) {
            s.slots[s.noteCount] = {(uint8_t)ni, 3, TIE_NONE};
            s.drumOffset = false;
            s.noteCount++;
          }
        }
      }
      if (stepEditMode) {
        pushUndo();
        Step& s = tracks[TRACK_DRUM].steps[editCursor];
        s.active = true;
        if (s.noteCount < MAX_NOTES_PER_STEP) {
          s.slots[s.noteCount] = {(uint8_t)ni, 3, TIE_NONE};
          s.noteCount++;
        }
      }
      return;
    }

    if (keyState[3][0] && keyState[3][1]) {
      tracks[activeTrack].arp.enabled ? exitArpMode(activeTrack) : enterArpMode(activeTrack);
      return;
    }
    if (tracks[activeTrack].arp.enabled) {
      ArpData& a = tracks[activeTrack].arp;
      // Ajouter la note au buffer ARP (toujours — l'ARP doit fonctionner en live).
      // En REC : les notes tenues constituent la boucle permanente (comme avant).
      // Hors REC : les notes s'ajoutent tant qu'elles sont tenues, disparaissent au relâchement.
      bool found = false;
      for (int i = 0; i < a.noteCount; i++)
        if (a.notes[i] == (uint8_t)ni && a.octaves[i] == tracks[activeTrack].octave) { found = true; break; }
      if (!found && a.noteCount < 6) {
        a.notes[a.noteCount]   = (uint8_t)ni;
        a.octaves[a.noteCount] = tracks[activeTrack].octave;
        a.noteCount++;
        if (a.pos >= a.noteCount) a.pos = 0;
      }
      return;
    }
    if (stepEditMode) {
      Step& s = tracks[activeTrack].steps[editCursor];
      uint8_t tLen = tracks[activeTrack].trackLen;

      // Sauvegarder avant chaque modification de note
      pushUndo();

      // Avec NoteSlot : chaque note a son propre état tie — pas de conflit possible
      // Trouver si cette note est déjà dans le step
      int slotIdx = -1;
      for (int k = 0; k < s.noteCount; k++)
        if (s.slots[k].note == (uint8_t)ni && s.slots[k].octave == tracks[activeTrack].octave)
          { slotIdx = k; break; }

      if (slotIdx < 0) {
        // Note absente : ajouter comme TIE_NONE (note courte)
        if (s.noteCount < MAX_NOTES_PER_STEP) {
          s.slots[s.noteCount] = {(uint8_t)ni, tracks[activeTrack].octave, TIE_NONE};
          s.noteCount++;
          s.active = true;
        }
      }
      // Jouer en preview
      triggerNote(activeTrack, (uint8_t)ni, tracks[activeTrack].octave);
      return;
    }
    // Jouer la note + enregistrement si en REC
    if (tracks[activeTrack].chordMode > 0 && activeTrack != TRACK_DRUM) {
      uint8_t cNotes[MAX_NOTES_PER_STEP], cOcts[MAX_NOTES_PER_STEP];
      uint8_t cn = buildChord((uint8_t)ni, tracks[activeTrack].octave,
                               tracks[activeTrack].chordMode, cNotes, cOcts);
      for (int i = 0; i < cn; i++) {
        if (isPlaying) liveNoteOn(activeTrack, cNotes[i], cOcts[i]);
        triggerNoteSustained(activeTrack, cNotes[i], cOcts[i]);
      }
    } else {
      if (isPlaying) liveNoteOn(activeTrack, (uint8_t)ni, tracks[activeTrack].octave);
      triggerNoteSustained(activeTrack, (uint8_t)ni, tracks[activeTrack].octave);
    }
    return;
  }

  // ---- Ligne r=3 ----
  if (r == 3) {
    if (c == 0 && keyState[3][1]) {
      // OCT+ était déjà tenu → il a fait octave++ → on annule avec octave--
      if (!arpComboFired) {
        arpComboFired = true;
        if (tracks[activeTrack].octave > 0) tracks[activeTrack].octave--; // annule le ++ du OCT+
        tracks[activeTrack].arp.enabled ? exitArpMode(activeTrack) : enterArpMode(activeTrack);
      }
      return;
    }
    if (c == 1 && keyState[3][0]) {
      // OCT- était déjà tenu → il a fait octave-- → on annule avec octave++
      if (!arpComboFired) {
        arpComboFired = true;
        if (tracks[activeTrack].octave < 6) tracks[activeTrack].octave++; // annule le -- du OCT-
        tracks[activeTrack].arp.enabled ? exitArpMode(activeTrack) : enterArpMode(activeTrack);
      }
      return;
    }
    switch (c) {
      case 0:
        // MUTE + OCT- = passer en 16 steps (combo)
        if (keyState[3][4]) {
          tracks[activeTrack].trackLen = 16;
          stepViewOffset = 0;
          muteComboFired = true;
          return;
        }
        arpComboFired = false;
        if (tracks[activeTrack].octave > 0) {
          releaseAllSustainedForTrack(activeTrack);
          clearLiveNotes();
          // Vider le buffer ARP hors REC/latch avant de changer l'octave
          if (tracks[activeTrack].arp.enabled &&
              !tracks[activeTrack].arp.latch && !isRecording) {
            tracks[activeTrack].arp.noteCount  = 0;
            tracks[activeTrack].arp.pos        = 0;
            tracks[activeTrack].arp.noteLocked = 0;
          }
          tracks[activeTrack].octave--;
        }
        break;
      case 1:
        // MUTE + OCT+ = passer en 32 steps (combo)
        if (keyState[3][4]) {
          tracks[activeTrack].trackLen = 32;
          muteComboFired = true;
          return;
        }
        arpComboFired = false;
        if (tracks[activeTrack].octave < 6) {
          releaseAllSustainedForTrack(activeTrack);
          clearLiveNotes();
          // Vider le buffer ARP hors REC/latch avant de changer l'octave
          if (tracks[activeTrack].arp.enabled &&
              !tracks[activeTrack].arp.latch && !isRecording) {
            tracks[activeTrack].arp.noteCount  = 0;
            tracks[activeTrack].arp.pos        = 0;
            tracks[activeTrack].arp.noteLocked = 0;
          }
          tracks[activeTrack].octave++;
        }
        break;
      case 2: break; // REC: au relâchement
      case 3: nextStepHeld = true; break; // NEXT STEP: au relâchement
      case 4:
        isRecording = false;
        muteComboFired = false; // reset au press, sera leve si combo OCT utilise
        break;
    }
    return;
  }

  // ---- Ligne r=4 ----
  if (r == 4) {
    switch (c) {
      case 0: // PLAY ALL — immédiat
        if (isPlaying) {
          isPlaying = false; isRecording = false; currentStep = 0;
          Serial.println("PLAY STOP");
          stopAllVoices();
          clearLiveNotes();
          for (int p = 0; p < TRACK_COUNT * MAX_NOTES_PER_STEP; p++)
            pendingOffs[p].active = false;
        } else {
          isPlaying = true; currentStep = 0; halfStepCount = 0; lastStepUs = micros(); playJustStarted = true;
          Serial.printf("PLAY START: bpm=%d stepIntervalUs=%lu\n", bpm, stepIntervalUs);
        }
        break;
      case 1: break; // ERASE: au relâchement (pour distinguer court/long)
      case 2: break; // UNDO:  au relâchement (pour distinguer court/long press wave picker)
      case 3: bpmHeld = true; break;
      default: break;
    }
  }
}

// =============================================================================
// RELÂCHEMENT
// =============================================================================
void handleKeyRelease(int r, int c, unsigned long held) {
  unsigned long now = millis();

  // Note OFF
  if (r < 3) {
    int ni = KEY_NOTE_MAP[r][c];
    if (ni >= 0 && activeTrack != TRACK_DRUM) {
      if (tracks[activeTrack].arp.enabled) {
        ArpData& a = tracks[activeTrack].arp;
        // Hors REC et hors latch : retirer la note au relâchement (comportement live normal)
        // En REC ou latch : garder la note dans le buffer (boucle verrouillée)
        if (!a.latch && !isRecording) {
          for (int i = 0; i < a.noteCount; i++) {
            if (a.notes[i] == (uint8_t)ni && a.octaves[i] == tracks[activeTrack].octave) {
              for (int j = i; j < a.noteCount - 1; j++) {
                a.notes[j]   = a.notes[j+1];
                a.octaves[j] = a.octaves[j+1];
              }
              a.noteCount--;
              if (a.pos >= a.noteCount && a.noteCount > 0) a.pos = 0;
              break;
            }
          }
        }
        return; // pas d'enregistrement en mode arp
      }
      // Reset ancre tie stepEdit au relâchement
      tieAnchorStep = -1;
      tieAnchorNote = -1;
      // Chord mode : libérer toutes les notes de l'accord
      if (tracks[activeTrack].chordMode > 0) {
        uint8_t cNotes[MAX_NOTES_PER_STEP], cOcts[MAX_NOTES_PER_STEP];
        uint8_t cn = buildChord((uint8_t)ni, tracks[activeTrack].octave,
                                 tracks[activeTrack].chordMode, cNotes, cOcts);
        for (int i = 0; i < cn; i++) {
          if (isPlaying) liveNoteOff(activeTrack, cNotes[i], cOcts[i]);
          triggerNoteOff(activeTrack, cNotes[i], cOcts[i]);
        }
      } else {
        if (isPlaying) liveNoteOff(activeTrack, (uint8_t)ni, tracks[activeTrack].octave);
        triggerNoteOff(activeTrack, (uint8_t)ni, tracks[activeTrack].octave);
      }
    }
    return;
  }

  // REC (r=3,c=2)
  if (r == 3 && c == 2) {
    if (!keyLongDone[r][c]) {
      // En mode ARP : REC = toggle latch
      if (tracks[activeTrack].arp.enabled) {
        ArpData& a = tracks[activeTrack].arp;
        if (!a.latch) {
          // Activer latch — verrouiller les notes actuellement tenues
          a.latch = true;
          // Si aucune note tenue, pas de latch utile
          if (a.noteCount == 0) a.latch = false;
        } else {
          // Désactiver latch — vider le buffer
          a.latch     = false;
          a.noteCount = 0;
          a.pos       = 0;
        }
        return;
      }
      if (preCountActive) { preCountActive = false; }
      else if (isRecording) { isRecording = false; }
      else {
        pushUndo(); isRecording = true;
        if (!isPlaying) { isPlaying = true; currentStep = 0; halfStepCount = 0; lastStepUs = micros(); playJustStarted = true; }
      }
    }
    return;
  }

  // NEXT STEP (r=3,c=3)
  if (r == 3 && c == 3) {
    nextStepHeld = false;
    if (!keyLongDone[r][c]) {
      // Bloquer le changement d'instrument si l'appui était long
      // (l'utilisateur visait step edit mais a relâché juste avant le seuil)
      if (held >= 250) return;
      if (stepEditMode) stepEditMode = false;
      else {
        clearLiveNotes();
          // Vider le buffer ARP hors REC/latch avant changement de contexte
          if (tracks[activeTrack].arp.enabled &&
              !tracks[activeTrack].arp.latch && !isRecording) {
            tracks[activeTrack].arp.noteCount  = 0;
            tracks[activeTrack].arp.pos        = 0;
            tracks[activeTrack].arp.noteLocked = 0;
          }
        activeTrack = (activeTrack + 1) % TRACK_COUNT;
      }
    }
    return;
  }

  // MUTE (r=3,c=4) — action au relâchement pour permettre le combo avec OCT
  if (r == 3 && c == 4) {
    if (!muteComboFired) {
      // Pas de combo : action normale = toggle mute
      tracks[activeTrack].muted = !tracks[activeTrack].muted;
      if (tracks[activeTrack].muted)
        releaseAllSustainedForTrack(activeTrack);
    }
    muteComboFired = false;
    return;
  }

  // ERASE (r=4,c=1) — clic court au relâchement si pas long press
  if (r == 4 && c == 1) {
    if (!keyLongDone[r][c]) {
      // Si un menu est ouvert → ECHAP (fermer sans effacer)
      if (waveMenuOpen || wavePickerOpen || presetMenuOpen ||
          projectMenuOpen || chordMenuOpen || arpOctMenuOpen || effectMenuOpen) {
        waveMenuOpen    = false;
        wavePickerOpen  = false;
        presetMenuOpen  = false;
        projectMenuOpen = false;
        chordMenuOpen   = false;
        arpOctMenuOpen  = false;
        effectMenuOpen  = false;
        activeEffect    = -1;
        loadCursor = -1;
        return;
      }
      pushUndo();
      if (stepEditMode) {
        Step& s = tracks[activeTrack].steps[editCursor];
        s.active    = false;
        s.noteCount = 0;
        // Pas de stopAllVoices : les voix s'éteignent naturellement
      } else if (tracks[activeTrack].arp.enabled) {
        tracks[activeTrack].arp.noteCount = 0;
        tracks[activeTrack].arp.pos       = 0;
        tracks[activeTrack].arp.latch     = false;
        isRecording = false; // stopper le REC — le buffer est vide, rien à looper
        for (int s = 0; s < STEPS_PER_TRACK; s++) {
          tracks[activeTrack].steps[s].active    = false;
          tracks[activeTrack].steps[s].noteCount = 0;
        }
      } else {
        for (int s = 0; s < STEPS_PER_TRACK; s++) {
          tracks[activeTrack].steps[s].active    = false;
          tracks[activeTrack].steps[s].noteCount = 0;
          tracks[activeTrack].arpSteps[s].active    = false;
          tracks[activeTrack].arpSteps[s].noteCount = 0;
        }
        tracks[activeTrack].arp.noteCount = 0;
      }
    }
    return;
  }

  // UNDO (r=4,c=2) — clic court = undo / long press = wave picker (géré dans checkLongPress)
  if (r == 4 && c == 2) {
    if (!keyLongDone[r][c]) {
      if (isRecording) isRecording = false;
      else doUndo();
    }
    return;
  }

  // BPM (r=4,c=3)
  if (r == 4 && c == 3) {
    bpmHeld = false;
    if (!keyLongDone[r][c] && held < 500) metronomeOn = !metronomeOn;
    return;
  }
}

// =============================================================================
// LONG PRESS
// =============================================================================
void checkLongPress(int r, int c, unsigned long held) {

  // REC long -> pre-count
  if (r == 3 && c == 2 && held >= LONG_PRESS_MS) {
    keyLongDone[r][c] = true;
    isRecording = false;
    preCountActive = true; preCountVal = 4; preCountLastMs = millis();
  }

  // NEXT STEP long -> step edit
  if (r == 3 && c == 3 && held >= STEP_EDIT_LONG_MS) {
    keyLongDone[r][c] = true;
    isRecording = false; stepEditMode = true; editCursor = 0;
  }

  // ERASE long -> master reset toutes pistes
  if (r == 4 && c == 1 && held >= ERASE_LONG_MS) {
    keyLongDone[r][c] = true;
    isRecording = false;
    pushUndo();
    for (int t = 0; t < TRACK_COUNT; t++) {
      for (int s = 0; s < STEPS_PER_TRACK; s++) {
        tracks[t].steps[s].active    = false;
        tracks[t].steps[s].noteCount = 0;
        // NoteSlot: tie par slot
        tracks[t].arpSteps[s].active    = false;
        tracks[t].arpSteps[s].noteCount = 0;
      }
      tracks[t].arp.enabled = false; tracks[t].arp.noteCount = 0;
    }
    stopAllVoices();
    Serial.println("MASTER RESET");
  }

  // UNDO long -> REDO
  if (r == 4 && c == 2 && held >= LONG_PRESS_MS) {
    keyLongDone[r][c] = true;
    doRedo();
  }
}
