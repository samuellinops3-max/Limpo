// Cobra Arena v1.2 - Nativo para PS3 (PSL1GHT) com Mini-mapa e Skins
// Controles: analogico esquerdo (ou direcional) = direcao, X / R1 / R2 = turbo
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

#define CB_SIZE   (0x100000)
#define HOST_SIZE (32 * 1024 * 1024)

#define WORLD  3600.0f
#define MAXSEG 500
#define NSN    12
#define NPEL   600
#define SP     7.0f
#define PI     3.14159265f

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, respawn, boost, grow, score, tick, avoid_t;
    float ang, tgt, avoid_dir;
    u32 col;
    int skin_type; // 0: Normal, 1: EUA, 2: Brasil
} Snake;

typedef struct {
    float x, y;
    int size, active;
    u32 col;
} Pellet;

static const u32 palette[10] = {
    0x00FF4D4D, 0x0038B6FF, 0x005CE65C, 0x00FFD23F, 0x00C77DFF,
    0x00FF758F, 0x0000F5D4, 0x00FF9F1C, 0x00A9DEF9, 0x00FF5D8F
};

/* ---------- video ---------- */
static gcmContextData *ctx;
static u32 W, H;
static u32 *fb[2];
static u32 fboff[2];
static u32 *back;
static int cur = 0, first_flip = 1;
static volatile int running = 1;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
    (void)param; (void)usrdata;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void init_screen(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);

    videoState st;
    videoGetState(VIDEO_PRIMARY, 0, &st);
    u32 resid = st.displayMode.resolution;
    if (videoGetResolutionAvailability(VIDEO_PRIMARY, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0))
        resid = VIDEO_RESOLUTION_720;

    videoResolution res;
    videoGetResolution(resid, &res);
    W = res.width;
    H = res.height;

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = resid;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.aspect = VIDEO_ASPECT_AUTO;
    vc.pitch = W * 4;
    videoConfigure(VIDEO_PRIMARY, &vc, NULL, 0);
    videoGetState(VIDEO_PRIMARY, 0, &st);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, W * H * 4);
        gcmAddressToOffset(fb[i], &fboff[i]);
        gcmSetDisplayBuffer(i, fboff[i], W * 4, W, H);
    }
    back = (u32 *)memalign(64, W * H * 4);
    memset(back, 0, W * H * 4);
    gcmResetFlipStatus();
}

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void present(void)
{
    if (!first_flip) wait_flip();
    else gcmResetFlipStatus();
    memcpy(fb[cur], back, W * H * 4);
    gcmSetFlip(ctx, cur);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    cur ^= 1;
    first_flip = 0;
}

/* ---------- desenho ---------- */
static void fill_rect(int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int)W) x1 = W;
    if (y1 > (int)H) y1 = H;
    for (int yy = y0; yy < y1; yy++) {
        u32 *p = back + yy * W;
        for (int xx = x0; xx < x1; xx++) p[xx] = c;
    }
}

static void circle(int cx, int cy, int r, u32 c)
{
    if (r < 1) r = 1;
    if (cx + r < 0 || cx - r >= (int)W || cy + r < 0 || cy - r >= (int)H) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= (int)H) continue;
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - hw, x1 = cx + hw;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)W) x1 = W - 1;
        u32 *p = back + y * W;
        for (int x = x0; x <= x1; x++) p[x] = c;
    }
}

