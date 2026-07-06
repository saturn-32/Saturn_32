// =============================================================================
// ENCODERS.INO v9 — architecture événements propre
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "globals.h"

void drawSaveConfirm();
void drawSaveNotif(const char* line1, const char* line2);

// Prototypes
void applyEnc1(int d, float speed);
void applyEnc2(int d, float speed);
static void handleBtnEvents(uint8_t e1, uint8_t e2);
void onPresetClick();

static BtnState btn1 = {false, true, 0}; // longFired=true au boot pour ignorer 1er appui
static BtnState btn2 = {false, true, 0};

// Retourne EVT_SHORT, EVT_LONG ou EVT_NONE.
// Met TOUJOURS à jour state.last — plus jamais de `return` qui saute cette ligne.
static uint8_t pollBtn(BtnState& s, bool pressed, unsigned long nowMs) {
  uint8_t evt = EVT_NONE;

  if (pressed && !s.last) {
    // Front montant : début d'appui
    s.downMs    = nowMs;
    s.longFired = false;
  }

  if (pressed && !s.longFired && (nowMs - s.downMs >= LONG_PRESS_MS)) {
    // Long press déclenché une seule fois
    s.longFired = true;
    evt = EVT_LONG;
  }

  if (!pressed && s.last && !s.longFired && (nowMs - s.downMs >= BTN_DEBOUNCE_MS)) {
    // Relâchement sans long press = clic court
    evt = EVT_SHORT;
  }

  s.last = pressed; // TOUJOURS mis à jour ici
  return evt;
}

// ─── Encodeurs ───────────────────────────────────────────────────────────────
static const int8_t ENC_TABLE[16] = {
   0,-1, 1, 0,
   1, 0, 0,-1,
  -1, 0, 0, 1,
   0, 1,-1, 0
};

static int     enc1Acc10 = 0, enc2Acc10 = 0;
static uint8_t enc1St    = 0, enc2St    = 0;
static uint8_t enc1Stable = 0, enc1StableCount = 0;
static uint8_t enc2Stable = 0, enc2StableCount = 0;
static float enc1Speed = 1.0f, enc2Speed = 1.0f;

void initEncoders() {
  pinMode(ENC1_CLK, INPUT); pinMode(ENC1_DT, INPUT); pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_CLK, INPUT); pinMode(ENC2_DT, INPUT); pinMode(ENC2_SW, INPUT_PULLUP);
  enc1St = enc1Stable = (uint8_t)((digitalRead(ENC1_CLK)<<1)|digitalRead(ENC1_DT));
  enc2St = enc2Stable = (uint8_t)((digitalRead(ENC2_CLK)<<1)|digitalRead(ENC2_DT));
}

static uint8_t readFiltered(uint8_t pinClk, uint8_t pinDt,
                             uint8_t& stableState, uint8_t& stableCount) {
  uint8_t raw = (uint8_t)((digitalRead(pinClk)<<1)|digitalRead(pinDt));
  if (raw == stableState) { stableCount = 0; return stableState; }
  if (++stableCount >= ENC_STABLE_READS) { stableState = raw; stableCount = 0; }
  return stableState;
}

static float encAccel(int d, float speed) {
  return (d < 0 ? -1.0f : 1.0f) * speed;
}

