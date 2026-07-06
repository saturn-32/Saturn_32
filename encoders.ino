// =============================================================================
// ANIMATION S2-V3 — TRÈS LENT, PLANÈTES DISPERSÉES
// Rotation très lente, planètes à des positions variées côté droit
// Donne une impression de snapshot du système solaire
// =============================================================================

void playBootAnimation() {
  const int SUN_X = 18, SUN_Y = 22;
  const float PL[4][4] = {
    {13, 3,  0.03f,  0.1f},    // Mercure  — en haut à droite
    {22, 4,  0.02f,  -0.4f},   // Vénus    — en bas à droite
    {32, 5,  0.012f,  0.6f},   // Jupiter  — milieu droite
    {43, 6,  0.007f, -0.2f},   // Saturne  — droite légèrement bas
  };
  float angles[4] = {PL[0][3], PL[1][3], PL[2][3], PL[3][3]};

  for (int frame = 0; frame < 75; frame++) {
    display.clearDisplay();

    // Zoom très lent ease-in-out : 0.65 → 1.0
    float t    = frame / 74.0f;
    float ease = t < 0.5f
      ? 4.0f*t*t*t
      : 1.0f - (-2.0f*t+2.0f)*(-2.0f*t+2.0f)*(-2.0f*t+2.0f)/2.0f;
    float zoom = 0.65f + ease * 0.35f;

    display.setTextSize(2); display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 50); display.print("SATURN 32");

    // Soleil
    int sr = 2 + (int)(3.0f * zoom);
    display.drawCircle(SUN_X, SUN_Y, sr, SSD1306_WHITE);
    // Petit point central pour le soleil
    display.drawPixel(SUN_X, SUN_Y, SSD1306_WHITE);

    for (int p = 0; p < 4; p++) {
      float rz = PL[p][0] * zoom;
      int   pr = 2 + (int)(PL[p][1] * zoom * 0.55f);

      // Orbite complète pointillée
      for (int a = 0; a < 360; a += 9) {
        float rad = a * 0.01745f;
        int ox = SUN_X + (int)(rz * cosf(rad));
        int oy = SUN_Y + (int)(rz * sinf(rad));
        if (ox >= 0 && ox < 128 && oy >= 0 && oy < 48)
          display.drawPixel(ox, oy, SSD1306_WHITE);
      }

      int px = SUN_X + (int)(rz * cosf(angles[p]));
      int py = SUN_Y + (int)(rz * sinf(angles[p]));

      if (px >= pr+1 && px < 128-pr-1 && py >= pr+1 && py < 47-pr) {
        display.drawCircle(px, py, pr, SSD1306_WHITE);
        if (p == 1) {
          display.drawPixel(px, py, SSD1306_WHITE);
        }
        else if (p == 2) {
          // Jupiter : bandes horizontales internes
          display.drawFastHLine(px-pr+1, py-1, (pr-1)*2, SSD1306_WHITE);
          display.drawFastHLine(px-pr+1, py+1, (pr-1)*2, SSD1306_WHITE);
          display.drawCircle(px, py, pr, SSD1306_WHITE);
          // Anneaux fins
          for (int ring = 1; ring <= 2; ring++) {
            int rw = pr + (int)((3+ring*3)*zoom);
            for (int a = 0; a < 360; a += 6) {
              float rad = a*0.01745f;
              float ca=cosf(rad), sa=sinf(rad);
              int rx=px+(int)(rw*ca), ry=py+(int)(1.5f*sa);
              if (sa<0 || ca*ca*(float)(rw*rw)>(float)(pr*pr)*1.2f)
                if (rx>=0&&rx<128&&ry>=0&&ry<48) display.drawPixel(rx,ry,SSD1306_WHITE);
            }
          }
          display.drawCircle(px, py, pr, SSD1306_WHITE);
        }
        else if (p == 3) {
          // Saturne : grand anneau + cercle vide
          int rw = pr + (int)(7.0f*zoom);
          for (int a = 0; a < 360; a += 5) {
            float rad = a*0.01745f;
            float ca=cosf(rad), sa=sinf(rad);
            int rx=px+(int)(rw*ca), ry=py+(int)(2.5f*sa);
            if (sa<0 || ca*ca*(float)(rw*rw)>(float)(pr*pr)*0.8f)
              if (rx>=0&&rx<128&&ry>=0&&ry<48) display.drawPixel(rx,ry,SSD1306_WHITE);
          }
          display.drawCircle(px, py, pr, SSD1306_WHITE);
        }
      }
      angles[p] += PL[p][2];
    }

    display.display();
    delay(20);
  }
  display.clearDisplay(); display.display();
}