/* fonte 3x5 minima */
typedef struct { char c; const char *p; } Glyph;
static const Glyph glyphs[] = {
    {'0', "###" "#.#" "#.#" "#.#" "###"}, {'1', ".#." "##." ".#." ".#." "###"},
    {'2', "###" "..#" "###" "#.." "###"}, {'3', "###" "..#" "###" "..#" "###"},
    {'4', "#.#" "#.#" "###" "..#" "..#"}, {'5', "###" "#.." "###" "..#" "###"},
    {'6', "###" "#.." "###" "#.#" "###"}, {'7', "###" "..#" "..#" "..#" "..#"},
    {'8', "###" "#.#" "###" "#.#" "###"}, {'9', "###" "#.#" "###" "..#" "###"},
    {'A', ".#." "#.#" "###" "#.#" "#.#"}, {'B', "##." "#.#" "##." "#.#" "##."},
    {'C', "###" "#.." "#.." "#.." "###"}, {'D', "##." "#.#" "#.#" "#.#" "##."},
    {'E', "###" "#.." "###" "#.." "###"}, {'F', "###" "#.." "###" "#.." "#.."},
    {'G', "###" "#.." "#.#" "#.#" "###"}, {'H', "#.#" "#.#" "###" "#.#" "#.#"},
    {'I', "###" ".#." ".#." ".#." "###"}, {'J', "..#" "..#" "..#" "#.#" "###"},
    {'K', "#.#" "#.#" "##." "#.#" "#.#"}, {'L', "#.." "#.." "#.." "#.." "###"},
    {'M', "#.#" "###" "###" "#.#" "#.#"}, {'N', "#.#" "###" "###" "###" "#.#"},
    {'O', "###" "#.#" "#.#" "#.#" "###"}, {'P', "###" "#.#" "###" "#.." "#.."},
    {'R', "##." "#.#" "##." "#.#" "#.#"}, {'S', "###" "#.." "###" "..#" "###"},
    {'T', "###" ".#." ".#." ".#." ".#."}, {'U', "#.#" "#.#" "#.#" "#.#" "###"},
    {'V', "#.#" "#.#" "#.#" "#.#" ".#."}, {'W', "#.#" "#.#" "###" "###" "#.#"},
    {'X', "#.#" "#.#" ".#." "#.#" "#.#"}, {'Y', "#.#" "#.#" ".#." ".#." ".#."},
    {'Z', "###" "..#" ".#." "#.." "###"}, {':', "..." ".#." "..." ".#." "..."},
    {'-', "..." "..." "###" "..." "..."}, {'#', "#.#" "###" "#.#" "###" "#.#"}
};

static const char *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (glyphs[i].c == c) return glyphs[i].p;
    return NULL;
}

static int text_width(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }

static void draw_text(const char *s, int x, int y, int sc, u32 c)
{
    for (; *s; s++) {
        const char *g = find_glyph(*s);
        if (g)
            for (int r = 0; r < 5; r++)
                for (int k = 0; k < 3; k++)
                    if (g[r * 3 + k] == '#') fill_rect(x + k * sc, y + r * sc, sc, sc, c);
        x += 4 * sc;
    }
}

/* ---------- jogo ---------- */
static Snake sn[NSN];
static Pellet pel[NPEL];
static int pelrot = 0, dead_t = 0, x_prev = 0, start_prev = 0;
static int paused = 0, best_score = 0;
static float camx = WORLD / 2, camy = WORLD / 2, cam_zoom = 1.0f;
static float pad_ax = 0, pad_ay = 0, pad_mag = 0;
static int pad_boost = 0, pad_x = 0, pad_start = 0;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static float norm_angle(float a)
{
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}

static float radius_of(const Snake *s)
{
    int l = s->len > 300 ? 300 : s->len;
    return 10.0f + l * 0.036f;
}

static u32 get_snake_color(const Snake *s, int j)
{
    if (s->skin_type == 1) {
        int mod = j % 3;
        if (mod == 0) return 0x00FF4D4D;
        if (mod == 1) return 0x00FFFFFF;
        return 0x0038B6FF;
    } else if (s->skin_type == 2) {
        int mod = j % 3;
        if (mod == 0) return 0x00009933;
        if (mod == 1) return 0x00FFCC00;
        return 0x000066CC;
    }
    return (j % 6 < 3) ? s->col : ((s->col >> 1) & 0x007F7F7F);
}

