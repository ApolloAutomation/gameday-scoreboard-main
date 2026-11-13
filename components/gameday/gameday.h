#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/panel_layout/panel_layout.h"
#include "esphome/components/select/select.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include "logo_cache.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"

#include "alerts.h"
#include "apps.h"
#include "auto_update.h"
#include "badge.h"
#include "bets.h"
#include "board_layout.h"
#include "captive.h"
#include "espn_parse.h"
#include "favorites.h"
#include "follow.h"
#include "guard.h"
#include "health.h"
#include "hotspot.h"
#include "idle.h"
#include "offseason.h"
#include "panel_text.h"
#include "players.h"
#include "props.h"
#include "recap.h"
#include "poll_timing.h"
#include "set_request.h"
#include "scroll_speed.h"
#include "setup_qr.h"
#include "source_hockeytech.h"
#include "source_mlbstats.h"
#include "split.h"
#include "startup.h"
#include "update_mirror.h"
#include "watch.h"
#include "wifi_history.h"
#ifdef GAMEDAY_DEMO
#include "demo.h"
#endif
#if defined(GAMEDAY_BETA_CHANNEL) || defined(GAMEDAY_UPDATE_MIRROR)
#include "esphome/components/http_request/update/http_request_update.h"
#endif

namespace esphome {
namespace gameday {

using ::espn::GameSnapshot;
using ::espn::GameState;
using ::espn::League;
using ::espn::Mode;
using ::espn::Schedule;
using ::espn::Splash;
using ::espn::TickerOptions;

// Exactly the fields the Home Assistant blueprint used to push to the page.
struct UpdateFields {
  std::string game_state;
  std::string team_abbr;
  int team_score{0};
  std::string opponent_abbr;
  int opponent_score{0};
  std::string status_text;
  std::vector<TickerSpan> ticker_spans;  // bets and props, in color
  std::string team_logo;
  std::string opponent_logo;
  int possession{0};
  int team_timeouts{0};
  int opponent_timeouts{0};
  std::string team_record;  // or seed or place, per the Standing setting
  std::string opponent_record;
  // College rank, 0 when unranked.
  uint8_t team_rank{0};
  uint8_t opponent_rank{0};
  std::string splash_text;
  uint32_t splash_color{0};
  // Logo side: -1 none, 0 team, 1 opponent, 2 both.
  int8_t splash_side{-1};
  uint8_t splash_lead{0};
  std::string splash_points;
  // The play animation before it (scene.h); seq: the splash's millis()
  uint8_t splash_anim{0};
  int16_t splash_anim_dist{0};
  uint8_t splash_anim_flags{0};
  uint32_t splash_seq{0};
  std::string splash_say, splash_score;  // its caption, Splash's anim_say and anim_score
  std::string last_play;
  std::string clock_text;  // "5:36 - 3rd" while the game is live, else empty
  std::string down_distance;  // "3rd & 10" while live, else empty
  bool is_red_zone{false};
  uint32_t team_color{0xFFFFFF};
  uint32_t opponent_color{0xFFFFFF};
  // A side with no logo shows its abbreviation badge in the logo slot (badge.h):
  // the fill is its color above, the ring this second color. Only the demo
  // build's made-up teams set these; release firmware leaves them false.
  bool team_badge{false};
  bool opponent_badge{false};
  uint32_t team_ring{0};
  uint32_t opponent_ring{0};
  std::string kickoff;  // "Sun 3:25 PM" style label while PRE, else empty
  // Other sports (football leaves these at their defaults).
  uint8_t sport{0};        // ::espn::Sport
  std::string situation;   // baseball: the count "2-1" while a half inning is on
                           // basketball: the playoff series "NY lead 2-1"
  int outs{-1};            // baseball: outs in the half inning, -1 when none
  uint8_t bases{0};        // baseball: 1 first, 2 second, 4 third
  bool highlight{false};   // the situation row stands out (bases loaded)
  int team_marks{0};       // soccer: red cards, drawn under each side's logo
  int opp_marks{0};
  std::string message;  // the no-game screen's text; empty leaves the page's default
  // For Home Assistant's Game Status and Last Play. They follow one game and
  // leave out the ticker's per-play parts (espn::ha_status_text), so the
  // recorder isn't writing a row every poll or every playlist card.
  std::string ha_status_text;
  std::string ha_last_play;
  bool quiet{false};  // a timed redraw: nothing to publish
  // The off-season clock (offseason.h) on the no-game screen, in team_color.
  // next_line is the next game for a wide board, its kickoff on a second
  // line; empty when none is known, and the page shows the date instead.
  bool clock{false};
  std::string next_line;
  // A leaderboard sport (golf, sport 5; racing, 6): the board cut to its columns
  // (espn::board_lines), which gd_board (pages/gameday-live.yaml) draws when
  // board is set. board_card: no leaderboard to show (before the event,
  // canceled, the tour's name while it loads): the no-game message carries
  // next_line (the event name, a line break, the dates). Neither is set under
  // the off-season clock. board_value_narrow: one panel shows the value
  // column (golf's score); false gives its room to the names.
  bool board{false};
  bool board_card{false};
  // Post-join address line leads the status text.
  bool join_line{false};
  // Team logo screen; next_line: opponent, when, short when
  bool logo_screen{false};
  std::string board_title, board_status;
  uint32_t board_status_color{0xFFFFFF};
  bool board_value_narrow{true};
  std::vector<std::string> board_pos, board_name, board_value, board_detail;
  std::vector<int> board_value_color, board_name_color;
  // Another display's cell colors and columns (espn::BoardLines)
  std::vector<int> board_pos_color, board_detail_color;
  std::vector<int16_t> board_cols;
  // Another display's polyline over the board: x, y pairs.
  std::vector<int16_t> line;
  uint32_t line_color{0xFFFFFF};
  // Another display's own labels over the board.
  std::vector<::espn::AppText> texts;
  // Another display's filled box, under the rows.
  ::espn::AppFill bar;
  // Another display's loaded logos, with x, y pairs.
  image::Image *icon_img[::espn::kAppIcons]{};
  int16_t icon_xy[2 * ::espn::kAppIcons]{};
  uint8_t icon_n{0};
  std::vector<::espn::AppArt> art;  // and its picture
  image::Image *photo_img{nullptr};  // or its loaded photo
  int16_t photo_xy[2]{};
};

enum class SelectType : uint8_t { TEAM, TIMEZONE, MODE, FAVORITE };

// Favorite Teams mode holds up to this many teams. The WizMote's buttons 1-4
// jump to the first four.
static constexpr uint8_t FAV_MAX = 16;

class GamedayComponent;

using ::espn::parse_team_ref;

class GamedaySelect : public select::Select, public Component {
 public:
  void set_type(SelectType type) { this->type_ = type; }
  void set_slot(uint8_t slot) { this->slot_ = slot; }
  void set_parent(GamedayComponent *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  SelectType type_{SelectType::TEAM};
  uint8_t slot_{0};
  GamedayComponent *parent_{nullptr};
};

// The component is also the web handler for /gameday/*: the device page reads
// one JSON document and posts settings there instead of driving entities.
//   GET  /gameday/state              full snapshot + settings, and "wifi" (wifi_history.h)
//   GET  /gameday/heap               free heap now: internal, largest internal block, PSRAM (KiB)
//   POST /gameday/set?key=value...   (the keys and limits are in set_request.h)
//                                    team=nfl:6|none mode=0-5 rotate=1-30 fav1..fav4=nfl:6|none
//                                    favs=nfl:6,mlb:10,... (up to FAV_MAX in order, "" clears)
//                                    today=0/1 live=nfl,mlb,... ("" = the leagues you follow)
//                                    tz=<name, IANA name or index> tzauto=0/1
//                                    down/count/match/play/odds/opp/bootaddr=0/1 panels=1/2
//                                    (down: football, count: baseball, match: soccer)
//                                    lockon=5-120 (min) release=30-3600 (s) collide=0 stick|1 alternate
//                                    autoupd=0/1 (install updates on its own, auto_update.h)
//                                    beta=0/1 (follow the beta channel; release builds only)
//   POST /gameday/action?do=refresh|qc|power_ok|anim (anim: play=pass|run|ret|fg|pat|hr|soccer|hockey;
//                                    qc: the factory check, firmware/pages/qc.yaml;
//                                    power_ok: lifts the brownout brightness cap, guard.h)
//   GET  /gameday/wifi               the saved Wi-Fi networks in the order they are tried,
//                                    names only (components/saved_networks)
//   POST /gameday/wifi?forget=<ssid> drops one saved network; 404 when it is not saved
//   GET  /gameday/crash              the last crash, watchdog or brownout restart and its
//                                    backtrace, kept in flash (components/crash_report)
//   POST /gameday/crash?clear=1      empties it
// The demo build (firmware/gameday-demo.yaml) adds do=demo|demo_stop and
// GET/POST /gameday/demo (demo.cpp).
// Requests arrive on the HTTP task; settings are applied on the main loop.
class GamedayComponent : public Component, public AsyncWebHandler {
 public:
  void set_http(http_request::HttpRequestComponent *http) {
    this->http_ = http;
    this->logos_.set_http(http);
  }
  // Team logos for the board (gameday-common.yaml, gd_show_logos)
  LogoCache &logos() { return this->logos_; }
  // Board's logos, or badges where missing
  void show_logos();
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  // Registers the hotspot handler first, before any setup().
  void set_web_server_base(web_server_base::WebServerBase *base);
  void set_panel_layout(panel_layout::PanelLayout *p) { this->panels_ = p; }
  void add_on_action_callback(std::function<void(std::string)> &&cb) {
    this->action_callbacks_.push_back(std::move(cb));
  }
  void set_team_select(select::Select *s) { this->team_select_ = s; }
  void set_timezone_select(select::Select *s) { this->timezone_select_ = s; }
  void set_mode_select(select::Select *s) { this->mode_select_ = s; }
  void set_favorite_select(uint8_t slot, select::Select *s) {
    if (slot >= 1 && slot <= 4)
      this->favorite_selects_[slot - 1] = s;
  }
  void add_on_update_callback(std::function<void(const UpdateFields &)> &&cb) {
    this->callbacks_.push_back(std::move(cb));
  }

