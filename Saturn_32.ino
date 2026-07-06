// =============================================================================
// SEQUENCER.INO v9 — NoteSlot : chaque note a son propre état tie
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "globals.h"
// activeEffect, effectIntensity déclarés dans globals.h
void advanceHalfStep();

uint8_t halfStepCount = 0;
static const float DECAY_MULT[3] = { 0.25f, 1.0f, 2.0f };
static unsigned long drumManualTriggerUs[13] = {0};

void markDrumManualTrigger(uint8_t noteIdx) {
  if (noteIdx < 13) drumManualTriggerUs[noteIdx] = micros();
}
static bool drumRecentlyTriggered(uint8_t noteIdx) {
  if (noteIdx >= 13) return false;
  return (micros() - drumManualTriggerUs[noteIdx]) < stepIntervalUs;
}

struct LiveNote {
  bool active; uint8_t trackIdx; uint8_t noteIdx;
  uint8_t octave; uint8_t stepAtPress; unsigned long pressMs;
  bool noteOffReceived; // true = relâchement reçu avant le prochain step → pas de TIE
};
static LiveNote liveNotes[MAX_NOTES_PER_STEP * TRACK_COUNT];

void clearLiveNotes() {
  for (int i=0;i<MAX_NOTES_PER_STEP*TRACK_COUNT;i++) liveNotes[i].active=false;
}

PendingOff pendingOffs[TRACK_COUNT * MAX_NOTES_PER_STEP];

// =============================================================================
// HELPERS NoteSlot
// =============================================================================
static int findSlot(const Step& s, uint8_t note, uint8_t oct) {
  for (int i=0;i<s.noteCount;i++)
    if (s.slots[i].note==note && s.slots[i].octave==oct) return i;
  return -1;
}
static int addSlot(Step& s, uint8_t note, uint8_t oct, uint8_t tie) {
  if (s.noteCount>=MAX_NOTES_PER_STEP) return -1;
  int idx=s.noteCount++;
  s.slots[idx]={note,oct,tie};
  s.active=true;
  return idx;
}
static int findOrAddSlot(Step& s, uint8_t note, uint8_t oct, uint8_t tie) {
  int idx=findSlot(s,note,oct);
  if (idx>=0) return idx;
  return addSlot(s,note,oct,tie);
}

// =============================================================================
// PENDING OFFS
// =============================================================================
static void flushPendingOffs() {
  for (int i = 0; i < TRACK_COUNT * MAX_NOTES_PER_STEP; i++) {
    if (!pendingOffs[i].active) continue;
    _releaseVoice(pendingOffs[i].trackIdx, pendingOffs[i].noteIdx, pendingOffs[i].octave);
    pendingOffs[i].active = false;
  }
}

static void addPendingOff(uint8_t ti, uint8_t ni, uint8_t oct) {
  // Vérifier si ce pendingOff existe déjà (éviter les doublons)
  for (int i = 0; i < TRACK_COUNT * MAX_NOTES_PER_STEP; i++) {
    if (pendingOffs[i].active &&
        pendingOffs[i].trackIdx == ti &&
        pendingOffs[i].noteIdx  == ni &&
        pendingOffs[i].octave   == oct) return; // déjà en attente
  }
  // Trouver un slot libre
  for (int i = 0; i < TRACK_COUNT * MAX_NOTES_PER_STEP; i++) {
    if (!pendingOffs[i].active) {
      pendingOffs[i] = {true, ti, ni, oct};
      return;
    }
  }
  // Tableau plein : forcer la libération immédiate pour éviter la fuite
  _releaseVoice(ti, ni, oct);
}

// =============================================================================
// BPM
// =============================================================================
void calcBpmInterval() { stepIntervalUs=15000000UL/(unsigned long)bpm; }

// =============================================================================
// UPDATE
// =============================================================================
void updateSequencer() {
  if (preCountActive) {
    unsigned long beatMs=60000UL/(unsigned long)bpm;
    if (millis()-preCountLastMs>=beatMs) {
      preCountLastMs=millis();
      if (metronomeOn) triggerMetroAccent(preCountVal%4==0);
      if (preCountVal>0) preCountVal--;
      if (preCountVal==0) {
        preCountActive=false; isRecording=true; isPlaying=true;
        currentStep=0; halfStepCount=0; lastStepUs=micros();
      }
    }
    return;
  }
  if (!isPlaying) return;
  unsigned long now=micros();
  if (now-lastStepUs<stepIntervalUs) return;
  lastStepUs+=stepIntervalUs;
  if (now-lastStepUs>stepIntervalUs+stepIntervalUs/2) lastStepUs=now;
  advanceHalfStep();
}