static void add_pellet(float x, float y, int size)
{
    if (x < 35.0f) x = 35.0f;
    if (x > WORLD - 35.0f) x = WORLD - 35.0f;
    if (y < 35.0f) y = 35.0f;
    if (y > WORLD - 35.0f) y = WORLD - 35.0f;

    for (int k = 0; k < NPEL; k++) {
        int i = (pelrot + k) % NPEL;
        if (!pel[i].active) {
            pel[i].x = x; pel[i].y = y;
            pel[i].size = size; pel[i].active = 1;
            pel[i].col = palette[rand() % 10];
            pelrot = (i + 1) % NPEL;
            return;
        }
    }
}

static void spawn_snake(int i)
{
    Snake *s = &sn[i];
    float x = 550.0f + frand() * (WORLD - 1100.0f);
    float y = 550.0f + frand() * (WORLD - 1100.0f);
    s->ang = frand() * 2.0f * PI;
    s->tgt = s->ang;
    s->len = (i == 0) ? 14 : (12 + rand() % 18);
    for (int j = 0; j < s->len; j++) {
        s->x[j] = x - cosf(s->ang) * SP * j;
        s->y[j] = y - sinf(s->ang) * SP * j;
    }
    s->alive = 1; s->respawn = 0; s->boost = 0;
    s->grow = 0; s->tick = 0; s->avoid_t = 0; s->avoid_dir = 1.0f;
    s->col = palette[i % 10];
    s->skin_type = rand() % 3;
}

static void kill_snake(int i)
{
    Snake *s = &sn[i];
    for (int j = 0; j < s->len; j += 2) {
        add_pellet(s->x[j] + (frand() - 0.5f) * 14.0f,
                   s->y[j] + (frand() - 0.5f) * 14.0f, 2);
    }
    s->alive = 0;
    s->respawn = 140;
    if (i == 0) {
        if (s->score > best_score) best_score = s->score;
        dead_t = 0;
    }
}

static void bot_think(int i)
{
    Snake *s = &sn[i];
    float hx = s->x[0], hy = s->y[0];

    float margin = 360.0f;
    if (hx < margin || hx > WORLD - margin || hy < margin || hy > WORLD - margin) {
        s->tgt = atan2f((WORLD * 0.5f) - hy, (WORLD * 0.5f) - hx);
        s->avoid_t = 25;
        return;
    }

    if (s->avoid_t > 0) {
        s->avoid_t--;
        return;
    }

    float rs = radius_of(s);
    float look = 95.0f + rs;
    float px = hx + cosf(s->ang) * look;
    float py = hy + sinf(s->ang) * look;

    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = rs + radius_of(&sn[o]) + 35.0f;
        float lim2 = lim * lim;
        for (int j = 0; j < sn[o].len; j += 2) {
            float dx = sn[o].x[j] - px, dy = sn[o].y[j] - py;
            if (dx * dx + dy * dy < lim2) {
                float to_center = norm_angle(atan2f((WORLD * 0.5f) - hy, (WORLD * 0.5f) - hx) - s->ang);
                s->avoid_dir = (to_center >= 0.0f) ? 1.0f : -1.0f;
                s->tgt = norm_angle(s->ang + s->avoid_dir * 1.15f);
                s->avoid_t = 22;
                return;
            }
        }
    }

    if (s->tick % 8 == 0) {
        float best = 460.0f * 460.0f;
        int bi = -1;
        for (int k = 0; k < NPEL; k++) {
            if (!pel[k].active) continue;
            float dx = pel[k].x - hx, dy = pel[k].y - hy;
            float d = dx * dx + dy * dy;
            if (pel[k].size > 1) d *= 0.6f;
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0) s->tgt = atan2f(pel[bi].y - hy, pel[bi].x - hx);
        else s->tgt = norm_angle(s->tgt + (frand() - 0.5f) * 0.35f);
    }
}