  // Called by the selects and by template entities in YAML.
  void select_team(const std::string &option);
  void select_team_id(League league, uint32_t id);
  void select_timezone(const std::string &option);
  void select_mode(const std::string &option);
  std::string hostname() const;  // the device name, with its MAC suffix
  // Favorites: four team slots for the remote's numbered buttons.
  void select_favorite(uint8_t slot, const std::string &option);
  void press_favorite(uint8_t slot);
  // Seconds: 30, or whole minutes up to 30.
  void set_rotate_seconds(int seconds);
  int rotate_seconds() const {
    return ::espn::rotate_saved_seconds(this->prefs2_.rotate_minutes, this->prefs8_.rotate_half == 1);
  }
  // Favorite Teams mode timing (see favorites.h for the rules).
  void set_lockon_minutes(int minutes);
  void set_release_seconds(int seconds);
  void set_collision_alternate(bool alternate);
  void set_today_only(bool on);
  // Memory only: never saved across restarts.
  void set_follow(bool on);
  // Live mode's leagues as keys ("nfl,mlb"); "" = the leagues you follow.
  void set_live_leagues(const std::string &keys);
  void set_live_mask_(uint16_t mask);
  std::vector<League> live_leagues_() const;  // the leagues Live mode scans
  bool today_only() const { return (this->prefs7_.flags & FAV7_TODAY) != 0; }
  void refresh_now();
  // WizMote buttons 1-4. Favorites in release firmware; the demo build gives
  // them to the demo (demo.cpp).
  void remote_number(uint8_t n);
  // No team picked and nothing else chosen: the panel follows live games and
  // the finals rotation, and the boot screen gives the QR code for its device
  // page a turn every few minutes (boot.yaml, boot_team_qr_turn) until a team
  // is picked.
  bool no_team_default() const;
  bool team_qr_turns() const { return this->no_team_default() && !this->demo_playing_(); }
#ifdef GAMEDAY_DEMO
  // Loops a scripted game through the real splash and render path, for
  // showing the panel off without a live game, until stop_demo().
  // kind: 1 pro football, 2 college (espn::DemoKind).
  void start_demo(uint8_t kind);
  void stop_demo();
#endif

  bool ticker_clock() const { return this->flag_(FLAG_CLOCK); }
  bool ticker_down_distance() const { return this->flag_(FLAG_DOWN); }
  bool ticker_baseball_count() const { return (this->prefs8_.ticker & TICK8_COUNT) != 0; }
  bool ticker_soccer_stats() const { return (this->prefs8_.ticker & TICK8_MATCH) != 0; }
  bool ticker_last_play() const { return this->flag_(FLAG_PLAY); }
  bool ticker_odds() const { return this->flag_(FLAG_ODDS); }
  bool opponent_splashes() const { return this->flag_(FLAG_OPP); }
  bool setup_done() const { return this->flag_(FLAG_SETUP); }
  // The first real scoreboard since boot, or since the team last changed,
  // is on the board. The boot screen waits for it (boot.yaml, boot_hide).
  bool board_ready() const { return this->board_ready_; }
  // A game, golf round or race session in progress.
  bool board_live() const;
  void show_join_address();
  // What the board is drawing: the fields of the on_update in progress, or
  // of the last one. gd_board reads its leaderboard from here.
  const UpdateFields &board_fields() const {
    return this->main_view_ != ::espn::MainView::GAMEDAY && this->main_app_ != nullptr ? *this->main_app_
                                                                                        : this->last_fields_;
  }
  // The Wi-Fi answered ESPN with a sign-in page (captive.h) and ESPN has not
  // answered since. The boot screen says so over the board (boot.yaml).
  bool captive() const { return this->captive_; }
  // Bench only: read some other address in place of captive.h's kProbeUrl,
  // such as a host on the LAN that answers like a portal. Pass a string
  // literal: only the pointer is kept, and the probe task reads it.
  void set_probe_url(const char *url) { this->probe_url_.store(url); }
  // All score hosts; excludes ~6 KB headers/TLS per request.
  uint64_t espn_bytes() const { return this->espn_bytes_.load() + this->logos_.bytes_downloaded(); }
  uint32_t espn_requests() const { return this->espn_requests_.load(); }
  // Keeps only the pointers: pass string literals.
  void set_hockeytech_feed(League league, const char *key, const char *client_code) {
    ::espn::ht_set_feed(league, key, client_code);
  }
  // True while a boot screen hold that started at since_ms should go on
  // (startup.h). 0 means no hold is running.
  bool boot_hold(uint32_t since_ms) const { return ::espn::boot_hold(this->board_ready_, since_ms, millis()); }
  // Stored inverted so panels from before this setting keep showing it.
  bool show_boot_address() const { return !this->flag_(FLAG_NOBOOTADDR); }
  void set_show_boot_address(bool on) { this->set_flag_(FLAG_NOBOOTADDR, !on); }
  // Stored inverted (a "manual" bit) so panels flashed before this existed
  // come up with auto on without touching their saved preferences.
  bool tz_auto() const { return !this->flag_(FLAG_TZMANUAL); }
  void set_tz_auto(bool on) { this->set_flag_(FLAG_TZMANUAL, !on); }
  void set_ticker_clock(bool on) { this->set_flag_(FLAG_CLOCK, on); }
  void set_ticker_down_distance(bool on) { this->set_flag_(FLAG_DOWN, on); }
  void set_ticker_baseball_count(bool on) { this->set_ticker8_(TICK8_COUNT, on); }
  void set_ticker_soccer_stats(bool on) { this->set_ticker8_(TICK8_MATCH, on); }
  void set_standing(uint8_t kind);
  bool clock_large() const { return (this->prefs8_.sizes & SIZE8_CLOCK) != 0; }
  bool text_large() const { return (this->prefs8_.sizes & SIZE8_TEXT) != 0; }
  Sizes sizes() const { return Sizes{this->clock_large(), this->text_large()}; }
  void set_clock_size(int size) { this->set_size8_(SIZE8_CLOCK, size); }
  void set_text_size(int size) { this->set_size8_(SIZE8_TEXT, size); }
  void set_ticker_last_play(bool on) { this->set_flag_(FLAG_PLAY, on); }
  void set_ticker_odds(bool on) { this->set_flag_(FLAG_ODDS, on); }
  void set_opponent_splashes(bool on) { this->set_flag_(FLAG_OPP, on); }
  uint32_t alerts_on() const { return ::espn::kAlertsAll & ~this->prefs9_.alerts_off; }
  bool alerts_favorites_only() const { return (this->prefs9_.flags & ALERT9_FAV) != 0; }
  bool anim_on() const { return (this->prefs9_.flags & ALERT9_NOANIM) == 0; }
  void set_anim(bool on);
  void try_anim_(const std::string &play);
  void set_alerts(uint32_t on);
  void set_alerts_favorites_only(bool on);
  bool watch_on() const { return (this->prefs9_.flags & ALERT9_NOWATCH) == 0; }
  uint32_t watch_stay_ms() const { return ::espn::watch_stay_ms(this->prefs9_.stay); }
  bool watch_from_mine() const { return (this->prefs9_.flags & ALERT9_MINE) != 0; }
  void set_watch(bool on);
  void set_watch_stay(int seconds);
  void set_watch_from_mine(bool on);
  // "Install updates automatically", off unless the owner turns it on.
#ifdef GAMEDAY_BAR
  // The bar build (firmware/pages/bar.yaml) always installs from the beta
  // channel; there is no setting.
  bool auto_update() const { return true; }
  void set_report_email(const std::string &email) { this->report_email_ = email; }
#else
  bool auto_update() const { return this->prefs6_.auto_update != 0; }
#endif
  void set_auto_update(bool on);
  // Heat and power protection (guard.h). guard_brightness() is what the
  // display gets for the owner's Brightness; it runs from on_boot before
  // setup(), so it loads the power log itself the first time.
  // 0 stops the HUB75 driver; relight needs gd_repaint.
  uint8_t guard_brightness(uint8_t want, bool idle = true);
  // Owner's choice beats night brightness until morning.
  void note_brightness();
  void set_split(int show);
  void set_split_team(uint8_t league, uint32_t id);
  // The main area's display; refuses one the firmware lacks.
  void set_display(int display);
  ::espn::SplitArea split_area() const {
    return this->panels_ != nullptr
               ? ::espn::split_area(this->panels_->cols(), this->panels_->rows(), this->split_prefs_.show)
               : ::espn::SplitArea{};
  }
  void redraw_area() { this->emit_a2_({}); }
  void set_idle(int show);
  void set_idle_dim(int level);
  void set_night(bool on);
  void set_night_start(int minute);
  void set_night_end(int minute);
  void set_night_bri(int level);
  // The idle or night cap is darker than want.
  bool idle_dimmed(uint8_t want) const {
    return ::espn::idle_caps(want, this->idle_, this->idle_prefs_.night_bri, this->idle_prefs_.dim, this->night_.on);
  }
  // The chip temperature sensor's readings (firmware/pages/guard.yaml).
  void set_chip_temp(float c);
  // What the panel's corner says: 0 nothing, 1 too warm, 2 check power.
  uint8_t guard_mark() const { return (uint8_t) ::espn::guard_mark(this->heat_.step, this->power_log_.capped != 0); }
  // The owner's clear of the power cap: /gameday/action?do=power_ok.
  void clear_power_cap();
  // "Follow beta": the update entity reads the beta manifest instead of the
  // release one. Off unless the owner turns it on; only in builds with
  // beta_channel (the release builds, gameday-common.yaml).
#ifdef GAMEDAY_BETA_CHANNEL
  void set_beta_channel(http_request::HttpRequestUpdate *update, const char *stable, const char *beta) {
    this->beta_update_ = update;
    this->stable_source_ = stable;
    this->beta_source_ = beta;
  }
  bool follow_beta() const { return this->follow_beta_ != 0; }
#else
  bool follow_beta() const { return false; }
#endif
  void set_follow_beta(bool on);
  // The update feed's second host (update_mirror.h): `mirror` is `source`
  // on the mirror, `beta_mirror` the beta manifest there. Only in a build
  // that configures update_mirror; the release builds have none.
#ifdef GAMEDAY_UPDATE_MIRROR
  void set_update_mirror(http_request::HttpRequestUpdate *update, const char *source, const char *mirror,
                         const char *beta_mirror) {
    this->mirror_update_ = update;
    this->primary_source_ = source;
    this->mirror_source_ = mirror;
    this->beta_mirror_source_ = beta_mirror;
  }
#endif