// =============================================================================
// DEMI-STEP
// =============================================================================
void advanceHalfStep() {
  static uint32_t stepCount = 0;
  if (++stepCount % 16 == 0) Serial.printf("advanceHalfStep: step=%d half=%d\n", currentStep, halfStepCount);
  if (halfStepCount==0) {
    if (metronomeOn) triggerMetroAccent(currentStep%4==0);
    flushPendingOffs();

    // ── RÉCONCILIATION : killer les voix sustain orphelines ──────────────
    // Les voix live (dans liveNotes[]) sont protégées dans tous les cas.
    // En lecture seule, toutes les voix sustain sont live → aucune ne sera tuée.
    if (xSemaphoreTake(voiceMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
      for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active || !voices[v].sustained) continue;
        uint8_t ti  = voices[v].trackIdx;
        uint8_t ni  = voices[v].noteIdx;
        uint8_t oct = voices[v].octave;
        if (ti >= TRACK_COUNT) continue;

        uint8_t effStep = currentStep % tracks[ti].trackLen;
        Step& s = tracks[ti].steps[effStep];

        // Chercher si cette note est en TIE_MID, TIE_START ou TIE_END dans le step courant
        // TIE_END = la voix va être relâchée CE step par addPendingOff → ne pas tuer
        bool shouldBeSustained = false;
        for (int n = 0; n < s.noteCount; n++) {
          if (s.slots[n].note == ni && s.slots[n].octave == oct &&
              (s.slots[n].tie == TIE_MID ||
               s.slots[n].tie == TIE_START ||
               s.slots[n].tie == TIE_END)) {
            shouldBeSustained = true;
            break;
          }
        }
        // Vérifier aussi le pendingOff (note en TIE_END, sera relâchée ce step)
        for (int p = 0; p < TRACK_COUNT * MAX_NOTES_PER_STEP; p++) {
          if (pendingOffs[p].active && pendingOffs[p].trackIdx == ti &&
              pendingOffs[p].noteIdx == ni && pendingOffs[p].octave == oct) {
            shouldBeSustained = true; // sera libérée par flushPendingOffs, pas orpheline
            break;
          }
        }

        if (!shouldBeSustained) {
          // Vérifier si c'est une voix live en cours (note jouée au clavier)
          // Ces voix sont dans liveNotes et ne doivent pas être tuées ici
          bool isLive = false;
          for (int li = 0; li < MAX_NOTES_PER_STEP * TRACK_COUNT; li++) {
            if (liveNotes[li].active && liveNotes[li].trackIdx == ti &&
                liveNotes[li].noteIdx == ni && liveNotes[li].octave == oct) {
              isLive = true; break;
            }
          }
          if (!isLive) {
            // Voix orpheline : forcer en release
            voices[v].sustained = false;
            voices[v].releasing = true;
            voices[v].sustainSamples = 0;
            if (voices[v].releaseRate < 0.000001f)
              voices[v].releaseRate = 0.01f;
          }
        }
      }
      xSemaphoreGive(voiceMutex);
    }
    // ── FIN RÉCONCILIATION ───────────────────────────────────────────────

    for (int t=0;t<TRACK_COUNT;t++) {
      if (tracks[t].muted) continue;
      uint8_t effStep=currentStep%tracks[t].trackLen;
      Step& s=tracks[t].steps[effStep];

      if (s.active && s.noteCount>0) {
        // En mode REVERSE (7) ou REWIND (6) : inverser TIE_START ↔ TIE_END
        bool reversed = (activeEffect == 6 || activeEffect == 7);
        for (int n=0;n<s.noteCount;n++) {
          NoteSlot& sl=s.slots[n];
          uint8_t tieState = sl.tie;
          if (reversed) {
            if (tieState == TIE_START) tieState = TIE_END;
            else if (tieState == TIE_END) tieState = TIE_START;
          }
          switch(tieState) {
            case TIE_START:
              for (int p=0;p<TRACK_COUNT*MAX_NOTES_PER_STEP;p++)
                if (pendingOffs[p].active && pendingOffs[p].trackIdx==t &&
                    pendingOffs[p].noteIdx==sl.note && pendingOffs[p].octave==sl.octave) {
                  _releaseVoice(t,sl.note,sl.octave); pendingOffs[p].active=false;
                }
              triggerNoteSustained(t,sl.note,sl.octave);
              break;
            case TIE_END:
              addPendingOff(t,sl.note,sl.octave);
              break;
            case TIE_NONE:
              if (t==TRACK_DRUM) {
                // États 0 et 3 : sonner au 1er demi-step
                if ((s.drumOffset==0 || s.drumOffset==3) &&
                    !(isRecording&&drumRecentlyTriggered(sl.note)))
                  triggerDrum(sl.note);
              } else {
                if (s.drumOffset==0 || s.drumOffset==3) {
                  float mult=DECAY_MULT[s.stepDecay<3?s.stepDecay:1];
                  triggerNoteWithDecay(t,sl.note,sl.octave,mult);
                }
                // État 1 : pas de son au 1er demi-step
              }
              break;
            case TIE_MID: break;
          }
        }
      }

      // ARP
      if (tracks[t].arp.enabled && tracks[t].arp.noteCount>0) {
        ArpData& a=tracks[t].arp;
        bool trigger=false;
        switch(a.div){case 0:trigger=(effStep%4==0);break;case 1:trigger=(effStep%2==0);break;default:trigger=true;break;}
        if (trigger) {
          uint8_t range=(a.octaveRange>0)?a.octaveRange:1;
          if (a.pos >= a.noteCount) a.pos = 0;
          uint8_t playOct;
          if (range == 3) {
            // RANDOM : tire une note aléatoire dans le buffer
            lfsrState ^= lfsrState<<13; lfsrState ^= lfsrState>>17; lfsrState ^= lfsrState<<5;
            a.pos = (uint8_t)(lfsrState % a.noteCount);
            playOct = a.octaves[a.pos]; if(playOct>6)playOct=6;
          } else {
            playOct = a.octaves[a.pos]+a.octaveShift; if(playOct>6)playOct=6;
          }
          triggerNoteWithDecay(t,a.notes[a.pos],playOct,1.0f);
          if (range != 3) {
            a.pos++;
            if (a.pos>=a.noteCount){a.pos=0;a.octaveShift=(a.octaveShift+1)%range;}
          }
        }
      }
    }
    liveNotesAdvance();
    halfStepCount=1;

  } else {
    for (int t=0;t<TRACK_COUNT;t++) {
      if (tracks[t].muted) continue;
      uint8_t effStep=currentStep%tracks[t].trackLen;
      if (tracks[t].arp.enabled && tracks[t].arp.noteCount>0 && tracks[t].arp.div==3) {
        ArpData& a=tracks[t].arp;
        uint8_t range=(a.octaveRange>0)?a.octaveRange:1;
        if (a.pos >= a.noteCount) a.pos = 0;
        uint8_t playOct;
        if (range == 3) {
          lfsrState ^= lfsrState<<13; lfsrState ^= lfsrState>>17; lfsrState ^= lfsrState<<5;
          a.pos = (uint8_t)(lfsrState % a.noteCount);
          playOct = a.octaves[a.pos]; if(playOct>6)playOct=6;
        } else {
          playOct = a.octaves[a.pos]+a.octaveShift; if(playOct>6)playOct=6;
        }
        triggerNoteWithDecay(t,a.notes[a.pos],playOct,1.0f);
        if (range != 3) {
          a.pos++;
          if(a.pos>=a.noteCount){a.pos=0;a.octaveShift=(a.octaveShift+1)%range;}
        }
      }
      Step& s=tracks[t].steps[effStep];
      if (!s.active||s.noteCount==0) continue;
      if (t==TRACK_DRUM) {
        for (int n=0;n<s.noteCount;n++)
          if (s.slots[n].tie==TIE_NONE &&
              (s.drumOffset==1 || s.drumOffset==3) &&
              !(isRecording&&drumRecentlyTriggered(s.slots[n].note)))
            triggerDrum(s.slots[n].note);
      } else if (!stepHasTieStart(s) && !stepHasTieEnd(s) && !stepIsAllMid(s)) {
        if (s.drumOffset==1 || s.drumOffset==3) {
          float mult=DECAY_MULT[s.stepDecay<3?s.stepDecay:1];
          for (int n=0;n<s.noteCount;n++)
            if (s.slots[n].tie==TIE_NONE)
              triggerNoteWithDecay(t,s.slots[n].note,s.slots[n].octave,mult);
        }
      }
    }
    uint8_t maxLen=16;
    for(int t=0;t<TRACK_COUNT;t++) if(tracks[t].trackLen>maxLen) maxLen=tracks[t].trackLen;
    // Avancement du step selon effet actif
    // REVERSE (6) et REWIND (7) = lecture à l'envers
    if (activeEffect == 6 || activeEffect == 7) {
      // Reculer d'un step
      currentStep = (currentStep == 0) ? maxLen - 1 : currentStep - 1;
    } else {
      currentStep = (currentStep + 1) % maxLen;
    }
    halfStepCount=0;
  }
}

