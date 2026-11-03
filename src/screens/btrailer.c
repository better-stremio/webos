#include "screens/btrailer.h"
#include "screens/player.h"
#include "screens/saver.h"
#include "screens/board.h"
#include "core/app.h"
#include "screens/cwmenu.h"
#include <stdlib.h>
#include "core/store.h"
#include "core/bgtrailer.h"
#include "core/trailer.h"
#include "core/video.h"
#include "core/pace.h"
#include "ui/gfx.h"
#include <stdio.h>

/* own = the decoder holds the trailer (the player takes it over the moment it opens) */
static struct { int own, under, live, backing, held, resumes, byKey, quiet, endFade, unloadLater; double holdAt, pauseAt; unsigned holdGen; double backAt, resumeAt, coverAt; Tween cover, zoom, melt; } B; /* held: we paused it ourselves */

/* the page frames the video 2.5% larger around (960, 400) inside 1920x799, cover-cropped: the picture zooms
   to exactly that framing as it fades off */
/* the trailer in the expanded card (as Nuvio's expanded_card): the plane goes behind the card's rectangle, cover-cropped;
   the board makes the window see-through there. The page itself never gives way */
static struct { float x, y, w, h, px, py, pw, ph; double at; int on; Tween a; } CD;
void btr_card_rect(float x, float y, float w, float h) { CD.x = x; CD.y = y; CD.w = w; CD.h = h; CD.at = app_now(); }
float btr_card_alpha(void) { return CD.a.v; }
static int card_place(void) {
  int vw = video_width(), vh = video_height(); if (!vw || !vh || CD.w <= 0 || app_now() - CD.at > 0.25) return 0;
  float k = CD.w / vw > CD.h / vh ? CD.w / vw : CD.h / vh; int sw = (int)(CD.w / k), sh = (int)(CD.h / k), sx = (vw - sw) / 2, sy = (vh - sh) / 2;
  int crop = video_set_crop(sx, sy, sw, sh, CD.x, CD.y, CD.w, CD.h); if (!crop) video_set_window(CD.x, CD.y, CD.w, CD.h);
  CD.px = CD.x; CD.py = CD.y; CD.pw = CD.w; CD.ph = CD.h; fprintf(stderr, "[btr] card: %dx%d, %s -> %.0f,%.0f %.0fx%.0f\n", vw, vh, crop ? "crop" : "window", CD.x, CD.y, CD.w, CD.h); return 1;
}
static void place(void) {
  int vw = video_width(), vh = video_height(); if (!vw || !vh) { fprintf(stderr, "[btr] place: no picture size yet, window 0,0 1920x799\n"); video_set_window(0, 0, SB_W, 799); return; }
  /* a TV whose plane takes the whole screen only (webOS 3/4): the picture fills it and the box shows its top part */
  if (!video_partial_plane()) { fprintf(stderr, "[btr] place: %dx%d on the whole screen (this TV places the plane full-screen only)\n", vw, vh); video_set_window(0, 0, SB_W, SB_H); return; }
  float bx = -24, by = -10, bw = 1968, bh = 819, k = bw / vw > bh / vh ? bw / vw : bh / vh;
  float Vw = vw * k, Vh = vh * k, Vx = bx + (bw - Vw) / 2, Vy = by + (bh - Vh) / 2;
  float x0 = Vx > 0 ? Vx : 0, y0 = Vy > 0 ? Vy : 0, x1 = Vx + Vw < SB_W ? Vx + Vw : SB_W, y1 = Vy + Vh < by + bh ? Vy + Vh : by + bh;
  if (x1 <= x0 || y1 <= y0) return;
  int sx = (int)((x0 - Vx) / k), sy = (int)((y0 - Vy) / k), sw = (int)((x1 - x0) / k), sh = (int)((y1 - y0) / k);
  int crop = video_set_crop(sx, sy, sw, sh, x0, y0, x1 - x0, y1 - y0);
  fprintf(stderr, "[btr] place: %dx%d, %s %d,%d %dx%d -> %.0f,%.0f %.0fx%.0f\n", vw, vh, crop ? "crop" : "no crop on this TV: window", sx, sy, sw, sh, x0, y0, x1 - x0, y1 - y0);
  if (!crop) video_set_window(x0, y0, x1 - x0, y1 - y0);
}
static void h_load(const char *url, const char *mime, const char *titleId, void *ud) {
  (void)mime; (void)titleId; (void)ud; if (player_open()) return;
  fprintf(stderr, "[trailer] background %s\n", url);
  B.unloadLater = 0; video_load(url); B.own = 1; B.live = 0; B.backing = 0; B.held = 0; B.resumes = 0;
  B.quiet = 1; /* silent until it shows (set once the media is loaded) */
}
static void h_hide(int now, void *ud) {
  /* the sound fades out (0.45 s) as the picture gives way, and the video pauses once it is silent; at once only when asked */
  (void)ud; CD.on = 0; if (B.byKey || now) tween_set(&CD.a, 0); else tween_to(&CD.a, 0, 0.4f, EASE_CSS_EASE); /* a key: shut at once. Ended by itself: it closes softly over the last frame */
  if (B.own && !player_open()) { if (now) { video_set_volume(0, 0); video_pause(); B.held = 1; } else { video_set_volume(0, 450); B.pauseAt = B.byKey ? 0 : app_now() + 0.47; } } /* a move: it plays on, silent, until the next title's picture covers it (a frozen frame read as a hang) */
  tween_to(&B.melt, 0, 0.9f, EASE_CSS_EASE);
  if (now || !B.under) { B.under = 0; B.backing = 0; B.coverAt = 0; B.holdGen = 0; B.byKey = 0; tween_set(&B.cover, 1); tween_to(&B.zoom, 1, 0.7f, EASE_STD); return; }
  /* over its last frame, at the same framing. Stopped by a key (a move to another title): its own backdrop never
     comes back; the frame holds until the next title's backdrop takes over, and the cover brings that one up (1.2 s at
     most, then whatever is there). Ended by itself: its own backdrop comes back at once */
  B.backing = 1;
  if (B.byKey) { B.holdGen = app_bg_gen(); B.coverAt = app_now() + 5.0; B.backAt = B.coverAt + 0.46; B.holdAt = app_now(); }
  else { B.holdGen = 0; B.coverAt = app_now(); B.backAt = B.coverAt + 0.46; }
  B.byKey = 0;
}
static void unload_now(void) { B.unloadLater = 0; if (B.own && !player_open()) video_unload(); B.own = 0; B.live = 0; }
/* the decoder goes only once the picture covers the video again: let go earlier, its plane went black under the page
   for a moment (the frame held for the next title, a key pressed meanwhile) before the backdrop came back */