  void setup() override;
  void loop() override;
  void dump_config() override;
  // Marks the restart as asked for, in the boot log (health.h).
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // AsyncWebHandler (HTTP task)
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void apply_team_(const ::espn::Team &t);
  void clear_team_();
  bool mark_setup_done_();
  void fire_action_(const std::string &name);
  void set_favorite_(uint8_t slot, uint8_t league, uint32_t id);
  // The whole list at once, in priority order (the page's favs= key).
  void set_favorites_(const std::vector<std::pair<uint8_t, uint32_t>> &list);
  // live_before: live_leagues_() from before the change.
  void favorites_changed_(bool first_pick, const std::vector<League> &live_before);
  uint8_t fav_league_(uint8_t i) const;  // i = 0..FAV_MAX-1
  uint32_t fav_id_(uint8_t i) const;     // 0 = empty
  void put_fav_(uint8_t i, uint8_t league, uint32_t id);  // no save
  void handle_set_(AsyncWebServerRequest *request);
#ifdef USE_GAMEDAY_SAVED_NETWORKS
  void handle_wifi_(AsyncWebServerRequest *request);
#endif
  void apply_set_(const std::vector<std::pair<std::string, std::string>> &kv);
  void rebuild_state_(const UpdateFields *f);  // main loop only
  // PSRAM, so a long document cannot split internal RAM
  using StateJson = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;
  StateJson current_state_json_();
  // The "health" object, read live on each GET /gameday/state (HTTP task).
  std::string health_json_();
  ::espn::Restart start_reason_{::espn::Restart::NONE};
  bool boot_clock_noted_{false};  // boot_log_clock() ran
  // guard.h. heat_ and power_log_ are main loop only; the HTTP task reads
  // their small fields for the health block.
  void load_power_log_();
  void save_power_log_();
  ::espn::HeatGuard heat_;
  ::espn::PowerLog power_log_{};
  ESPPreferenceObject power_pref_;
  bool power_loaded_{false};
  bool power_settled_{false};  // ran kPowerSettledMs without a brownout
  // Wi-Fi history (wifi_history.h): sampled and fed link changes on the main
  // loop, read as the "wifi" object of GET /gameday/state on the HTTP task.
  void wifi_tick_();
  std::string wifi_json_();
  ::espn::WifiHistory wifi_hist_;
  std::mutex wifi_mutex_;
  uint32_t wifi_sample_ms_{0};
  bool wifi_up_{false};  // under wifi_mutex_, as is wifi_rssi_
  int8_t wifi_rssi_{0};  // the last sample
  uint32_t wifi_reason_seen_{0};  // wifi_reason_seq when the link last came up
  std::mutex state_mutex_;
  StateJson state_json_{"{}"};
  UpdateFields last_fields_;
  web_server_base::WebServerBase *base_{nullptr};
  panel_layout::PanelLayout *panels_{nullptr};
  std::vector<std::function<void(std::string)>> action_callbacks_;
  Splash last_splash_;  // for the page: the splash goes out once per emit
  static constexpr uint8_t FLAG_CLOCK = 1;
  static constexpr uint8_t FLAG_DOWN = 2;
  static constexpr uint8_t FLAG_PLAY = 4;
  static constexpr uint8_t FLAG_ODDS = 8;
  static constexpr uint8_t FLAG_OPP = 16;
  static constexpr uint8_t FLAG_TZMANUAL = 32;  // timezone was picked by hand; page must not override
  static constexpr uint8_t FLAG_SETUP = 64;     // a team, mode or favorite has been picked at least once
  static constexpr uint8_t FLAG_NOBOOTADDR = 128;  // skip the address flash on a configured panel's boot
  static constexpr uint8_t FLAGS_DEFAULT = FLAG_DOWN | FLAG_PLAY | FLAG_ODDS | FLAG_OPP;

  struct Prefs {
    uint8_t league;
    uint32_t team_id;
    uint8_t tz_index;
    uint8_t flags;
  } __attribute__((packed));
  // Kept separate so adding fields never invalidates the original blob.
  // 30 seconds: rotate_minutes 1 plus Prefs8's rotate_half.
  struct Prefs2 {
    uint8_t mode;
    uint8_t rotate_minutes;
  } __attribute__((packed));
  static_assert(sizeof(Prefs2) == 2, "Prefs2 keeps its size, or the mode and Switch every reset");
  struct Prefs3 {
    uint8_t fav_league[4];
    uint32_t fav_id[4];  // 0 = slot empty
  } __attribute__((packed));
  struct Prefs4 {
    uint8_t lockon_minutes;
    uint16_t release_seconds;
    uint8_t collide;  // 0 = stick with the higher slot, 1 = alternate on the rotate timer
  } __attribute__((packed));
  // Its presence says team_id 0 means "no team" on this panel. Firmware
  // before it saved Dallas as the team on first boot (recap.h,
  // legacy_team_unpicked).
  struct Prefs5 {
    uint8_t no_team_ok;
  } __attribute__((packed));
  struct Prefs6 {
    uint8_t auto_update;  // 1 = install new releases on its own
    int32_t tried_day;    // espn::auto_update_day() of the last install try, 0 = never
  } __attribute__((packed));
  // Favorites 5-16 (1-4 stay in Prefs3, so older panels keep theirs) and the
  // settings that came with the longer list.
  // Its size is fixed: a blob that changes size fails to load and resets every
  // setting in it. More favorites would need a new blob, not a bigger one.
  static constexpr uint8_t PREFS7_FAVS = 12;
  static_assert(FAV_MAX == 4 + PREFS7_FAVS, "Prefs7 holds favorites 5-16; grow the list with a new blob");
  struct Prefs7 {
    uint8_t fav_league[PREFS7_FAVS];
    uint32_t fav_id[PREFS7_FAVS];  // 0 = empty
    uint8_t flags;                 // FAV7_TODAY
    uint16_t live_mask;            // Live mode: 1 << League per league, 0 = the leagues you follow
  } __attribute__((packed));
  static constexpr uint8_t FAV7_TODAY = 1;  // idle playlist shows only today's games
  // The ticker's situation line per sport. Football's stays FLAG_DOWN in
  // Prefs; Prefs::flags is full. The spare bits are for later settings,
  // since a blob that changes size fails to load and resets.
  struct Prefs8 {
    uint8_t ticker;    // TICK8_*
    uint8_t standing;  // espn::Standing; 0 (Record) on older panels
    // 1 = 30 s, only while rotate_minutes is 1.
    uint8_t rotate_half;
    uint8_t sizes;  // SIZE8_*, 0 (both Normal) on older panels
  } __attribute__((packed));
  static_assert(sizeof(Prefs8) == 4,
                "Prefs8 keeps its size, or the ticker, standing, Switch every and the sizes reset");
  static constexpr uint8_t TICK8_COUNT = 1;  // baseball count, outs, pitcher and batter
  static constexpr uint8_t TICK8_MATCH = 2;  // soccer goals, cards, possession and shots
  static constexpr uint8_t SIZE8_CLOCK = 1;
  static constexpr uint8_t SIZE8_TEXT = 2;
  void set_ticker8_(uint8_t f, bool on);
  void set_size8_(uint8_t f, int size);
  // Stores types off, so new alert types default on.
  struct Prefs9 {
    uint32_t alerts_off;  // bit (Alert - 1) per type off
    uint8_t flags;
    uint8_t stay;         // tens of seconds; 0 = default minute
    uint8_t spare[2];
  } __attribute__((packed));
  static_assert(sizeof(Prefs9) == 8, "Prefs9 keeps its size, or every alert setting resets");
  static constexpr uint8_t ALERT9_FAV = 1;      // only games with the team or a favorite
  static constexpr uint8_t ALERT9_NOWATCH = 2;  // don't switch to big plays in other games
  static constexpr uint8_t ALERT9_MINE = 4;     // favorite's big play may preempt own team's game
  static constexpr uint8_t ALERT9_NOANIM = 8;   // no play animations before splashes
  void save_prefs9_();
  // neutral: live modes, where no team is ours.
  ::espn::Splash board_splash_(bool neutral) const;
  // 128 px sizing also fits three or four wide.
  bool wide_() const { return this->panels_ != nullptr && this->panels_->cols() >= 2; }
  int board_px_() const { return this->panels_ != nullptr ? this->split_area().main_w : ::espn::kBoardPx; }
  // Fixed size: resizing the blob resets every setting.
  struct IdlePrefs {
    uint8_t idle;
    uint8_t dim;           // Off or dim's level, 0 = dark
    uint8_t night_on;
    uint8_t night_bri;     // night brightness, 1-255
    uint16_t night_start;  // minutes after local midnight
    uint16_t night_end;
    uint8_t spare[4];
  } __attribute__((packed));
  static_assert(sizeof(IdlePrefs) == 12, "IdlePrefs is saved as it is: a new size resets every setting in it");
  void save_idle_prefs_() { this->idle_pref_.save(&this->idle_prefs_); }