void handleEncoders() {
  unsigned long nowMs = millis();

  bool p1 = (digitalRead(ENC1_SW) == LOW);
  bool p2 = (digitalRead(ENC2_SW) == LOW);

  // ── Combo ENC1+ENC2 → menu chord ────────────────────────────────────────
  static bool comboArmed = false;
  if (p1 && p2 && !comboArmed) {
    comboArmed = true;
    enc2ComboUsed = true;
    btn1.longFired = true;
    btn2.longFired = true;
    // Fermer tous les menus sauf chord
    waveMenuOpen = wavePickerOpen = presetMenuOpen = false;
    projectMenuOpen = effectMenuOpen = arpOctMenuOpen = false;
    saveConfirmOpen = false;
    chordMenuOpen = true;
  }
  if (!p1 && !p2) comboArmed = false;

  // Rotation ENC1
  uint8_t a1 = readFiltered(ENC1_CLK, ENC1_DT, enc1Stable, enc1StableCount);
  if (a1 != enc1St) {
    int8_t step = ENC_TABLE[((enc1St&3)<<2)|(a1&3)];
    if (step) { enc1Acc10 += step * 10; enc1Speed = 1.0f; }
    enc1St = a1;
  }
  int d1 = 0;
  while (enc1Acc10 >=  ENC_THRESHOLD_X10) { d1++;  enc1Acc10 -= ENC_THRESHOLD_X10; }
  while (enc1Acc10 <= -ENC_THRESHOLD_X10) { d1--;  enc1Acc10 += ENC_THRESHOLD_X10; }

  // Rotation ENC2
  uint8_t a2 = readFiltered(ENC2_CLK, ENC2_DT, enc2Stable, enc2StableCount);
  if (a2 != enc2St) {
    int8_t step = ENC_TABLE[((enc2St&3)<<2)|(a2&3)];
    if (step) { enc2Acc10 += step * 10; enc2Speed = 1.0f; }
    enc2St = a2;
  }
  int d2 = 0;
  while (enc2Acc10 >=  ENC_THRESHOLD_X10) { d2++;  enc2Acc10 -= ENC_THRESHOLD_X10; }
  while (enc2Acc10 <= -ENC_THRESHOLD_X10) { d2--;  enc2Acc10 += ENC_THRESHOLD_X10; }

  if (d1) applyEnc1(d1, enc1Speed);
  if (d2) applyEnc2(d2, enc2Speed);

  // Événements boutons — pollBtn met toujours à jour .last
  uint8_t e1 = pollBtn(btn1, p1, nowMs);
  uint8_t e2 = pollBtn(btn2, p2, nowMs);

  // Consommer combo
  if (enc2ComboUsed && e2 == EVT_SHORT) { enc2ComboUsed = false; e2 = EVT_NONE; }

  handleBtnEvents(e1, e2);
}

