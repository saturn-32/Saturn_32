// =============================================================================
// UI.INO v1
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "globals.h"

// Prototypes internes
void drawMain();
void drawPreCount();
void drawWavePicker();
void drawWaveMenu();
void drawPresetMenu();
void drawProjectMenu();
void drawChordMenu();
void drawEffectMenu();
void drawSaveConfirm();
void drawStepGrid(int yTop);
void drawWaveShapeMorphed(int bx, int by, int bw, int bh, uint8_t w, float morph, uint16_t col);
void drawArpOctMenu();
void drawWaveShape(int bx, int by, int bw, int bh, uint8_t w, uint16_t col);

void drawUI() {
  display.clearDisplay();
  if (preCountActive)  { drawPreCount();    display.display(); return; }
  if (wavePickerOpen)  { drawWavePicker();  display.display(); return; }
  if (waveMenuOpen)    { drawWaveMenu();    display.display(); return; }
  if (presetMenuOpen)  { drawPresetMenu();  display.display(); return; }
  if (projectMenuOpen) { drawProjectMenu(); display.display(); return; }
  if (chordMenuOpen)   { drawChordMenu();   display.display(); return; }
  if (arpOctMenuOpen)  { drawArpOctMenu();  display.display(); return; }
  if (effectMenuOpen)   { drawEffectMenu();   display.display(); return; }
  if (saveConfirmOpen)  { drawSaveConfirm();  display.display(); return; }
  drawMain();
  display.display();
}

// =============================================================================
// WAVE PICKER — long press UNDO
// ENC1 = changer la forme / ENC2 = morpher la courbe en temps réel
// La forme dessinée reflète exactement le son produit
// =============================================================================
void drawWavePicker() {
  float morph = (wavePickerPage == 0)
                ? tracks[activeTrack].waveMorph
                : tracks[activeTrack].waveMorph2;

  // ── Déterminer l'onde à afficher selon la page ──────────────────────────
  bool   isOsc2   = (wavePickerPage == 1);
  uint8_t w       = isOsc2 ? tracks[activeTrack].waveform2 : tracks[activeTrack].waveform;
  bool   osc2None = (isOsc2 && w == 255);

  // ── Bandeau titre ────────────────────────────────────────────────────────
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print(isOsc2 ? "OSC2 " : "OSC1 ");
  display.print(TRACK_NAMES[activeTrack]);
  // Indicateur de page : [1] 2  ou  1 [2]
  display.setCursor(90, 2);
  if (!isOsc2) { display.print("[1] 2"); }
  else         { display.print(" 1 [2]"); }
  display.setTextColor(SSD1306_WHITE);

  // ── Ligne navigation ondes : < NOM > ─────────────────────────────────────
  display.setCursor(0, 15);  display.print("<");
  display.setCursor(120, 15); display.print(">");
  const char* waveName = osc2None ? "NONE" : WAVE_NAMES[w];
  int nx = (128 - (int)strlen(waveName) * 6) / 2;
  display.setCursor(nx, 15); display.print(waveName);

  display.drawFastHLine(0, 25, 128, SSD1306_WHITE);

  // ── Forme d'onde (grisée si NONE sur OSC2) ───────────────────────────────
  if (!osc2None) {
    drawWaveShapeMorphed(2, 28, 124, 22, w, morph, SSD1306_WHITE);
  } else {
    // OSC2 = NONE : afficher message centré
    display.setCursor(22, 36); display.print("OSC2 desactive");
  }

  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);

  // ── Barre de mix OSC1/OSC2 ───────────────────────────────────────────────
  if (wavePickerMixOpen && activeTrack != TRACK_DRUM) {
    float mix2   = tracks[activeTrack].osc2Mix;
    // Barre en V : trait central = 100%+100%, curseur indique la position
    int cursorX  = 2 + (int)(mix2 * 122.0f); // 2..124
    display.drawRect(2, 54, 124, 9, SSD1306_WHITE);
    display.drawFastVLine(64, 54, 9, SSD1306_WHITE); // repere centre
    display.fillRect(cursorX, 55, 3, 7, SSD1306_WHITE); // curseur toujours visible
    display.setTextColor(SSD1306_WHITE);
  } else {
    // Afficher le % de morph + hint ENC2
    int morphPct = (int)(morph * 100.0f);
    char morphStr[12]; snprintf(morphStr, sizeof(morphStr), "MORPH %d%%", morphPct);
    int mpx = (128 - (int)strlen(morphStr) * 6) / 2;
    display.setCursor(mpx, 55); display.print(morphStr);
  }
}