static void update_snake(int i)
{
    Snake *s = &sn[i];
    if (!s->alive) {
        if (i > 0 && --s->respawn <= 0) spawn_snake(i);
        return;
    }
    s->tick++;

    if (i == 0) {
        if (pad_mag > 0.18f) s->tgt = atan2f(pad_ay, pad_ax);
        s->boost = pad_boost;
    } else {
        bot_think(i);
        s->boost = (s->len > 35 && s->avoid_t == 0 && (s->tick / 110) % 6 == 0) ? 1 : 0;
    }

    float turn_speed = (i == 0) ? 0.095f : 0.080f;
    float d = norm_angle(s->tgt - s->ang);
    if (d > turn_speed) d = turn_speed;
    if (d < -turn_speed) d = -turn_speed;
    s->ang = norm_angle(s->ang + d);

    float sp = 3.2f;
    if (s->boost && s->len > 12) {
        sp = 6.2f;
        if (s->tick % 10 == 0) {
            s->len--;
            add_pellet(s->x[s->len], s->y[s->len], 1);
        }
    }

    s->x[0] += cosf(s->ang) * sp;
    s->y[0] += sinf(s->ang) * sp;

    for (int j = 1; j < s->len; j++) {
        float dx = s->x[j - 1] - s->x[j], dy = s->y[j - 1] - s->y[j];
        float dd = sqrtf(dx * dx + dy * dy);
        if (dd > SP) {
            float f = (dd - SP) / dd;
            s->x[j] += dx * f;
            s->y[j] += dy * f;
        }
    }

    if (s->x[0] < 12.0f || s->x[0] > WORLD - 12.0f ||
        s->y[0] < 12.0f || s->y[0] > WORLD - 12.0f) {
        kill_snake(i);
        return;
    }

    float rs = radius_of(s), er = rs + 14.0f;
    float er2 = er * er;
    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        float dx = pel[k].x - s->x[0], dy = pel[k].y - s->y[0];
        if (dx * dx + dy * dy < er2) {
            pel[k].active = 0;
            s->grow += pel[k].size;
            s->score += pel[k].size * 10;
            if (i == 0 && s->score > best_score) best_score = s->score;
        }
    }

    while (s->grow >= 2 && s->len < MAXSEG) {
        s->x[s->len] = s->x[s->len - 1];
        s->y[s->len] = s->y[s->len - 1];
        s->len++;
        s->grow -= 2;
    }
    if (s->len >= MAXSEG) s->grow = 0;

    for (int o = 0; o < NSN; o++) {
        if (o == i || !sn[o].alive) continue;
        float lim = (rs + radius_of(&sn[o])) * 0.72f;
        float lim2 = lim * lim;
        for (int j = 0; j < sn[o].len; j++) {
            float dx = sn[o].x[j] - s->x[0], dy = sn[o].y[j] - s->y[0];
            if (dx * dx + dy * dy < lim2) {
                kill_snake(i);
                return;
            }
        }
    }
}

static void init_game(void)
{
    memset(pel, 0, sizeof(pel));
    for (int k = 0; k < NPEL; k++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));
    for (int i = 0; i < NSN; i++) {
        sn[i].score = 0;
        spawn_snake(i);
    }
    dead_t = 0;
    paused = 0;
}

