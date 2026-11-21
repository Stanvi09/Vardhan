/*
 * brand.c - the Vardhan logo, the opening animation and the sign-in page.
 *
 * The mark: a rounded tile (violet -> cyan) with a white "V" whose right arm
 * rises into an arrow. V for Vardhan (Sanskrit: growth), a tick for "checked",
 * and a rising line for growth. Drawn as vectors, so it can grow, spin and
 * react to the mouse.
 */
#include "app.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static Texture2D tile; static bool tile_ok;
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float eout(float t) { t = clampf(t, 0, 1); return 1 - powf(1 - t, 3); }
static float eback(float t) { t = clampf(t, 0, 1); const float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2); }

/* tile image: diagonal gradient, rounded corners with a soft edge, light sheen on top */
static void bake_tile(void) {
    const int N = 512; const float R = N * 0.25f;
    Image im = GenImageColor(N, N, BLANK);
    Color *px = (Color *)im.data;
    Color c0 = { 139, 92, 246, 255 }, c1 = { 6, 182, 212, 255 };
    if (THEME == THEME_PAPER) { c0 = (Color){ 58, 52, 45, 255 }; c1 = (Color){ 16, 14, 12, 255 }; }   /* an ink seal */
    if (THEME == THEME_AMETHYST) { c0 = (Color){ 168, 85, 247, 255 }; c1 = (Color){ 91, 33, 182, 255 }; }
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float qx = fabsf(fx - N / 2.0f) - (N / 2.0f - R), qy = fabsf(fy - N / 2.0f) - (N / 2.0f - R);
        float d = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) + fmaxf(qy, 0) * fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0) - R;
        float a = clampf(0.5f - d, 0, 1);
        if (a <= 0) continue;
        float g = clampf((fx + fy) / (2.0f * N), 0, 1);
        float sheen = clampf(1 - fy / (N * 0.55f), 0, 1) * (THEME == THEME_PAPER ? 0.07f : 0.16f);
        Color c = { (unsigned char)clampf(c0.r + (c1.r - c0.r) * g + 255 * sheen, 0, 255), (unsigned char)clampf(c0.g + (c1.g - c0.g) * g + 255 * sheen, 0, 255),
                    (unsigned char)clampf(c0.b + (c1.b - c0.b) * g + 255 * sheen, 0, 255), (unsigned char)(255 * a) };
        px[y * N + x] = c;
    }
    tile = LoadTextureFromImage(im);
    GenTextureMipmaps(&tile);
    SetTextureFilter(tile, TEXTURE_FILTER_TRILINEAR);
    UnloadImage(im);
    tile_ok = true;
}

/* draws the mark centred on (cx,cy). draw: 0..1 how much of the V is drawn. */
void brand_mark(float cx, float cy, float size, float rot_deg, float draw, float alpha) {
    if (!tile_ok) bake_tile();
    if (size < 1) return;
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlTranslatef(cx, cy, 0); rlRotatef(rot_deg, 0, 0, 1); rlScalef(size, size, 1);
    DrawTexturePro(tile, (Rectangle){ 0, 0, (float)tile.width, (float)tile.height }, (Rectangle){ -0.5f, -0.5f, 1, 1 }, (Vector2){ 0, 0 }, 0, fade(WHITE, alpha));
    Vector2 p0 = { -0.25f, -0.13f }, p1 = { -0.05f, 0.21f }, p2 = { 0.20f, -0.19f };
    float w = 0.105f; Color ink = fade(WHITE, alpha);
    float l1 = Vector2Length(Vector2Subtract(p1, p0)), l2 = Vector2Length(Vector2Subtract(p2, p1)), L = l1 + l2;
    float d = draw * L;
    if (d > 0) {
        Vector2 e = d < l1 ? Vector2Lerp(p0, p1, d / l1) : p1;
        DrawLineEx(p0, e, w, ink);
        DrawCircleSector(p0, w / 2, 0, 360, 24, ink); DrawCircleSector(e, w / 2, 0, 360, 24, ink);
        if (d > l1) {
            Vector2 e2 = Vector2Lerp(p1, p2, fminf(1, (d - l1) / l2));
            DrawLineEx(p1, e2, w, ink); DrawCircleSector(e2, w / 2, 0, 360, 24, ink);
        }
    }
    /* arrow head pops in at the end */
    float ah = eback((draw - 0.85f) / 0.15f);
    if (draw > 0.85f) {
        Vector2 dir = Vector2Normalize(Vector2Subtract(p2, p1)), nrm = { -dir.y, dir.x };
        float s = 0.13f * ah;
        Vector2 tip = Vector2Add(p2, Vector2Scale(dir, s * 0.9f));
        Vector2 b1 = Vector2Add(Vector2Subtract(p2, Vector2Scale(dir, s * 0.35f)), Vector2Scale(nrm, s * 0.75f));
        Vector2 b2 = Vector2Subtract(Vector2Subtract(p2, Vector2Scale(dir, s * 0.35f)), Vector2Scale(nrm, s * 0.75f));
        DrawTriangle(tip, b1, b2, ink); DrawTriangle(tip, b2, b1, ink);
    }
    /* the gold coin: the money that grows */
    float cpop = eback((draw - 0.95f) / 0.05f);
    if (draw > 0.95f) DrawCircleSector((Vector2){ 0.25f, 0.24f }, 0.055f * cpop, 0, 360, 24, fade(THEME == THEME_PAPER ? (Color){ 232, 146, 46, 255 } : (Color){ 255, 200, 70, 255 }, alpha));
    rlDrawRenderBatchActive();
    rlPopMatrix();
}

