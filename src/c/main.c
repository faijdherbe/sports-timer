#include <pebble.h>

// Default quarter length (17m30s); the watch settings can change it.
#define QUARTER_MS (17 * 60 * 1000 + 30 * 1000)
#define STATE_KEY 1
// Photo: 144x168 1-bit, sent and saved in chunks of 8 rows (144 bytes, under the 256-byte persist limit).
#define PHOTO_KEY 10
#define PHOTO_W 144
#define PHOTO_H 168
#define ROW_BYTES (PHOTO_W / 8)
#define CHUNK_ROWS 8

typedef struct {
  int64_t elapsed;  // ms counted before `started`
  int64_t started;  // wall clock ms when last resumed
  bool running;
  bool acked;       // zero-alarm confirmed
  int home, away;
  int32_t quarter_ms;  // new fields go last: older saved state reads them as 0
  bool unused_use_photo; // was: photo or logo; kept so the saved state keeps its layout
  bool hide_background; // faint image behind the timer
  bool saver_on;        // screensaver: full image after saver_s idle seconds
  int16_t saver_s;
  bool no_start_saver;  // else the app opens in the screensaver (when that is on)
  bool saver_time;      // time of day, big, on the screensaver
  uint8_t saver_time_pos; // 0 middle, 1 bottom, 2 top
  uint8_t saver_time_size; // 0 large, 1 medium, 2 small
} State;

static State s;
static Window *window;
static TextLayer *home_layer, *time_layer, *status_layer, *away_layer;
static AppTimer *timer;
static GBitmap *photo, *faint;
static BitmapLayer *bg_layer;
static Window *saver;
static BitmapLayer *saver_layer;
static Layer *saver_time_layer;
static char saver_time_text[8];
static int64_t last_activity;  // last button press, for the screensaver

static int64_t now_ms(void) {
  time_t t; uint16_t ms;
  time_ms(&t, &ms);
  return (int64_t)t * 1000 + ms;
}

static int64_t elapsed(void) {
  return s.elapsed + (s.running ? now_ms() - s.started : 0);
}

static bool alarming(void) {
  return !s.acked && elapsed() >= s.quarter_ms;
}

// Timer reset to the start of a quarter: only then the screensaver and the settings can open.
static bool at_start(void) {
  return !s.running && s.elapsed == 0;
}

static void render(void) {
  static char home_buf[16], away_buf[16], time_buf[16];
  snprintf(home_buf, sizeof home_buf, "HOME  %d", s.home);
  snprintf(away_buf, sizeof away_buf, "AWAY  %d", s.away);

  int64_t left = s.quarter_ms - elapsed();
  if (left > 0) {
    int secs = (left + 999) / 1000;  // round up: show 0:00 only at zero
    snprintf(time_buf, sizeof time_buf, "%d:%02d", secs / 60, secs % 60);
  } else {
    int secs = -left / 1000;
    snprintf(time_buf, sizeof time_buf, "+%d:%02d", secs / 60, secs % 60);
  }
  layer_set_hidden(bitmap_layer_get_layer(bg_layer), s.hide_background);
  text_layer_set_text(home_layer, home_buf);
  text_layer_set_text(away_layer, away_buf);
  text_layer_set_text(time_layer, time_buf);
  text_layer_set_text(status_layer,
    alarming() ? "TIME! press select" : s.running ? "running" : "paused");
}

static GBitmap *image(void) {
  return photo;  // NULL: no image
}

static bool is_black(uint8_t *data, int stride, int x, int y) {
  return !((data[y * stride + x / 8] >> (x % 8)) & 1);  // 1-bit: 1 = white, LSB = left
}

// Faint copy of the image for behind the timer: one dot per 2x2 block that is at least half black.
// (Keeping every 4th pixel does not work: it lines up with the dither pattern.)
static void update_faint(void) {
  GBitmap *src = image();
  if (!faint) faint = gbitmap_create_blank(GSize(PHOTO_W, PHOTO_H), GBitmapFormat1Bit);
  uint8_t *out = gbitmap_get_data(faint);
  int os = gbitmap_get_bytes_per_row(faint);
  memset(out, 0xFF, os * PHOTO_H);
  uint8_t *in = src ? gbitmap_get_data(src) : NULL;
  int is = src ? gbitmap_get_bytes_per_row(src) : 0;
  for (int y = 0; in && y < PHOTO_H; y += 2)
    for (int x = 0; x < PHOTO_W; x += 2)
      if (is_black(in, is, x, y) + is_black(in, is, x + 1, y) +
          is_black(in, is, x, y + 1) + is_black(in, is, x + 1, y + 1) >= 2)
        out[y * os + x / 8] &= ~(1 << (x % 8));
  if (bg_layer) layer_mark_dirty(bitmap_layer_get_layer(bg_layer));
  if (saver_layer) bitmap_layer_set_bitmap(saver_layer, src);
}