// =============================================================================
// REC LIVE
// =============================================================================
void liveNoteOn(uint8_t ti, uint8_t ni, uint8_t oct) {
  if (!isPlaying || ti == TRACK_DRUM) return;
  if (tracks[ti].arp.enabled) return;

  if (!isRecording) {
    // Hors REC : tracker dans liveNotes pour protéger la voix sustain
    // de la réconciliation — sans écrire dans les steps
    for (int i = 0; i < MAX_NOTES_PER_STEP * TRACK_COUNT; i++) {
      if (!liveNotes[i].active) {
        liveNotes[i] = {true, ti, ni, oct, (uint8_t)(currentStep % tracks[ti].trackLen), millis(), false};
        return;
      }
    }
    return;
  }

  // En REC : enregistrer dans les steps et suivre dans liveNotes
  // NE PAS appeler _releaseVoice — les voix sustain du séquenceur lui appartiennent
  uint8_t effStep = currentStep % tracks[ti].trackLen;
  Step& s = tracks[ti].steps[effStep];

  int idx = findSlot(s, ni, oct);
  if (idx < 0) idx = addSlot(s, ni, oct, TIE_NONE);
  if (idx >= 0 && s.slots[idx].tie == TIE_END) {
    // Libérer uniquement le pendingOff de cette note précise
    for (int p = 0; p < TRACK_COUNT * MAX_NOTES_PER_STEP; p++)
      if (pendingOffs[p].active && pendingOffs[p].trackIdx == ti &&
          pendingOffs[p].noteIdx == ni && pendingOffs[p].octave == oct)
        pendingOffs[p].active = false;
    s.slots[idx].tie = TIE_NONE;
  }

  // Enregistrer dans liveNotes pour le suivi du tie
  for (int i = 0; i < MAX_NOTES_PER_STEP * TRACK_COUNT; i++) {
    if (!liveNotes[i].active) {
      liveNotes[i] = {true, ti, ni, oct, effStep, millis(), false};
      return;
    }
  }
}