  bool flag_(uint8_t f) const { return (this->prefs_.flags & f) != 0; }
  void set_flag_(uint8_t f, bool on);
  void save_prefs_();
  void publish_selects_();
  void publish_team_select_(const std::string &option);
  void apply_timezone_();
  const ::espn::Team *current_team_() const;
  std::string team_option_() const;

  // One fetch cycle runs on its own FreeRTOS task so the display loop never
  // waits on the network. The main loop fills a Job, the worker performs the
  // HTTP requests and parsing into it, and the main loop applies the result.
  struct Job {
    uint32_t generation{0};
    const ::espn::Team *team{nullptr};
    uint8_t mode{0};  // effective_mode_() when the job was made; the worker reads this, not the prefs
    // live-game modes: scan the day's games first, then follow one
    bool need_scan{false};
    std::vector<League> scan_leagues;
    std::vector<::espn::LiveGame> live;
    bool scan_ok{false};
    int64_t next_kickoff{0};  // earliest kickoff not started yet, 0 for none
    bool need_schedule{false};
    Schedule schedule;
    bool schedule_ok{false};
    // the "Up next" list rides along with the schedule refresh
    bool need_upcoming{false};
    std::vector<::espn::Upcoming> upcoming;
    bool upcoming_ok{false};
    bool no_event{false};
    GameSnapshot game;
    bool game_ok{false};
    // Favorite Teams mode: which cached entry this cycle refreshes or polls
    int fav_index{-1};
    uint32_t our_id{0};
    // Soccer: the team's reads per competition (comps[0] the league's, then
    // its kCups in order), and the one cup this cycle reads (1.., -1 none).
    std::vector<Schedule> comps;
    int comp{-1};
    bool need_game{false};   // the board has no game for the current schedule yet
    bool cup_ok{false};      // the cup's team read answered
    bool polled{false};      // a cup read polled the game it picked
    std::string done_event;  // a game seen to finish, passed over by the pick
    bool no_lookahead{false};  // My team: a final is still lingering on the board
    int8_t lookahead{0};       // 0 not run, 1 found the next game, 2 nothing ahead, -1 a read failed
    // The finals rotation. A scan keeps the day's finals and the games still
    // to come too when asked, and a recap job reads one other day's
    // scoreboard for one league.
    bool want_finals{false};
    std::vector<::espn::Final> finals;
    std::vector<::espn::Pending> pending;
    bool need_recap{false};
    bool recap_forward{false};  // looking for the next kickoff, not for finals
    uint8_t recap_league{0};
    int32_t recap_day{0};
    bool recap_ok{false};
    // ESPN answered 403 or 429 to a request in this job (set by open_)
    bool throttled{false};
    uint32_t retry_after_ms{0};
    // Per non-ESPN host, by Source; ESPN's slot unused.
    struct HostJob {
      bool read{false};
      bool throttled{false};
      bool scan_failed{false};
      uint32_t retry_after_ms{0};
    };
    HostJob host[::espn::kSourceCount];
    bool read_espn{false};
    // A leaderboard sport (golf, racing): the league's scoreboard header into game
    // (no_event when it lists none), then after a final, a canceled event or
    // none listed, the calendar for the next event (espn::board_next).
    bool need_board{false};
    bool board_next_ok{false};
    // Scores rotation read, between the mode's reads.
    bool side{false};
    // For My team today: also fetch today's final.
    int64_t today_since{0};
    int64_t today_have{0};
    std::unique_ptr<GameSnapshot> today_final;
    // Favorite and soccer prop players at the job's start.
    ::espn::WatchRoster roster;
    // A box score read for player props, in a gap.
    bool need_box{false};
    bool box_final{false};  // started after the game's final
    bool box_ok{false};
    uint8_t box_league{0};
    uint32_t box_event{0};
    ::espn::PlayerWatch box_watch;
    std::vector<::espn::BoxLine> box_lines;
    // No time server answer yet: read only the Date header of an ESPN reply.
    bool need_clock{false};
    int64_t clock_epoch{0};    // 0 when there was no reply or no usable date
    uint32_t clock_got_ms{0};  // millis() when the reply's headers arrived
    // Watcher read in a gap, or a visit's poll.
    bool need_watch{false};
    ::espn::WatchJob watch;
    uint32_t watch_key{0};
    bool visit{false};
    // Second area's read: A2_TEAM or A2_JUMP.
    uint8_t a2{0};
    // Another display's read (apps.h), by Display; 0 none.
    uint8_t app{0};
    bool app_ok{false};
    int64_t app_now{0};
    ::espn::AppRequest app_req;
    // Board's next read; later requests must fit before it.
    uint32_t deadline_ms{0};
    uint32_t event_ms{0}, day_ms{0};
    bool fits(bool day) const;
    bool game_skipped{false};
    uint32_t took_ms{0};
  };
  static constexpr uint8_t A2_TEAM = 1, A2_JUMP = 2;
  Job &new_job_();
  void start_job_();
  void start_worker_();
  static void worker_(void *arg);
  void run_job_();
  void apply_job_();
  void start_clock_job_();
  void apply_clock_job_();
  bool watching_() const;
  // Also feeds the second area, ignoring follow and alerts.
  bool watch_reads_() const;
  bool rotation_on_() const { return this->a2_show_() == ::espn::SplitShow::LIVE && !this->demo_playing_(); }
  // Starts a read if one fits before board_due.
  bool start_watch_job_(uint32_t board_due);
  void run_watch_job_();
  void apply_watch_job_(uint32_t now);
  // College games from the scan, read one by one.
  void watch_feed_(const std::vector<::espn::LiveGame> &live);
  uint32_t watch_key_() const;  // what the watched games depend on
  void watch_reset_();
  void watch_sync_();
  ::espn::WatchCtx watch_ctx_() const;
  // Unlike board_league_, ignores a big-play visit.
  uint8_t mode_league_() const;
  bool own_team_live_() const;
  ::espn::WatchSwitch watch_switch_(bool alert_fav) const;
  // The single hook: Follow this game blocks all switching.
  bool switch_allowed_() const { return !this->follow_.on; }
  void start_visit_(const ::espn::WatchAlert &a, uint8_t league);
  void apply_visit_job_();
  void end_visit_();
  void watch_refused_(uint32_t now);
  GameSnapshot board_prev_(const GameSnapshot &cur);  // game_ as the read before cur
  bool visit_back_{false};  // next mode poll is the first after a visit
  // No way back: what the board follows changed.
  void drop_visit_() {
    this->visit_.active = false;
    this->visit_.saved.reset();
  }
  std::vector<::espn::WatchGame> watch_;
  std::vector<::espn::WatchSource> watch_src_;
  uint32_t watch_cycle_{0};
  uint32_t watch_cycle_ms_{0};
  uint32_t watch_key_last_{0};
  // millis(): no read before this; 0 none
  uint32_t watch_hold_until_{0};
  bool watch_idle_{false};        // this cycle's reads are done
  ::espn::WatchKept watch_board_;  // what the last cycle kept off
  ::espn::WatchTimes watch_times_;
  uint32_t splash_ms_{0};  // millis() of the last splash sent, 0 none
  uint32_t splash_stay_ms_{::espn::kSplashMs};  // its scene plus kSplashMs
  struct Visit {
    bool active{false};
    bool first{false};
    bool fav{false};
    uint8_t fails{0};
    uint32_t first_ms{0};
    uint32_t since_ms{0};
    uint32_t away_id{0};
    Schedule sched;
    Splash splash;
    std::unique_ptr<GameSnapshot> saved;
  };
  Visit visit_;
  bool fetch_schedule_(const ::espn::Team *team, Schedule &out, const char *comp_slug = nullptr);
  void run_cup_job_();
  void apply_cup_job_(uint32_t now);
  // A leaderboard sport (golf, racing): a tour or a series followed like a
  // team, by its own reads (no team endpoint, boot cache, Up next, cups,
  // look-ahead or linger).
  void run_board_job_();
  void apply_board_job_(uint32_t now);
  // 1 the league's event, 0 it lists none, -1 the read failed.
  int fetch_board_(League league, GameSnapshot &out);
  // The next event after after_event (a race weekend also starting no
  // earlier than after_start, its start), from this season's calendar or the
  // next one's; false when a read failed.
  bool fetch_board_next_(League league, const std::string &after_event, int64_t after_start, int64_t now,
                         ::espn::GolfNext &out);
  // Older CDN reads turned away in a row (espn::board_read_is_older), at most two.
  uint8_t board_stale_reads_{0};
  // "Nov 12-15" in the panel's time zone (espn::golf_dates): golf's days
  // (midnight Eastern, read at noon) and a race weekend's (real times).
  std::string golf_dates_(int64_t start, int64_t end) const;
  std::string racing_dates_(int64_t start, int64_t end) const;
  // AHL/ECHL: also fills read, with records the game lacks.
  bool fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out, Schedule *read = nullptr);
  bool fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out);
  // Minor league baseball, from the MLB Stats API.
  bool fetch_mlb_schedule_(const ::espn::Team *team, Schedule &out,
                           std::vector<::espn::Upcoming> *upcoming = nullptr, Schedule *last_final = nullptr);
  bool fetch_mlb_game_(const Schedule &schedule, GameSnapshot &out);
  // AHL and ECHL, from HockeyTech.
  bool fetch_ht_schedule_(const ::espn::Team *team, Schedule &out,
                          std::vector<::espn::Upcoming> *upcoming = nullptr, Schedule *last_final = nullptr);
  bool fetch_ht_game_(League league, const Schedule &schedule, GameSnapshot &out);
  // epoch picks the day (now when 0); finals and pending, when given, also
  // get that day's finished games and the ones not started yet.
  bool fetch_live_games_(League league, std::vector<::espn::LiveGame> &out, int64_t &next_kickoff,
                         std::vector<::espn::Final> *finals = nullptr,
                         std::vector<::espn::Pending> *pending = nullptr, int64_t epoch = 0);
  // The mode the panel runs: the saved one, except that with no team picked
  // My team (and Favorites with none set) runs as Live anything.
  uint8_t effective_mode_() const {
    return this->no_team_default() ? (uint8_t) Mode::LIVE_ANY : this->prefs2_.mode;
  }
  static bool is_live_mode_(uint8_t m) {
    return (m >= (uint8_t) Mode::LIVE_NFL && m <= (uint8_t) Mode::LIVE_ANY) || m == (uint8_t) Mode::LIVE;
  }
  bool live_mode_() const { return is_live_mode_(this->effective_mode_()); }
  // The leagues the scan reads (set_request.h). The finals rotation covers
  // the same ones, as recap_bit()s, so it never walks a league the scan skips.
  // Other modes scan only for the idle scores.
  std::vector<League> scan_leagues_() const {
    uint8_t m = this->effective_mode_();
    if (this->scores_wanted_() && !is_live_mode_(m))
      return ::espn::scan_leagues(m, this->side_leagues_(), true);
    return ::espn::scan_leagues(m, m == (uint8_t) Mode::LIVE ? this->live_leagues_() : std::vector<League>{});
  }
  // Empty means the clock (golf or racing, no favorites).
  std::vector<League> side_leagues_() const;
  uint16_t recap_leagues_() const {
    uint16_t mask = 0;
    for (League l : this->scan_leagues_())
      mask |= ::espn::recap_bit((uint8_t) l);
    return mask;
  }
  // Mode 4 with at least one favorite set; with none it behaves like My Team.
  bool favorites_mode_() const { return this->prefs2_.mode == (uint8_t) Mode::FAVORITES && !this->fav_.empty(); }
  uint32_t our_id_() const;  // team id the snapshot is oriented around
  // app: another display's read, its method and headers.
  std::shared_ptr<http_request::HttpContainer> open_(const std::string &url, const ::espn::AppRequest *app = nullptr);
  void schedule_next_(uint32_t ms) { this->next_fetch_ms_ = millis() + ms; }
  uint32_t interval_for_phase_() const;
  uint32_t linger_ms_() const;  // how long a final stays up before the next game
  std::string lingered_event_;  // the final the last linger ended on (My team)
  uint32_t linger_backoff_ms_{0};  // a longer linger on that final when no next game turned up
  // Non-football leagues without a season list find the next game in the
  // coming days' scoreboards when the team endpoint names a finished one.
  bool needs_lookahead_(League league) const;
  // 1 found the next game, 0 the team is off that day, -1 the read failed.
  int fetch_team_day_(const ::espn::Team *team, uint32_t group, int64_t day_epoch, const std::string &skip_event,
                      Schedule &out, const char *comp_slug = nullptr);
  // A team read that names a game not started yet: swap in the team's game on
  // now from today's scoreboard (the league's, or the cup's), if it has one.
  void take_game_on_now_(const ::espn::Team *team, Schedule &s, const std::string &done_event,
                         const char *comp_slug = nullptr);
  void emit_(const ::espn::Splash &splash);
  // countdown: a kickoff countdown replaces the clock.
  void game_fields_(const GameSnapshot &g, const ::espn::Splash &splash, UpdateFields &f, bool countdown = false);
  // "Sun 3:25 PM" in the panel's time zone.
  std::string kickoff_of_(const GameSnapshot &g) const;
  // board: the game is a leaderboard.
  void mode_fields_(UpdateFields &f, bool board);
  bool hold_for_logos_(const UpdateFields &f);
  bool logo_hold_done_();
  void draw_(const UpdateFields &f);
  // The logos on the board, and an update with new ones waiting for them
  // (hold_for_logos_) since logo_hold_ms_.
  std::string shown_team_logo_, shown_opp_logo_;
  bool shown_badge_[2]{};
  uint32_t logo_hold_ms_{0};
  bool logo_held_{false};
  // Turns f into the off-season clock card when the board has nothing to
  // show for months (offseason.h); leaves it alone otherwise.
  // force: idle clock, any season, next game below.
  void clock_fields_(UpdateFields &f, bool force = false);