// Time of day on the screensaver: watch timezone and 12/24h setting, same font as the timer.
// Black digits with a 3 px white outline: the text drawn in white at every offset within 3 px, then in black.
#define OUTLINE 3
// Per size: font, empty space above the digits in that font, digit height (pixels, measured in the emulator).
static const struct { const char *font; int8_t pad, digits; } TIME_SIZES[] = {
  { FONT_KEY_BITHAM_42_BOLD, 14, 28 },   // large (same as the timer)
  { FONT_KEY_BITHAM_30_BLACK, 8, 21 },   // medium
  { FONT_KEY_GOTHIC_24_BOLD, 8, 15 },    // small
};

static void draw_saver_time(Layer *l, GContext *ctx) {
  GFont font = fonts_get_system_font(TIME_SIZES[s.saver_time_size % 3].font);
  GRect b = layer_get_bounds(l);
  int w = b.size.w;
  graphics_context_set_text_color(ctx, GColorWhite);
  for (int dx = -OUTLINE; dx <= OUTLINE; dx++)
    for (int dy = -OUTLINE; dy <= OUTLINE; dy++)
      if ((dx || dy) && dx * dx + dy * dy <= OUTLINE * OUTLINE)
        graphics_draw_text(ctx, saver_time_text, font, GRect(dx, OUTLINE + dy, w, b.size.h),
                           GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, saver_time_text, font, GRect(0, OUTLINE, w, b.size.h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void update_saver_time(void) {
  char buf[8];
  time_t now = time(NULL);
  strftime(buf, sizeof buf, clock_is_24h_style() ? "%H:%M" : "%I:%M", localtime(&now));
  const char *t = buf[0] == '0' && !clock_is_24h_style() ? buf + 1 : buf;
  if (strcmp(t, saver_time_text)) {  // redraw only when the minute changes
    strcpy(saver_time_text, t);
    layer_mark_dirty(saver_time_layer);
  }
  int pad = TIME_SIZES[s.saver_time_size % 3].pad, digits = TIME_SIZES[s.saver_time_size % 3].digits;
  int top[] = { (PHOTO_H - digits) / 2, PHOTO_H - digits - 6, 6 };  // where the digits start: middle, bottom, top
  int y = top[s.saver_time_pos % 3] - OUTLINE - pad;
  layer_set_frame(saver_time_layer, GRect(0, y, PHOTO_W, pad + digits + 2 * OUTLINE + 8));
  layer_set_hidden(saver_time_layer, !s.saver_time);
}

// Wakes on every countdown second boundary; vibrates while the alarm is unconfirmed.
static void loop(void *data) {
  render();
  update_saver_time();
  if (alarming()) vibes_long_pulse();
  if (s.saver_on && image() && at_start() && window_stack_get_top_window() == window &&
      now_ms() - last_activity >= s.saver_s * 1000)
    window_stack_push(saver, true);
  timer = app_timer_register(s.running ? 1000 - elapsed() % 1000 : 1000, loop, NULL);
}

static void refresh(void) {
  app_timer_cancel(timer);
  loop(NULL);
}

static void select_click(ClickRecognizerRef r, void *ctx) {
  if (alarming()) {
    s.acked = true;
    vibes_cancel();
  } else if (s.running) {
    s.elapsed = elapsed();
    s.running = false;
  } else {
    s.started = now_ms();
    s.running = true;
  }
  refresh();
}

static void reset_time(void) {
  s.elapsed = 0;
  s.running = false;
  s.acked = false;
  vibes_cancel();
  refresh();
}

static Window *menu;
static int cursor, page;  // settings: selected row; page 0 main, 1 Screensaver

// Hold Select: reset the time; when the time is already reset, open the settings.
static void select_long(ClickRecognizerRef r, void *ctx) {
  if (at_start()) {
    page = cursor = 0;
    window_stack_push(menu, true);
  }
  else reset_time();
}

static void score_click(ClickRecognizerRef r, void *ctx) {
  int *score = click_recognizer_get_button_id(r) == BUTTON_ID_UP ? &s.home : &s.away;
  bool dbl = click_number_of_clicks_counted(r) == 2;
  if (dbl && *score > 0) (*score)--;
  if (!dbl) (*score)++;
  render();
}

static uint8_t held;  // bitmask of Up/Down buttons that are down now

static void raw_down(ClickRecognizerRef r, void *ctx) {
  held |= 1 << click_recognizer_get_button_id(r);
  last_activity = now_ms();
}
static void raw_up(ClickRecognizerRef r, void *ctx) { held &= ~(1 << click_recognizer_get_button_id(r)); }

// Hold Up or Down: reset that score. Hold both: also reset the timer (not the quarter length).
static void score_reset(ClickRecognizerRef r, void *ctx) {
  if (held == ((1 << BUTTON_ID_UP) | (1 << BUTTON_ID_DOWN))) {
    s.home = s.away = 0;
    reset_time();
    vibes_short_pulse();
    return;
  }
  *(click_recognizer_get_button_id(r) == BUTTON_ID_UP ? &s.home : &s.away) = 0;
  render();
}

static void click_config(void *ctx) {
  window_raw_click_subscribe(BUTTON_ID_SELECT, raw_down, raw_up, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 700, select_long, NULL);
  ButtonId score_buttons[] = { BUTTON_ID_UP, BUTTON_ID_DOWN };
  for (int i = 0; i < 2; i++) {
    // One handler for 1 or 2 clicks; fires ~300ms after the last click.
    window_multi_click_subscribe(score_buttons[i], 1, 2, 0, true, score_click);
    window_long_click_subscribe(score_buttons[i], 1500, score_reset, NULL);
    window_raw_click_subscribe(score_buttons[i], raw_down, raw_up, NULL);
  }
}

// Screensaver: any button (also Back) only closes it.
static void saver_dismiss(ClickRecognizerRef r, void *ctx) {
  last_activity = now_ms();
  window_stack_remove(saver, true);
}

static void saver_click_config(void *ctx) {
  ButtonId all[] = { BUTTON_ID_BACK, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN };
  for (int i = 0; i < 4; i++) window_single_click_subscribe(all[i], saver_dismiss);
}

static void photo_rows(int row, const uint8_t *data, int rows) {
  if (!photo) photo = gbitmap_create_blank(GSize(PHOTO_W, PHOTO_H), GBitmapFormat1Bit);
  uint8_t *dst = gbitmap_get_data(photo);
  int stride = gbitmap_get_bytes_per_row(photo);
  for (int i = 0; i < rows && row + i < PHOTO_H; i++)
    memcpy(dst + (row + i) * stride, data + i * ROW_BYTES, ROW_BYTES);
}

static void load_photo(void) {
  uint8_t buf[CHUNK_ROWS * ROW_BYTES];
  for (int c = 0; c < PHOTO_H / CHUNK_ROWS; c++)
    if (persist_read_data(PHOTO_KEY + c, buf, sizeof buf) == sizeof buf) photo_rows(c * CHUNK_ROWS, buf, CHUNK_ROWS);
}

static void settings_received(DictionaryIterator *it, void *ctx) {
  if (dict_find(it, MESSAGE_KEY_ClearPhoto) && photo) {
    for (int c = 0; c < PHOTO_H / CHUNK_ROWS; c++) persist_delete(PHOTO_KEY + c);
    bitmap_layer_set_bitmap(saver_layer, NULL);
    gbitmap_destroy(photo);
    photo = NULL;
    if (window_stack_get_top_window() == saver) window_stack_pop(false);  // no screensaver without an image
  }
  Tuple *row = dict_find(it, MESSAGE_KEY_PhotoRow);
  Tuple *data = dict_find(it, MESSAGE_KEY_PhotoData);
  if (row && data && data->length == CHUNK_ROWS * ROW_BYTES) {
    persist_write_data(PHOTO_KEY + row->value->int32 / CHUNK_ROWS, data->value->data, data->length);
    photo_rows(row->value->int32, data->value->data, CHUNK_ROWS);
  }
  update_faint();
  refresh();
}

// On-watch settings: a main page and a Screensaver page. Up/Down: move, or change a value.
// Select: toggle, open the Screensaver page, or start/stop a value edit. Back: stop the edit, go back, or close.
enum { ROW_TIMER, ROW_BACKGROUND, ROW_SAVER_PAGE,                                  // main page
       ROW_SAVER, ROW_TIMEOUT, ROW_START, ROW_TIME, ROW_TIME_POS, ROW_TIME_SIZE };  // Screensaver page
static Layer *menu_layer;
static bool editing;

// Row ids shown on the current page. Returns the count.
static int menu_rows(int *rows) {
  int n = 0;
  if (page == 0) {
    rows[n++] = ROW_TIMER; rows[n++] = ROW_BACKGROUND; rows[n++] = ROW_SAVER_PAGE;
    return n;
  }
  rows[n++] = ROW_SAVER;
  if (!s.saver_on) return n;  // the other rows only when the screensaver is on
  rows[n++] = ROW_TIMEOUT; rows[n++] = ROW_START; rows[n++] = ROW_TIME;
  if (!s.saver_time) return n;  // place and size only when Show time is on
  rows[n++] = ROW_TIME_POS; rows[n++] = ROW_TIME_SIZE;
  return n;
}

static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void menu_draw(Layer *l, GContext *ctx) {
  static const char *labels[] = { "Timer", "Background", "Screensaver",
    "Screensaver", "Timeout", "Start in saver", "Show time", "Time place", "Time size" };
  static const char *places[] = { "Middle", "Bottom", "Top" }, *sizes[] = { "Large", "Medium", "Small" };
  int rows[6], n = menu_rows(rows);
  GRect b = layer_get_bounds(l);
  int h = b.size.h / 6;
  for (int i = 0; i < n; i++) {
    char val[16], shown[20];
    int q = s.quarter_ms / 1000;
    switch (rows[i]) {
      case ROW_TIMER: snprintf(val, sizeof val, "%d:%02d", q / 60, q % 60); break;
      case ROW_BACKGROUND: snprintf(val, sizeof val, "%s", s.hide_background ? "Off" : "On"); break;
      case ROW_SAVER_PAGE: snprintf(val, sizeof val, "%s >", s.saver_on ? "On" : "Off"); break;
      case ROW_SAVER: snprintf(val, sizeof val, "%s", s.saver_on ? "On" : "Off"); break;
      case ROW_TIMEOUT: snprintf(val, sizeof val, "%d s", s.saver_s); break;
      case ROW_START: snprintf(val, sizeof val, "%s", s.no_start_saver ? "Off" : "On"); break;
      case ROW_TIME: snprintf(val, sizeof val, "%s", s.saver_time ? "On" : "Off"); break;
      case ROW_TIME_POS: snprintf(val, sizeof val, "%s", places[s.saver_time_pos % 3]); break;
      default: snprintf(val, sizeof val, "%s", sizes[s.saver_time_size % 3]);
    }
    snprintf(shown, sizeof shown, editing && i == cursor ? "- %s +" : "%s", val);

    GRect r = GRect(0, i * h, b.size.w, h);
    graphics_context_set_fill_color(ctx, i == cursor ? GColorBlack : GColorWhite);
    graphics_fill_rect(ctx, r, 0, GCornerNone);
    graphics_context_set_text_color(ctx, i == cursor ? GColorWhite : GColorBlack);
    // One line per row: label left, value right.
    graphics_draw_text(ctx, labels[rows[i]], fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(4, r.origin.y + h / 2 - 12, b.size.w - 8, 22), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, shown, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(4, r.origin.y + h / 2 - 17, b.size.w - 8, 28), GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }
}

static void menu_step(ClickRecognizerRef r, void *ctx) {
  int rows[6], n = menu_rows(rows);
  int up = click_recognizer_get_button_id(r) == BUTTON_ID_UP ? 1 : -1;
  if (!editing) cursor = (cursor - up + n) % n;
  else if (rows[cursor] == ROW_TIMER) s.quarter_ms = clamp(s.quarter_ms + up * 30000, 30000, 99 * 60000);
  else s.saver_s = clamp(s.saver_s + up * 5, 5, 3600);
  layer_mark_dirty(menu_layer);
}

static void menu_select(ClickRecognizerRef r, void *ctx) {
  int rows[6];
  menu_rows(rows);
  switch (rows[cursor]) {
    case ROW_BACKGROUND: s.hide_background = !s.hide_background; break;
    case ROW_SAVER_PAGE: page = 1; cursor = 0; break;
    case ROW_SAVER: s.saver_on = !s.saver_on; break;
    case ROW_START: s.no_start_saver = !s.no_start_saver; break;
    case ROW_TIME: s.saver_time = !s.saver_time; break;
    case ROW_TIME_POS: s.saver_time_pos = (s.saver_time_pos + 1) % 3; break;
    case ROW_TIME_SIZE: s.saver_time_size = (s.saver_time_size + 1) % 3; break;
    default: editing = !editing;  // Timer, Timeout
  }
  layer_mark_dirty(menu_layer);
}

static void menu_back(ClickRecognizerRef r, void *ctx) {
  if (editing || page == 1) {
    if (!editing) { page = 0; cursor = ROW_SAVER_PAGE; }
    editing = false;
    layer_mark_dirty(menu_layer);
    return;
  }
  last_activity = now_ms();
  window_stack_remove(menu, true);
  refresh();
}

static void menu_click_config(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, menu_step);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, menu_step);
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_select);
  window_single_click_subscribe(BUTTON_ID_BACK, menu_back);
}

static TextLayer *add_text(Layer *root, GRect frame, const char *font) {
  TextLayer *t = text_layer_create(frame);
  text_layer_set_font(t, fonts_get_system_font(font));
  text_layer_set_text_alignment(t, GTextAlignmentCenter);
  text_layer_set_background_color(t, GColorClear);
  layer_add_child(root, text_layer_get_layer(t));
  return t;
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  bg_layer = bitmap_layer_create(b);
  bitmap_layer_set_bitmap(bg_layer, faint);
  layer_add_child(root, bitmap_layer_get_layer(bg_layer));
  home_layer   = add_text(root, GRect(0, 4, b.size.w, 34), FONT_KEY_GOTHIC_28_BOLD);
  time_layer   = add_text(root, GRect(0, b.size.h / 2 - 34, b.size.w, 50), FONT_KEY_BITHAM_42_BOLD);
  status_layer = add_text(root, GRect(0, b.size.h / 2 + 16, b.size.w, 24), FONT_KEY_GOTHIC_18);
  away_layer   = add_text(root, GRect(0, b.size.h - 40, b.size.w, 34), FONT_KEY_GOTHIC_28_BOLD);
  refresh();
}

static void window_unload(Window *w) {
  text_layer_destroy(home_layer);
  text_layer_destroy(time_layer);
  text_layer_destroy(status_layer);
  text_layer_destroy(away_layer);
  bitmap_layer_destroy(bg_layer);
}

int main(void) {
  // State survives leaving the app; a running clock keeps counting while closed.
  persist_read_data(STATE_KEY, &s, sizeof s);
  if (s.quarter_ms <= 0) s.quarter_ms = QUARTER_MS;
  if (s.saver_s <= 0) s.saver_s = 30;
  app_message_register_inbox_received(settings_received);
  load_photo();
  update_faint();
  last_activity = now_ms();
  app_message_open(256, 0);

  saver = window_create();
  saver_layer = bitmap_layer_create(GRect(0, 0, PHOTO_W, PHOTO_H));
  bitmap_layer_set_bitmap(saver_layer, image());
  layer_add_child(window_get_root_layer(saver), bitmap_layer_get_layer(saver_layer));
  saver_time_layer = layer_create(GRect(0, PHOTO_H / 2 - 36, PHOTO_W, 60));
  layer_set_update_proc(saver_time_layer, draw_saver_time);
  layer_add_child(window_get_root_layer(saver), saver_time_layer);
  update_saver_time();
  window_set_click_config_provider(saver, saver_click_config);

  menu = window_create();
  menu_layer = layer_create(GRect(0, 0, PHOTO_W, PHOTO_H));
  layer_set_update_proc(menu_layer, menu_draw);
  layer_add_child(window_get_root_layer(menu), menu_layer);
  window_set_click_config_provider(menu, menu_click_config);

  window = window_create();
  window_set_click_config_provider(window, click_config);
  window_set_window_handlers(window, (WindowHandlers) { .load = window_load, .unload = window_unload });
  window_stack_push(window, true);
  if (s.saver_on && image() && !s.no_start_saver && at_start()) window_stack_push(saver, false);
  app_event_loop();
  persist_write_data(STATE_KEY, &s, sizeof s);
  window_destroy(window);
  bitmap_layer_destroy(saver_layer);
  layer_destroy(saver_time_layer);
  window_destroy(saver);
  layer_destroy(menu_layer);
  window_destroy(menu);
  gbitmap_destroy(faint);
  if (photo) gbitmap_destroy(photo);
}