void liveNotesAdvance() {
  for (int i = 0; i < MAX_NOTES_PER_STEP * TRACK_COUNT; i++) {
    if (!liveNotes[i].active) continue;
    uint8_t ti  = liveNotes[i].trackIdx;
    if (tracks[ti].arp.enabled) continue;
    uint8_t ni  = liveNotes[i].noteIdx;
    uint8_t oct = liveNotes[i].octave;
    uint8_t start   = liveNotes[i].stepAtPress;
    uint8_t tLen    = tracks[ti].trackLen;
    uint8_t effStep = currentStep % tLen;
    if (effStep == start) continue;

    // NL seulement si aucun noteOff n'a été reçu avant ce step.
    // liveNoteOff() lève noteOffReceived → liveNotesAdvance() voit le flag
    // et n'écrit pas de TIE, quelle que soit la durée ou le BPM.
    if (liveNotes[i].noteOffReceived) continue; // relâchée avant le step → note courte

    if (isRecording) {
      // Écrire dans les steps seulement en REC
      Step& startStep = tracks[ti].steps[start];
      int sIdx = findSlot(startStep, ni, oct);
      if (sIdx < 0) sIdx = addSlot(startStep, ni, oct, TIE_START);
      else           startStep.slots[sIdx].tie = TIE_START;

      Step& curStep = tracks[ti].steps[effStep];
      int cIdx = findSlot(curStep, ni, oct);
      if (cIdx < 0) addSlot(curStep, ni, oct, TIE_MID);
      else if (curStep.slots[cIdx].tie == TIE_NONE) curStep.slots[cIdx].tie = TIE_MID;
    }
    // Note tenue au-delà du step → la voix sustain continue naturellement
  }
}