  ESPPreferenceObject idle_pref_;
  IdlePrefs idle_prefs_{};
  ::espn::IdleState idle_;
  ::espn::IdleView idle_view_{::espn::IdleView::BOARD};
  ::espn::NightOverride night_;  // owner set brightness during these night hours
  uint32_t idle_tick_ms_{0};
  int idle_minute_{-1};  // minute the countdown was last drawn
  // Fires the "dim" action when brightness should change.
  bool idle_update_();
  void idle_tick_();
  bool idle_busy_() const;
  // False after a restart: finals then wait only while new.
  bool post_saw_play_{false};
  void note_post_(uint32_t now);  // game_ is final: start its wait once
  ::espn::IdleView idle_effective_(::espn::IdleView v) const;
  void idle_changed_(bool live_switch);  // redraw; in Live games maybe scan again
  // Keeps Home Assistant's lines.
  void idle_fields_(UpdateFields &f);
  // nullptr means show the clock.
  const GameSnapshot *today_game_() const;
  // nullptr means show the clock; game may be nullptr.
  const ::espn::Team *logo_team_(const GameSnapshot **game, std::string *color = nullptr) const;
  bool logo_fields_(UpdateFields &f);
  // Night hours need the team's card for the clock.
  bool live_recap_() const {
    return this->current_team_() == nullptr ||
           (this->idle_prefs_.idle == (uint8_t) ::espn::IdleShow::SCORES && !this->idle_.night);
  }
  // 0 when My team today isn't shown.
  int64_t today_since_() const;
  // Mode's own ticker line while the idle display shows.
  std::string page_status_;
  // PSRAM: [0] scores card, [1] today's final.
  std::vector<GameSnapshot, PsramAllocator<GameSnapshot>> idle_snaps_;
  GameSnapshot &side_card_() { return this->idle_snaps_[0]; }
  GameSnapshot &today_final_() { return this->idle_snaps_[1]; }
  const GameSnapshot &today_final_() const { return this->idle_snaps_[1]; }
  // Kept final's start (day_order), 0 for none.
  int64_t today_have_() const {
    const GameSnapshot &f = this->today_final_();
    return f.valid ? ::espn::day_order(f.kickoff_epoch, f.time_tbd) : 0;
  }
  void note_final_(const GameSnapshot &g);  // My team today keeps it until midnight
  bool side_on_{false};
  uint32_t side_check_ms_{0};
  // Per league millis() of next scan; 0 = due now.
  uint32_t scan_due_[::espn::kLeagueCount]{};
  void scan_due_now_() {
    for (auto &d : this->scan_due_)
      d = 0;
  }
  uint32_t side_wait_since_{0};
  uint32_t side_wait_ms_{0};  // after a failed read, 0 = none
  // millis() scores last gave way, 0 never.
  uint32_t side_off_ms_{0};
  // Starts a read only if it fits before due.
  bool start_side_job_(uint32_t now, uint32_t due);
  void apply_side_job_();
  bool scores_wanted_() const { return this->side_on_ || this->a2_scores_; }
  void reset_game_();
  // Clears the board and scans again now, for a change in what the mode
  // follows. The caller starts the boot screen hold first.
  void restart_board_();
  // Restarts the board when Live games now scans other leagues than before.
  bool live_leagues_changed_(const std::vector<League> &before);
  void drop_team_placeholder_();
  // A game poll came back clean: clear the miss count and start the clock over.
  void mark_good_poll_() {
    this->misses_ = 0;
    this->last_good_ms_ = millis() == 0 ? 1 : millis();
    this->clear_backoff_();
  }
  // A read failed: start the next, longer wait (poll_timing.h) and return it.
  // A job reading one other host backs off it.
  uint32_t back_off_();
  // ESPN when the job read ESPN, nothing, or several.
  ::espn::Source job_host_() const {
    if (this->job_.read_espn)
      return ::espn::Source::ESPN;
    ::espn::Source only = ::espn::Source::ESPN;
    for (size_t i = 1; i < ::espn::kSourceCount; i++) {
      if (!this->job_.host[i].read)
        continue;
      if (only != ::espn::Source::ESPN)
        return ::espn::Source::ESPN;
      only = (::espn::Source) i;
    }
    return only;
  }
  // 403 or 429 in this job: back off.
  bool job_refused_() const {
    bool refused = this->job_.throttled;
    for (const auto &h : this->job_.host)
      refused = refused || h.throttled;
    return refused;
  }
  // The answering host's next failure restarts its backoff.
  void clear_backoff_() {
    ::espn::Source h = this->job_host_();
    if (h != ::espn::Source::ESPN) {
      this->host_wait_[(size_t) h].clear();
      return;
    }
    // Other hosts' waits updated per answer; ESPN's isn't ours.
    if (!this->job_.read_espn)
      for (size_t i = 1; i < ::espn::kSourceCount; i++)
        if (this->job_.host[i].read)
          return;
    this->fail_run_.clear();
    this->backoff_ms_ = 0;
  }
  // Inside a wait back_off_ started. Only Favorite Teams mode needs to ask:
  // it ticks every second for the playlist and must not read ESPN on each one.
  bool backing_off_() const {
    return this->backoff_ms_ != 0 && (millis() - this->backoff_since_ms_) < this->backoff_ms_;
  }
  // Favorite teams skips that favorite until it ends.
  bool host_waiting_(League league) const {
    ::espn::Source s = ::espn::league_source(league);
    return s != ::espn::Source::ESPN ? this->host_wait_[(size_t) s].waiting(millis()) : this->backing_off_();
  }
  // Game Day's hosts only; displays keep their own.
  bool other_host_failing_() const {
    bool failing = false;
    for (size_t i = 1; i < ::espn::kGameSourceCount; i++)
      failing = failing || this->host_wait_[i].run.streak > 0;
    return failing;
  }
  // 0 for ESPN (see backing_off_) or no wait.
  uint32_t host_left_(League league, uint32_t now) const {
    ::espn::Source s = ::espn::league_source(league);
    return s != ::espn::Source::ESPN ? this->host_wait_[(size_t) s].left(now) : 0;
  }
  // 0 when no other host's wait runs.
  uint32_t soonest_host_left_(uint32_t now) const {
    uint32_t best = 0;
    for (size_t i = 1; i < ::espn::kGameSourceCount; i++) {
      uint32_t left = this->host_wait_[i].left(now);
      if (left != 0 && (best == 0 || left < best))
        best = left;
    }
    return best;
  }
  // The fallback card must not outlast the rescan.
  uint32_t rescan_left_() const {
    const uint32_t since = millis() - this->live_started_ms_;
    return since >= this->live_rescan_ms_ ? 0 : this->live_rescan_ms_ - since;
  }
  // Someone asked for fresh data (Refresh Now, a new team, mode or favorite):
  // read now, but keep the failure count so another failure waits longer.
  void end_backoff_wait_() {
    this->backoff_ms_ = 0;
    for (auto &w : this->host_wait_)
      w.end_wait();
  }
  // True once a poll has actually put game data on the board this boot.
  bool ever_polled_good_() const { return this->last_good_ms_ != 0; }
  // Seconds since the last good poll. With nothing ever polled, report the
  // time since boot rather than 0: the board has had no update for its whole
  // uptime, and 0 would read to the app as "fresh". Callers that need to
  // phrase it for a person test ever_polled_good_() first.
  uint32_t stale_seconds_() const {
    if (this->last_good_ms_ == 0)
      return millis() / 1000;
    return (millis() - this->last_good_ms_) / 1000;
  }