// Dessine la forme en tenant compte de waveMorph — miroir exact de la synthèse
void drawWaveShapeMorphed(int bx, int by, int bw, int bh,
                           uint8_t w, float morph, uint16_t col) {
  if (bw <= 1 || bh <= 1) return;
  int midY = by + bh/2;
  int halfH = bh/2 - 1;

  for (int i = 0; i < bw-1; i++) {
    float ph1 = (float)i       / (float)(bw-1);
    float ph2 = (float)(i+1)   / (float)(bw-1);
    float v1  = 0.0f, v2 = 0.0f;

    for (int p = 0; p < 2; p++) {
      float ph  = (p == 0) ? ph1 : ph2;
      float val = 0.0f;

      switch (w) {
        case WAVE_SINE: {
          float mult = 1.0f + fabsf(morph - 0.5f) * 14.0f;
          float mph  = fmodf(ph * mult, 1.0f);
          float x    = (mph < 0.5f) ? (mph*4.0f-1.0f) : (3.0f-mph*4.0f);
          val = x*(1.0f-0.25f*(x<0?-x:x));
          break;
        }
        case WAVE_SQUARE:
        case WAVE_PULSE: {
          float pw = 0.05f + morph * 0.9f;
          val = (ph < pw) ? 0.85f : -0.85f;
          break;
        }
        case WAVE_SAW: {
          float saw    =  ph*2.0f-1.0f;
          float sawInv = -ph*2.0f+1.0f;
          float tri    = (ph<0.5f)?(ph*4.0f-1.0f):(3.0f-ph*4.0f);
          if (morph < 0.5f) { float t=morph*2.0f; val=sawInv*(1.0f-t)+saw*t; }
          else               { float t=(morph-0.5f)*2.0f; val=saw*(1.0f-t)+tri*t; }
          break;
        }
        case WAVE_NOISE: {
          // Pour l'affichage: simuler l'effet LP/HP visuellement
          float noise = sinf(ph*37.7f) * cosf(ph*19.3f) * sinf(ph*53.1f);
          float smooth = sinf(ph*6.2832f*(1.0f+morph*3.0f));
          if (morph < 0.5f) { float t=morph*2.0f; val=smooth*(1.0f-t)+noise*t; }
          else               { float t=(morph-0.5f)*2.0f; val=noise*(1.0f-t)+noise*fabsf(cosf(ph*200.0f))*t; }
          break;
        }
        case WAVE_TRIANGLE: {
          float tri    = (ph<0.5f)?(ph*4.0f-1.0f):(3.0f-ph*4.0f);
          float rampUp = ph*2.0f-1.0f;
          float rampDn = 1.0f-ph*2.0f;
          if (morph < 0.5f) { float t=morph*2.0f; val=rampUp*(1.0f-t)+tri*t; }
          else               { float t=(morph-0.5f)*2.0f; val=tri*(1.0f-t)+rampDn*t; }
          break;
        }
        case WAVE_FM: {
          float ratio  = 0.5f + morph*13.5f;
          float modPh  = fmodf(ph + 0.5f*sinf(ph*ratio*6.2832f), 1.0f);
          float x      = (modPh<0.5f)?(modPh*4.0f-1.0f):(3.0f-modPh*4.0f);
          val = x*(1.0f-0.25f*(x<0?-x:x));
          break;
        }
        case WAVE_WTBL: {
          float pos  = morph * 3.0f;
          int   wi0  = (int)pos; if (wi0>3) wi0=3;
          int   wi1  = wi0+1;   if (wi1>3) wi1=3;
          float frac = pos - (float)wi0;
          float forms[4];
          forms[0] = 0.6f*sinf(ph*6.2832f)+0.3f*sinf(ph*12.566f)+0.1f*sinf(ph*18.85f);
          forms[1] = 0.5f*sinf(ph*6.2832f)+0.3f*sinf(ph*14.3f)+0.2f*sinf(ph*23.7f);
          forms[2] = 0.4f*sinf(ph*6.2832f)+0.3f*sinf(ph*12.566f)+0.15f*sinf(ph*18.85f)
                    +0.1f*sinf(ph*25.1f)+0.05f*sinf(ph*31.4f);
          forms[3] = 0.85f*sinf(ph*6.2832f)+0.15f*sinf(ph*12.566f);
          val = forms[wi0]*(1.0f-frac) + forms[wi1]*frac;
          val *= 0.8f;
          break;
        }
        case WAVE_HARM: {
          // Sinus + harmoniques progressifs selon morph
          float h2 = morph > 0.0f  ? morph*0.50f : 0.0f;
          float h3 = morph > 0.33f ? (morph-0.33f)*1.5f*0.25f : 0.0f;
          val = sinf(ph*6.2832f) + h2*sinf(ph*12.566f) + h3*sinf(ph*18.85f);
          val /= (1.0f+h2+h3);
          break;
        }
        case WAVE_FOLD: {
          float gain = 1.0f + morph * 5.0f;
          float raw  = sinf(ph*6.2832f) * gain;
          raw = raw - 4.0f * floorf((raw+1.0f)*0.25f);
          if (raw> 1.0f) raw= 2.0f-raw;
          if (raw<-1.0f) raw=-2.0f-raw;
          val = raw;
          break;
        }
        case WAVE_WARM: {
          // Saw band-limitée : 1..5 harmoniques
          int nH = 1+(int)(morph*4.0f);
          float acc=0.0f; float sg=1.0f;
          for (int h=1;h<=nH;h++) { acc+=sg*sinf(ph*6.2832f*h)/(float)h; sg=-sg; }
          val = acc * (2.0f/3.14159f);
          break;
        }
        case WAVE_SUB: {
          // Sinus fond + sinus -1 octave
          val = sinf(ph*6.2832f)*(1.0f-morph) + sinf(ph*3.14159f)*morph;
          break;
        }
        case WAVE_CHEW: {
          float x = sinf(ph*6.2832f);
          float t1=x, t2=2.0f*x*x-1.0f, t3=x*(4.0f*x*x-3.0f);
          float m=morph*2.0f;
          float w1=1.0f, w2=constrain(m,0.0f,1.0f), w3=constrain(m-1.0f,0.0f,1.0f);
          val = (t1*w1*(1.0f-w2)+t2*w2*(1.0f-w3)+t3*w3)*0.85f;
          break;
        }
      }
      if (p==0) v1=val; else v2=val;
    }

    int y1 = midY - (int)(v1*(float)halfH);
    int y2 = midY - (int)(v2*(float)halfH);
    y1 = constrain(y1, by, by+bh-1);
    y2 = constrain(y2, by, by+bh-1);
    display.drawLine(bx+i, y1, bx+i+1, y2, col);
  }
}