static void read_pad(void)
{
    padInfo info;
    padData d;
    pad_ax = pad_ay = pad_mag = 0.0f;
    pad_boost = pad_x = pad_start = 0;

    ioPadGetInfo(&info);
    for (int p = 0; p < 7; p++) {
        if (info.status[p]) {
            memset(&d, 0, sizeof(d));
            if (ioPadGetData(p, &d) == 0) {
                float ax = ((int)d.ANA_L_H - 128) / 128.0f;
                float ay = ((int)d.ANA_L_V - 128) / 128.0f;
                if (fabsf(ax) < 0.15f) ax = 0.0f;
                if (fabsf(ay) < 0.15f) ay = 0.0f;

                float dx = 0.0f, dy = 0.0f;
                if (d.BTN_LEFT)  dx -= 1.0f;
                if (d.BTN_RIGHT) dx += 1.0f;
                if (d.BTN_UP)    dy -= 1.0f;
                if (d.BTN_DOWN)  dy += 1.0f;
                if (dx != 0.0f || dy != 0.0f) { ax = dx; ay = dy; }

                pad_ax = ax; pad_ay = ay;
                pad_mag = sqrtf(ax * ax + ay * ay);
                if (pad_mag > 1.0f) pad_mag = 1.0f;

                pad_boost = (d.BTN_CROSS || d.BTN_R1 || d.BTN_R2) ? 1 : 0;
                pad_x = d.BTN_CROSS ? 1 : 0;
                pad_start = d.BTN_START ? 1 : 0;
                break;
            }
        }
    }
}

static void update_game(void)
{
    if (pad_start && !start_prev && sn[0].alive) paused = !paused;
    start_prev = pad_start;
    if (paused) { x_prev = pad_x; return; }

    for (int i = 0; i < NSN; i++) update_snake(i);

    int count = 0;
    for (int k = 0; k < NPEL; k++) count += pel[k].active;
    for (int n = 0; n < 3 && count < NPEL - 40; n++, count++)
        add_pellet(frand() * WORLD, frand() * WORLD, 1 + (rand() % 5 == 0));

    if (sn[0].alive) {
        camx += (sn[0].x[0] - camx) * 0.25f;
        camy += (sn[0].y[0] - camy) * 0.25f;
        float target_zoom = 1.05f - (sn[0].len / (float)MAXSEG) * 0.32f;
        cam_zoom += (target_zoom - cam_zoom) * 0.05f;
    } else {
        dead_t++;
        if (dead_t > 45 && pad_x && !x_prev) {
            spawn_snake(0);
            sn[0].score = 0;
        }
    }
    x_prev = pad_x;
}