  http_request::HttpRequestComponent *http_{nullptr};
  LogoCache logos_;
  time::RealTimeClock *time_{nullptr};
  select::Select *team_select_{nullptr};
  select::Select *timezone_select_{nullptr};
  std::vector<std::function<void(const UpdateFields &)>> callbacks_;

  ESPPreferenceObject pref_;
  Prefs prefs_{};
  ESPPreferenceObject pref2_;
  Prefs2 prefs2_{};
  select::Select *mode_select_{nullptr};
  select::Select *favorite_selects_[4]{nullptr, nullptr, nullptr, nullptr};
  ESPPreferenceObject pref3_;
  Prefs3 prefs3_{};
  ESPPreferenceObject pref4_;
  Prefs4 prefs4_{};
  ESPPreferenceObject pref5_;
  ESPPreferenceObject pref6_;
  Prefs6 prefs6_{};
  // Install updates automatically (auto_update.h), run from loop().
  uint32_t auto_update_ms_{0};      // last look
  int32_t auto_update_checked_{0};  // the night the manifest was read for it
  uint32_t auto_update_check_ms_{0};
  bool auto_update_waiting_{false};  // logged "waiting for the game" tonight
  int32_t auto_update_skipped_{0};   // the night it last logged skipping a rolled-back release
  // The install under way and the release that rolled back here, in its own
  // blob so Prefs6 keeps its size (auto_update.h, UpdateMark).
  ESPPreferenceObject update_mark_pref_;
  ::espn::UpdateMark update_mark_{};
  bool update_installing_{false};  // the update entity is installing, mark written
  void settle_update_mark_();
  void update_state_changed_();
#ifdef GAMEDAY_BAR
  // fnv1_hash() of the version last installed through "Install now"
  // (auto_update.h), saved before the install so a crash or power cut
  // partway through doesn't try the same version again until the night.
  ESPPreferenceObject bar_pref_;
  uint32_t now_tried_{0};
  // Where the bar's "Something wrong?" link on the device page writes to.
  std::string report_email_;
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  http_request::HttpRequestUpdate *beta_update_{nullptr};
  const char *stable_source_{nullptr};
  const char *beta_source_{nullptr};
  ESPPreferenceObject beta_pref_;
  uint8_t follow_beta_{0};  // 1 = read the beta manifest
  void apply_channel_();
#endif
#ifdef GAMEDAY_UPDATE_MIRROR
  http_request::HttpRequestUpdate *mirror_update_{nullptr};
  const char *primary_source_{nullptr};
  const char *mirror_source_{nullptr};
  const char *beta_mirror_source_{nullptr};
  uint32_t mirror_since_ms_{0};
  bool on_mirror_{false};
  bool mirror_answered_{false};
  void mirror_tick_();
  void use_primary_();
#endif
  void auto_update_tick_();
  bool followed_game_live_() const;
  ESPPreferenceObject pref7_;
  Prefs7 prefs7_{};
  ESPPreferenceObject pref8_;
  Prefs8 prefs8_{};
  ESPPreferenceObject pref9_;
  Prefs9 prefs9_{};
  // The team endpoint's answer for the saved team (startup.h), so a boot can
  // skip that read and go straight to the scoreboard.
  ESPPreferenceObject team_cache_pref_;
  bool team_cache_tried_{false};     // one look per boot
  bool cache_unconfirmed_{false};    // schedule_ came from the cache and no poll has confirmed it
  bool upcoming_after_poll_{false};  // booted from the cache: season list after the first board
  bool load_team_cache_(const ::espn::Team *team);
  void save_team_cache_(const ::espn::Team *team);
  // Boot, before the first fetch: Wi-Fi is up but the clock is not set yet.
  void wait_for_clock_();
  bool clock_set_() const { return this->time_ != nullptr && this->time_->now().is_valid(); }
  uint32_t wifi_up_ms_{0};      // millis() when the loop first saw Wi-Fi this boot, 0 = not yet
  uint32_t sntp_asked_ms_{0};   // millis() of the last nudge to the time server, 0 = none since Wi-Fi came up
  uint32_t http_clock_ms_{0};   // millis() of the last Date header read, 0 = none since Wi-Fi came up
  bool clock_approx_{false};    // the clock came from ESPN's Date header and no time server has answered since
  bool clock_wait_shown_{false};
  bool board_ready_{false};
  uint32_t board_pending_ms_{0};  // millis() of the last team change, 0 = waiting since boot
  void mark_board_ready_();
  std::string favorite_option_(uint8_t slot) const;
  // Favorite Teams mode: one cached next-game card per set slot, in slot
  // order. The worker refreshes one entry per cycle; the main loop picks
  // which one the panel shows (favorites.h) and polls only that game.
  struct FavEntry {
    const ::espn::Team *team{nullptr};
    uint8_t slot{0};  // 1-4, the remote button
    Schedule sched;
    GameSnapshot game;
    uint32_t fetched_ms{0};  // last schedule attempt
    int64_t final_epoch{0};  // when the panel saw the game go final, 0 if it was fetched final
    bool stale{false};       // released after a final: fetch the next game
    std::string stuck_event;  // a final the team endpoint kept naming: no minute-by-minute retries
    uint32_t stuck_ms{0};      // millis() when that back-off started
    uint32_t stuck_for_ms{0};  // and how long it lasts
    std::vector<Schedule> comps;  // soccer: reads per competition, as Job::comps
    int comp_next{-1};            // the cup read due next, -1 when none
  };
  // The favorite cards are ~1 KB each and internal heap is the panel's
  // scarcest resource: PSRAM (logo_cache.h's allocator).
  using FavList = std::vector<FavEntry, PsramAllocator<FavEntry>>;
  FavList fav_;
  int fav_shown_{-1};
  int64_t fav_shown_since_{0};  // epoch seconds
  int fav_pinned_{-1};
  bool fav_locked_{false};
  uint32_t fav_poll_ms_{0};
  void rebuild_favorites_(bool keep_cards);
  ::espn::FavRules fav_rules_() const;
  std::vector<::espn::FavGame> fav_games_() const;
  bool fav_choose_(int64_t now_epoch);  // true when the shown entry changed
  void fav_no_event_(FavEntry &e, bool shown, int64_t now_epoch);
  const GameSnapshot &ha_game_() const;  // the game Home Assistant's sensors follow
  // Memory only; lets go after the final's wait.
  ::espn::FollowPin follow_;
  bool following_();
  // During a visit, checks the saved game instead.
  bool follow_can_start_() const;
  bool follow_picked_() const;  // the mode itself put the board's game up
  // bets_tick_: true when a done bet was dropped.
  ESPPreferenceObject bets_pref_;
  ::espn::ManualBets bets_;
  void set_bet_(uint8_t slot, ::espn::Bet b);
  void remove_bet_(uint8_t slot);
  bool bets_tick_();
  uint8_t board_league_() const;
  // False for no game, no event id, leaderboards, demo.
  bool bet_game_(const GameSnapshot &g, uint8_t league, ::espn::BetGame &out) const;
  // nullptr when the panel holds no game for b.
  const GameSnapshot *bet_snapshot_(const ::espn::Bet &b, ::espn::BetGame &out) const;
  void bets_ticker_(const GameSnapshot &g, uint8_t league, UpdateFields &f) const;
  // Player props: followed like bets, numbers from box scores.
  // One per saved half (prop_half_bit).
  ESPPreferenceObject props_pref_[2];
  ::espn::PropBlob props_{};
  void set_prop_(uint8_t slot, ::espn::Prop p);
  void remove_prop_(uint8_t slot);
  void save_props_(uint8_t halves);
  void props_changed_(uint8_t halves);
  // True when a done prop was dropped.
  bool props_tick_();
  const GameSnapshot *game_for_(uint8_t league, uint32_t event, ::espn::BetGame &out) const;
  // Half units, kPropNoValue when no number yet.
  int prop_now_(const ::espn::Prop &p, const GameSnapshot *g) const;
  void props_ticker_(const GameSnapshot &g, uint8_t league, UpdateFields &f) const;
  // A game's box score while a prop waits.
  struct BoxGame {
    uint8_t league{0};
    uint32_t event{0};
    uint32_t tried_ms{0};  // last read started, 0 none
    uint32_t final_ms{0};  // game first seen final, 0 not yet
    uint8_t fails{0};      // failed reads in a row
    bool read{false};
    bool final_read{false};
    std::vector<::espn::BoxLine> lines;
  };
  std::vector<BoxGame> box_;
  uint32_t box_check_ms_{0};
  ::espn::BoxTimes box_times_;
  bool start_box_job_(uint32_t now, uint32_t due);
  void run_box_job_();
  void apply_box_job_();
  void box_failed_(uint32_t took_ms);
  // One per saved half (player_half_bit).
  ESPPreferenceObject players_pref_[2];
  ::espn::PlayerBlob players_{};
  void set_player_(const ::espn::SetOp &op);
  void remove_player_(uint8_t league, uint32_t athlete);
  void players_changed_(uint8_t halves);
  // e.g. "ALLEN 21/34, 241 YDS"
  std::vector<std::string> player_lines_(const GameSnapshot &g) const {
    return this->player_lines_(g, this->board_league_());
  }
  std::vector<std::string> player_lines_(const GameSnapshot &g, uint8_t league) const;
  int64_t next_midnight_(int64_t now) const;
  void start_fav_job_(uint32_t now);
  void apply_fav_job_(uint32_t now);
  uint32_t live_away_id_{0};      // the followed live game's away team
  uint32_t live_started_ms_{0};   // game's first read (pick until then), or last fallback scan
  // Turn starts at first good read, not at pick.
  bool live_unread_{false};
  bool live_none_{false};         // last scan found nothing in progress
  // With nothing live, the wait until the next scan: up to the next kickoff.
  uint32_t live_rescan_ms_{2 * 60 * 1000};
  // A live mode with nothing live: the board shows the saved team's card and
  // keeps scanning. The mode the owner picked does not change.
  bool live_fallback_{false};