void brand_theme_changed(void) { if (tile_ok) { UnloadTexture(tile); tile_ok = false; } }

void logo_mark(float x, float y, float s) { brand_mark(x + s / 2, y + s / 2, s, 0, 1, 1); }

/* window/taskbar icon, rendered from the same drawing */
void brand_window_icon(void) {
    RenderTexture2D rt = LoadRenderTexture(256, 256);
    BeginTextureMode(rt); ClearBackground(BLANK); brand_mark(128, 128, 240, 0, 1, 1); EndTextureMode();
    Image im = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&im);
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    SetWindowIcon(im);
    if (getenv("RL_ICON")) ExportImage(im, getenv("RL_ICON"));
    UnloadImage(im); UnloadRenderTexture(rt);
}

/* ---------------------------- opening animation ---------------------------- *
 * 0.0-0.7 the tile spins in and overshoots, 0.5-1.2 the V draws itself,
 * 1.2 a growth ripple, 1.3-2.0 the name slides out, 2.6-3.1 fade to sign-in.
 * Returns true once finished (a click or key skips it). */
bool brand_intro(float t) {
    float W = ui_w(), H = ui_h(), cx = W / 2, cy = H / 2 - 20;
    if (t > 3.1f) return true;
    float out = t < 2.6f ? 1 : 1 - (t - 2.6f) / 0.5f;
    DrawRectangle(0, 0, (int)W + 1, (int)H + 1, fade(C_BG, out));
    ui_modal(true, (Rectangle){ 0, 0, 0, 0 });
    float s = 150 * eback(t / 0.7f);
    float rot = -220 * (1 - eout(t / 0.8f));
    float pulse = t > 1.15f && t < 1.65f ? 1 + 0.07f * sinf((t - 1.15f) / 0.5f * PI) : 1;
    const char *name = "Vardhan";
    float nw = text_w(F.b64, name), gap = 34;
    float shift = eout((t - 1.3f) / 0.6f) * (nw + gap) / 2;          /* logo moves left to make room */
    float lx = cx - shift;
    glow(lx, cy, 260 * out, C_VIOLET, 0.30f * out);
    glow(lx, cy, 200 * out, C_CYAN, 0.12f * out);
    if (t > 1.15f && t < 2.2f) {                                     /* growth ripple */
        float k = (t - 1.15f) / 1.05f;
        DrawRing((Vector2){ lx, cy }, 80 + 200 * k, 82 + 200 * k, 0, 360, 96, fade(C_CYAN, (1 - k) * 0.5f * out));
    }
    brand_mark(lx, cy, s * pulse, rot, clampf((t - 0.5f) / 0.7f, 0, 1), out);
    /* name, letter by letter */
    float x = lx + 75 + gap - 6;
    char ch[2] = { 0, 0 };
    for (int i = 0; name[i]; i++) {
        ch[0] = name[i];
        float e = eout((t - 1.45f - i * 0.06f) / 0.35f);
        text(F.b64, ch, x + (1 - e) * 24, cy - 40, fade(C_TEXT, e * out));
        x += text_w(F.b64, ch);
    }
    float e2 = eout((t - 2.0f) / 0.4f);
    text(F.r18, "Know the downside before you invest", lx + 75 + gap - 4, cy + 34, fade(C_DIM, e2 * out));
    if (t > 0.4f && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetKeyPressed())) return true;
    return false;
}