static void render(void)
{
    float sc = ((float)W / 1280.0f) * cam_zoom;
    float hw = W / 2.0f, hh = H / 2.0f;
    u32 n = W * H;
    for (u32 i = 0; i < n; i++) back[i] = 0x000B111E;

    int sx0 = (int)((0.0f - camx) * sc + hw), sx1 = (int)((WORLD - camx) * sc + hw);
    int sy0 = (int)((0.0f - camy) * sc + hh), sy1 = (int)((WORLD - camy) * sc + hh);
    u32 wall_bg = 0x002B090E, border_line = 0x00FF3B3B;

    if (sx0 > 0) fill_rect(0, 0, sx0, H, wall_bg);
    if (sx1 < (int)W) fill_rect(sx1, 0, W - sx1, H, wall_bg);
    if (sy0 > 0) fill_rect(0, 0, W, sy0, wall_bg);
    if (sy1 < (int)H) fill_rect(0, sy1, W, H - sy1, wall_bg);

    fill_rect(sx0 - 4, sy0, 4, sy1 - sy0, border_line);
    fill_rect(sx1, sy0, 4, sy1 - sy0, border_line);
    fill_rect(sx0, sy0 - 4, sx1 - sx0, 4, border_line);
    fill_rect(sx0, sy1, sx1 - sx0, 4, border_line);

    float step = 120.0f;
    float gx0 = floorf((camx - hw / sc) / step) * step;
    float gy0 = floorf((camy - hh / sc) / step) * step;
    for (float gx = gx0; gx < camx + hw / sc; gx += step) {
        if (gx <= 0 || gx >= WORLD) continue;
        int sx = (int)((gx - camx) * sc + hw);
        for (float gy = gy0; gy < camy + hh / sc; gy += step) {
            if (gy <= 0 || gy >= WORLD) continue;
            int sy = (int)((gy - camy) * sc + hh);
            fill_rect(sx - 2, sy - 2, 5, 5, 0x001B2940);
        }
    }

    for (int k = 0; k < NPEL; k++) {
        if (!pel[k].active) continue;
        int px = (int)((pel[k].x - camx) * sc + hw);
        int py = (int)((pel[k].y - camy) * sc + hh);
        int pr = (int)((5 + pel[k].size * 2.5f) * sc);
        circle(px, py, pr, pel[k].col);
        if (pr >= 4) circle(px, py, pr / 2, 0x00FFFFFF);
    }

    for (int i = NSN - 1; i >= 0; i--) {
        Snake *s = &sn[i];
        if (!s->alive) continue;
        int r = (int)(radius_of(s) * sc);

        for (int j = s->len - 1; j >= 0; j--) {
            int px = (int)((s->x[j] - camx) * sc + hw);
            int py = (int)((s->y[j] - camy) * sc + hh);
            if (s->boost && j % 3 == 0) circle(px, py, r + 3, 0x00FFFFFF);
            circle(px, py, r, get_snake_color(s, j));
        }

        int hx = (int)((s->x[0] - camx) * sc + hw);
        int hy = (int)((s->y[0] - camy) * sc + hh);
        for (int e = -1; e <= 1; e += 2) {
            int ex = hx + (int)(cosf(s->ang + e * 0.65f) * r * 0.58f);
            int ey = hy + (int)(sinf(s->ang + e * 0.65f) * r * 0.58f);
            circle(ex, ey, r * 36 / 100 + 2, 0x00FFFFFF);
            circle(ex + (int)(cosf(s->ang) * r * 0.16f),
                   ey + (int)(sinf(s->ang) * r * 0.16f),
                   r * 19 / 100 + 1, 0x00000000);
        }
    }

    // Mini-mapa (radar)
    int mm_size = 130;
    int mm_x = W - mm_size - 30;
    int mm_y = 30;
    fill_rect(mm_x - 3, mm_y - 3, mm_size + 6, mm_size + 6, 0x00FFFFFF);
    fill_rect(mm_x, mm_y, mm_size, mm_size, 0x000B111E);

    for (int i = 0; i < NSN; i++) {
        if (!sn[i].alive) continue;
        int mx = mm_x + (int)((sn[i].x[0] / WORLD) * mm_size);
        int my = mm_y + (int)((sn[i].y[0] / WORLD) * mm_size);
        u32 mcol = (i == 0) ? 0x0000FF00 : 0x00FF4D4D;
        fill_rect(mx - 1, my - 1, 3, 3, mcol);
    }

    char buf[32];
    int ui = (int)(4.0f * ((float)W / 1280.0f) + 0.5f);
    if (ui < 3) ui = 3;

    draw_text("PONTOS", 24, 20, ui, 0x00A8C5E5);
    sprintf(buf, "%d", sn[0].score);
    draw_text(buf, 24, 20 + 7 * ui, ui + 2, 0x00FFD23F);

    sprintf(buf, "RECORDE: %d", best_score);
    draw_text(buf, 24, 20 + 16 * ui, ui - 1 > 2 ? ui - 1 : 2, 0x007CE38B);

    if (!sn[0].alive) {
        draw_text("GAME OVER", W / 2 - text_width("GAME OVER", ui + 2) / 2, H / 2 - 20, ui + 2, 0x00FF4D4D);
        draw_text("APERTE X PARA RENASCER", W / 2 - text_width("APERTE X PARA RENASCER", ui - 1) / 2, H / 2 + 25, ui - 1, 0x00FFFFFF);
    }

    if (paused) {
        draw_text("JOGO PAUSADO", W / 2 - text_width("JOGO PAUSADO", ui + 2) / 2, H / 2 - 10, ui + 2, 0x0038B6FF);
    }

    present();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
    init_screen();
    ioPadInit(7);
    srand(20261111);
    init_game();

    while (running) {
        sysUtilCheckCallback();
        read_pad();
        update_game();
        render();
    }

    ioPadEnd();
    return 0;
}