  // The finals rotation (recap.h): a live mode with nothing live on a panel
  // with no team. Finals are kept across live spells and only re-read for a
  // day the panel has not seen; the cards are rebuilt from them.
  bool recap_on_{false};
  std::vector<::espn::RecapDay> recap_days_;    // every league seen, newest day first
  std::vector<::espn::RecapCard> recap_cards_;  // the leagues this mode reads; index into recap_days_
  size_t recap_step_{0};
  uint32_t recap_step_ms_{0};  // when the card on the board went up, 0 = none yet
  std::string recap_message_, recap_ticker_;
  int64_t recap_scan_next_{0};  // the last scan's next kickoff today, 0 for none
  static_assert(::espn::leagues_fill_indexes(), "League values must run 0 to kLeagueCount - 1");
  // Per league, indexed by League. walk: next day back (or ahead) to read,
  // 1 to kRecapBackDays back (kRecapMaxDays ahead); 0 not started;
  // kRecapMaxDays + 1 done. walk_day: the league's ESPN day a walk finished,
  // so a fruitless one is tried again the next day rather than every scan.
  // fwd_match: "BOS at NYR", the first game on the day the forward walk
  // found.
  uint8_t recap_back_[::espn::kLeagueCount]{};
  int32_t recap_back_day_[::espn::kLeagueCount]{};
  uint8_t recap_fwd_[::espn::kLeagueCount]{};
  int32_t recap_fwd_day_[::espn::kLeagueCount]{};
  int64_t recap_fwd_next_[::espn::kLeagueCount]{};
  std::string recap_fwd_match_[::espn::kLeagueCount];
  bool recap_job_(Job &j, int64_t tnow);  // fills j with the next walk step, if any
  void apply_recap_job_();
  bool recap_day_(Job &j);  // folds a day's read in; false when the read failed
  void recap_absorb_(const Job &j, int64_t tnow);  // a scan's finals, games to come and next kickoff
  // The next kickoff after today's games, and its matchup when known.
  int64_t recap_next_kickoff_(std::string *matchup = nullptr) const;
  // "college ", "NHL ", or "" when the mode reads several leagues: the word
  // in "No live NHL games".
  std::string live_word_() const;
  void recap_rebuild_();
  void recap_tick_();
  void recap_show_();
  // Sets up the current card without drawing it.
  void recap_prepare_();
  // side: not Live games' board, so no "No live games".
  void recap_card_(size_t step, bool side, GameSnapshot &card, std::string &message, std::string &ticker);

  // ---- Split mode (split.h) ----
  // Fixed size: resizing the blob resets every setting.
  struct SplitPrefs {
    uint8_t show;
    uint8_t team_league;
    uint32_t team_id;     // 0 means no team
    uint8_t display;      // the main area's Display, 0 on older panels
    uint8_t spare[9];
  } __attribute__((packed));
  static_assert(sizeof(SplitPrefs) == 16, "SplitPrefs keeps its size, or every setting in it resets");
  ESPPreferenceObject split_pref_;
  SplitPrefs split_prefs_{};
  ::espn::SplitShow a2_show_() const {
    return this->panels_ != nullptr
               ? ::espn::split_effective(this->split_prefs_.show,
                                         ::espn::split_at(this->panels_->cols(), this->panels_->rows()))
               : ::espn::SplitShow::OFF;
  }
  // As last drawn, and as the state document says.
  ::espn::AreaView a2_view_{::espn::AreaView::NONE};
  ::espn::AreaView a2_view_doc_{::espn::AreaView::NONE};
  uint8_t a2_n_doc_{0};
  bool a2_scores_{false};  // the area wants the scores rotation read
  uint32_t a2_gen_{0};     // bumped on change; in-flight reads get dropped
  // One favorite's card or none, in PSRAM.
  using A2List = std::vector<FavEntry, PsramAllocator<FavEntry>>;
  A2List a2_team_;
  bool a2_none_{false};      // ESPN named no game for it
  bool a2_saw_live_{false};  // seen going final: keeps the panel lit
  uint32_t a2_poll_ms_{0};
  uint32_t a2_final_ms_{0};  // first seen final, 0 none
  // The area's own failures, apart from the board's.
  ::espn::Backoff a2_fail_;
  uint8_t a2_misses_{0};
  uint32_t a2_wait_since_{0}, a2_wait_ms_{0};
  uint32_t gap_hold_until_{0};  // millis(), 0 none
  uint32_t times_cycle_ms_{0};  // the last watch_times_.cycle()
  // The card up (league, event) and since when.
  uint8_t rot_league_{0};
  std::string rot_event_;
  uint32_t rot_ms_{0};
  // The card after it, whose logos are downloading
  uint8_t rot_next_league_{0};
  std::string rot_next_event_;
  bool rot_here_() const;          // the card up is still one to show
  bool rot_advance_(uint32_t now);  // true when the area needs a redraw
  // Big play's game held in the area.
  struct A2Jump {
    bool active{false};
    bool fav{false};
    bool first{false};
    uint8_t fails{0};
    uint8_t league{0};
    uint32_t away_id{0};
    uint32_t first_ms{0}, since_ms{0}, poll_ms{0};
    std::string event_id;
    Splash splash;
  };
  A2Jump jump_;
  GameSnapshot &jump_game_() { return this->idle_snaps_[2]; }
  const GameSnapshot &jump_game_() const { return this->idle_snaps_[2]; }
  uint32_t a2_splash_ms_{0};
  bool a2_splash_up_(uint32_t now) const { return ::espn::splash_up(now, this->a2_splash_ms_); }
  // A 2x2's board splash covers the area
  bool a2_covered_(uint32_t now) const {
    return this->panels_ != nullptr &&
           ::espn::area_covered(::espn::split_at(this->panels_->cols(), this->panels_->rows()), now,
                                this->splash_ms_, this->splash_stay_ms_);
  }
  // Either splash up: the next card waits
  bool a2_held_(uint32_t now) const { return this->a2_splash_up_(now) || this->a2_covered_(now); }
  void a2_splash_timer_();  // hides the area's splash once seen
  void a2_uncovered_();     // the board's splash left the area
  std::string a2_logo_[2];
  bool a2_badge_[2]{};
  // A new card waiting for these logos
  ::espn::LogoHold a2_hold_;
  std::string a2_hold_logo_[2];
  ::espn::AreaView a2_compute_() const;
  // Draws if bound; updates split_now and split_n.
  void emit_a2_(const ::espn::Splash &splash);
  void a2_fields_(::espn::AreaView v, const ::espn::Splash &splash, UpdateFields &f);
  void a2_show_logos_();
  void a2_tick_();             // runs every IDLE_TICK
  void a2_reset_();            // what the area follows changed
  void a2_set_team_();
  bool a2_team_main_() const;  // second team plays in the main board's game
  bool a2_busy_() const;       // keeps the panel lit through Off or dim
  bool a2_wide_() const { return this->split_area().w >= 128; }
  uint8_t a2_league_() const;
  void a2_failed_(const Job &j);
  void a2_answered_();  // host answered: reset its failure run
  void a2_jump_(const ::espn::WatchAlert &a, uint8_t league);
  void end_jump_();
  // 1 started, -1 doesn't fit before due, 0 none.
  int start_a2_job_(uint32_t now, uint32_t due);
  void apply_a2_job_();
  // Order: the area's own, scores, then the watcher.
  void start_gap_job_(uint32_t due);
  bool gap_ok_(uint32_t now) const;
  bool espn_gap_ok_(uint32_t now) const;
  // Favorite teams' next read, which ticks every second.
  uint32_t fav_gap_due_(uint32_t now) const;
  // Next known games, minus the main board's.
  std::string a2_clock_line_() const;
  // The board's splash goes to the area's Game Day.
  bool a2_take_splash_{false};