// =============================================================================
// GESTION ÉVÉNEMENTS BOUTONS — machine à états propre
// =============================================================================
static void handleBtnEvents(uint8_t e1, uint8_t e2) {

  // ── ENC1 LONG : fermer tous les menus ou ouvrir wave picker ─────────────
  if (e1 == EVT_LONG) {
    bool wasOpen = waveMenuOpen || wavePickerOpen || presetMenuOpen ||
                   projectMenuOpen || effectMenuOpen || arpOctMenuOpen ||
                   chordMenuOpen || saveConfirmOpen;
    waveMenuOpen = wavePickerOpen = presetMenuOpen = projectMenuOpen = false;
    effectMenuOpen = arpOctMenuOpen = chordMenuOpen = saveConfirmOpen = false;
    wavePickerPage    = 0;
    wavePickerMixOpen = false;
    if (!wasOpen) {
      wavePickerOpen    = true;
      wavePickerPage    = 0;     // toujours ouvrir sur OSC1
      wavePickerMixOpen = false;
    }
  }

  // ── ENC2 LONG : ouvrir menu projet ou ARP octave ─────────────────────────
  if (e2 == EVT_LONG) {
    if (tracks[activeTrack].arp.enabled) {
      bool was = arpOctMenuOpen;
      waveMenuOpen = wavePickerOpen = presetMenuOpen = projectMenuOpen = false;
      effectMenuOpen = chordMenuOpen = saveConfirmOpen = false;
      arpOctMenuOpen = !was;
    } else {
      bool was = projectMenuOpen;
      waveMenuOpen = wavePickerOpen = presetMenuOpen = effectMenuOpen = false;
      arpOctMenuOpen = chordMenuOpen = saveConfirmOpen = false;
      if (!was) {
        projectMenuOpen = true;
        projectSlot     = 0;
        projectSaveMode = false;  // s'ouvre en mode LOAD
        projectPage     = 0;      // s'ouvre sur l'onglet PRESET
      } else {
        projectMenuOpen = false;
      }
    }
  }

  // ── Clics courts : par état de menu actif ────────────────────────────────

  // POPUP SAVE CONFIRM — priorité absolue
  if (saveConfirmOpen) {
      if (e2 == EVT_SHORT) {
          saveConfirmOpen = false;
      if (!saveConfirmCursor) {
        if (saveConfirmIsProject) {
          saveProject(projectSlot);
          // Affichage simple sans delay
          display.clearDisplay();
          display.fillRoundRect(0,20,128,24,4,SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
          display.setCursor(16,29);
          display.print("PROJECT SAVED ");
          display.print(projectSlot+1);
          display.setTextColor(SSD1306_WHITE);
          display.display();
        } else {
          savePreset(activeTrack, tracks[activeTrack].activePreset);
          display.clearDisplay();
          display.fillRoundRect(0,20,128,24,4,SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
          display.setCursor(16,29);
          display.print("PRESET SAVED ");
          display.print(tracks[activeTrack].activePreset+1);
          display.setTextColor(SSD1306_WHITE);
          display.display();
        }
      } else {
            }
    }
    return;
  }

  // WAVE PICKER
  if (wavePickerOpen) {
    if (e1 == EVT_SHORT) {
      // ENC1 clic : basculer OSC1 ↔ OSC2
      wavePickerPage    = (wavePickerPage == 0) ? 1 : 0;
      wavePickerMixOpen = false; // refermer la barre de mix au changement de page
    }
    if (e2 == EVT_SHORT) {
      // ENC2 clic : ouvrir/fermer la barre de mix OSC1/OSC2
      // (ENC2 ne ferme plus le wave picker — utiliser ERASE pour quitter)
      if (activeTrack != TRACK_DRUM) {
        wavePickerMixOpen = !wavePickerMixOpen;
      }
    }
    return;
  }

  // WAVE MENU
  if (waveMenuOpen) {
    if (e1 == EVT_SHORT) {
      // Page 3 (FX) maintenant disponible pour TRACK_DRUM (REVERB, DRIVE, FILTER)
      // Pour DRUM, la page LFO (index 2) est vide — on la saute automatiquement
      do {
        waveMenuPage = (waveMenuPage + 1) % 4;
        // Pour DRUM : sauter les pages vides (OSC=0, LFO=2)
      } while (activeTrack == TRACK_DRUM && (waveMenuPage == 0 || waveMenuPage == 2 || waveMenuPage == 3));
      // Premier param de chaque page
      static const uint8_t PAGE_FIRST[4]      = { PARAM_DECAY,  PARAM_FILTER, PARAM_LFO_DEP, PARAM_DELAY };
      static const uint8_t PAGE_FIRST_DRUM[4] = { 255,          PARAM_FILTER, 255,           PARAM_DELAY };
      waveMenuParam = (activeTrack == TRACK_DRUM)
                    ? PAGE_FIRST_DRUM[waveMenuPage]
                    : PAGE_FIRST[waveMenuPage];
    }
    if (e2 == EVT_SHORT) waveMenuOpen = false;
    return;
  }

  // MENU PROJET
  if (projectMenuOpen) {
    if (e1 == EVT_SHORT) {
      // Changer page PROJET <-> PRESET (mode SAVE/LOAD conservé)
      projectPage = (projectPage + 1) % 2;
      projectSlot = 0;
    }
    if (e2 == EVT_SHORT) {
      if (projectSaveMode) {
        projectMenuOpen      = false;
        saveConfirmOpen      = true;
        saveConfirmIsProject = (projectPage == 1);
        saveConfirmCursor    = false;
        // Forcer l'affichage immédiat du popup
        drawSaveConfirm();
        display.display();
      } else {
        // LOAD direct
        if (projectPage == 1) {
          loadProject(projectSlot); isPlaying = false; isRecording = false; currentStep = 0;
        } else {
          // Charger le preset du slot sélectionné pour la piste active
          tracks[activeTrack].activePreset = projectSlot;
          loadPreset(activeTrack, projectSlot);
        }
        projectMenuOpen = false;
      }
    }
    return;
  }

  // MENU PRESET (rapide)
  if (presetMenuOpen) {
    if (e1 == EVT_SHORT) { presetMenuOpen = false; loadCursor = -1; }
    if (e2 == EVT_SHORT) onPresetClick();
    return;
  }

  // MENU EFFET
  if (effectMenuOpen) {
    if (e1 == EVT_SHORT) effectCursor = (effectCursor < 6) ? 6 : 0; // changer de page
    if (e2 == EVT_SHORT) {
      if (activeEffect == effectCursor) {
        activeEffect = -1;
        stopAllVoices();
        scratchPitchFactor = 1.0f;
      } else {
        activeEffect = effectCursor;
        scratchPitchFactor = 1.0f;
        if (effectCursor == 6 || effectCursor == 7) {
          stopAllVoices();
          for (int p = 0; p < TRACK_COUNT * MAX_NOTES_PER_STEP; p++)
            pendingOffs[p].active = false;
        }
      }
    }
    return;
  }

  // MENU CHORD
  if (chordMenuOpen) {
    if (e2 == EVT_SHORT) chordMenuOpen = false;
    return;
  }

  // MENU ARP OCT
  if (arpOctMenuOpen) {
    if (e2 == EVT_SHORT) arpOctMenuOpen = false;
    return;
  }

  // ── Pas de menu ouvert : actions globales ────────────────────────────────
  if (e1 == EVT_SHORT) {
    // ENC1 clic = ouvrir wave menu
    waveMenuOpen = true;
    if (activeTrack == TRACK_DRUM) {
      // Pour DRUM : ouvrir directement sur FLT (page 1), première page non vide
      waveMenuPage  = 1;
      waveMenuParam = PARAM_FILTER;
    } else {
      waveMenuPage  = 0;
      waveMenuParam = PARAM_DECAY;
    }
  }

  if (e2 == EVT_SHORT) {
    if (tracks[activeTrack].arp.enabled) {
      // ARP : cycler la division
      tracks[activeTrack].arp.div = (tracks[activeTrack].arp.div + 1) % 4;
    } else {
      // Ouvrir menu effet master
      int8_t saved = activeEffect;
      effectMenuOpen = true;
      activeEffect   = saved;
    }
  }
}

// =============================================================================
// ENC1 ROTATION
// =============================================================================
void applyEnc1(int d, float speed) {
  if (wavePickerOpen) {
    if (wavePickerPage == 0) {
      // Page OSC1 : changer la forme d'onde principale
      tracks[activeTrack].waveform = (uint8_t)((tracks[activeTrack].waveform + d + WAVE_COUNT) % WAVE_COUNT);
    } else {
      // Page OSC2 : cycler parmi les formes + NONE (255)
      // NONE → 0 → 1 → ... → WAVE_COUNT-1 → NONE
      if (tracks[activeTrack].waveform2 == 255) {
        if (d > 0) tracks[activeTrack].waveform2 = 0;
        else       tracks[activeTrack].waveform2 = WAVE_COUNT - 1;
      } else {
        int next = (int)tracks[activeTrack].waveform2 + d;
        if (next < 0)              tracks[activeTrack].waveform2 = 255; // retour à NONE
        else if (next >= WAVE_COUNT) tracks[activeTrack].waveform2 = 255; // wrap vers NONE
        else                         tracks[activeTrack].waveform2 = (uint8_t)next;
      }
    }
    return;
  }
  if (waveMenuOpen) {
    static const uint8_t PAGE_PARAMS[4][5] = {
      { PARAM_DECAY, PARAM_ATTACK, PARAM_SUB, PARAM_NOISE, 255 },
      { PARAM_FILTER, PARAM_FILRES, PARAM_SUSTAIN, PARAM_RELEASE, 255 },
      { PARAM_LFO_DEP, PARAM_LFO_RAT, PARAM_LFO_DST, 255, 255 },
      { PARAM_DELAY, PARAM_CHORUS, PARAM_DRIVE, PARAM_GLIDE, 255 },
    };
    static const uint8_t PAGE_PARAMS_DRUM[4][5] = {
      { 255, 255, 255, 255, 255                          }, // OSC : vide
      { PARAM_FILTER, PARAM_DELAY, PARAM_DRIVE, 255, 255 }, // FLT+FX : filtre + reverb + drive
      { 255, 255, 255, 255, 255                          }, // LFO : vide
      { 255, 255, 255, 255, 255                          }, // FX  : vide (fusionné avec FLT)
    };
    static const uint8_t PAGE_SIZES[4]      = { 4, 4, 3, 4 };
    static const uint8_t PAGE_SIZES_DRUM[4] = { 0, 3, 0, 0 };
    bool isDrum = (activeTrack == TRACK_DRUM);
    const uint8_t (*curParams)[5] = isDrum ? PAGE_PARAMS_DRUM : PAGE_PARAMS;
    const uint8_t  *curSizes      = isDrum ? PAGE_SIZES_DRUM  : PAGE_SIZES;
    uint8_t pg = waveMenuPage % 4;
    uint8_t sz = curSizes[pg];
    if (sz == 0) return; // page vide pour DRUM (ex: LFO)
    int8_t cur = 0;
    for (int i = 0; i < sz; i++) if (curParams[pg][i] == waveMenuParam) { cur = i; break; }
    cur = (cur + d + sz) % sz;
    waveMenuParam = curParams[pg][cur];
    return;
  }
  if (projectMenuOpen) {
    // ENC1 : bascule uniquement SAVE/LOAD (inversé pour correspondre au sens physique)
    // d > 0 = droite = LOAD, d < 0 = gauche = SAVE
    if (d > 0) projectSaveMode = false; // droite = LOAD
    else        projectSaveMode = true;  // gauche = SAVE
    return;
  }
  if (presetMenuOpen) {
    if (loadCursor < 0) presetCursor = (presetCursor + d + 2) % 2;
    else                loadCursor   = (loadCursor   + d + 4) % 4;
    return;
  }
  if (effectMenuOpen) {
    effectCursor = (int8_t)((effectCursor + d + 8) % 8);
    return;
  }
  if (saveConfirmOpen) {
    saveConfirmCursor = !saveConfirmCursor;
    return;
  }
  if (chordMenuOpen) {
    tracks[activeTrack].chordMode = (tracks[activeTrack].chordMode + d + 5) % 5;
    return;
  }
  if (bpmHeld) { bpm = constrain(bpm + d, 40, 240); calcBpmInterval(); return; }
  if (nextStepHeld || stepEditMode) {
    uint8_t tLen = tracks[activeTrack].trackLen;
    if (stepEditMode && activeTrack != TRACK_DRUM) {
      uint8_t heldNotes[MAX_NOTES_PER_STEP]; uint8_t heldCount = 0;
      for (int r = 0; r < 3 && heldCount < MAX_NOTES_PER_STEP; r++)
        for (int c = 0; c < 5 && heldCount < MAX_NOTES_PER_STEP; c++)
          if (keyState[r][c] && KEY_NOTE_MAP[r][c] >= 0)
            heldNotes[heldCount++] = (uint8_t)KEY_NOTE_MAP[r][c];
      if (heldCount > 0) {
        // Si chordMode actif, étendre heldNotes/heldOcts avec toutes les notes de l'accord
        uint8_t heldOcts[MAX_NOTES_PER_STEP];
        uint8_t baseOct = tracks[activeTrack].octave;
        if (tracks[activeTrack].chordMode > 0 && activeTrack != TRACK_DRUM) {
          uint8_t chordNotes[MAX_NOTES_PER_STEP], chordOcts[MAX_NOTES_PER_STEP];
          uint8_t cn = buildChord(heldNotes[0], baseOct,
                                  tracks[activeTrack].chordMode,
                                  chordNotes, chordOcts);
          heldCount = 0;
          for (uint8_t i = 0; i < cn && heldCount < MAX_NOTES_PER_STEP; i++) {
            heldNotes[heldCount] = chordNotes[i];
            heldOcts[heldCount]  = chordOcts[i];
            heldCount++;
          }
        } else {
          // Pas de chord : toutes les notes ont la même octave active
          for (uint8_t i = 0; i < heldCount; i++) heldOcts[i] = baseOct;
        }
        if (tieAnchorStep < 0) {
          tieAnchorStep = editCursor; tieAnchorNote = heldNotes[0];
          Step& anchorInit = tracks[activeTrack].steps[tieAnchorStep];
          tieAnchorWasExisting = false;
          for (int k = 0; k < anchorInit.noteCount; k++)
            if (anchorInit.slots[k].tie == TIE_START || anchorInit.slots[k].tie == TIE_MID)
              { tieAnchorWasExisting = true; break; }
        }
        editCursor = (uint8_t)((editCursor + d + tLen) % tLen);
        if (tLen == 32) stepViewOffset = (editCursor >= 16) ? 16 : 0;
        { uint8_t st = ((uint8_t)tieAnchorStep + 1) % tLen;
          for (int k = 0; k < tLen - 1; k++) {
            Step& sc = tracks[activeTrack].steps[st]; if (!sc.active) break;
            bool modified = false;
            for (int n = 0; n < heldCount; n++)
              for (int j = 0; j < sc.noteCount; j++)
                if (sc.slots[j].note == heldNotes[n] && sc.slots[j].octave == heldOcts[n] &&
                    (sc.slots[j].tie == TIE_MID || sc.slots[j].tie == TIE_END)) {
                  for (int m = j; m < sc.noteCount-1; m++) sc.slots[m] = sc.slots[m+1];
                  sc.noteCount--; if (!sc.noteCount) sc.active = false;
                  modified = true; j--;
                }
            if (!modified) break;
            st = (st + 1) % tLen;
          }
        }
        { Step& sf = tracks[activeTrack].steps[tieAnchorStep]; sf.active = true;
          for (int n = 0; n < heldCount; n++) {
            uint8_t ts = (editCursor == (uint8_t)tieAnchorStep) ? TIE_NONE : TIE_START;
            int idx = -1;
            for (int k = 0; k < sf.noteCount; k++)
              if (sf.slots[k].note == heldNotes[n] && sf.slots[k].octave == heldOcts[n]) { idx = k; break; }
            if (idx < 0 && sf.noteCount < MAX_NOTES_PER_STEP) sf.slots[sf.noteCount++] = {heldNotes[n], heldOcts[n], ts};
            else if (idx >= 0) sf.slots[idx].tie = ts;
          }
        }
        if (editCursor != (uint8_t)tieAnchorStep) {
          uint8_t st = ((uint8_t)tieAnchorStep + 1) % tLen;
          while (st != (uint8_t)editCursor) {
            Step& sm = tracks[activeTrack].steps[st]; sm.active = true;
            for (int n = 0; n < heldCount; n++) {
              int idx = -1;
              for (int k = 0; k < sm.noteCount; k++)
                if (sm.slots[k].note == heldNotes[n] && sm.slots[k].octave == heldOcts[n]) { idx = k; break; }
              if (idx < 0 && sm.noteCount < MAX_NOTES_PER_STEP) sm.slots[sm.noteCount++] = {heldNotes[n], heldOcts[n], TIE_MID};
              else if (idx >= 0) sm.slots[idx].tie = TIE_MID;
            }
            st = (st + 1) % tLen;
          }
          Step& se = tracks[activeTrack].steps[editCursor]; se.active = true;
          for (int n = 0; n < heldCount; n++) {
            int idx = -1;
            for (int k = 0; k < se.noteCount; k++)
              if (se.slots[k].note == heldNotes[n] && se.slots[k].octave == heldOcts[n]) { idx = k; break; }
            if (idx < 0 && se.noteCount < MAX_NOTES_PER_STEP) se.slots[se.noteCount++] = {heldNotes[n], heldOcts[n], TIE_END};
            else if (idx >= 0) se.slots[idx].tie = TIE_END;
          }
        }
        return;
      }
    }
    tieAnchorStep = -1; tieAnchorNote = -1;
    editCursor = (uint8_t)((editCursor + d + tLen) % tLen);
    if (tLen == 32) stepViewOffset = (editCursor >= 16) ? 16 : 0;
    return;
  }
  masterVolume = constrain(masterVolume + encAccel(d, speed) * 0.05f, 0.0f, 1.0f);
}

// =============================================================================
// ENC2 ROTATION
// =============================================================================
void applyEnc2(int d, float speed) {
  if (wavePickerOpen) {
    if (wavePickerMixOpen && activeTrack != TRACK_DRUM) {
      // Régler le mix OSC1/OSC2
      tracks[activeTrack].osc2Mix = constrain(
        tracks[activeTrack].osc2Mix + encAccel(d, speed) * 0.05f, 0.0f, 1.0f);
    } else {
      // Morph indépendant par oscillateur
      if (wavePickerPage == 0) {
        tracks[activeTrack].waveMorph = constrain(
          tracks[activeTrack].waveMorph + encAccel(d, speed) * 0.05f, 0.0f, 1.0f);
      } else {
        tracks[activeTrack].waveMorph2 = constrain(
          tracks[activeTrack].waveMorph2 + encAccel(d, speed) * 0.05f, 0.0f, 1.0f);
      }
    }
    return;
  }
  if (projectMenuOpen) {
    // ENC2 rotation = naviguer les slots
    projectSlot = (uint8_t)((projectSlot + d + 4) % 4);
    // Sur la page PRESET : activePreset suit le slot
    if (projectPage == 0) tracks[activeTrack].activePreset = projectSlot;
    return;
  }
  if (effectMenuOpen) {
    effectIntensity = (uint8_t)constrain((int)effectIntensity + d, 0, 8);
    return;
  }
  if (saveConfirmOpen) {
    saveConfirmCursor = !saveConfirmCursor;
    return;
  }
  if (chordMenuOpen) {
    tracks[activeTrack].chordMode = (tracks[activeTrack].chordMode + d + 5) % 5;
    return;
  }
  if (waveMenuOpen) {
    uint8_t p = waveMenuParam;
    switch (p) {
      case PARAM_WAVE:    tracks[activeTrack].waveform = (uint8_t)((tracks[activeTrack].waveform+d+WAVE_COUNT)%WAVE_COUNT); break;
      case PARAM_DECAY:   tracks[activeTrack].decayTime    = constrain(tracks[activeTrack].decayTime    + encAccel(d,speed)*0.05f,0,1); break;
      // PARAM_REVERB (slot 2) : reverbAmt conservé en RAM pour compatibilité NVS
      // mais n'est plus dans les pages FX — ce case ne sera jamais atteint
      case PARAM_DELAY:   tracks[activeTrack].delayAmt     = constrain(tracks[activeTrack].delayAmt     + encAccel(d,speed)*0.05f,0,1); updateEffects(); break;
      case PARAM_ATTACK:  tracks[activeTrack].attackAmt    = constrain(tracks[activeTrack].attackAmt    + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_CHORUS:  tracks[activeTrack].chorusAmt    = constrain(tracks[activeTrack].chorusAmt    + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_DRIVE:   tracks[activeTrack].driveAmt     = constrain(tracks[activeTrack].driveAmt     + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_FILTER:  tracks[activeTrack].filterCutoff = constrain(tracks[activeTrack].filterCutoff + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_GLIDE:   tracks[activeTrack].glideAmt     = constrain(tracks[activeTrack].glideAmt     + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_LFO_DEP: tracks[activeTrack].lfoDepth     = constrain(tracks[activeTrack].lfoDepth     + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_LFO_RAT: tracks[activeTrack].lfoRate      = constrain(tracks[activeTrack].lfoRate      + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_LFO_DST: if (d) tracks[activeTrack].lfoDest = (tracks[activeTrack].lfoDest+(d>0?1:2))%3; break;
      case PARAM_SUB:     tracks[activeTrack].subOscAmt    = constrain(tracks[activeTrack].subOscAmt    + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_NOISE:   tracks[activeTrack].noiseAmt     = constrain(tracks[activeTrack].noiseAmt     + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_FILRES:  tracks[activeTrack].filterRes    = constrain(tracks[activeTrack].filterRes    + encAccel(d,speed)*0.05f,0,0.95f); break;
      case PARAM_SUSTAIN: tracks[activeTrack].sustainLevel = constrain(tracks[activeTrack].sustainLevel + encAccel(d,speed)*0.05f,0,1); break;
      case PARAM_RELEASE: tracks[activeTrack].releaseTime  = constrain(tracks[activeTrack].releaseTime  + encAccel(d,speed)*0.05f,0,1); break;
    }
    return;
  }
  if (presetMenuOpen) {
    if (loadCursor < 0) presetCursor = (presetCursor + d + 2) % 2;
    else                loadCursor   = (loadCursor   + d + 4) % 4;
    return;
  }
  if (stepEditMode) {
    Step& s = tracks[activeTrack].steps[editCursor];
    uint8_t tLen = tracks[activeTrack].trackLen;
    if (s.active && stepHasTieStart(s)) {
      uint8_t refNote = 0, refOct = 0;
      for (int k = 0; k < s.noteCount; k++)
        if (s.slots[k].tie == TIE_START) { refNote=s.slots[k].note; refOct=s.slots[k].octave; break; }
      int endStep = -1;
      for (int i = 1; i < tLen; i++) {
        int st = (editCursor + i) % tLen;
        Step& se = tracks[activeTrack].steps[st];
        for (int k = 0; k < se.noteCount; k++)
          if (se.slots[k].note==refNote && se.slots[k].octave==refOct && se.slots[k].tie==TIE_END)
            { endStep = st; break; }
        if (endStep >= 0) break;
        if (!se.active) break;
      }
      if (d > 0) {
        int newEnd = endStep < 0 ? (editCursor+1)%tLen : (endStep+1)%tLen;
        if (endStep >= 0) {
          Step& sOld = tracks[activeTrack].steps[endStep];
          for (int k=0;k<sOld.noteCount;k++)
            if (sOld.slots[k].note==refNote&&sOld.slots[k].octave==refOct&&sOld.slots[k].tie==TIE_END)
              sOld.slots[k].tie=TIE_MID;
        }
        Step& ne = tracks[activeTrack].steps[newEnd]; ne.active=true;
        for (int k=0;k<s.noteCount;k++) if (s.slots[k].tie==TIE_START) {
          int idx=-1;
          for (int j=0;j<ne.noteCount;j++)
            if (ne.slots[j].note==s.slots[k].note&&ne.slots[j].octave==s.slots[k].octave){idx=j;break;}
          if (idx<0&&ne.noteCount<MAX_NOTES_PER_STEP) ne.slots[ne.noteCount++]={s.slots[k].note,s.slots[k].octave,TIE_END};
          else if (idx>=0) ne.slots[idx].tie=TIE_END;
        }
      } else if (endStep >= 0) {
        Step& sEnd = tracks[activeTrack].steps[endStep];
        for (int k=0;k<sEnd.noteCount;k++)
          if (sEnd.slots[k].note==refNote&&sEnd.slots[k].octave==refOct&&sEnd.slots[k].tie==TIE_END) {
            for (int m=k;m<sEnd.noteCount-1;m++) sEnd.slots[m]=sEnd.slots[m+1];
            sEnd.noteCount--; if (!sEnd.noteCount) sEnd.active=false; break;
          }
        int prevEnd = (endStep-1+tLen)%tLen;
        if (prevEnd == (int)editCursor) {
          for (int k=0;k<s.noteCount;k++) if (s.slots[k].tie==TIE_START) s.slots[k].tie=TIE_NONE;
        } else {
          Step& sPrev = tracks[activeTrack].steps[prevEnd];
          for (int k=0;k<sPrev.noteCount;k++)
            if (sPrev.slots[k].note==refNote&&sPrev.slots[k].octave==refOct&&sPrev.slots[k].tie==TIE_MID)
              sPrev.slots[k].tie=TIE_END;
        }
      }
    } else if (s.active && !stepHasTieEnd(s)) {
      if (d > 0) {
        if      (s.drumOffset == 0) s.drumOffset = 1;
        else if (s.drumOffset == 1) s.drumOffset = 3;
        else                        s.drumOffset = 0;
      } else {
        if      (s.drumOffset == 0) s.drumOffset = 3;
        else if (s.drumOffset == 3) s.drumOffset = 1;
        else                        s.drumOffset = 0;
      }
    }
    return;
  }
  if (activeTrack == TRACK_DRUM) {
    tracks[TRACK_DRUM].volume = constrain(tracks[TRACK_DRUM].volume + encAccel(d,speed)*0.05f, 0, 1);
    return;
  }
  if (tracks[activeTrack].arp.enabled) {
    if (arpOctMenuOpen) {
      int nr = (int)tracks[activeTrack].arp.octaveRange + d;
      tracks[activeTrack].arp.octaveRange = (uint8_t)constrain(nr, 1, 3);
      tracks[activeTrack].arp.octaveShift = 0; // reset shift quand on change de mode
    } else {
      tracks[activeTrack].volume = constrain(tracks[activeTrack].volume + encAccel(d,speed)*0.05f, 0, 1);
    }
    return;
  }
  tracks[activeTrack].volume = constrain(tracks[activeTrack].volume + encAccel(d,speed)*0.05f, 0, 1);
}

// =============================================================================
// PRESET CLICK (menu preset rapide)
// =============================================================================
void onPresetClick() {
  if (loadCursor < 0) {
    if (presetCursor == 0) {
      presetMenuOpen       = false;
      saveConfirmOpen      = true;
      saveConfirmIsProject = false;
      saveConfirmCursor    = false;
      // drawUI affichera le popup au prochain cycle
    } else {
      loadCursor = 0;
    }
  } else {
    tracks[activeTrack].activePreset = (uint8_t)loadCursor;
    loadPreset(activeTrack, (uint8_t)loadCursor);
    presetMenuOpen = false;
    loadCursor     = -1;
  }
}
