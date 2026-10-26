/* clipong.fse - CliPong for FisixOS, compiled with tools/fisix-gcc.
 * Original: CliPong/pc, Rust + crossterm, 60x20, real-time
 * (45 ms ticks, non-blocking key poll, alternate screen, W/S
 * or arrows, first to 5, CPU paddle AI).
 * FisixOS .fse v1 has no crossterm, no alternate screen, no
 * non-blocking poll, and no timer, so a verbatim port is
 * impossible today. This is a Fisix-native terminal port of
 * the same game, with the same board, physics, and rules:
 *   board 60x20, paddles 5 cells tall, first to 5
 *   ball starts centre, vx = +/-1.00, vy = 0.55
 *   paddle bounce: vx *= 1.04, vy = offset*0.25, clamped
 *   CPU paddle moves 1 cell per tick toward the ball
 *   W = up 2, S = down 2, arrows also work if the loader
 *   reports them as scan codes, Q = quit
 * The one honest change: it is turn-based, not real-time.
 * Each keypress is one tick (any key other than W/S/Q just
 * advances the ball), because fse read_key() blocks and
 * there is no poll/timer in the .fse API yet. The board is
 * redrawn as text lines each tick, since there is no cursor
 * positioning in the API either.
 * Usage: run clipong.fse
 */
#include "../boot/fse.h"

#define W 60
#define H 20
#define WIN 5

static int g_bx, g_by, g_vx, g_vy;
static int g_lp, g_rp, g_ps, g_cs;

static void out(fisix_api_t *api, const char *s) { api->print(s); }
static void nl(fisix_api_t *api) { api->print_newline(); }
static int clamp_paddle(int p)
{
    if (p < 3) return 3;
    if (p > 16) return 16;
    return p;
}
static int abs_i(int v) { return v < 0 ? -v : v; }
static void reset_ball(int dir)
{
    g_bx = 3000; g_by = 1000; g_vx = dir * 100; g_vy = 55;
}
static void draw(fisix_api_t *api)
{
    int x, y, ballx, bally;
    ballx = (g_bx + 50) / 100; bally = (g_by + 50) / 100;
    out(api, "CLI PONG    You "); api->print_u64((unsigned long long)g_ps);
    out(api, " : "); api->print_u64((unsigned long long)g_cs);
    out(api, " CPU    First to 5    W/S move, other key = tick, Q quit"); nl(api);
    for (y = 1; y < H; y++) {
        for (x = 0; x < W; x++) {
            char s[2]; char c = ' ';
            if (y == 1 || y == H - 1) c = '-';
            else if (x == W / 2) c = '|';
            if (y > 1 && y < H - 1) {
                if (x == 1 && abs_i(y - g_lp) <= 2) c = '|';
                if (x == W - 2 && abs_i(y - g_rp) <= 2) c = '|';
                if (x == ballx && y == bally) c = 'O';
            }
            s[0] = c; s[1] = 0; api->print(s);
        }
        nl(api);
    }
}
static void update(fisix_api_t *api)
{
    int ballrow;
    (void)api;
    g_bx += g_vx; g_by += g_vy;
    if (g_by <= 100 || g_by >= 1800) { g_vy = -g_vy; if (g_by < 100) g_by = 100; if (g_by > 1800) g_by = 1800; }
    ballrow = (g_by + 50) / 100;
    if (g_vx < 0 && g_bx <= 200 && abs_i(ballrow - g_lp) <= 2) {
        int off;
        g_bx = 200; g_vx = -g_vx * 104 / 100;
        off = (ballrow - g_lp) * 25; if (off > 140) off = 140; if (off < -140) off = -140; g_vy = off;
    } else if (g_vx > 0 && g_bx >= 5700 && abs_i(ballrow - g_rp) <= 2) {
        int off;
        g_bx = 5700; g_vx = -g_vx * 104 / 100;
        off = (ballrow - g_rp) * 25; if (off > 140) off = 140; if (off < -140) off = -140; g_vy = off;
    }
    if (g_bx < 0) { g_cs++; reset_ball(-1); }
    else if (g_bx >= 6000) { g_ps++; reset_ball(1); }
    if (g_rp < ballrow) g_rp++;
    else if (g_rp > ballrow) g_rp--;
    g_rp = clamp_paddle(g_rp);
}

int fse_main(fisix_api_t *api)
{
    g_lp = 10; g_rp = 10; g_ps = 0; g_cs = 0; reset_ball(1);
    out(api, "CliPong for FisixOS - turn-based Pong (see source header)"); nl(api);
    while (g_ps < WIN && g_cs < WIN) {
        int k;
        draw(api);
        out(api, "move> ");
        k = api->read_key();
        if (k == 'q' || k == 'Q' || k == 27) break;
        if (k == 'w' || k == 'W' || k == 0x10001) g_lp = clamp_paddle(g_lp - 2);
        else if (k == 's' || k == 'S' || k == 0x10002) g_lp = clamp_paddle(g_lp + 2);
        update(api);
    }
    draw(api);
    if (g_ps >= WIN) out(api, "You win!");
    else if (g_cs >= WIN) out(api, "CPU wins!");
    else out(api, "Quit.");
    nl(api);
    out(api, "Press any key to exit."); nl(api);
    api->read_key();
    return 0;
}