  // ---- Other displays (apps.h) ----
  ::espn::Display main_display_() const {
    return ::espn::display_effective(this->split_prefs_.display, ::espn::app_slots().mask());
  }
  bool app_busy_(::espn::Display d) const;
  bool app_lit_(::espn::Display d) const;
  bool app_on_screen_(::espn::Display d) const;
  bool app_night_(::espn::Display d) const;
  void apps_setup_();
  // Per second: shown changes and timed redraws.
  void apps_tick_();
  void app_redraw_(::espn::Display d, bool timed = false);
  // A frame into fields the board or area draws.
  void app_fields_(::espn::Display d, int w, int h, UpdateFields &f);
  void draw_main_app_(bool quiet = false);
  // 1 started, -1 doesn't fit before due, 0 none.
  int start_app_job_(uint32_t now, uint32_t due);
  void run_app_job_();
  void apply_app_job_();
  void app_failed_(::espn::Display d, uint32_t took_ms);
  bool app_set_(const std::string &key, const std::string &value);
  ::espn::MainView main_view_{::espn::MainView::GAMEDAY};
  UpdateFields *main_app_{nullptr};  // PSRAM: what the board draws then
  ::espn::AppTimes app_times_;
  uint32_t app_last_ms_[::espn::kDisplayCount]{};  // last read's start
  uint32_t app_gen_[::espn::kDisplayCount]{};      // bumped by a settings change
  uint32_t app_drawn_ms_[::espn::kDisplayCount]{};
  // Displays drawn while a logo downloads, by bit
  uint32_t icon_wait_{0};
  bool app_shown_[::espn::kDisplayCount]{};
  ESPPreferenceObject app_pref_[::espn::kDisplayCount];
  static constexpr size_t APP_HOSTS = 8;
  ::espn::AppHost app_hosts_[APP_HOSTS]{};
  size_t app_host_n_{0};

  Schedule schedule_;
  uint32_t schedule_fetched_ms_{0};
  // Soccer: the saved team's reads per competition (as Job::comps), the cup
  // read due next (-1 when none) and the last game seen to finish.
  std::vector<Schedule> comps_;
  int comp_next_{-1};
  std::string done_event_;
  // Refresh Now: re-read the team endpoint on the next cycle, whatever
  // the timestamp says. Cleared once that read succeeds, so a refresh
  // that fails on a flaky network is retried rather than dropped.
  bool force_schedule_{false};
  std::vector<::espn::Upcoming> upcoming_;  // next games after the one on the board
  bool upcoming_due_{false};  // fetch the list on its own cycle, after the board has its game
  // True while the demo build's scripted game owns the board. Always false
  // in release firmware, so the checks on it compile away.
  bool demo_playing_() const {
#ifdef GAMEDAY_DEMO
    return this->demo_kind_ != 0 && !this->demo_gap_;
#else
    return false;
#endif
  }
#ifdef GAMEDAY_DEMO
  // demo.cpp
  struct DemoPrefs {
    uint32_t pro_home, pro_away;          // NFL ESPN ids, 0 = the default pick
    uint32_t college_home, college_away;  // NCAA ESPN ids, 0 = the default pick
    uint8_t madeup;                       // 1 = made-up teams, no logos
    uint8_t running;                      // the demo playing at power off, 0 = none
  } __attribute__((packed));
  ESPPreferenceObject demo_pref_;
  DemoPrefs demo_prefs_{};
  uint8_t demo_kind_{0};    // what is playing, 0 = off
  bool demo_gap_{false};    // between runs: the normal board's turn
  uint8_t demo_resume_{0};  // a demo to pick back up after boot
  uint8_t demo_step_{0};
  uint32_t demo_next_ms_{0};
  GameSnapshot demo_saved_game_, demo_saved_prev_;
  std::string demo_json_{"{}"};  // GET /gameday/demo, under state_mutex_
  void demo_setup_();
  bool demo_loop_();  // true while the scripted game owns the board
  void demo_tick_();
  void demo_show_step_(bool splash);
  void demo_badges_(UpdateFields &f) const;  // made-up teams: badges in the logo slots
  ::espn::DemoMatchup demo_matchup_(uint8_t kind) const;
  uint8_t demo_league_() const;
  void rebuild_demo_json_();
  void handle_demo_(AsyncWebServerRequest *request);
  void apply_demo_(const std::vector<std::pair<std::string, std::string>> &kv);
#endif
  GameSnapshot game_;
  GameSnapshot prev_;
  uint32_t post_since_ms_{0};
  uint32_t next_fetch_ms_{0};
  uint8_t misses_{0};
  ::espn::Backoff fail_run_;
  uint32_t backoff_since_ms_{0};
  uint32_t backoff_ms_{0};      // 0 = no wait running
  // Indexed by Source; ESPN's slot unused (see fail_run_).
  ::espn::HostWait host_wait_[::espn::kSourceCount];
  uint32_t last_good_ms_{0};  // millis() of the last successful game poll, 0 = never
  // ESPN itself answered that the team on the board has no game. Until it
  // has, an empty board after a failed read means ESPN could not be reached.
  bool no_game_confirmed_{false};
  // What an empty board says in place of "No upcoming game" (no_game_text),
  // for the board and for the page and the app. Empty when the board shows
  // a game, or its default or a live mode's words.
  std::string no_game_why_() const;
  // Empty means show the record.
  std::string standing_text_() const;
  uint32_t busy_since_ms_{0};  // millis() when the worker task was started
  // No worker starts for RETRY_INTERVAL after; 0 never.
  uint32_t worker_gave_up_ms_{0};
  // Bumped by the main loop when it abandons a worker, read by the worker.
  std::atomic<uint32_t> worker_seq_{0};
  Job job_;
  uint32_t generation_{0};
  bool busy_{false};
  bool selects_published_{false};
  // Set by the worker once job_ is complete. An atomic store and load (not
  // volatile) order the worker's writes to job_ before the main loop's reads
  // of it on the other core.
  std::atomic<bool> job_done_{false};
  // Written by the fetch task, read by the main loop.
  std::atomic<uint64_t> espn_bytes_{0};
  std::atomic<uint32_t> espn_requests_{0};
  // The last ESPN read through open_() (every read but logos and the probe):
  // its HTTP status (0 for no answer)
  // and millis() when it came, 0 before the first. Fetch task, HTTP task.
  std::atomic<int> espn_status_{0};
  std::atomic<uint32_t> espn_answer_ms_{0};
  // Other hosts by Source: the health block's "mlb", "ht".
  std::atomic<int> host_status_[::espn::kSourceCount]{};
  std::atomic<uint32_t> host_answer_ms_[::espn::kSourceCount]{};

  // Sign-in Wi-Fi (captive.h). The probe runs on its own short task, since
  // it is needed before the clock is set, when no fetch job runs.
  void captive_tick_();
  void set_captive_(bool on);
  void start_probe_();
  static void probe_task_(void *arg);
  bool captive_{false};
  // The probe saw a sign-in page while ESPN reads were still fine: ESPN is
  // asked at once, and captive_ is set only if that read fails.
  bool captive_suspect_{false};
  uint32_t read_fails_{0};        // back_off_() calls this boot, main loop only
  uint32_t suspect_fails_mark_{0};
  bool probe_running_{false};     // main loop: a probe task is out
  bool probe_join_done_{false};   // probed since Wi-Fi last came up
  uint32_t probe_ms_{0};          // millis() the last probe started
  uint32_t probe_join_ms_{0};     // millis() Wi-Fi came up, 0 while it is down
  uint32_t probe_ok_mark_{0};     // espn_ok_reads_ when the last probe started
  std::atomic<const char *> probe_url_{::espn::kProbeUrl};
  // Written by the probe task, read by the main loop once probe_done_ is set.
  std::atomic<bool> probe_done_{false};
  std::atomic<uint8_t> probe_result_{0};
  // ESPN answered 200 over HTTPS (open_), from any task: the network is open,
  // whatever the probe said.
  std::atomic<uint32_t> espn_ok_reads_{0};

  // Redraw at the line's end only if it was shown.
  uint32_t join_addr_ms_{0};
  bool join_addr_shown_{false};

  // Registered first so hotspot phones get Wi-Fi setup. HTTP task.
  class HotspotHandler : public AsyncWebHandler {
   public:
    explicit HotspotHandler(GamedayComponent *parent) : parent_(parent) {}
    bool canHandle(AsyncWebServerRequest *request) const override;
    void handleRequest(AsyncWebServerRequest *request) override;

   protected:
    GamedayComponent *parent_;
  };
  HotspotHandler hotspot_handler_{this};
};

class UpdateTrigger : public Trigger<const UpdateFields &> {
 public:
  explicit UpdateTrigger(GamedayComponent *parent) {
    parent->add_on_update_callback([this](const UpdateFields &f) { this->trigger(f); });
  }
};

// Fires on the main loop for POST /gameday/action?do=<name>.
class ActionTrigger : public Trigger<std::string> {
 public:
  explicit ActionTrigger(GamedayComponent *parent) {
    parent->add_on_action_callback([this](std::string name) { this->trigger(std::move(name)); });
  }
};

}  // namespace gameday
}  // namespace esphome