void liveNoteOff(uint8_t ti, uint8_t ni, uint8_t oct) {
  if (!isPlaying || ti == TRACK_DRUM) return;

  if (!isRecording) {
    // Hors REC : retirer de liveNotes[] pour que la réconciliation
    // ne protège plus cette voix (elle sera libérée par triggerNoteOff dans matrix.ino)
    for (int i = 0; i < MAX_NOTES_PER_STEP * TRACK_COUNT; i++) {
      if (liveNotes[i].active && liveNotes[i].trackIdx == ti &&
          liveNotes[i].noteIdx == ni && liveNotes[i].octave == oct) {
        liveNotes[i].active = false;
        return;
      }
    }
    return;
  }
  // En REC : lever le flag noteOffReceived AVANT de finaliser le tie.
  // liveNotesAdvance() qui tourne en parallèle (seqTask) verra ce flag
  // et ne créera pas de TIE_MID si le step avance dans la même fenêtre.

  // En REC : finaliser le tie dans les steps
  for (int i = 0; i < MAX_NOTES_PER_STEP * TRACK_COUNT; i++) {
    if (!liveNotes[i].active || liveNotes[i].trackIdx != ti ||
        liveNotes[i].noteIdx != ni || liveNotes[i].octave != oct) continue;
    liveNotes[i].noteOffReceived = true; // bloquer liveNotesAdvance() immédiatement

    uint8_t tLen      = tracks[ti].trackLen;
    uint8_t startStep = liveNotes[i].stepAtPress;
    uint8_t endStep = currentStep % tLen;

    if (startStep == endStep) {
      // Note courte (relâchée dans le même step) : laisser TIE_NONE
      Step& s = tracks[ti].steps[startStep];
      int idx = findSlot(s, ni, oct);
      if (idx >= 0) s.slots[idx].tie = TIE_NONE;
    } else {
      // Tie multi-steps
      Step& startSt = tracks[ti].steps[startStep];
      int sIdx = findSlot(startSt, ni, oct);
      if (sIdx >= 0) startSt.slots[sIdx].tie = TIE_START;

      uint8_t st = (startStep + 1) % tLen;
      while (st != endStep) {
        Step& sm = tracks[ti].steps[st];
        int mIdx = findOrAddSlot(sm, ni, oct, TIE_MID);
        if (mIdx >= 0) sm.slots[mIdx].tie = TIE_MID;
        st = (st + 1) % tLen;
      }
      Step& endSt = tracks[ti].steps[endStep];
      int eIdx = findOrAddSlot(endSt, ni, oct, TIE_END);
      if (eIdx >= 0) endSt.slots[eIdx].tie = TIE_END;
      addPendingOff(ti, ni, oct);

      // Nettoyer le TIE_MID orphelin que liveNotesAdvance() a pu écrire sur
      // currentStep AVANT que liveNoteOff() soit appelé (race seqTask/loop).
      // Ce TIE_MID n'a aucun TIE_START correspondant après endStep → bug lecture.
      uint8_t curStep = currentStep % tLen;
      if (curStep != endStep) {
        Step& orphanStep = tracks[ti].steps[curStep];
        int oIdx = findSlot(orphanStep, ni, oct);
        if (oIdx >= 0 && orphanStep.slots[oIdx].tie == TIE_MID) {
          for (int m = oIdx; m < orphanStep.noteCount-1; m++)
            orphanStep.slots[m] = orphanStep.slots[m+1];
          orphanStep.noteCount--;
          if (!orphanStep.noteCount) orphanStep.active = false;
        }
      }
    }

    liveNotes[i].active = false;
    return;
  }
}

// =============================================================================
// ARP
// =============================================================================
void enterArpMode(uint8_t t) {
  tracks[t].arp.enabled=true; tracks[t].arp.noteCount=0; tracks[t].arp.pos=0;
  tracks[t].arp.latch=false; tracks[t].arp.octaveShift=0; tracks[t].arp.noteLocked=0;
  if(tracks[t].arp.octaveRange==0) tracks[t].arp.octaveRange=1;
  tracks[t].arp.div=3;
}
void exitArpMode(uint8_t t) {
  tracks[t].arp.enabled=false; tracks[t].arp.noteCount=0;
  tracks[t].arp.pos=0; tracks[t].arp.latch=false;
}