static void h_unload(void *ud) { (void)ud; if (B.under && B.cover.v < 0.999f) { B.unloadLater = 1; return; } unload_now(); }
void btr_init(void) { static BgtHooks h = { h_load, h_hide, h_unload, NULL }; bgt_init(&h); tween_set(&CD.a, 0); tween_set(&B.cover, 1); tween_set(&B.zoom, 1); tween_set(&B.melt, 0); }
void btr_key(int arrow) { (void)arrow; if (CD.on) B.byKey = 1; bgt_key(); B.byKey = 0; } /* only a move to another title holds the frame for the next backdrop */
int btr_playing(void) { return B.under && !B.backing && !B.byKey; } /* (a key just pressed counts as leaving: the same frame the focus moves) */
unsigned btr_hold_gen(void) { return B.under && B.backing ? B.holdGen : 0; }
/* play pressed with a trailer on: it plays on under the player's art as that fades in, and is let go once covered */
static int handing; static double handAt;
void btr_handover(void) { handing = 1; handAt = app_now(); tween_to(&B.melt, 0, 0.3f, EASE_CSS_EASE); if (B.own) video_set_volume(0, 250); } /* Play pressed: its sound goes at once, as the loader comes (its picture stays until covered) */
int btr_handing(void) { return handing; }
void btr_release(void) { handing = 0; bgt_release_now(); B.under = 0; B.backing = 0; tween_set(&B.cover, 1); tween_set(&B.zoom, 1); tween_set(&B.melt, 0); }
int btr_under(void) { return B.under; }
float btr_cover(void) { return B.cover.v; }
float btr_zoom(void) { return B.zoom.v; }
float btr_melt(void) { return B.melt.v; }
void btr_tick(float dt) {
  trailer_tick();
  if (player_open() && (B.own || bgt_state() != BGT_OFF)) { B.own = 0; btr_release(); } /* the player has the decoder now */
  const Item *f = NULL;
  int cardScr = SCR == SCR_HOME || SCR == SCR_DISC || SCR == SCR_COL; /* where a card opens */
  if (cardScr && !topNav.active && screens[SCR] && screens[SCR]->focused) f = screens[SCR]->focused();
  BgtView v = { app_trailer_idle(), cardScr, f, topNav.active || app_overlay != NULL || /* (a sheet or the long-press menu: it gives way, softly, and none starts under it) */ player_open() || !app_splash_done || saver_on(),
                board_spot_up(), pace_period() > 21.5 };
  bgt_tick(&v);
  if (handing && (player_art_full() || !player_open() || app_now() - handAt > 1.5)) { handing = 0; bgt_release_now(); B.under = 0; B.backing = 0; tween_set(&B.cover, 1); tween_set(&B.zoom, 1); tween_set(&B.melt, 0); } /* covered: now it goes */
  if (B.own && !player_open()) {
    video_pump();
    VidState vs = video_state();
    if (B.quiet && video_has_media()) { B.quiet = 0; if (!B.under) video_set_volume(0, 0); } /* (as soon as the media exists: silent until it shows) */
    /* the backdrop gives way only once the trailer has been playing steadily for a moment (its plane placed early):
       the TV says "playing" a few frames before the picture is there, which showed as black between the two */
    static double steadyAt; double now = app_now();
    if (!B.live && video_live() && vs == VID_PLAYING) { if (!steadyAt) { steadyAt = now; card_place(); } } else if (!B.live) steadyAt = 0;
    if (!B.live && steadyAt && now - steadyAt > 0.5) { steadyAt = 0;
      if (!video_partial_plane()) { fprintf(stderr, "[btr] this TV places the plane full-screen only: no trailer in a card\n"); B.live = 1; bgt_ended(); }
      else if (card_place()) { B.live = 1; /* (the card not open yet: asked again next frame, the video runs on behind the page) */
        if (bgt_state() == BGT_LOAD) { bgt_playing(); CD.on = 1; fprintf(stderr, "[btr] showing %dx%d\n", video_width(), video_height());
          B.endFade = 0; B.pauseAt = 0; video_set_volume(atoi(store_get("bgTrailerVol", "70")), 1800); /* the sound comes up softly to the chosen level */
          tween_set(&CD.a, 0); tween_to(&CD.a, 1, 0.35f, EASE_CSS_EASE); } } else steadyAt = now; }
    if (CD.on && app_now() - CD.at < 0.25 && (CD.x != CD.px || CD.y != CD.py || CD.w != CD.pw || CD.h != CD.ph)) card_place(); /* the card moved under it */
    if ((vs == VID_ENDED || vs == VID_ERROR) && (bgt_state() == BGT_LOAD || bgt_state() == BGT_SHOW)) bgt_ended();
    /* paused by something other than us (the pipeline settling, a focus change of the TV's own): it plays on, a few
       times at most, a second apart, so a pipeline that insists is not fought every frame */
    if (vs == VID_PAUSED && !B.held && (bgt_state() == BGT_LOAD || bgt_state() == BGT_SHOW) && B.resumes < 3 && app_now() >= B.resumeAt) {
      B.resumes++; B.resumeAt = app_now() + 1.0; fprintf(stderr, "[btr] paused without us: play again (%d)\n", B.resumes); video_play(); }
  }
  /* covered again: the video goes from under the picture, which eases back to its own framing */
  if (B.backing && app_now() >= B.backAt) { B.backing = 0; B.under = 0; tween_to(&B.zoom, 1, 0.6f, EASE_STD); }
  if (B.pauseAt && app_now() >= B.pauseAt) { B.pauseAt = 0; if (B.own && !player_open()) { video_pause(); B.held = 1; } } /* silent now: it stops */
  if (CD.on && !B.endFade) { double d = video_duration(), p = video_time(); if (d > 3 && p > d - 1.6) { B.endFade = 1; video_set_volume(0, 1300); } }
  if (B.under && !B.backing && !B.endFade) { double d = video_duration(), p = video_time(); if (d > 3 && p > d - 1.6) { B.endFade = 1; video_set_volume(0, 1300); } } /* its last moments: the sound fades out before it ends */
  if (B.holdGen && B.coverAt && app_bg_gen() != B.holdGen) B.coverAt = app_now(); /* the next title's backdrop is in: up it comes */
  if (B.holdGen && B.coverAt && (topNav.active || !app_is_board(SCR))) B.coverAt = app_now(); /* up into the tabs (or off the page): no next title is coming, its own backdrop comes back now */
  if (B.holdGen && B.coverAt && app_bg_gen() == B.holdGen && !app_bg_pending() && app_now() - B.holdAt > 0.6 && app_idle() > 0.6) B.coverAt = app_now(); /* no other title's picture on its way (a move along the edge): its own comes back; else it waits for that one (5 s at most), never showing its own between */
  if (B.coverAt && app_now() >= B.coverAt) { B.coverAt = 0; if (B.backing) tween_to(&B.cover, 1, 0.45f, EASE_CSS_EASEOUT); B.backAt = app_now() + 0.46; }
  tween_tick(&CD.a, dt); tween_tick(&B.cover, dt); tween_tick(&B.zoom, dt); tween_tick(&B.melt, dt);
  if (B.unloadLater && (!B.under || B.cover.v >= 0.999f)) unload_now(); /* covered now */
}