// =============================================================================
// ÉCRAN PRINCIPAL
// =============================================================================
void drawMain() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne y=0 : BPM valeur
  display.setCursor(0, 0);
  char b[5]; snprintf(b, 5, "%3d", bpm); display.print(b);

  // M métronome sous le BPM (y=9)
  if (metronomeOn) { display.setCursor(0, 9); display.print("M"); }

  // Barre volume master
  int vm = (int)(masterVolume * 36.0f);
  display.drawRect(24, 2, 36, 5, SSD1306_WHITE);
  if (vm > 0) display.fillRect(24, 2, vm, 5, SSD1306_WHITE);

  // Barre volume track
  int vt = (int)(tracks[activeTrack].volume * 32.0f);
  display.drawRect(62, 2, 32, 5, SSD1306_WHITE);
  if (vt > 0) display.fillRect(62, 2, vt, 5, SSD1306_WHITE);

  // Ligne y=9 : PLAY (sous barre V) | octave (milieu) | division arp
  if (isPlaying) { display.setCursor(24, 9); display.print(">"); }

  int od = (int)tracks[activeTrack].octave - 3;
  display.setCursor(44, 9);
  if (od >= 0) display.print("+");
  display.print(od);

  // Division arp — affichée si arp actif sur la piste active
  if (tracks[activeTrack].arp.enabled) {
    static const char* DIV_NAMES[4] = {"1/4","1/8","1/16","1/32"};
    display.setCursor(68, 9);
    display.print(DIV_NAMES[tracks[activeTrack].arp.div % 4]);
  }

  // Coin supérieur droit (x=96..127, y=0..13) :
  // - isRecording                → bloc REC inversé
  // - isPlaying && !isRecording  → onde sinusoïdale animée
  // - !isPlaying && !isRecording → trait plat
  if (isRecording) {
    display.fillRect(96, 0, 32, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(98, 4); display.print("REC");
    display.setTextColor(SSD1306_WHITE);
  } else {
    const int WX = 96, WY = 7, WW = 32, WAMP = 5;
    if (isPlaying) {
      float phaseOffset = (currentStep % 16) * (2.0f * 3.14159f / 16.0f);
      int prevY = WY + (int)(WAMP * sinf(phaseOffset));
      for (int x = 1; x < WW; x++) {
        float angle = phaseOffset + x * (2.0f * 3.14159f * 2.0f / WW);
        int y = WY + (int)(WAMP * sinf(angle));
        display.drawLine(WX + x - 1, prevY, WX + x, y, SSD1306_WHITE);
        prevY = y;
      }
    } else {
      display.drawFastHLine(WX, WY, WW, SSD1306_WHITE);
    }
  }
  display.drawFastHLine(0, 15, 128, SSD1306_WHITE);

  // Barre des 4 pistes
  for (int t = 0; t < TRACK_COUNT; t++) {
    int x = t*32; bool sel = (t==activeTrack); bool mut = tracks[t].muted;
    if (sel) { display.fillRoundRect(x,17,30,10,2,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
    else     { display.drawRoundRect(x,17,30,10,2,SSD1306_WHITE); display.setTextColor(SSD1306_WHITE); }
    display.setCursor(x+2, 19); display.print(TRACK_NAMES[t]);
    display.setTextColor(SSD1306_WHITE);
    if (mut) display.drawLine(x+1,26,x+28,18,SSD1306_WHITE);
    if (tracks[t].arp.enabled) display.fillRect(x+26,24,3,3,sel?SSD1306_BLACK:SSD1306_WHITE);
  }

  // Info piste
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 29);
  if (stepEditMode) {
    uint8_t tLen = tracks[activeTrack].trackLen;
    display.print("EDIT "); display.print(editCursor+1); display.print("/"); display.print(tLen);
    if (activeTrack == TRACK_DRUM) {
      Step& s = tracks[TRACK_DRUM].steps[editCursor];
      if (s.active && s.noteCount > 0) {
        display.print(": ");
        for (int n = 0; n < s.noteCount && n < 4; n++) {
          if (n) display.print(",");
          const char* dn = DRUM_NAMES[s.slots[n].note<13?s.slots[n].note:0];
          display.print(dn[0]); display.print(dn[1]);
        }
      }
    } else {
      Step& s = tracks[activeTrack].steps[editCursor];
      if (s.active && s.noteCount > 0) {
        display.print(":");
        for (int n=0; n<s.noteCount&&n<3; n++) { if(n) display.print(","); display.print(NOTE_NAMES[s.slots[n].note]); }
      }
    }
  } else {
    // Animation points accumulatifs sous chaque instrument
    // 1 point par temps (4 steps), reset après 4 temps
    // Chaque piste a sa zone de 32px alignée sous son nom
    if (isPlaying) {
      int beat    = (currentStep / 4) % 4; // 0,1,2,3
      int numDots = beat + 1;              // 1,2,3,4
      // Spinner latch: 4 frames par cycle de 16 steps (1 frame par beat)
      static const char* SPINNER[4] = {"-", "\\", "|", "/"};
      int spinFrame = (currentStep / 4) % 4;

      for (int t = 0; t < TRACK_COUNT; t++) {
        int zoneX = t * 32;

        // Piste en latch ARP → spinner à la place des points
        if (tracks[t].arp.enabled && tracks[t].arp.latch && tracks[t].arp.noteCount > 0) {
          display.setTextSize(1);
          display.setCursor(zoneX + 12, 29);
          display.print(SPINNER[spinFrame]);
          continue;
        }

        bool hasNotes = false;
        for (int s = 0; s < STEPS_PER_TRACK; s++) {
          if (tracks[t].steps[s].active && tracks[t].steps[s].noteCount > 0) {
            hasNotes = true; break;
          }
        }
        if (!hasNotes) continue;

        for (int d = 0; d < numDots; d++) {
          int px = zoneX + 2 + d * 7;
          display.fillRect(px, 30, 5, 3, SSD1306_WHITE);
        }
      }
    }
  }

  drawStepGrid(38);
}

// =============================================================================
// GRILLE STEP
// =============================================================================
void drawStepGrid(int yTop) {
  const int W=15, H=11, GAP=1;
  uint8_t tLen = tracks[activeTrack].trackLen;

  // Scroll automatique : la page suit la tête de lecture
  uint8_t effStep = isPlaying ? (currentStep % tLen) : currentStep;
  if (tLen == 32) {
    if (stepEditMode) {
      // En stepEdit, la page suit le curseur d'édition
      stepViewOffset = (editCursor >= 16) ? 16 : 0;
    } else {
      stepViewOffset = (effStep >= 16) ? 16 : 0;
    }
  } else {
    stepViewOffset = 0;
  }

  for (int row=0; row<2; row++) {
    for (int col=0; col<8; col++) {
      int idx = stepViewOffset + row*8 + col;
      int x   = col*(W+GAP);
      int y   = yTop + row*(H+GAP);
      Step& st = tracks[activeTrack].steps[idx];
      bool hasNote = st.active;
      bool hasArp  = tracks[activeTrack].arpSteps[idx].active;
      bool isCur   = isPlaying && ((effStep == (uint8_t)idx));
      bool isEdit  = stepEditMode && ((int)editCursor == idx);

      if (isCur && hasNote) {
        bool isMid   = stepIsAllMid(st);
        bool hasEnd  = stepHasTieEnd(st);
        bool hasStart= stepHasTieStart(st);
        if (isMid) {
          display.fillRect(x-GAP, y+H/2-1, W+GAP+2, 3, SSD1306_WHITE);
          display.drawRect(x, y, W, H, SSD1306_WHITE);
        } else if (hasEnd && !hasStart) {
          display.fillRect(x-GAP, y+H/2-1, GAP+W/2+2, 3, SSD1306_WHITE);
          display.fillRoundRect(x+W/2-2,y,W/2+2,H,1,SSD1306_WHITE);
          display.drawRoundRect(x+W/2-1,y+1,W/2,H-2,1,SSD1306_BLACK);
        } else {
          display.fillRoundRect(x,y,W,H,1,SSD1306_WHITE);
          display.drawRoundRect(x+1,y+1,W-2,H-2,1,SSD1306_BLACK);
          if (hasStart)
            display.fillRect(x+W-2, y+H/2-1, GAP+4, 3, SSD1306_WHITE);
        }
      } else if (isCur) {
        display.fillRoundRect(x,y,W,H,1,SSD1306_WHITE);

      } else if (hasNote) {
        bool isMid   = stepIsAllMid(st);
        bool hasEnd  = stepHasTieEnd(st);
        bool hasStart= stepHasTieStart(st);
        bool hasShort= stepHasShort(st);

        if (hasStart && !isMid) {
          display.fillRoundRect(x,y,W,H,1,SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
          display.setCursor(x+2, y+2);
          if (st.noteCount > 1) display.print(st.noteCount);
          display.setTextColor(SSD1306_WHITE);
          display.fillRect(x+W-2, y+H/2-1, GAP+4, 3, SSD1306_WHITE);

        } else if (isMid) {
          display.fillRect(x-GAP, y+H/2-1, W+GAP+2, 3, SSD1306_WHITE);

        } else if (hasEnd && !hasStart) {
          display.fillRect(x-GAP, y+H/2-1, GAP+W/2+2, 3, SSD1306_WHITE);
          display.fillRoundRect(x+W/2-2,y,W/2+2,H,1,SSD1306_WHITE);

        } else {
          display.fillRoundRect(x,y,W,H,1,SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
          if (activeTrack == TRACK_DRUM) {
            uint8_t ni = st.slots[0].note;
            const char* dn = DRUM_NAMES[ni<13?ni:0];
            display.setCursor(x+1, y+2);
            display.print(dn[0]); display.print(dn[1]);
            if (st.noteCount > 1) {
              display.print('+'); display.print(st.noteCount - 1);
            }
            if (st.drumOffset==1 || st.drumOffset==3) // barre droite = 2ème demi-step
              display.fillRect(x+W-4, y+1, 3, H-2, SSD1306_BLACK);
            if (st.drumOffset==3) // 2ème barre = double note
              display.fillRect(x+1,   y+1, 3, H-2, SSD1306_BLACK);
          } else {
            if (st.noteCount > 1) { display.setCursor(x+5,y+2); display.print(st.noteCount); }
            if (st.drumOffset==1 || st.drumOffset==3)
              display.fillRect(x+W-4, y+1, 3, H-2, SSD1306_BLACK);
            if (st.drumOffset==3)
              display.fillRect(x+1,   y+1, 3, H-2, SSD1306_BLACK);
          }
          display.setTextColor(SSD1306_WHITE);
        }

      } else if (hasArp) {
        display.drawRoundRect(x,y,W,H,1,SSD1306_WHITE);
        display.drawLine(x+2,y+H-2,x+W-2,y+2,SSD1306_WHITE);
      } else {
        display.drawRoundRect(x,y,W,H,1,SSD1306_WHITE);
        if (idx%2==0) display.drawPixel(x+W/2,y+H/2,SSD1306_WHITE);
      }

      if (isEdit) {
        display.drawRect(x-1,y-1,W+2,H+2,SSD1306_WHITE);
      }
    }
  }

  // Indicateur 32 steps : barre sous la grille
  // Page 1 (steps 1-16)  → barre à GAUCHE  (x=0..63)
  // Page 2 (steps 17-32) → barre à DROITE  (x=64..127)
  if (tLen == 32) {
    int barY = yTop + 2*(H+GAP) + 1;
    if (stepViewOffset == 0)
      display.drawLine(0, barY, 63, barY, SSD1306_WHITE);   // page 1 → gauche
    else
      display.drawLine(64, barY, 127, barY, SSD1306_WHITE); // page 2 → droite
  }
}

// =============================================================================
// MENU SYNTH — pages horizontales
// =============================================================================
void drawWaveMenu() {
  display.clearDisplay();

  static const char* PAGE_NAMES[4]    = { "OSC", "FLT", "LFO", "FX " };
  // Params FX normaux (synths) : REVERB, CHORUS, DRIVE, GLIDE
  static const uint8_t PAGE_PARAMS[4][5] = {
    { PARAM_DECAY, PARAM_ATTACK, PARAM_SUB,    PARAM_NOISE,   255        }, // OSC (4)
    { PARAM_FILTER, PARAM_FILRES, PARAM_SUSTAIN, PARAM_RELEASE, 255      }, // FLT (4)
    { PARAM_LFO_DEP, PARAM_LFO_RAT, PARAM_LFO_DST, 255, 255             }, // LFO (3)
    { PARAM_DELAY, PARAM_CHORUS, PARAM_DRIVE, PARAM_GLIDE, 255        }, // FX  (4)
  };
  // Pages DRUM : OSC vide, FLT=cutoff seul, LFO vide, FX=drive seul
  static const uint8_t PAGE_PARAMS_DRUM[4][5] = {
    { 255, 255, 255, 255, 255                          }, // OSC : vide
    { PARAM_FILTER, PARAM_DELAY, PARAM_DRIVE, 255, 255 }, // FLT+FX : filtre + reverb + drive
    { 255, 255, 255, 255, 255                          }, // LFO : vide
    { 255, 255, 255, 255, 255                          }, // FX  : vide
  };
  static const uint8_t PAGE_SIZES[4]      = { 4, 4, 3, 4 };
  static const uint8_t PAGE_SIZES_DRUM[4] = { 0, 3, 0, 0 };

  bool isDrum = (activeTrack == TRACK_DRUM);
  const uint8_t (*curParams)[5] = isDrum ? PAGE_PARAMS_DRUM : PAGE_PARAMS;
  const uint8_t  *curSizes      = isDrum ? PAGE_SIZES_DRUM  : PAGE_SIZES;
  // Noms courts 3 chars pour les tabs
  static const char* TAB_NAMES[17] = {
    "WVE","DEC","---","REV","ATK","CHO","DRV","FLT","GLD",
    "LDP","LRT","LDS","SUB","NSE","RES","SUS","REL"
  };

  uint8_t pg   = waveMenuPage % 4;

  // Garde anti-crash : si la page courante est vide pour DRUM (sz==0),
  // corriger silencieusement vers la première page valide
  if (isDrum && curSizes[pg] == 0) {
    for (uint8_t p = 0; p < 4; p++) {
      if (curSizes[p] > 0) { waveMenuPage = p; pg = p; break; }
    }
    // Corriger aussi le param affiché
    waveMenuParam = curParams[pg][0];
  }

  uint8_t sz   = curSizes[pg];
  if (sz == 0) return; // sécurité ultime : page vide, ne rien dessiner

  // ── Ligne 1 : titre + nom de page ────────────────────────────────────
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print(TRACK_NAMES[activeTrack]);
  // Page actuelle centrée à droite
  display.setCursor(70, 2);
  display.print(PAGE_NAMES[pg]);
  display.setTextColor(SSD1306_WHITE);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // ── Ligne 2 : onglets paramètres ─────────────────────────────────────
  int tabW = 128 / sz;
  for (int i = 0; i < (int)sz; i++) {
    uint8_t pi = curParams[pg][i];
    if (pi == 255) continue;
    int tx  = i * tabW;
    bool sel = (pi == waveMenuParam);
    if (sel) {
      display.fillRect(tx, 11, tabW, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRect(tx, 11, tabW, 10, SSD1306_WHITE);
    }
    int nw = strlen(TAB_NAMES[pi]) * 6;
    display.setCursor(tx + (tabW - nw) / 2, 13);
    display.print(TAB_NAMES[pi]);
    display.setTextColor(SSD1306_WHITE);
  }
  display.drawFastHLine(0, 21, 128, SSD1306_WHITE);

  // ── Contenu (y=23..63) ───────────────────────────────────────────────
  uint8_t p = waveMenuParam;

  if (p == PARAM_WAVE) {
    // Grille 4x2 des formes d'onde + hint morph
    uint8_t w = tracks[activeTrack].waveform;
    for (int i = 0; i < WAVE_COUNT; i++) {
      int col = i % 4, row = i / 4;
      int bx  = col * 31 + 2;
      int by  = 24 + row * 18;
      if (i == w) {
        display.fillRoundRect(bx, by, 29, 14, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRoundRect(bx, by, 29, 14, 2, SSD1306_WHITE);
      }
      display.setCursor(bx + 3, by + 4);
      display.print(WAVE_NAMES[i]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(2, 60);
    display.print("MORPH:");
    display.print((int)(tracks[activeTrack].waveMorph * 100));
    display.print("%");

  } else if (p == PARAM_LFO_DST) {
    // 3 boutons destination LFO
    static const char* DST_NAMES[3] = { "WAH", "TREMOLO", "VIBRATO" };
    uint8_t cur = tracks[activeTrack].lfoDest % 3;
    for (int i = 0; i < 3; i++) {
      int bx = i * 43, by = 30;
      if (i == cur) {
        display.fillRoundRect(bx, by, 41, 20, 2, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRoundRect(bx, by, 41, 20, 2, SSD1306_WHITE);
      }
      int nw = strlen(DST_NAMES[i]) * 6;
      display.setCursor(bx + (41 - nw) / 2, by + 7);
      display.print(DST_NAMES[i]);
      display.setTextColor(SSD1306_WHITE);
    }

  } else {
    // Paramètre numérique : nom complet + valeur taille 2 + barre
    static const char* PARAM_FULL_NAMES[17] = {
      "FORME", "DECAY", "-----", "REVERB", "ATTACK",
      "CHORUS", "DRIVE", "FILTER", "GLIDE",
      "LFO DEPTH", "LFO RATE", "LFO DEST",
      "SUB OSC", "NOISE", "RESONANCE",
      "SUSTAIN", "RELEASE"
    };
    float val = 0.0f;
    switch (p) {
      case PARAM_DECAY:   val = tracks[activeTrack].decayTime;    break;
      case PARAM_REVERB:  val = tracks[activeTrack].reverbAmt;    break;
      case PARAM_DELAY:   val = tracks[activeTrack].delayAmt;     break;
      case PARAM_ATTACK:  val = tracks[activeTrack].attackAmt;    break;
      case PARAM_CHORUS:  val = tracks[activeTrack].chorusAmt;    break;
      case PARAM_DRIVE:   val = tracks[activeTrack].driveAmt;     break;
      case PARAM_FILTER:  val = tracks[activeTrack].filterCutoff; break;
      case PARAM_GLIDE:   val = tracks[activeTrack].glideAmt;     break;
      case PARAM_LFO_DEP: val = tracks[activeTrack].lfoDepth;     break;
      case PARAM_LFO_RAT: val = tracks[activeTrack].lfoRate;      break;
      case PARAM_SUB:     val = tracks[activeTrack].subOscAmt;    break;
      case PARAM_NOISE:   val = tracks[activeTrack].noiseAmt;     break;
      case PARAM_FILRES:  val = tracks[activeTrack].filterRes;    break;
      case PARAM_SUSTAIN: val = tracks[activeTrack].sustainLevel; break;
      case PARAM_RELEASE: val = tracks[activeTrack].releaseTime;  break;
    }
    // Nom complet centré
    const char* fullName = (p < 17) ? PARAM_FULL_NAMES[p] : PARAM_NAMES[p];
    int nameW = strlen(fullName) * 6;
    display.setCursor((128 - nameW) / 2, 24);
    display.print(fullName);
    // Valeur textSize(2)
    char vstr[6]; snprintf(vstr, 6, "%d%%", (int)(val * 100.0f));
    display.setTextSize(2);
    int vx = (128 - (int)strlen(vstr) * 12) / 2;
    display.setCursor(vx, 33);
    display.print(vstr);
    display.setTextSize(1);
    // Barre de progression
    display.drawRect(2, 52, 124, 8, SSD1306_WHITE);
    int bw = (int)(val * 122.0f);
    if (bw > 0) display.fillRect(3, 53, bw, 6, SSD1306_WHITE);
  }
}

// =============================================================================
// MENU PROJET — Save / Load 4 slots
// =============================================================================
void drawProjectMenu() {
  display.clearDisplay();

  // ── Bandeau titre avec onglets PROJET / PRESET ────────────────────────
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print(projectPage == 1 ? "PROJET" : "PRESET");
  // Onglets
  for (int i = 0; i < 2; i++) {
    int tx = 78 + i * 26;
    if (i == (int)projectPage) {
      display.fillRect(tx, 0, 25, 10, SSD1306_BLACK);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setTextColor(SSD1306_BLACK);
    }
    display.setCursor(tx + 3, 2);
    display.print(i == 1 ? "PRJ" : "PRE");
    display.setTextColor(SSD1306_WHITE);
  }
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  if (projectPage == 1) {
    // ── PAGE PROJET : 4 slots ─────────────────────────────────────────
    // Toggle SAVE/LOAD
    display.setCursor(2, 13);
    if (projectSaveMode) {
      display.fillRect(0, 12, 40, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.print("SAVE");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRect(0, 12, 40, 9, SSD1306_WHITE);
      display.print("SAVE");
    }
    display.setCursor(44, 13);
    if (!projectSaveMode) {
      display.fillRect(42, 12, 40, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.print("LOAD");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRect(42, 12, 40, 9, SSD1306_WHITE);
      display.print("LOAD");
    }
    display.drawFastHLine(0, 22, 128, SSD1306_WHITE);

    // 4 slots
    for (int i = 0; i < 4; i++) {
      int bx = i * 32;
      bool sel    = (i == (int)projectSlot);
      bool exists = projectExists((uint8_t)i);
      if (sel) {
        display.fillRect(bx, 24, 31, 38, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRect(bx, 24, 31, 38, SSD1306_WHITE);
      }
      display.setCursor(bx + 11, 29);
      display.print(i + 1);
      display.setCursor(bx + 3, 42);
      display.print(exists ? "SAV" : "-");
      display.setTextColor(SSD1306_WHITE);
    }

  } // page PROJET
  if (projectPage == 0) {
    // ── PAGE PRESET : 4 presets pour la piste active ─────────────────
    display.setCursor(2, 13);
    if (projectSaveMode) {
      display.fillRect(0, 12, 40, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.print("SAVE");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRect(0, 12, 40, 9, SSD1306_WHITE);
      display.print("SAVE");
    }
    display.setCursor(44, 13);
    if (!projectSaveMode) {
      display.fillRect(42, 12, 40, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.print("LOAD");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRect(42, 12, 40, 9, SSD1306_WHITE);
      display.print("LOAD");
    }
    // Nom piste active
    display.setCursor(90, 13);
    display.print(TRACK_NAMES[activeTrack]);
    display.drawFastHLine(0, 22, 128, SSD1306_WHITE);

    // 4 presets
    for (int i = 0; i < 4; i++) {
      int bx = i * 32;
      bool sel = (i == (int)tracks[activeTrack].activePreset);
      if (sel) {
        display.fillRect(bx, 24, 31, 38, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRect(bx, 24, 31, 38, SSD1306_WHITE);
      }
      display.setCursor(bx + 11, 29);
      display.print(i + 1);
      display.setCursor(bx + 5, 42);
      display.print("PRE");
      display.setTextColor(SSD1306_WHITE);
    }
  }
}

// =============================================================================
// MENU CHORD — Mode accord par piste
// =============================================================================
// =============================================================================
// MENU EFFET
// =============================================================================
void drawEffectMenu() {
  static const char* EFFECT_NAMES[8] = {
    "FILTER SW", "GATE", "BIT CRUSH", "PITCH   ",
    "VINYL STP", "FLANGER",  "REWIND",   "REVERSE"
  };

  display.clearDisplay();

  // Page : 0 = effets 0-5 (3 lignes x 2), page 1 = effets 6-7 (1 ligne x 2)
  uint8_t page     = (effectCursor < 6) ? 0 : 1;
  uint8_t idxStart = (page == 0) ? 0 : 6;
  uint8_t idxEnd   = (page == 0) ? 6 : 8;
  uint8_t count    = idxEnd - idxStart;

  // Titre
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(30, 2);
  display.print("EFFECT");
  // Indicateur de page
  display.setCursor(95, 2);
  display.print(page == 0 ? "1/2" : "2/2");
  display.setTextColor(SSD1306_WHITE);

  // Grille 2 colonnes, lignes dynamiques
  const int BW = 62, BH = 14, GAP = 2;
  const int X0 = 1, Y0 = 13;

  for (uint8_t i = 0; i < count; i++) {
    uint8_t effectIdx = idxStart + i;
    int col = i % 2;
    int row = i / 2;
    int x   = X0 + col * (BW + GAP);
    int y   = Y0 + row * (BH + GAP);

    bool isSelected = (effectIdx == (uint8_t)effectCursor);
    bool isActive   = (effectIdx == (uint8_t)activeEffect);

    if (isActive) {
      // Effet actif : bloc plein blanc
      display.fillRoundRect(x, y, BW, BH, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else if (isSelected) {
      // Curseur sélectionné : fond noir avec bordure blanche épaisse (2px)
      display.fillRoundRect(x, y, BW, BH, 3, SSD1306_WHITE);
      display.fillRoundRect(x+2, y+2, BW-4, BH-4, 2, SSD1306_BLACK);
      display.setTextColor(SSD1306_WHITE);
    } else {
      // Non sélectionné : contour simple
      display.drawRoundRect(x, y, BW, BH, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }

    int tx = x + (BW - (int)strlen(EFFECT_NAMES[effectIdx]) * 6) / 2;
    display.setCursor(tx, y + 4);
    display.print(EFFECT_NAMES[effectIdx]);
    display.setTextColor(SSD1306_WHITE);
  }

  // Barre d'intensité en bas (si effet actif)
  if (activeEffect >= 0) {
    display.drawRect(1, 61, 118, 3, SSD1306_WHITE);
    int barW = (int)(effectIntensity * 114 / 8);
    if (barW > 0) display.fillRect(2, 62, barW, 1, SSD1306_WHITE);
    display.setCursor(121, 60);
    display.print(effectIntensity);
  }
}

void drawChordMenu() {
  display.clearDisplay();
  static const char* CHORD_NAMES[5] = { "OFF", "MAJEUR", "MINEUR", "DOM 7", "MIN 7" };

  // Titre
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print("CHORD  ");
  display.print(TRACK_NAMES[activeTrack]);
  display.setTextColor(SSD1306_WHITE);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Instruction
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  // Mode actuel en grand
  uint8_t cm = tracks[activeTrack].chordMode;
  display.setTextSize(2);
  const char* cn = CHORD_NAMES[cm < 5 ? cm : 0];
  int nw = strlen(cn) * 12;
  display.setCursor((128 - nw) / 2, 28);
  display.print(cn);
  display.setTextSize(1);

  // Indicateur 5 points en bas
  for (int i = 0; i < 5; i++) {
    int bx = 44 + i * 9;
    if (i == cm) display.fillCircle(bx, 58, 3, SSD1306_WHITE);
    else         display.drawCircle(bx, 58, 3, SSD1306_WHITE);
  }
}

// =============================================================================
// MENU ARP OCTAVE RANGE
// =============================================================================
void drawArpOctMenu() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print("ARP OCTAVE RANGE");
  display.setTextColor(SSD1306_WHITE);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  uint8_t cur = tracks[activeTrack].arp.octaveRange;
  static const char* OCT_LABELS[3] = { "1 OCTAVE", "2 OCTAVES", "RANDOM" };

  display.setTextSize(2);
  const char* lbl = OCT_LABELS[(cur > 0 && cur <= 3) ? cur-1 : 0];
  int nw = strlen(lbl) * 12;
  display.setCursor((128 - nw) / 2, 28);
  display.print(lbl);
  display.setTextSize(1);

  // 3 points indicateurs
  for (int i = 0; i < 3; i++) {
    int bx = 50 + i * 14;
    if ((int)cur == i+1) display.fillCircle(bx, 58, 4, SSD1306_WHITE);
    else                  display.drawCircle(bx, 58, 4, SSD1306_WHITE);
  }
}

void drawWaveShape(int bx, int by, int bw, int bh, uint8_t w, uint16_t col) {
  if (bw <= 1) return;
  int midY = (bh > 0) ? by + bh/2 : by;
  int halfH = (bh > 2) ? bh/2 - 1 : 1;
  for (int i = 0; i < bw-1; i++) {
    float t1 = (float)i     / (float)(bw-1);
    float t2 = (float)(i+1) / (float)(bw-1);
    float v1=0, v2=0;
    switch (w % 5) { // Pour aperçu, on mappe les 8 ondes sur 5 formes visuelles
      case 0: v1=-sinf(t1*6.2832f); v2=-sinf(t2*6.2832f); break;
      case 1: v1=t1<0.5f?-0.8f:0.8f; v2=t2<0.5f?-0.8f:0.8f; break;
      case 2: v1=t1*2-1; v2=t2*2-1; break;
      case 3: { uint32_t h1=(uint32_t)(t1*63)^0xA3; h1^=h1<<7; h1^=h1>>5;
                uint32_t h2=(uint32_t)(t2*63)^0xA3; h2^=h2<<7; h2^=h2>>5;
                v1=(float)(int8_t)(h1&0xFF)/128.f; v2=(float)(int8_t)(h2&0xFF)/128.f; break; }
      case 4: v1=t1<0.5f?t1*4-1:3-t1*4; v2=t2<0.5f?t2*4-1:3-t2*4; break;
    }
    int y1 = midY + (int)(v1*halfH);
    int y2 = midY + (int)(v2*halfH);
    display.drawLine(bx+i, y1, bx+i+1, y2, col);
  }
}

// =============================================================================
// MENU PRESET
// =============================================================================
void drawPresetMenu() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print("PRESET ");
  display.print(TRACK_NAMES[activeTrack]);
  display.print(" ");
  display.print(tracks[activeTrack].activePreset + 1);
  display.setTextColor(SSD1306_WHITE);

  if (loadCursor < 0) {
    for (int i = 0; i < 2; i++) {
      int y=18+i*16; bool sel=(i==presetCursor);
      if (sel) { display.fillRoundRect(20,y,88,13,2,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
      else     { display.drawRoundRect(20,y,88,13,2,SSD1306_WHITE); display.setTextColor(SSD1306_WHITE); }
      display.setCursor(47, y+3); display.print(i==0?"SAVE":"LOAD");
      display.setTextColor(SSD1306_WHITE);
    }
  } else {
    display.setCursor(0, 13); display.print("Charger preset:");
    for (int i=0; i<4; i++) {
      int y=23+i*10; bool sel=(i==loadCursor);
      if (sel) { display.fillRect(0,y-1,128,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
      display.setCursor(6, y);
      display.print(PRESET_PREFIXES[activeTrack]); display.print(" "); display.print(i+1);
      if (i==(int)tracks[activeTrack].activePreset) display.print(" *");
      display.setTextColor(SSD1306_WHITE);
    }
  }
}

// =============================================================================
// PRE-COUNT
// =============================================================================
void drawPreCount() {
  display.clearDisplay();
  for (int i=0; i<3; i++) display.drawRoundRect(i*3,i*3,128-i*6,64-i*6,4,SSD1306_WHITE);
  display.setTextSize(5); display.setCursor(preCountVal>=10?28:44, 8); display.print(preCountVal);
  display.setTextSize(1);
}

// =============================================================================
// ANIMATION SAVE PRESET
// =============================================================================
// =============================================================================
// POPUP CONFIRMATION SAVE
// =============================================================================
void drawSaveConfirm() {
  display.clearDisplay();
  // Titre
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(saveConfirmIsProject ? 22 : 22, 2);
  display.print(saveConfirmIsProject ? "SAVE PROJECT ?" : "SAVE PRESET ?");
  display.setTextColor(SSD1306_WHITE);

  // Deux boutons YES / NO
  // YES
  if (!saveConfirmCursor) {
    display.fillRoundRect(10, 26, 44, 22, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(10, 26, 44, 22, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(24, 33); display.print("YES");
  display.setTextColor(SSD1306_WHITE);

  // NO
  if (saveConfirmCursor) {
    display.fillRoundRect(74, 26, 44, 22, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(74, 26, 44, 22, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(90, 33); display.print("NO");
  display.setTextColor(SSD1306_WHITE);
}

void drawSaveNotif(const char* line1, const char* line2) {
  // Barre de progression rapide
  for (int i = 0; i <= 128; i += 16) {
    display.clearDisplay();
    display.drawRoundRect(0, 15, 128, 34, 4, SSD1306_WHITE);
    display.fillRoundRect(0, 15, i, 34, 4, SSD1306_WHITE);
    display.setTextColor(i > 64 ? SSD1306_BLACK : SSD1306_WHITE);
    display.setCursor(10, 22); display.print(line1);
    display.setCursor(10, 33); display.print(line2);
    display.setTextColor(SSD1306_WHITE);
    display.display(); delay(20);
  }
  // Message final — court (400ms max)
  display.clearDisplay();
  display.fillRoundRect(0, 15, 128, 34, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(10, 22); display.print(line1);
  display.setCursor(10, 33); display.print(line2);
  display.setTextColor(SSD1306_WHITE);
  display.display(); delay(400);
}

void drawPresetSaveAnim() {
  drawSaveNotif("  PRESET  SAVE", "  Sauvegarde...");
}
