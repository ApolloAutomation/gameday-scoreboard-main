#include "gameday.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>
#include <strings.h>
#include <vector>

#include "logo_lru.h"
#include "panel_text.h"
#include "board_view.h"
#include "psram_tasks.h"
#include "timezones.h"
#include "timezones_parsed.h"

#ifdef GAMEDAY_TZ_PARSED
#include "esphome/components/time/posix_tz.h"
#endif
#ifdef USE_UPDATE
#include "esphome/components/update/update_entity.h"
#include <esp_ota_ops.h>
#include <esp_system.h>
#endif
#ifdef USE_GAMEDAY_SAVED_NETWORKS
#include "esphome/components/saved_networks/saved_networks.h"
#endif
#ifdef USE_GAMEDAY_CRASH_REPORT
#include "esphome/components/crash_report/crash_report.h"
#endif
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#include <esp_event.h>
#include <esp_wifi.h>
#endif

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday";

static_assert(::espn::kResetPowerOn == ESP_RST_POWERON && ::espn::kResetSoftware == ESP_RST_SW &&
                  ::espn::kResetPanic == ESP_RST_PANIC && ::espn::kResetWdt == ESP_RST_WDT &&
                  ::espn::kResetBrownout == ESP_RST_BROWNOUT && ::espn::kResetUsb == ESP_RST_USB &&
                  ::espn::kResetPowerGlitch == ESP_RST_PWR_GLITCH && ::espn::kResetCpuLockup == ESP_RST_CPU_LOCKUP,
              "health.h reset reasons no longer match esp_reset_reason_t");

// Survives a restart, a crash and a watchdog (health.h). Keep it the only
// RTC_NOINIT variable in the firmware: one placed before it in link order
// would move it, and the first start after an update would read an invalid
// log and lose the boot count, the last fault and the planned flag.
RTC_NOINIT_ATTR static ::espn::BootLog boot_log;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// For the crash report (components/crash_report): this start is a restart
// the firmware asked for. It sets up before setup() below clears the flag.
bool boot_was_planned() { return ::espn::boot_log_valid(boot_log) && boot_log.planned != 0; }

// curl's, which ESPN's edge accepts (poll_timing.h).
static const char *const USER_AGENT = ::espn::kUserAgent;
// Log names, indexed by Source.
static const char *const HOST_NAMES[::espn::kSourceCount] = {"ESPN", "MLB", "HockeyTech"};

static const uint32_t MINUTE = 60 * 1000;
static const uint32_t SCHEDULE_INTERVAL = 6 * 60 * MINUTE;
static const uint32_t PRE_FAR_INTERVAL = 15 * MINUTE;
static const uint32_t PRE_NEAR_INTERVAL = MINUTE;
// ESPN's day scoreboard changes about every 10 s (its cache-control max-age
// counts down from 10) and its clock once a play, so the single-game read
// (max-age 1) gains little from going faster.
static const uint32_t IN_INTERVAL = 10 * 1000;
static const uint32_t POST_INTERVAL = MINUTE;
static const uint32_t POST_LINGER = 30 * MINUTE;
// For the panel's own trouble (a fetch task that never finished or could not
// start). A failed ESPN read backs off instead (back_off_).
static const uint32_t RETRY_INTERVAL = MINUTE;
// Wi-Fi and the clock are re-checked this often at boot: every second waited
// here is a second of boot screen.
static const uint32_t NOT_READY_INTERVAL = 500;
// With Wi-Fi up and the clock still unset, the time server is asked again this
// often. lwIP's own wait for a reply is 15 s, so a slow answer is never cut off.
static const uint32_t SNTP_ASK_INTERVAL = 15 * 1000;
// A time server usually answers within about a second of Wi-Fi. With none
// this long after (a network that blocks NTP, or a slow one), the clock is set
// from the Date header of an ESPN reply instead, and a failed read is tried
// again this often. Any ESPN URL will do: only the headers are read.
static const uint32_t HTTP_CLOCK_WAIT = 5 * 1000;
static const uint32_t HTTP_CLOCK_RETRY = 30 * 1000;
static const char *const HTTP_CLOCK_URL = "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard";
static const uint32_t NO_LIVE_RESCAN = 2 * MINUTE;
// Install updates automatically: how often loop() looks, and how long a
// manifest read gets before its answer is used (the HTTP timeout is 20 s,
// plus one read from the mirror in a build that configures update_mirror,
// update_mirror.h).
static const uint32_t AUTO_UPDATE_TICK = 10 * 1000;
static const uint32_t AUTO_UPDATE_CHECK_WAIT = 2 * MINUTE;
// After an install, how long the new firmware runs before the update counts
// as taken: past safe_mode's one minute, when the bootloader can no longer
// roll it back.
static const uint32_t UPDATE_GOOD_BOOT = 2 * MINUTE;
// The Wi-Fi signal is read this often; each minute keeps the mean.
static const uint32_t WIFI_SAMPLE_INTERVAL = 10 * 1000;
// A college scan is ~750KB on a Saturday. With nothing live it waits for the
// next kickoff instead of re-reading every 2 minutes, but never longer than
// this, so a moved kickoff or the next day's slate is still picked up.
static const uint32_t NO_GAMES_RESCAN = 3 * 60 * MINUTE;
// How long the loop waits on a worker task before writing it off. A job chains
// at most two requests (schedule then game, or the NFL then the NCAA
// scoreboard). http_request in gameday-common.yaml allows each one 20s, with a
// 30s watchdog_timeout as the hard ceiling, so two requests can legitimately
// spend 60s on the network alone. Add the TLS handshakes and the streamed
// parse of a Saturday NCAA scoreboard and the old 60s deadline was under the
// worst honest case, not over it. 2 x watchdog_timeout for the network plus
// 60s for handshakes and parsing leaves only a genuinely wedged worker here.
static const uint32_t WORKER_DEADLINE = 120 * 1000;
// Days of league scoreboards read to find a team's next game when ESPN's team
// endpoint still names a finished one (leagues without a season list).
static const int LOOKAHEAD_DAYS = 3;
static const uint32_t IDLE_TICK = 1000;
// Idle scores: rescan for finals; failed reads wait SIDE_RETRY.
static const uint32_t SIDE_LIVE_RESCAN = 30 * MINUTE;
static const uint32_t SIDE_RETRY = 5 * MINUTE;
// Back within two big-play stays keeps the scan's timing.
static const uint32_t SIDE_AWAY_RESCAN = 2 * ::espn::kWatchStayMaxSeconds * 1000;
// College days are 750 KB-1.4 MB: need this slack.
static const uint32_t SIDE_COLLEGE_MS = 20 * 1000;
static const uint32_t A2_SPLASH_MS = 4000;

static const char *const MODE_OPTIONS[] = {"My team",       "Live NFL",       "Live college",
                                           "Live anything", "Favorite teams", "Live games"};
// apply_set_ and publish_selects_ index this with any mode parse_set takes.
static_assert(sizeof(MODE_OPTIONS) / sizeof(MODE_OPTIONS[0]) == (size_t) ::espn::kModeCount,
              "MODE_OPTIONS needs a name for every mode parse_set takes");
static const uint32_t FAV_TICK = 1000;  // playlist / lock decisions re-run this often

// Feeds ArduinoJson straight from the HTTP socket so a scoreboard document
// never has to sit in RAM as a whole. Adds what it read to `counter` when done.
class ContainerReader {
 public:
  ContainerReader(std::shared_ptr<http_request::HttpContainer> container, std::atomic<uint64_t> &counter)
      : container_(std::move(container)), counter_(counter) {}
  ~ContainerReader() { this->counter_ += this->total_; }
  ContainerReader(const ContainerReader &) = delete;
  ContainerReader &operator=(const ContainerReader &) = delete;

  int read() {
    if (!this->fill_())
      return -1;
    return static_cast<unsigned char>(this->buf_[this->pos_++]);
  }

  size_t readBytes(char *dst, size_t n) {
    size_t copied = 0;
    while (copied < n) {
      if (!this->fill_())
        break;
      size_t take = std::min(n - copied, this->len_ - this->pos_);
      memcpy(dst + copied, this->buf_ + this->pos_, take);
      this->pos_ += take;
      copied += take;
    }
    return copied;
  }

  size_t total() const { return this->total_; }

 private:
  bool fill_() {
    if (this->pos_ < this->len_)
      return true;
    if (this->eof_)
      return false;
    int n = this->container_->read(this->buf_, sizeof(this->buf_));
    if (n <= 0) {
      this->eof_ = true;
      return false;
    }
    this->pos_ = 0;
    this->len_ = (size_t) n;
    this->total_ += (size_t) n;
    return true;
  }

  std::shared_ptr<http_request::HttpContainer> container_;
  std::atomic<uint64_t> &counter_;
  uint8_t buf_[1024];
  size_t pos_{0};
  size_t len_{0};
  size_t total_{0};
  bool eof_{false};
};

// ---- GamedaySelect ---------------------------------------------------------

void GamedaySelect::control(const std::string &value) {
  this->publish_state(value);
  if (this->parent_ == nullptr)
    return;
  if (this->type_ == SelectType::TEAM)
    this->parent_->select_team(value);
  else if (this->type_ == SelectType::MODE)
    this->parent_->select_mode(value);
  else if (this->type_ == SelectType::FAVORITE)
    this->parent_->select_favorite(this->slot_, value);
  else
    this->parent_->select_timezone(value);
}

// ---- GamedayComponent ------------------------------------------------------

#ifdef USE_PSRAM
// Keep ESPN parsing and /gameday/state in PSRAM because internal RAM is the
// heap that runs out
static json::SpiRamAllocator json_psram;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

#ifdef USE_WIFI
// The reason the connection ended, set on the event task. ESPHome's own
// handler logs it but keeps nothing; wifi_tick_() reads it when the link goes
// down, and the sequence says whether one came since the link was up.
static std::atomic<int> wifi_reason{0};           // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<uint32_t> wifi_reason_seq{0};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *data) {
  // ESPHome marks the link down when its main loop handles this same event,
  // so the one that ended a connection still finds it up. The retries after
  // it (no AP found, a failed sign-in) don't, and keep the first reason.
  auto *w = wifi::global_wifi_component;
  if (w != nullptr && !w->is_connected())
    return;
  wifi_reason.store(((wifi_event_sta_disconnected_t *) data)->reason);
  wifi_reason_seq.fetch_add(1);
}
#endif

void GamedayComponent::setup() {
#ifdef USE_PSRAM
  ::espn::set_json_allocator(&json_psram);
  ::espn::recap_heap() = {[](size_t n) { return psram_alloc(n); }, [](void *p) { heap_caps_free(p); }};
#endif
  this->idle_snaps_.resize(3);  // before anything can draw (side, today, jump)
  // A restart keeps the wall clock, so a crash a moment ago has its time now.
  this->start_reason_ = ::espn::boot_log_start(boot_log, (int) esp_reset_reason(), (uint32_t) App.get_build_time(),
                                               (int64_t) ::time(nullptr));
  ESP_LOGI(TAG, "Started by %s, start %u since the power came on", ::espn::restart_key(this->start_reason_),
           (unsigned) boot_log.boots);
  if (boot_log.fault != 0 && boot_log.fault_boot != boot_log.boots)
    ESP_LOGW(TAG, "Last trouble: %s, %u start(s) ago", ::espn::restart_key((::espn::Restart) boot_log.fault),
             (unsigned) (boot_log.boots - boot_log.fault_boot));
  this->load_power_log_();  // on_boot has normally done it already
#ifdef USE_WIFI
  // About 2 KB: a day of minutes and the last drops. Without PSRAM (or with
  // it full) there is no history and the "wifi" object only has the total.
  auto *rssi = (int8_t *) heap_caps_calloc(::espn::kWifiMinutes, 1, MALLOC_CAP_SPIRAM);
  auto *drops = (::espn::WifiDrop *) heap_caps_calloc(::espn::kWifiDrops, sizeof(::espn::WifiDrop), MALLOC_CAP_SPIRAM);
  if (rssi != nullptr && drops != nullptr) {
    this->wifi_hist_.attach(rssi, drops);
  } else {
    heap_caps_free(rssi);
    heap_caps_free(drops);
  }
  if (esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnected, nullptr,
                                          nullptr) != ESP_OK)
    ESP_LOGW(TAG, "Can't follow Wi-Fi disconnects; drops will show no cause");
#endif
  this->logos_.setup();
  this->pref_ = global_preferences->make_preference<Prefs>(fnv1_hash("gameday_prefs_v1"));
  bool loaded = this->pref_.load(&this->prefs_);
  if (!loaded) {
    // No team until the owner picks one: a fresh panel follows live games
    // (no_team_default), never a team nobody chose.
    this->prefs_.league = (uint8_t) League::NFL;
    this->prefs_.team_id = 0;
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
    this->prefs_.flags = FLAGS_DEFAULT;
  } else if (this->prefs_.team_id != 0 && this->current_team_() == nullptr) {
    this->prefs_.team_id = 0;  // a team that is no longer in the list
  }
  if (this->prefs_.tz_index >= ::espn::kTimezoneCount)
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
  this->pref2_ = global_preferences->make_preference<Prefs2>(fnv1_hash("gameday_prefs2_v1"));
  if (!this->pref2_.load(&this->prefs2_) || this->prefs2_.mode > (uint8_t) Mode::LIVE ||
      !::espn::rotate_minutes_valid(this->prefs2_.rotate_minutes)) {
    this->prefs2_.mode = (uint8_t) Mode::MY_TEAM;
    this->prefs2_.rotate_minutes = ::espn::kRotateDefaultMinutes;
  }
  this->pref5_ = global_preferences->make_preference<Prefs5>(fnv1_hash("gameday_prefs5_v1"));
  Prefs5 p5{};
  if (!this->pref5_.load(&p5) || p5.no_team_ok != 1) {
    // First boot on firmware that knows "no team". Older firmware saved
    // Dallas on first boot; drop it where nobody can have picked it.
    bool live = is_live_mode_(this->prefs2_.mode);
    if (loaded && ::espn::legacy_team_unpicked(this->prefs_.league, this->prefs_.team_id, this->flag_(FLAG_SETUP),
                                               live)) {
      ESP_LOGI(TAG, "Dropping the Dallas default nobody picked");
      this->prefs_.team_id = 0;
      this->save_prefs_();
    }
    p5.no_team_ok = 1;
    this->pref5_.save(&p5);
  }
  this->pref3_ = global_preferences->make_preference<Prefs3>(fnv1_hash("gameday_prefs3_v1"));
  if (!this->pref3_.load(&this->prefs3_))
    this->prefs3_ = Prefs3{};
  this->pref4_ = global_preferences->make_preference<Prefs4>(fnv1_hash("gameday_prefs4_v1"));
  if (!this->pref4_.load(&this->prefs4_) || this->prefs4_.lockon_minutes < 5 || this->prefs4_.lockon_minutes > 120 ||
      this->prefs4_.release_seconds < 30 || this->prefs4_.release_seconds > 3600 || this->prefs4_.collide > 1) {
    this->prefs4_.lockon_minutes = 15;
    this->prefs4_.release_seconds = 30;
    // With several sports, two favorites playing at once is common: a new
    // panel alternates. One that was set up before keeps sticking.
    this->prefs4_.collide = this->flag_(FLAG_SETUP) ? 0 : 1;
    // Saved now: the choice depends on FLAG_SETUP, which the first team pick
    // sets, so recomputing it at the next boot would flip it.
    this->pref4_.save(&this->prefs4_);
  }
  this->pref7_ = global_preferences->make_preference<Prefs7>(fnv1_hash("gameday_prefs7_v1"));
  if (!this->pref7_.load(&this->prefs7_)) {
    this->prefs7_ = Prefs7{};
    // Only today's games in the playlist, for new panels; an existing one
    // keeps cycling every favorite until its owner turns this on.
    if (!this->flag_(FLAG_SETUP))
      this->prefs7_.flags |= FAV7_TODAY;
    this->pref7_.save(&this->prefs7_);  // same reason as Prefs4 above
  }
  this->pref8_ = global_preferences->make_preference<Prefs8>(fnv1_hash("gameday_prefs8_v1"));
  if (!this->pref8_.load(&this->prefs8_)) {
    // Firmware before this had one switch for every sport: start baseball
    // and soccer where football's is, so an update changes nothing.
    this->prefs8_ = Prefs8{};
    if (this->flag_(FLAG_DOWN))
      this->prefs8_.ticker = TICK8_COUNT | TICK8_MATCH;
    this->pref8_.save(&this->prefs8_);
  }
  this->pref9_ = global_preferences->make_preference<Prefs9>(fnv1_hash("gameday_prefs9_v1"));
  if (!this->pref9_.load(&this->prefs9_))
    this->prefs9_ = Prefs9{};  // every alert on, every game, other games' big plays switch
  if (this->prefs9_.stay != ::espn::clamp_watch_stay_seconds(this->prefs9_.stay * 10) / 10)
    this->prefs9_.stay = 0;
  this->pref6_ = global_preferences->make_preference<Prefs6>(fnv1_hash("gameday_prefs6_v1"));
  if (!this->pref6_.load(&this->prefs6_) || this->prefs6_.auto_update > 1)
    this->prefs6_ = Prefs6{};
  // Unsaved until changed: missing blob means defaults each boot.
  this->idle_pref_ = global_preferences->make_preference<IdlePrefs>(fnv1_hash("gameday_idle_v1"));
  if (!this->idle_pref_.load(&this->idle_prefs_) || this->idle_prefs_.idle >= ::espn::kIdleCount ||
      this->idle_prefs_.night_on > 1 || this->idle_prefs_.night_bri < ::espn::kNightBriMin ||
      this->idle_prefs_.night_start >= ::espn::kDayMinutes || this->idle_prefs_.night_end >= ::espn::kDayMinutes) {
    this->idle_prefs_ = IdlePrefs{};
    this->idle_prefs_.night_bri = ::espn::kNightBrightnessDefault;
    this->idle_prefs_.night_start = ::espn::kNightStartDefault;
    this->idle_prefs_.night_end = ::espn::kNightEndDefault;
  }
  this->split_pref_ = global_preferences->make_preference<SplitPrefs>(fnv1_hash("gameday_split_v1"));
  if (!this->split_pref_.load(&this->split_prefs_) || this->split_prefs_.show >= ::espn::kSplitCount)
    this->split_prefs_ = SplitPrefs{};
  this->a2_set_team_();
  // Own blob, so no other setting moves.
  this->bets_pref_ = global_preferences->make_preference<::espn::BetBlob>(fnv1_hash("gameday_bets_v1"));
  if (!this->bets_pref_.load(&this->bets_.blob))
    this->bets_.blob = ::espn::BetBlob{};
  this->bets_.loaded();
  this->players_pref_ = global_preferences->make_preference<::espn::PlayerBlob>(fnv1_hash("gameday_players_v1"));
  if (!this->players_pref_.load(&this->players_))
    this->players_ = ::espn::PlayerBlob{};
  for (auto &p : this->players_.players)
    if (!::espn::player_empty(p) && !::espn::player_valid(p))
      p = ::espn::FavPlayer{};
#ifdef GAMEDAY_BETA_CHANNEL
  // Before the update entity's first check, which waits for the main loop.
  this->beta_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("gameday_beta_v1"));
  if (!this->beta_pref_.load(&this->follow_beta_) || this->follow_beta_ > 1)
    this->follow_beta_ = 0;
  this->apply_channel_();
#endif
  this->settle_update_mark_();
#ifdef GAMEDAY_UPDATE_MIRROR
  // Any state the update entity publishes while it points at the mirror is
  // the mirror's answer (a failed read publishes nothing; mirror_tick_).
  if (this->mirror_update_ != nullptr)
    this->mirror_update_->add_on_state_callback([this]() {
      if (this->on_mirror_)
        this->mirror_answered_ = true;
    });
#endif
#ifdef GAMEDAY_BAR
  this->bar_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("gameday_bar_now_v1"));
  if (!this->bar_pref_.load(&this->now_tried_))
    this->now_tried_ = 0;
#endif
  this->team_cache_pref_ =
      global_preferences->make_preference<::espn::TeamCache>(fnv1_hash("gameday_team_cache_v1"));
  this->rebuild_favorites_(false);
  this->apply_timezone_();
  if (this->time_ != nullptr)
    this->time_->add_on_time_sync_callback([this]() {
      if (!this->clock_approx_)
        return;
      ESP_LOGI(TAG, "Time server answered: clock no longer approximate");
      this->clock_approx_ = false;
      this->rebuild_state_(&this->last_fields_);  // drops clock_approx
    });
  this->publish_selects_();
#ifdef GAMEDAY_DEMO
  this->demo_setup_();
#endif
  this->rebuild_state_(nullptr);
  if (this->base_ != nullptr)
    this->base_->add_handler(this);
  this->schedule_next_(3000);
  // The page laid out before the split setting loaded.
  this->defer([this]() { this->fire_action_("layout"); });
}

std::string GamedayComponent::hostname() const { return std::string(App.get_name().c_str()); }

uint8_t GamedayComponent::fav_league_(uint8_t i) const {
  return i < 4 ? this->prefs3_.fav_league[i] : i < FAV_MAX ? this->prefs7_.fav_league[i - 4] : 0;
}

uint32_t GamedayComponent::fav_id_(uint8_t i) const {
  return i < 4 ? this->prefs3_.fav_id[i] : i < FAV_MAX ? this->prefs7_.fav_id[i - 4] : 0;
}

void GamedayComponent::put_fav_(uint8_t i, uint8_t league, uint32_t id) {
  if (i < 4) {
    this->prefs3_.fav_league[i] = league;
    this->prefs3_.fav_id[i] = id;
  } else if (i < FAV_MAX) {
    this->prefs7_.fav_league[i - 4] = league;
    this->prefs7_.fav_id[i - 4] = id;
  }
}

bool GamedayComponent::no_team_default() const {
  if (this->current_team_() != nullptr)
    return false;
  return this->prefs2_.mode == (uint8_t) Mode::MY_TEAM ||
         (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->fav_.empty());
}

std::string GamedayComponent::favorite_option_(uint8_t slot) const {
  if (slot < 1 || slot > FAV_MAX || this->fav_id_(slot - 1) == 0)
    return "None";
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == this->fav_league_(slot - 1) && t.espn_id == this->fav_id_(slot - 1))
      return ::espn::team_option(t);
  }
  return "None";
}

void GamedayComponent::select_favorite(uint8_t slot, const std::string &option) {
  if (slot < 1 || slot > FAV_MAX)
    return;
  uint8_t league = 0;
  uint32_t id = 0;
  if (option != "None") {
    for (size_t i = 0; i < ::espn::kTeamCount; i++) {
      const auto &t = ::espn::kTeams[i];
      if (::espn::team_option(t) == option) {
        league = (uint8_t) t.league;
        id = t.espn_id;
        break;
      }
    }
    if (id == 0) {
      ESP_LOGW(TAG, "Unknown favorite '%s'", option.c_str());
      return;
    }
  }
  this->set_favorite_(slot, league, id);
}

void GamedayComponent::set_favorite_(uint8_t slot, uint8_t league, uint32_t id) {
  if (slot < 1 || slot > FAV_MAX)
    return;
  std::vector<League> live_before = this->live_leagues_();
  this->put_fav_(slot - 1, league, id);
  if (slot <= 4)
    this->pref3_.save(&this->prefs3_);
  else
    this->pref7_.save(&this->prefs7_);
  bool first_pick = id != 0 && this->mark_setup_done_();
  ESP_LOGI(TAG, "Favorite %u: %s", (unsigned) slot, this->favorite_option_(slot).c_str());
  if (slot <= 4 && this->favorite_selects_[slot - 1] != nullptr)
    this->favorite_selects_[slot - 1]->publish_state(this->favorite_option_(slot));
  this->favorites_changed_(first_pick, live_before);
}

void GamedayComponent::set_favorites_(const std::vector<std::pair<uint8_t, uint32_t>> &list) {
  std::vector<League> live_before = this->live_leagues_();
  bool any = false;
  for (uint8_t i = 0; i < FAV_MAX; i++) {
    bool set = i < list.size();
    this->put_fav_(i, set ? list[i].first : 0, set ? list[i].second : 0);
    any = any || (set && list[i].second != 0);
  }
  this->pref3_.save(&this->prefs3_);
  this->pref7_.save(&this->prefs7_);
  bool first_pick = any && this->mark_setup_done_();
  ESP_LOGI(TAG, "Favorites: %u team(s)", (unsigned) list.size());
  for (uint8_t slot = 1; slot <= 4; slot++)
    if (this->favorite_selects_[slot - 1] != nullptr)
      this->favorite_selects_[slot - 1]->publish_state(this->favorite_option_(slot));
  this->favorites_changed_(first_pick, live_before);
}

void GamedayComponent::favorites_changed_(bool first_pick, const std::vector<League> &live_before) {
  bool was_fav_mode = this->favorites_mode_();
  this->rebuild_favorites_(true);
  this->follow_ = ::espn::FollowPin{};  // a new list: back to the mode's own pick
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->idle_snaps_.size() >= 2)
    this->today_final_() = GameSnapshot{};  // it may be a team no longer on the list
  // Idle scores follow the favorites' leagues.
  this->scan_due_now_();
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && (was_fav_mode || this->favorites_mode_())) {
    // The list changed under the mode: drop the board and let the next
    // cycle refresh what is missing and re-pick.
    this->drop_visit_();
    this->generation_++;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->no_game_confirmed_ = false;  // ESPN has not answered for the new list
    this->emit_({});
    this->end_backoff_wait_();
    this->schedule_next_(0);
    return;
  }
  // Live games on auto reads the favorites' leagues too.
  if (this->live_leagues_changed_(live_before))
    return;
  if (first_pick)
    this->emit_({});  // redraw the ticker without the setup prompt
  else
    this->rebuild_state_(&this->last_fields_);
}

#ifndef GAMEDAY_DEMO
void GamedayComponent::remote_number(uint8_t n) { this->press_favorite(n); }
#endif

void GamedayComponent::press_favorite(uint8_t slot) {
  std::string option = this->favorite_option_(slot);
  if (option == "None") {
    ESP_LOGI(TAG, "Favorite %u is empty", (unsigned) slot);
    return;
  }
  ESP_LOGI(TAG, "Remote button %u: %s", (unsigned) slot, option.c_str());
  if (this->favorites_mode_()) {
    // Pinning a favorite overrides Follow and ends any visit.
    this->drop_visit_();
    for (size_t i = 0; i < this->fav_.size(); i++) {
      if (this->fav_[i].slot != slot)
        continue;
      this->follow_ = ::espn::FollowPin{};
      this->fav_pinned_ = (int) i;
      this->fav_shown_ = (int) i;
      this->fav_shown_since_ = (int64_t) this->time_->timestamp_now();
      this->fav_poll_ms_ = 0;
      this->prev_ = GameSnapshot{};
      this->game_ = this->fav_[i].game;
      this->post_since_ms_ = 0;
      this->emit_({});
      this->end_backoff_wait_();
      this->schedule_next_(0);
      return;
    }
    return;
  }
  if (this->prefs2_.mode != (uint8_t) Mode::MY_TEAM) {
    this->prefs2_.mode = (uint8_t) Mode::MY_TEAM;
    this->pref2_.save(&this->prefs2_);
    if (this->mode_select_ != nullptr)
      this->mode_select_->publish_state(MODE_OPTIONS[0]);
    this->reset_game_();
    this->generation_++;
    this->schedule_next_(0);
  }
  this->publish_team_select_(option);
  this->select_team(option);
}

// Men's college basketball teams are not among the select's options
// (__init__.py), so the select keeps its last team while one is followed.
void GamedayComponent::publish_team_select_(const std::string &option) {
  if (this->team_select_ != nullptr && this->team_select_->has_option(option))
    this->team_select_->publish_state(option);
}

// Push the stored choices into the two selects. Runs at setup and once more
// after the network is up, so the web page and the API always see them.
void GamedayComponent::publish_selects_() {
  std::string team = this->team_option_();
  ESP_LOGI(TAG, "Publishing selects: team '%s', timezone '%s'", team.c_str(),
           ::espn::kTimezones[this->prefs_.tz_index].name);
  this->publish_team_select_(team);
  if (this->timezone_select_ != nullptr)
    this->timezone_select_->publish_state(::espn::kTimezones[this->prefs_.tz_index].name);
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(MODE_OPTIONS[this->prefs2_.mode]);
  for (uint8_t slot = 1; slot <= 4; slot++)
    if (this->favorite_selects_[slot - 1] != nullptr)
      this->favorite_selects_[slot - 1]->publish_state(this->favorite_option_(slot));
}

void GamedayComponent::select_mode(const std::string &option) {
  for (uint8_t i = 0; i <= (uint8_t) Mode::LIVE; i++) {
    if (option != MODE_OPTIONS[i])
      continue;
    if (i == this->prefs2_.mode) {
      if (this->mark_setup_done_())
        this->emit_({});  // redraw the ticker without the setup prompt
      return;
    }
    this->prefs2_.mode = i;
    this->pref2_.save(&this->prefs2_);
    ESP_LOGI(TAG, "Mode: %s", option.c_str());
    // The new mode's first scoreboard, not the mode switch itself, ends the
    // boot screen hold (mirrors apply_team_).
    this->board_ready_ = false;
    this->board_pending_ms_ = millis() == 0 ? 1 : millis();
    this->mark_setup_done_();
    if (this->mode_select_ != nullptr)
      this->mode_select_->publish_state(option);
    this->restart_board_();
    return;
  }
  ESP_LOGW(TAG, "Unknown mode '%s'", option.c_str());
}

void GamedayComponent::set_rotate_seconds(int seconds) {
  seconds = ::espn::clamp_rotate_seconds(seconds);
  if (seconds == this->rotate_seconds())
    return;
  // 30 s is Prefs8's half flag beside 1 minute.
  const uint8_t minutes = (uint8_t) ::espn::rotate_minutes_of(seconds);
  const uint8_t half = seconds < 60 ? 1 : 0;
  if (minutes != this->prefs2_.rotate_minutes) {
    this->prefs2_.rotate_minutes = minutes;
    this->pref2_.save(&this->prefs2_);
  }
  if (half != this->prefs8_.rotate_half) {
    this->prefs8_.rotate_half = half;
    this->pref8_.save(&this->prefs8_);
  }
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_lockon_minutes(int minutes) {
  minutes = ::espn::clamp_lockon_minutes(minutes);
  if (minutes == this->prefs4_.lockon_minutes)
    return;
  this->prefs4_.lockon_minutes = (uint8_t) minutes;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_release_seconds(int seconds) {
  seconds = ::espn::clamp_release_seconds(seconds);
  if (seconds == this->prefs4_.release_seconds)
    return;
  this->prefs4_.release_seconds = (uint16_t) seconds;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_today_only(bool on) {
  if (on == this->today_only())
    return;
  this->prefs7_.flags = on ? (this->prefs7_.flags | FAV7_TODAY) : (this->prefs7_.flags & ~FAV7_TODAY);
  this->pref7_.save(&this->prefs7_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_live_leagues(const std::string &keys) { this->set_live_mask_(::espn::live_mask_from_keys(keys)); }

void GamedayComponent::set_live_mask_(uint16_t mask) {
  if (mask == this->prefs7_.live_mask)
    return;
  std::vector<League> before = this->live_leagues_();
  this->prefs7_.live_mask = mask;
  this->pref7_.save(&this->prefs7_);
  this->follow_ = ::espn::FollowPin{};  // other leagues: back to switching games
  this->scan_due_now_();  // the idle display's scores read these leagues too
  if (!this->live_leagues_changed_(before))
    this->rebuild_state_(&this->last_fields_);
}

std::vector<League> GamedayComponent::live_leagues_() const {
  std::vector<uint8_t> fav_leagues;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      fav_leagues.push_back(this->fav_league_(i));
  return ::espn::live_leagues(this->prefs7_.live_mask, this->prefs_.league, fav_leagues);
}

void GamedayComponent::set_collision_alternate(bool alternate) {
  if ((alternate ? 1 : 0) == this->prefs4_.collide)
    return;
  this->prefs4_.collide = alternate ? 1 : 0;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

// ---- Favorite Teams mode ------------------------------------------------------

// Rebuilds the entry list from the four slots. keep_cards carries a slot's
// fetched game over when the same team is still in the list (reordering).
void GamedayComponent::rebuild_favorites_(bool keep_cards) {
  FavList old = std::move(this->fav_);
  this->fav_.clear();
  for (uint8_t i = 0; i < FAV_MAX; i++) {
    if (this->fav_id_(i) == 0)
      continue;
    const ::espn::Team *team = nullptr;
    for (size_t t = 0; t < ::espn::kTeamCount; t++) {
      if ((uint8_t) ::espn::kTeams[t].league == this->fav_league_(i) &&
          ::espn::kTeams[t].espn_id == this->fav_id_(i)) {
        team = &::espn::kTeams[t];
        break;
      }
    }
    if (team == nullptr)
      continue;
    FavEntry e;
    e.team = team;
    e.slot = i + 1;
    if (keep_cards) {
      for (auto &o : old) {
        if (o.team == team) {
          e = o;
          e.slot = i + 1;
          break;
        }
      }
    }
    this->fav_.push_back(e);
  }
  this->fav_shown_ = -1;
  this->fav_pinned_ = -1;
  this->fav_locked_ = false;
  this->fav_poll_ms_ = 0;
}

::espn::FavRules GamedayComponent::fav_rules_() const {
  ::espn::FavRules r;
  r.lockon_s = (int64_t) this->prefs4_.lockon_minutes * 60;
  r.release_s = this->prefs4_.release_seconds;
  r.alternate = this->prefs4_.collide == 1;
  r.rotate_s = this->rotate_seconds();
  r.dwell_s = 10;
  r.today_only = this->today_only();
  if (this->time_ != nullptr) {
    ESPTime t = this->time_->now();
    if (t.is_valid()) {
      r.day_start = (int64_t) t.timestamp - (t.hour * 3600 + t.minute * 60 + t.second);
      r.day_end = r.day_start + 24 * 3600;
    }
  }
  return r;
}

std::vector<::espn::FavGame> GamedayComponent::fav_games_() const {
  std::vector<::espn::FavGame> out;
  for (const auto &e : this->fav_) {
    ::espn::FavGame g;
    g.valid = e.game.valid;
    g.state = e.game.state;
    g.kickoff_epoch = e.game.kickoff_epoch;
    g.time_tbd = e.game.time_tbd;
    g.final_epoch = e.final_epoch;
    // Out of season, a daily sport's next game is weeks away. Football has no
    // horizon: a bye week must not take a team out of the playlist.
    g.horizon_s = ::espn::season_horizon(::espn::league_sport(e.team->league));
    // The second area shows this team; skip it here.
    if (this->a2_show_() == ::espn::SplitShow::TEAM && !this->a2_team_.empty() && this->a2_team_[0].team == e.team)
      g.valid = false;
    out.push_back(g);
  }
  return out;
}

// Runs the pick and puts the chosen entry's card on the board when it differs
// from what is up. Returns true on a switch.
bool GamedayComponent::fav_choose_(int64_t now_epoch) {
  if (this->fav_shown_ >= 0 && this->following_()) {
    // Follow holds the favorite; its lock still sets polling.
    bool was_locked = this->fav_locked_;
    this->fav_locked_ = ::espn::fav_locked(this->fav_games_()[this->fav_shown_], now_epoch, this->fav_rules_());
    if (was_locked != this->fav_locked_)
      this->fav_poll_ms_ = 0;
    return false;
  }
  ::espn::FavChoice c = ::espn::pick_favorite(this->fav_games_(), now_epoch, this->fav_rules_(), this->fav_shown_,
                                              this->fav_shown_since_, this->fav_pinned_);
  bool was_locked = this->fav_locked_;
  this->fav_locked_ = c.locked;
  if (this->fav_pinned_ >= 0 && !c.locked && c.index != this->fav_pinned_)
    this->fav_pinned_ = -1;  // the playlist moved past the pinned card
  if (c.index == this->fav_shown_) {
    if (was_locked != c.locked)
      this->fav_poll_ms_ = 0;
    return false;
  }
  this->fav_shown_ = c.index;
  this->fav_shown_since_ = now_epoch;
  this->fav_poll_ms_ = 0;
  this->prev_ = GameSnapshot{};
  this->visit_back_ = false;
  this->game_ = c.index >= 0 ? this->fav_[c.index].game : GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
  if (c.index >= 0)
    ESP_LOGI(TAG, "Favorites: showing %s%s", this->fav_[c.index].team->abbr, c.locked ? " (locked)" : "");
  this->emit_({});
  if (this->fav_shown_ >= 0 && this->game_.valid)
    this->mark_board_ready_();  // a kept card counts as the first board too
  return true;
}

// The board's game, except in the Favorites idle playlist, where the card on
// the board changes every 10 s and Home Assistant would log each one.
const GameSnapshot &GamedayComponent::ha_game_() const {
  if (this->visit_.active && this->visit_.saved)
    return *this->visit_.saved;
  if (!this->favorites_mode_() || this->fav_locked_ || this->follow_.on || this->demo_playing_())
    return this->game_;
  int i = ::espn::followed_favorite(this->fav_games_());
  return i >= 0 ? this->fav_[i].game : this->game_;
}

// Main loop: refresh a stale entry, else poll the locked game when due, else tick.
void GamedayComponent::start_fav_job_(uint32_t now) {
  Job &j = this->job_;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  ::espn::FavRules rules = this->fav_rules_();
  for (auto &e : this->fav_) {
    if (e.game.valid && e.game.state == GameState::POST && e.final_epoch != 0 && !e.stale &&
        tnow - e.final_epoch >= rules.release_s &&
        !(e.game.event_id == e.stuck_event && now - e.stuck_ms < e.stuck_for_ms))
      e.stale = true;  // released: the team endpoint now points at the next game
  }
  int refresh = -1;
  for (size_t i = 0; i < this->fav_.size(); i++) {
    const FavEntry &e = this->fav_[i];
    // Its host is backing off; other favorites go on.
    if (this->host_waiting_(e.team->league))
      continue;
    bool missing = !e.sched.valid || e.stale;
    uint32_t age = now - e.fetched_ms;
    if ((missing && (e.fetched_ms == 0 || age >= RETRY_INTERVAL)) || (!missing && age >= SCHEDULE_INTERVAL)) {
      refresh = (int) i;
      break;
    }
  }
  bool forced = false;
  if (this->force_schedule_) {
    // Refresh Now in Favorites mode. Re-read the team that is on the board,
    // since that is the one the owner is looking at, and it is one fetch
    // rather than the whole list. The others keep their own six hour cycle.
    // Spend the flag here, not on success: a failed favorites fetch
    // reschedules at once, so a flag left set would retry in a tight loop.
    this->force_schedule_ = false;
    forced = true;
    if (this->fav_shown_ >= 0)
      refresh = this->fav_shown_;
    else if (!this->fav_.empty())
      refresh = 0;
  }
  this->fav_choose_(tnow);
  if (this->fav_locked_ && this->fav_shown_ >= 0 && !forced) {
    uint32_t due = this->interval_for_phase_();
    // A live game's poll goes first; a waiting refresh runs in the gap before
    // the next one, so a long list of favorites never stalls the score.
    if ((this->fav_poll_ms_ == 0 || now - this->fav_poll_ms_ >= due) &&
        !this->host_waiting_(this->fav_[this->fav_shown_].team->league))
      refresh = -2;  // poll the shown game
  }
  // Soccer: with nothing else due, one cup's team read per cycle.
  int cup = -1;
  for (size_t i = 0; refresh == -1 && cup < 0 && i < this->fav_.size(); i++) {
    const FavEntry &c = this->fav_[i];
    if (c.comp_next > 0 && c.comp_next < (int) c.comps.size() && !this->host_waiting_(c.team->league))
      cup = (int) i;
  }
  if (refresh == -1 && cup < 0) {
    // Most ticks read nothing, leaving gaps for side reads.
    this->schedule_next_(FAV_TICK);
    return;
  }
  int index = cup >= 0 ? cup : refresh >= 0 ? refresh : this->fav_shown_;
  FavEntry &e = this->fav_[index];
  this->new_job_();
  j.players = this->players_;
  j.team = e.team;
  j.our_id = e.team->espn_id;
  j.fav_index = index;
  j.need_schedule = refresh >= 0;
  j.schedule = e.sched;
  if (j.need_schedule) {
    j.today_since = this->today_since_();  // My team today: a favorite's final today
    j.today_have = this->today_have_();
  }
  size_t cups = ::espn::league_cup_count(e.team->league);
  if ((refresh >= 0 || cup >= 0) && cups > 0) {
    j.comps = e.comps;
    j.comps.resize(cups + 1);
  }
  if ((refresh >= 0 || cup >= 0) && e.game.valid && e.game.state == GameState::POST)
    j.done_event = e.game.event_id;
  if (cup >= 0) {
    j.comp = e.comp_next;
    j.need_game = e.game.event_id.empty() || e.game.event_id != e.sched.event_id;
  }
  if (refresh >= 0)
    e.fetched_ms = now == 0 ? 1 : now;
  this->start_worker_();
}

// Favorite teams ticks each second; fit gaps to real reads.
uint32_t GamedayComponent::fav_gap_due_(uint32_t now) const {
  // Overdue reads of waiting hosts must not starve gaps.
  uint32_t refresh_at = now + SCHEDULE_INTERVAL;
  for (const auto &e : this->fav_) {
    const bool missing = !e.sched.valid || e.stale;
    const uint32_t at = ::espn::watch_after_wait(
        missing && e.fetched_ms == 0 ? now : e.fetched_ms + (missing ? RETRY_INTERVAL : SCHEDULE_INTERVAL), now,
        this->host_left_(e.team->league, now));
    if ((int32_t) (at - refresh_at) < 0)
      refresh_at = at;
  }
  const bool locked = this->fav_locked_ && this->fav_shown_ >= 0;
  return ::espn::watch_fav_due(now, locked, this->fav_poll_ms_, this->interval_for_phase_(), refresh_at,
                               locked ? this->host_left_(this->fav_[this->fav_shown_].team->league, now) : 0);
}

// A team read (the league's or a cup's) that named no game: forget the one
// the card had and let another favorite take the board.
void GamedayComponent::fav_no_event_(FavEntry &e, bool shown, int64_t now_epoch) {
  ESP_LOGI(TAG, "No upcoming game for %s", e.team->abbr);
  this->no_game_confirmed_ = true;
  e.game = GameSnapshot{};
  e.final_epoch = 0;
  if (shown)
    this->game_ = GameSnapshot{};
  bool switched = this->fav_choose_(now_epoch);
  if (!switched && this->fav_shown_ < 0) {
    // No favorite has a card: once the last one is read, the off-season
    // clock's turn, and the boot screen can stop waiting for a game.
    this->emit_({});
    if (this->last_fields_.clock)
      this->mark_board_ready_();
  } else if (!switched) {
    this->rebuild_state_(&this->last_fields_);
  }
  this->schedule_next_(0);
}

void GamedayComponent::apply_fav_job_(uint32_t now) {
  Job &j = this->job_;
  if (j.fav_index < 0 || j.fav_index >= (int) this->fav_.size() || this->fav_[j.fav_index].team != j.team) {
    this->schedule_next_(0);  // the list changed while the worker ran
    return;
  }
  FavEntry &e = this->fav_[j.fav_index];
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  bool shown = j.fav_index == this->fav_shown_;
  // A cup read that did not poll the game leaves the poll timer alone.
  if (shown && (j.comp < 0 || j.polled))
    this->fav_poll_ms_ = now == 0 ? 1 : now;
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->misses_ = ::espn::add_miss(this->misses_);
      this->emit_({});  // the miss shows on the ticker and the page, as in My team
      this->back_off_();  // start_fav_job_ waits it out and keeps the playlist ticking
      this->schedule_next_(0);
      return;
    }
    this->clear_backoff_();
    e.sched = j.schedule;
    ::espn::adopt_league(e.sched, e.team->league);
    e.stale = false;
    if (j.today_final != nullptr)
      this->note_final_(*j.today_final);  // My team today, after the look-ahead moved on
    if (!j.comps.empty()) {
      e.comps = j.comps;
      e.comp_next = 1;
    }
    if (j.no_event) {
      this->fav_no_event_(e, shown, tnow);
      return;
    }
  }
  if (j.comp > 0) {
    e.comps = j.comps;
    e.comp_next = j.comp + 1 < (int) j.comps.size() ? j.comp + 1 : -1;
    e.sched = j.schedule;
    // A failed poll backs off below with the other failed polls
    if (!j.polled && ::espn::cup_read_backs_off(j.cup_ok, false, false, j.throttled))
      this->back_off_();  // start_fav_job_ waits it out
    if (j.no_event) {
      this->fav_no_event_(e, shown, tnow);
      return;
    }
    if (!j.polled) {
      this->schedule_next_(0);
      return;
    }
  }
  if (!j.game_ok) {
    this->misses_ = ::espn::add_miss(this->misses_);
    this->emit_({});
    this->back_off_();
    this->schedule_next_(0);
    return;
  }
  GameSnapshot old = e.game;
  ::espn::carry_football(old, j.game);
  e.game = j.game;
  // A refresh that came back with the same final backs off instead of
  // retrying every minute: six hours when the look-ahead found no game in the
  // coming days, 15 minutes when a read failed or the league does not look
  // ahead (soccer favorites). Football keeps its old behavior.
  if (j.need_schedule && old.valid && old.event_id == e.game.event_id && e.game.state == GameState::POST &&
      ::espn::league_sport(e.team->league) != ::espn::Sport::FOOTBALL) {
    e.stuck_event = e.game.event_id;
    e.stuck_ms = now == 0 ? 1 : now;
    e.stuck_for_ms = j.lookahead == 2 ? SCHEDULE_INTERVAL : 15 * MINUTE;
  }
  if (!old.valid || old.event_id != e.game.event_id)
    e.final_epoch = 0;
  if (e.game.state == GameState::POST && old.valid && old.event_id == e.game.event_id && old.state != GameState::POST)
    e.final_epoch = tnow;
  this->note_final_(e.game);
  this->mark_good_poll_();
  this->bets_tick_();  // a bet on this favorite's game
  bool switched = this->fav_choose_(tnow);
  if (!switched && shown) {
    // Same game on the board: render the new poll with its splash.
    this->prev_ = this->board_prev_(e.game);
    this->game_ = e.game;
    this->emit_(this->board_splash_(false));
  } else if (!switched) {
    this->rebuild_state_(&this->last_fields_);  // the page's list of favorites' games
  }
  if (this->fav_shown_ >= 0 && this->game_.valid)
    this->mark_board_ready_();
  this->schedule_next_(0);
}

// ---- Follow this game ---------------------------------------------------------

// My team, ended games and fallback cards can't follow.
void GamedayComponent::set_follow(bool on) {
  if (!on) {
    if (!this->follow_.on)
      return;
    ESP_LOGI(TAG, "Follow this game: off");
    this->follow_ = ::espn::FollowPin{};
    this->rebuild_state_(&this->last_fields_);
    return;
  }
  // During a visit, follow the mode's own game instead.
  const GameSnapshot &g = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  if (!::espn::follow_start(this->follow_, ::espn::follow_mode_ok(this->prefs2_.mode, !this->fav_.empty()),
                            this->follow_picked_(), g, (int64_t) this->time_->timestamp_now())) {
    ESP_LOGW(TAG, "Follow this game: no game to follow in this mode, or it has ended");
    return;
  }
  ESP_LOGI(TAG, "Follow this game: %s vs %s (event %s)", g.team_abbr.c_str(), g.opp_abbr.c_str(), g.event_id.c_str());
  if (this->visit_.active)
    this->end_visit_();  // emits the mode's board, with the page's state
  else
    this->rebuild_state_(&this->last_fields_);
}

bool GamedayComponent::follow_picked_() const {
  const bool picked = this->favorites_mode_() ? this->fav_shown_ >= 0
                                              : this->live_mode_() && !this->live_fallback_ && !this->recap_on_;
  return picked && !this->demo_playing_();
}

bool GamedayComponent::follow_can_start_() const {
  return ::espn::follow_can_start(this->follow_, ::espn::follow_mode_ok(this->prefs2_.mode, !this->fav_.empty()),
                                  this->follow_picked_(),
                                  ::espn::watch_mode_game(this->game_, this->visit_.saved.get()));
}

// Unreadable or never-ending games release the pin too.
bool GamedayComponent::following_() {
  if (!this->follow_.on)
    return false;
  bool fav = this->favorites_mode_();
  int64_t after_final = ::espn::follow_after_final_s(fav, this->prefs4_.release_seconds);
  if (::espn::follow_holds(this->follow_, ::espn::watch_mode_game(this->game_, this->visit_.saved.get()),
                           (int64_t) this->time_->timestamp_now(), after_final, this->stale_seconds_()))
    return true;
  ESP_LOGI(TAG, "Follow this game: over, back to %s", fav ? "the favorites" : "switching games");
  this->rebuild_state_(&this->last_fields_);
  return false;
}

// ---- Manual bets -----------------------------------------------------------------

// Slot 0 picks the first empty, so pages never collide.
void GamedayComponent::set_bet_(uint8_t slot, ::espn::Bet b) {
  b.placed = this->clock_set_() ? (uint32_t) this->time_->timestamp_now() : 0;
  uint8_t at = ::espn::bet_put(this->bets_.blob.bets, slot, b);
  if (at == 0) {
    ESP_LOGW(TAG, "Bet %s %s ignored: all %d bet slots are in use", ::espn::league_key((League) b.league),
             ::espn::bet_label(b).c_str(), ::espn::kBetSlots);
    return;
  }
  this->bets_.changed();
  ESP_LOGI(TAG, "Bet %u: %s %s", (unsigned) at, ::espn::league_key((League) b.league),
           ::espn::bet_label(b).c_str());
  this->bets_tick_();
  if (this->game_.valid)
    this->emit_({});  // the ticker gets it now
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::remove_bet_(uint8_t slot) {
  if (slot < 1 || slot > ::espn::kBetSlots || ::espn::bet_empty(this->bets_.at(slot - 1)))
    return;
  ESP_LOGI(TAG, "Bet %u removed", (unsigned) slot);
  this->bets_.at(slot - 1) = ::espn::Bet{};
  this->bets_.changed();
  this->bets_tick_();
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

// ---- Favorite players ---------------------------------------------------------

// Stats and plays arrive with the league's next game read.
void GamedayComponent::set_player_(const ::espn::SetOp &op) {
  const ::espn::FavPlayer p = ::espn::make_player(op.team.league, op.athlete, op.team.id, op.name);
  const uint8_t at = ::espn::player_put(this->players_.players, p);
  if (at == 0) {
    ESP_LOGW(TAG, "Favorite player %s %s ignored: all %d player slots are in use",
             ::espn::league_key((League) p.league), p.name, ::espn::kPlayerSlots);
    return;
  }
  ESP_LOGI(TAG, "Favorite player %u: %s %s (%u)", (unsigned) at, ::espn::league_key((League) p.league), p.name,
           (unsigned) p.athlete);
  this->players_changed_();
}

void GamedayComponent::remove_player_(uint8_t league, uint32_t athlete) {
  if (!::espn::player_drop(this->players_.players, league, athlete))
    return;
  ESP_LOGI(TAG, "Favorite player %s %u removed", ::espn::league_key((League) league), (unsigned) athlete);
  this->players_changed_();
}

void GamedayComponent::players_changed_() {
  this->players_pref_.save(&this->players_);
  if (this->game_.valid)
    this->emit_({});  // the ticker drops a removed player's line now
  else
    this->rebuild_state_(&this->last_fields_);
}

std::vector<std::string> GamedayComponent::player_lines_(const GameSnapshot &g, uint8_t league) const {
  if (this->demo_playing_())
    return {};
  return ::espn::player_lines(g, league, this->players_.players);
}

bool GamedayComponent::bets_tick_() {
  bool any = false;
  for (const auto &b : this->bets_.blob.bets)
    any = any || !::espn::bet_empty(b);
  bool gone = false;
  if (any && this->clock_set_()) {
    const int64_t now = (int64_t) this->time_->timestamp_now();
    const int64_t midnight = this->next_midnight_(now);
    ::espn::BetSource &src = this->bets_;
    ::espn::BetGame g;
    // During a visit, only bets already tied to it update.
    if (this->bet_game_(this->game_, this->board_league_(), g)) {
      if (this->visit_.active)
        ::espn::bets_update(src, g, now, midnight);
      else
        ::espn::bets_observe(src, g, now, midnight);
    }
    if (this->favorites_mode_())
      for (const auto &e : this->fav_)
        if (this->bet_game_(e.game, (uint8_t) e.team->league, g))
          ::espn::bets_observe(src, g, now, midnight);
    // Second team's game; a jump keeps only tied bets.
    if (this->a2_show_() == ::espn::SplitShow::TEAM && !this->a2_team_.empty() &&
        this->bet_game_(this->a2_team_[0].game, (uint8_t) this->a2_team_[0].team->league, g))
      ::espn::bets_observe(src, g, now, midnight);
    if (this->jump_.active && this->bet_game_(this->jump_game_(), this->jump_.league, g))
      ::espn::bets_update(src, g, now, midnight);
    gone = ::espn::bets_expire(src, now);
    if (gone)
      ESP_LOGI(TAG, "Bets: dropped the ones that are done");
  }
  if (this->bets_.dirty) {
    this->bets_.dirty = false;
    this->bets_pref_.save(&this->bets_.blob);
  }
  return gone;
}

uint8_t GamedayComponent::board_league_() const {
  // During a visit, the big play's league.
  if (this->visit_.active)
    return this->visit_.sched.league;
  uint8_t league =this->live_mode_() && this->schedule_.valid ? this->schedule_.league : this->prefs_.league;
  if (this->favorites_mode_() && this->fav_shown_ >= 0)
    league = (uint8_t) this->fav_[this->fav_shown_].team->league;
  if (this->recap_on_ && this->recap_step_ < this->recap_cards_.size() &&
      (this->recap_cards_[this->recap_step_].kind == ::espn::RecapKind::FINAL ||
       this->recap_cards_[this->recap_step_].kind == ::espn::RecapKind::LATER))
    league = this->recap_days_[this->recap_cards_[this->recap_step_].day].league;
#ifdef GAMEDAY_DEMO
  if (this->demo_playing_())
    league = this->demo_league_();
#endif
  return league;
}

bool GamedayComponent::bet_game_(const GameSnapshot &g, uint8_t league, ::espn::BetGame &out) const {
  return !this->demo_playing_() && ::espn::watch_bet_game(g, league, out);
}

const GameSnapshot *GamedayComponent::bet_snapshot_(const ::espn::Bet &b, ::espn::BetGame &out) const {
  if (b.event == 0)
    return nullptr;
  if (this->bet_game_(this->game_, this->board_league_(), out) && out.event == b.event && out.league == b.league)
    return &this->game_;
  // The mode's own game, saved when the visit began.
  const GameSnapshot *saved = this->visit_.saved.get();
  if (saved != nullptr && this->bet_game_(*saved, this->mode_league_(), out) && out.event == b.event &&
      out.league == b.league)
    return saved;
  if (this->favorites_mode_())
    for (const auto &e : this->fav_)
      if (this->bet_game_(e.game, (uint8_t) e.team->league, out) && out.event == b.event && out.league == b.league)
        return &e.game;
  if (!this->a2_team_.empty() && this->bet_game_(this->a2_team_[0].game, (uint8_t) this->a2_team_[0].team->league, out) &&
      out.event == b.event && out.league == b.league)
    return &this->a2_team_[0].game;
  if (this->jump_.active && this->bet_game_(this->jump_game_(), this->jump_.league, out) && out.event == b.event &&
      out.league == b.league)
    return &this->jump_game_();
  return nullptr;
}

void GamedayComponent::bets_ticker_(const GameSnapshot &game, uint8_t league, std::string &out) const {
  ::espn::BetGame g;
  if (!this->clock_set_() || !this->bet_game_(game, league, g))
    return;
  const int64_t now = (int64_t) this->time_->timestamp_now();
  for (const auto &b : this->bets_.blob.bets) {
    if (::espn::bet_empty(b) || b.event != g.event || b.league != g.league)
      continue;
    if (!out.empty())
      out += " | ";
    out += ::espn::bet_ticker(b, ::espn::bet_status(b, &g, now));
  }
}

// Not mktime: libc has no time zone here.
int64_t GamedayComponent::next_midnight_(int64_t now) const {
  ESPTime t = ESPTime::from_epoch_local((time_t) now);
  t.hour = 0;
  t.minute = 0;
  t.second = 0;
  if (t.day_of_month >= days_in_month(t.month, t.year)) {
    t.day_of_month = 1;
    if (++t.month > 12) {
      t.month = 1;
      t.year++;
    }
  } else {
    t.day_of_month++;
  }
  t.recalc_timestamp_local();
  return (int64_t) t.timestamp > now ? (int64_t) t.timestamp : now + 24 * 3600;
}

uint32_t GamedayComponent::our_id_() const {
  if (this->job_.fav_index >= 0 || this->job_.visit || this->job_.a2 != 0)
    return this->job_.our_id;
  // In the fallback the panel is showing the saved team's own game, so the
  // snapshot is oriented around that team, exactly as in My team mode.
  uint8_t m = this->job_.mode;
  if (is_live_mode_(m) && !this->live_fallback_)
    return this->live_away_id_;
  const ::espn::Team *t = this->current_team_();
  return t ? t->espn_id : 0;
}

void GamedayComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Game Day Scoreboard:");
  const ::espn::Team *t = this->current_team_();
  ESP_LOGCONFIG(TAG, "  Team: %s (%s id %u)", t ? t->name : "none", t ? t->abbr : "-", (unsigned) this->prefs_.team_id);
  ESP_LOGCONFIG(TAG, "  Timezone: %s", ::espn::kTimezones[this->prefs_.tz_index].name);
  ESP_LOGCONFIG(TAG, "  Flags: 0x%02X", this->prefs_.flags);
#ifdef GAMEDAY_BAR
  ESP_LOGCONFIG(TAG, "  Bar beta: installs from the beta channel");
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  ESP_LOGCONFIG(TAG, "  Follow beta: %s", this->follow_beta() ? "on" : "off");
#endif
}

void GamedayComponent::on_shutdown() { ::espn::boot_log_planned(boot_log); }

void GamedayComponent::loop() {
  PsramTasks::reap();
  if (this->logos_.loop()) {
    this->fire_action_("logo_ready");  // gameday-common.yaml redraws the logos
    this->a2_show_logos_();            // and the second area's here
  }
  if (this->logo_held_ && this->logo_hold_done_())
    this->draw_(this->last_fields_);
  if (this->a2_hold_.on && this->a2_hold_.over(this->logos_.waiting(this->a2_hold_logo_[0]),
                                               this->logos_.waiting(this->a2_hold_logo_[1]), millis()))
    this->emit_a2_({});
  if (millis() - this->auto_update_ms_ >= AUTO_UPDATE_TICK) {
    this->auto_update_ms_ = millis();
    this->auto_update_tick_();
    if (!this->power_settled_ && millis() >= ::espn::kPowerSettledMs) {
      // A settled run: the next brownout starts a new streak
      this->power_settled_ = true;
      if (::espn::power_log_settled(this->power_log_))
        this->save_power_log_();
    }
    if (!this->boot_clock_noted_ && this->time_ != nullptr && this->time_->now().is_valid()) {
      // A fault at a start with no clock yet: when it happened
      this->boot_clock_noted_ = true;
      ::espn::boot_log_clock(boot_log, (int64_t) this->time_->now().timestamp - esp_timer_get_time() / 1000000);
    }
    // A graded bet goes at local midnight (bets.h).
    if (this->bets_tick_())
      this->rebuild_state_(&this->last_fields_);
  }
#ifdef GAMEDAY_UPDATE_MIRROR
  this->mirror_tick_();
#endif
  this->captive_tick_();
  this->wifi_tick_();
  if (this->join_addr_ms_ != 0 && !join_ticker_on(this->join_addr_ms_, millis())) {
    this->join_addr_ms_ = 0;
    if (this->join_addr_shown_)
      this->emit_({});
  }
#ifdef USE_CAPTIVE_PORTAL
  // Drain DNS bursts, or phones decide there's no internet.
  auto *portal = captive_portal::global_captive_portal;
  if (portal != nullptr && portal->is_active())
    for (int i = 0; i < 7; i++)
      portal->loop();
#endif
#ifdef GAMEDAY_DEMO
  // While the scripted game plays, nothing new is fetched. A fetch already
  // running still finishes below, and is discarded as belonging to before.
  if (this->demo_loop_() && !this->busy_)
    return;
#endif
  if (millis() - this->idle_tick_ms_ >= IDLE_TICK) {
    this->idle_tick_ms_ = millis();
    this->idle_tick_();
  }
  // The finals rotation turns its cards on its own clock, fetch or no fetch.
  if ((this->recap_on_ || this->scores_wanted_()) && !this->demo_playing_())
    this->recap_tick_();
  if (this->busy_) {
    if (!this->job_done_) {
      // A worker that died without setting job_done_ would block the loop
      // forever and Refresh Now could never clear it. Give up eventually, but
      // not before a slow-but-honest job could have finished: abandoning a
      // live worker is what makes the accepted run_job_/job_ write race
      // reachable, and that must stay out of reach on a merely slow network.
      // A Live games scan reads one scoreboard per league, so past two
      // leagues it gets the same allowance per request (60 s) as the rest.
      uint32_t deadline = WORKER_DEADLINE;
      if (this->job_.need_scan && this->job_.scan_leagues.size() > 2)
        deadline = WORKER_DEADLINE / 2 * (uint32_t) this->job_.scan_leagues.size();
      // A team read can add the look-ahead's day reads, and any other team
      // or cup read today's scoreboard (run_job_).
      // My team today's final adds one more (Job::today_since).
      const ::espn::Team *team = this->job_.team;
      if (this->job_.need_schedule && team != nullptr && this->needs_lookahead_(team->league))
        deadline = WORKER_DEADLINE / 2 * (2 + LOOKAHEAD_DAYS + (this->job_.today_since > 0 ? 1 : 0));
      else if ((this->job_.need_schedule || this->job_.comp > 0) && team != nullptr)
        deadline = WORKER_DEADLINE / 2 * 3;
      // A leaderboard: the header, and at most two calendar reads after it.
      if (this->job_.need_board)
        deadline = WORKER_DEADLINE / 2 * 3;
      if (this->busy_since_ms_ != 0 && (millis() - this->busy_since_ms_) >= deadline) {
        ESP_LOGW(TAG, "Fetch task did not finish in %us, giving up on it", (unsigned) (deadline / 1000));
        this->busy_ = false;
        this->busy_since_ms_ = 0;
        this->worker_seq_++;  // a late finisher must not signal job_done_
        // Wait a full retry before starting another worker rather than
        // stacking a second one on top of a task that may still be alive.
        const uint32_t now = millis();
        this->worker_gave_up_ms_ = now == 0 ? 1 : now;
        this->watch_hold_until_ = (now + RETRY_INTERVAL) | 1;
        if (this->job_.need_watch) {
          // A hung watcher read counts as a full-wait failure.
          const ::espn::WatchJob &w = this->job_.watch;
          this->watch_times_.took(w.league, !w.event_id.empty(), deadline, false);
          if (!w.event_id.empty())
            ::espn::watch_failed(this->watch_, w.league, w.event_id);
        }
        if (this->job_.side || this->job_.need_watch || this->job_.a2 != 0) {
          // Side reads wait as failed; the mode keeps its time.
          if (this->job_.side) {
            this->side_wait_since_ = now;
            this->side_wait_ms_ = SIDE_RETRY;
            if (this->job_.need_scan && !this->job_.scan_leagues.empty())
              this->scan_due_[(uint8_t) this->job_.scan_leagues[0]] = 0;  // again, once the wait is over
          }
          if (this->job_.a2 != 0) {
            this->a2_wait_since_ = now;
            this->a2_wait_ms_ = RETRY_INTERVAL;
            if (this->job_.a2 == A2_JUMP)
              this->end_jump_();
          }
          if ((int32_t) (this->next_fetch_ms_ - (now + RETRY_INTERVAL)) < 0)
            this->schedule_next_(RETRY_INTERVAL);
        } else {
          this->schedule_next_(RETRY_INTERVAL);
        }
      }
      return;
    }
    this->busy_ = false;
    this->busy_since_ms_ = 0;
    this->job_done_ = false;
    this->apply_job_();
    return;
  }
  if (this->visit_.active && ::espn::watch_visit_over(millis(), this->visit_.since_ms, this->watch_stay_ms(),
                                                      this->watch_switch_(this->visit_.fav)))
    this->end_visit_();
  if ((int32_t) (millis() - this->next_fetch_ms_) < 0) {
    // Gap reads fit before the board's next read.
    this->start_gap_job_(this->favorites_mode_() && !this->visit_.active ? this->fav_gap_due_(millis())
                                                                          : this->next_fetch_ms_);
    return;
  }
  if (this->worker_gave_up_ms_ != 0) {
    // An abandoned worker may still run: wait a full retry.
    const uint32_t since = millis() - this->worker_gave_up_ms_;
    if (since < RETRY_INTERVAL) {
      this->schedule_next_(RETRY_INTERVAL - since);
      return;
    }
    this->worker_gave_up_ms_ = 0;
  }
  if (!network::is_connected() || !this->clock_set_()) {
    this->wait_for_clock_();
    const uint32_t now = millis();
    if (network::is_connected() && !this->clock_set_() && now - this->wifi_up_ms_ >= HTTP_CLOCK_WAIT &&
        (this->http_clock_ms_ == 0 || now - this->http_clock_ms_ >= HTTP_CLOCK_RETRY)) {
      this->start_clock_job_();
      return;
    }
    this->schedule_next_(NOT_READY_INTERVAL);
    return;
  }
  if (!this->selects_published_) {
    this->selects_published_ = true;
    const uint32_t now = millis();
    ESP_LOGI(TAG, "Startup: Wi-Fi and clock ready at %u ms, clock set %u ms after Wi-Fi%s", (unsigned) now,
             (unsigned) (this->wifi_up_ms_ != 0 ? now - this->wifi_up_ms_ : 0),
             this->clock_approx_ ? " (from ESPN)" : "");
    this->publish_selects_();
  }
  this->start_job_();
}

// lwIP's SNTP doubles its wait after each failed try, up to 150 s, and every
// try fails while Wi-Fi is down. A panel that spent minutes in Bluetooth
// setup joined Wi-Fi and then waited over two minutes for the next try, with
// no clock to ask ESPN for today's games. Ask as soon as Wi-Fi is up, and
// again until the clock is set; a restart sends at once and resets the wait.
void GamedayComponent::wait_for_clock_() {
  if (this->clock_set_())
    return;
  if (!network::is_connected()) {
    this->wifi_up_ms_ = 0;
    this->sntp_asked_ms_ = 0;
    this->http_clock_ms_ = 0;
    return;
  }
  const uint32_t now = millis();
  if (this->wifi_up_ms_ == 0)
    this->wifi_up_ms_ = now == 0 ? 1 : now;
  if (!this->clock_wait_shown_) {
    // The board would say "no game" until the first fetch; say why instead.
    this->clock_wait_shown_ = true;
    this->emit_({});
  }
  if (this->sntp_asked_ms_ != 0 && now - this->sntp_asked_ms_ < SNTP_ASK_INTERVAL)
    return;
  this->sntp_asked_ms_ = now == 0 ? 1 : now;
  if (esp_sntp_restart())
    ESP_LOGI(TAG, "Clock not set yet: asking the time server now");
}

// UDP 123 is blocked on some networks (guest Wi-Fi, some offices and
// schools), and then no time server ever answers: no clock meant no game all
// day. Every ESPN reply carries the time in its Date header, and the TLS
// certificate check means it is ESPN's and not a captive portal's.
void GamedayComponent::start_clock_job_() {
  const uint32_t now = millis();
  this->http_clock_ms_ = now == 0 ? 1 : now;
  this->new_job_().need_clock = true;
  this->start_worker_();
}

void GamedayComponent::apply_clock_job_() {
  const Job &j = this->job_;
  this->schedule_next_(0);
  if (this->clock_set_())
    return;  // a time server answered meanwhile
  if (j.clock_epoch == 0) {
    ESP_LOGW(TAG, "Clock not set yet and no date from ESPN; trying again in %us",
             (unsigned) (HTTP_CLOCK_RETRY / 1000));
    return;
  }
  // The header is in whole seconds, so on average half a second behind when
  // it was sent. Add that and the time since it arrived.
  int64_t ms = j.clock_epoch * 1000 + 500 + (int64_t) (millis() - j.clock_got_ms);
  struct timeval tv;
  tv.tv_sec = (time_t) (ms / 1000);
  tv.tv_usec = (suseconds_t) ((ms % 1000) * 1000);
  settimeofday(&tv, nullptr);
  this->clock_approx_ = true;
  ESP_LOGI(TAG, "No time server answer %us after Wi-Fi: clock set from ESPN's Date header until one answers",
           (unsigned) ((millis() - this->wifi_up_ms_) / 1000));
  this->fire_action_("clock_approx");  // gameday-common.yaml checks for an update
}

const ::espn::Team *GamedayComponent::current_team_() const {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == this->prefs_.league && t.espn_id == this->prefs_.team_id)
      return &t;
  }
  return nullptr;
}

std::string GamedayComponent::team_option_() const {
  const ::espn::Team *t = this->current_team_();
  if (t == nullptr)
    return "";
  return ::espn::team_option(*t);
}

void GamedayComponent::select_team(const std::string &option) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if (::espn::team_option(t) == option) {
      this->apply_team_(t);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown team option '%s'", option.c_str());
}

void GamedayComponent::select_team_id(League league, uint32_t id) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if (t.league == league && t.espn_id == id) {
      this->apply_team_(t);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown team %s %u", ::espn::league_key(league), (unsigned) id);
}

void GamedayComponent::fire_action_(const std::string &name) {
  for (auto &cb : this->action_callbacks_)
    cb(name);
}

// The first team, mode or favorite picked from the page, the app or Home
// Assistant ends setup: the setup ticker goes and the boot screen hides.
// Returns true when this call is the one that ended it.
bool GamedayComponent::mark_setup_done_() {
  if (this->flag_(FLAG_SETUP))
    return false;
  this->prefs_.flags |= FLAG_SETUP;
  this->save_prefs_();
  this->fire_action_("team_picked");  // the setup screen listens for this
  return true;
}

void GamedayComponent::show_join_address() {
  this->join_addr_ms_ = millis() == 0 ? 1 : millis();
  ESP_LOGI(TAG, "The ticker starts with the panel's address for %u s", (unsigned) (kJoinTickerMs / 1000));
  this->emit_({});
}

void GamedayComponent::apply_team_(const ::espn::Team &t) {
  if ((uint8_t) t.league == this->prefs_.league && t.espn_id == this->prefs_.team_id) {
    // Picking the team the panel already had is still a pick: setup ends.
    if (this->mark_setup_done_())
      this->emit_({});  // redraw the ticker without the setup prompt
    return;
  }
  this->prefs_.league = (uint8_t) t.league;
  this->prefs_.team_id = t.espn_id;
  this->save_prefs_();
  ESP_LOGI(TAG, "Team changed to %s", t.name);
  // The boot screen holds for this team's first scoreboard (board_pending
  // below). Cleared before mark_setup_done_, whose team_picked hides the
  // setup screen through that same hold.
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->publish_team_select_(this->team_option_());
  this->mark_setup_done_();
  // The select, the page and the app all land here, and only on a real
  // change, so this is the one place that knows a person picked a team.
  this->fire_action_("user_pick");
  this->reset_game_();
  this->generation_++;  // a fetch already in flight belongs to the old team
  // Behind the boot screen: the new team's logo and name, with no "Loading"
  // text. The game data follows in a second or two; a failed read drops it
  // (drop_team_placeholder_) so the board says why instead.
  this->game_ = ::espn::team_placeholder(t);
  this->emit_({});
  this->fire_action_("board_pending");  // the boot screen holds (gameday-common.yaml)
  this->schedule_next_(0);
}

// Back to no team: My team then runs the live board and the finals rotation
// (no_team_default). Setup stays done.
void GamedayComponent::clear_team_() {
  if (this->prefs_.team_id == 0)
    return;
  this->prefs_.team_id = 0;
  this->save_prefs_();
  ESP_LOGI(TAG, "Team cleared");
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->reset_game_();
  this->generation_++;
  this->emit_({});
  this->fire_action_("board_pending");
  this->schedule_next_(0);
}

void GamedayComponent::select_timezone(const std::string &option) {
  for (size_t i = 0; i < ::espn::kTimezoneCount; i++) {
    if (option != ::espn::kTimezones[i].name)
      continue;
    this->prefs_.tz_index = (uint8_t) i;
    this->save_prefs_();
    this->apply_timezone_();
    if (this->timezone_select_ != nullptr)
      this->timezone_select_->publish_state(::espn::kTimezones[i].name);
    // Re-render so a PRE ticker picks up the new kickoff time.
    if (this->game_.valid)
      this->emit_({});
    else
      this->rebuild_state_(&this->last_fields_);
    return;
  }
  ESP_LOGW(TAG, "Unknown timezone option '%s'", option.c_str());
}

void GamedayComponent::refresh_now() {
  // Read the team endpoint again, do not just re-poll the game already on
  // the board: the owner presses this when the board looks wrong, and a
  // moved, postponed or finished game only shows up in that read.
  this->end_visit_();
  this->watch_reset_();
  this->force_schedule_ = true;
  this->end_backoff_wait_();
  this->schedule_next_(0);
}

void GamedayComponent::set_flag_(uint8_t f, bool on) {
  uint8_t next = on ? (this->prefs_.flags | f) : (this->prefs_.flags & ~f);
  if (next == this->prefs_.flags)
    return;
  this->prefs_.flags = next;
  this->save_prefs_();
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_ticker8_(uint8_t f, bool on) {
  uint8_t next = on ? (this->prefs8_.ticker | f) : (this->prefs8_.ticker & ~f);
  if (next == this->prefs8_.ticker)
    return;
  this->prefs8_.ticker = next;
  this->pref8_.save(&this->prefs8_);
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_size8_(uint8_t f, int size) {
  if (size < 0 || size >= ::espn::kSizeCount)
    return;
  const uint8_t next = size ? (this->prefs8_.sizes | f) : (this->prefs8_.sizes & ~f);
  if (next == this->prefs8_.sizes)
    return;
  this->prefs8_.sizes = next;
  this->pref8_.save(&this->prefs8_);
  ESP_LOGI(TAG, "Clock size %s, text size %s", this->clock_large() ? "Large" : "Normal",
           this->text_large() ? "Large" : "Normal");
  this->fire_action_("layout");
  this->emit_({});
}

void GamedayComponent::set_alerts(uint32_t on) {
  const uint32_t off = ::espn::kAlertsAll & ~on;
  if (off == this->prefs9_.alerts_off)
    return;
  this->prefs9_.alerts_off = off;
  this->save_prefs9_();
}

void GamedayComponent::set_alerts_favorites_only(bool on) {
  const uint8_t flags = on ? (this->prefs9_.flags | ALERT9_FAV) : (this->prefs9_.flags & ~ALERT9_FAV);
  if (flags == this->prefs9_.flags)
    return;
  this->prefs9_.flags = flags;
  this->save_prefs9_();
}

void GamedayComponent::set_watch(bool on) {
  const uint8_t flags = on ? (this->prefs9_.flags & ~ALERT9_NOWATCH) : (this->prefs9_.flags | ALERT9_NOWATCH);
  if (flags == this->prefs9_.flags)
    return;
  this->prefs9_.flags = flags;
  if (!on)
    this->end_visit_();
  this->save_prefs9_();
}

void GamedayComponent::set_watch_stay(int seconds) {
  const uint8_t tens = (uint8_t) (::espn::clamp_watch_stay_seconds(seconds) / 10);
  if (tens == this->prefs9_.stay)
    return;
  this->prefs9_.stay = tens;
  this->save_prefs9_();
}

void GamedayComponent::set_watch_from_mine(bool on) {
  const uint8_t flags = on ? (this->prefs9_.flags | ALERT9_MINE) : (this->prefs9_.flags & ~ALERT9_MINE);
  if (flags == this->prefs9_.flags)
    return;
  this->prefs9_.flags = flags;
  this->save_prefs9_();
}

// Only the page shows these, so the board isn't redrawn.
void GamedayComponent::save_prefs9_() {
  this->pref9_.save(&this->prefs9_);
  this->rebuild_state_(&this->last_fields_);
}

::espn::Splash GamedayComponent::board_splash_(bool neutral) const {
  uint32_t alerts = this->alerts_on();
  if (neutral && this->alerts_favorites_only()) {
    // Live games: no team or favorite, no alerts.
    std::pair<uint8_t, uint32_t> teams[FAV_MAX + 1];
    teams[0] = {this->prefs_.league, this->prefs_.team_id};
    for (uint8_t i = 0; i < FAV_MAX; i++)
      teams[i + 1] = {this->fav_league_(i), this->fav_id_(i)};
    if (!::espn::game_has_team(this->schedule_.league, this->game_.team_id, this->game_.opp_id, teams, FAV_MAX + 1))
      alerts = 0;
  }
  // The demo's scripted game has no player splashes.
  if (this->demo_playing_())
    alerts &= ~::espn::alert_bit(::espn::Alert::PLAYER);
  return ::espn::board_splash_with_players(this->prev_, this->game_, this->opponent_splashes(), neutral, alerts,
                                           this->wide_(), this->board_league_(), this->players_.players);
}

void GamedayComponent::set_standing(uint8_t kind) {
  if (kind > (uint8_t) ::espn::Standing::PLACE || kind == this->prefs8_.standing)
    return;
  this->prefs8_.standing = kind;
  this->pref8_.save(&this->prefs8_);
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::save_prefs_() { this->pref_.save(&this->prefs_); }

void GamedayComponent::set_auto_update(bool on) {
#ifdef GAMEDAY_BAR
  (void) on;  // the bar build always installs; there is no setting
#else
  if (on == this->auto_update())
    return;
  this->prefs6_.auto_update = on ? 1 : 0;
  this->pref6_.save(&this->prefs6_);
  ESP_LOGI(TAG, "Install updates automatically: %s", on ? "on" : "off");
  this->rebuild_state_(&this->last_fields_);
#endif
}

void GamedayComponent::load_power_log_() {
  if (this->power_loaded_)
    return;
  this->power_loaded_ = true;
  this->power_pref_ = global_preferences->make_preference<::espn::PowerLog>(fnv1_hash("gameday_power_v1"));
  if (!this->power_pref_.load(&this->power_log_))
    this->power_log_ = ::espn::PowerLog{};
  const bool brownout =
      ::espn::classify_restart((int) esp_reset_reason(), false, false) == ::espn::Restart::BROWNOUT;
  if (::espn::power_log_start(this->power_log_, brownout)) {
    // Now, not at the next flash write: a weak brick can brown out again
    // within the minute.
    this->save_power_log_();
    ESP_LOGW(TAG, "Brownout restart, %u in a row, %u in all", (unsigned) this->power_log_.streak,
             (unsigned) this->power_log_.brownouts);
  }
  if (this->power_log_.capped)
    ESP_LOGW(TAG, "Brightness capped at %u after brownouts, until cleared on the device page",
             (unsigned) ::espn::kPowerCap);
}

void GamedayComponent::save_power_log_() {
  this->power_pref_.save(&this->power_log_);
  global_preferences->sync();
}

uint8_t GamedayComponent::guard_brightness(uint8_t want, bool idle) {
  this->load_power_log_();
  if (idle) {
    // Before setup(), zeros mean the owner's setting.
    want = ::espn::idle_brightness(want, this->idle_, this->idle_prefs_.night_bri, this->idle_prefs_.dim,
                                   this->night_.on);
    if (want == 0)
      return 0;
  }
  return ::espn::guarded_brightness(want, this->heat_.step, this->power_log_.capped != 0);
}

void GamedayComponent::note_brightness() {
  if (!this->idle_.night || this->night_.on)
    return;
  ESP_LOGI(TAG, "Brightness changed in the night hours: keeping it until they end");
  this->night_.brightness_changed(true);
}

// ---- The idle display (idle.h) ----------------------------------------------

void GamedayComponent::set_idle(int show) {
  if (show < 0 || show >= ::espn::kIdleCount || show == this->idle_prefs_.idle)
    return;
  const bool was_scores = this->live_recap_();
  this->idle_prefs_.idle = (uint8_t) show;
  this->save_idle_prefs_();
  ESP_LOGI(TAG, "Idle display: %d", show);
  this->idle_changed_(was_scores != this->live_recap_());
}

void GamedayComponent::set_idle_dim(int level) {
  level = ::espn::clamp_idle_dim(level);
  if (level == this->idle_prefs_.dim)
    return;
  this->idle_prefs_.dim = (uint8_t) level;
  this->save_idle_prefs_();
  if (this->idle_.dim)
    this->defer("idle_dim", [this]() { this->fire_action_("dim"); });
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_night(bool on) {
  if ((on ? 1 : 0) == this->idle_prefs_.night_on)
    return;
  this->idle_prefs_.night_on = on ? 1 : 0;
  this->save_idle_prefs_();
  ESP_LOGI(TAG, "Night hours: %s", on ? "on" : "off");
  this->idle_changed_(false);
}

void GamedayComponent::set_night_start(int minute) {
  if (minute < 0 || minute >= ::espn::kDayMinutes || minute == this->idle_prefs_.night_start)
    return;
  this->idle_prefs_.night_start = (uint16_t) minute;
  this->save_idle_prefs_();
  this->idle_changed_(false);
}

void GamedayComponent::set_night_end(int minute) {
  if (minute < 0 || minute >= ::espn::kDayMinutes || minute == this->idle_prefs_.night_end)
    return;
  this->idle_prefs_.night_end = (uint16_t) minute;
  this->save_idle_prefs_();
  this->idle_changed_(false);
}

void GamedayComponent::set_night_bri(int level) {
  level = ::espn::clamp_night_bri(level);
  if (level == this->idle_prefs_.night_bri)
    return;
  this->idle_prefs_.night_bri = (uint8_t) level;
  this->save_idle_prefs_();
  if (this->idle_.night)
    this->defer("idle_dim", [this]() { this->fire_action_("dim"); });
  this->rebuild_state_(&this->last_fields_);
}

// Rescan when Live games flips between card and finals.
void GamedayComponent::idle_changed_(bool live_switch) {
  if (live_switch && this->live_mode_() && (this->live_fallback_ || this->recap_on_)) {
    this->live_fallback_ = false;
    this->recap_on_ = false;
    if (!this->a2_scores_)
      this->recap_cards_.clear();  // the second area's scores keep theirs (recap_tick_)
    this->recap_step_ms_ = 0;
    this->live_none_ = false;
    this->live_started_ms_ = 0;
    this->schedule_ = Schedule{};
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->post_since_ms_ = 0;
    this->generation_++;  // a read in flight was for the other one
    this->end_backoff_wait_();
    this->schedule_next_(0);
  }
  this->emit_({});
}

// A big-play visit counts as busy too.
bool GamedayComponent::idle_busy_() const {
  ::espn::BusyInputs b;
  b.hold = this->demo_playing_() || this->follow_.on || this->visit_.active;
  b.live_mode = this->live_mode_();
  b.recap = this->recap_on_;
  b.live_fallback = this->live_fallback_;
  // The pick's board is cleared until its first read.
  b.picked = this->live_unread_ && this->schedule_.valid;
  b.favorites = this->favorites_mode_();
  b.fav_locked = this->fav_locked_;
  for (const auto &e : this->fav_)
    if (e.game.valid && e.game.state == GameState::IN)
      b.fav_live = true;
  const GameSnapshot &g = this->game_;
  b.valid = g.valid;
  b.state = g.state;
  b.leaderboard = ::espn::is_board_sport(g.sport);
  if (b.leaderboard)
    b.on_course = g.sport == ::espn::Sport::RACING ? g.race.phase == ::espn::RacePhase::LIVE
                                                   : g.golf.phase == ::espn::GolfPhase::LIVE;
  b.kickoff = g.kickoff_epoch;
  b.now = this->clock_set_() ? (int64_t) this->time_->timestamp_now() : 0;
  // One wait per final, even if the endpoint repeats it.
  b.post_seen = this->post_since_ms_ != 0;
  b.post_age_ms = millis() - this->post_since_ms_;
  b.linger_ms = b.leaderboard ? ::espn::kBoardFinalWaitMs : this->linger_ms_();
  b.saw_play = this->post_saw_play_;
  b.lingered = g.event_id == this->lingered_event_;
  return ::espn::idle_busy(b);
}

void GamedayComponent::note_post_(uint32_t now) {
  if (this->post_since_ms_ != 0)
    return;
  this->post_since_ms_ = now == 0 ? 1 : now;
  // Seen before it ended, else only as a final.
  this->post_saw_play_ = this->prev_.valid && this->prev_.event_id == this->game_.event_id &&
                         this->prev_.state != GameState::POST;
}

// By the time component's rules; libc's TZ is unset.
static ::espn::LocalDay local_today(ESPTime now) {
  return ::espn::local_day((int64_t) now.timestamp, now.to_c_tm(), [](int y, int m, int d) -> int64_t {
    ESPTime e{};
    e.year = (uint16_t) y;
    e.month = (uint8_t) m;
    e.day_of_month = (uint8_t) d;
    e.recalc_timestamp_local();
    return (int64_t) e.timestamp;
  });
}

int64_t GamedayComponent::today_since_() const {
  if (this->idle_prefs_.idle != (uint8_t) ::espn::IdleShow::TODAY || this->time_ == nullptr)
    return 0;
  ESPTime t = this->time_->now();
  return t.is_valid() ? local_today(t).start : 0;
}

::espn::IdleView GamedayComponent::idle_effective_(::espn::IdleView v) const {
  using ::espn::IdleView;
  const ::espn::Team *team = this->current_team_();
  switch (v) {
    case IdleView::SCORES:
      // Live games' rotation stands in for the team's card.
      if (this->live_mode_())
        return IdleView::BOARD;
      return this->side_leagues_().empty() ? IdleView::CLOCK : IdleView::SCORES;
    case IdleView::TODAY:
      if (this->favorites_mode_())
        return IdleView::TODAY;
      if (team == nullptr || ::espn::is_leaderboard(team->league))
        return IdleView::CLOCK;
      return IdleView::TODAY;
    default:
      return v;
  }
}

std::vector<League> GamedayComponent::side_leagues_() const {
  std::vector<uint8_t> fav_leagues;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      fav_leagues.push_back(this->fav_league_(i));
  const uint8_t team = this->current_team_() != nullptr ? this->prefs_.league : 0xFF;
  return ::espn::live_leagues(this->prefs7_.live_mask, team, fav_leagues, false);
}

bool GamedayComponent::idle_update_() {
  ::espn::IdleInputs in;
  in.show = this->idle_prefs_.idle;
  in.night_on = this->idle_prefs_.night_on != 0;
  in.night_start = this->idle_prefs_.night_start;
  in.night_end = this->idle_prefs_.night_end;
  // By clock alone, so an overnight update restart stays dark.
  if (this->time_ != nullptr) {
    ESPTime t = this->time_->now();
    in.clock_valid = t.is_valid();
    in.minute = t.hour * 60 + t.minute;
  }
  in.board_ready = this->board_ready_;
  in.connected = network::is_connected();
  in.busy = this->idle_busy_();
  in.area2_busy = this->a2_busy_();
  const ::espn::IdleState s = ::espn::idle_state(in);
  const ::espn::IdleView v = this->idle_effective_(s.view);
  // The owner's change lasts until the night hours end.
  const bool was_override = this->night_.on;
  this->night_.update(in.clock_valid, s.night);
  const bool light = s.dim != this->idle_.dim || s.night != this->idle_.night || was_override != this->night_.on;
  // At night the clock needs the team's card back.
  const bool live_switch = s.night != this->idle_.night && this->live_mode_() && this->current_team_() != nullptr &&
                           this->idle_prefs_.idle == (uint8_t) ::espn::IdleShow::SCORES;
  const bool changed = v != this->idle_view_;
  this->idle_ = s;
  this->idle_view_ = v;
  const bool side = v == ::espn::IdleView::SCORES;
  if (side != this->side_on_) {
    this->side_on_ = side;
    // Start over and rescan, unless back from a moment away.
    const uint32_t now = millis();
    this->recap_step_ = 0;
    this->recap_step_ms_ = 0;
    if (!side)
      this->side_off_ms_ = now | 1;
    else if (this->side_off_ms_ == 0 || now - this->side_off_ms_ >= SIDE_AWAY_RESCAN)
      this->scan_due_now_();
    if (side || this->a2_scores_)
      this->recap_cards_ = ::espn::recap_cards(this->recap_days_, this->recap_leagues_(),
                                               this->recap_next_kickoff_() != 0,
                                               (int64_t) this->time_->timestamp_now());
    else
      this->recap_cards_.clear();
  }
  if (changed)
    ESP_LOGI(TAG, "Idle display: showing %s%s", v == ::espn::IdleView::BOARD ? "the board" : ::espn::idle_view_key(v),
             s.night ? " (night hours)" : "");
  if (light) {
    ESP_LOGI(TAG, "Brightness: %s", s.dim ? "idle dim" : s.night && !this->night_.on ? "night hours" : "the owner's");
    // Deferred: emit_ may be deep in fav_choose_.
    this->defer("idle_dim", [this]() { this->fire_action_("dim"); });
  }
  if (live_switch)
    this->defer("idle_live", [this]() { this->idle_changed_(true); });
  return changed;
}

void GamedayComponent::idle_tick_() {
  const ::espn::IdleState before = this->idle_;
  bool redraw = this->idle_update_();
  this->a2_tick_();
  if (!redraw && before != this->idle_)
    this->rebuild_state_(&this->last_fields_);  // the page's night_now and dark
  if (this->idle_view_ == ::espn::IdleView::TODAY && this->time_ != nullptr) {
    ESPTime t = this->time_->now();
    const int minute = t.is_valid() ? t.hour * 60 + t.minute : -1;
    if (minute != this->idle_minute_) {
      this->idle_minute_ = minute;
      redraw = true;
    }
  }
  if (redraw)
    this->emit_({});
}

void GamedayComponent::note_final_(const GameSnapshot &g) {
  if (!g.valid || g.state != GameState::POST || ::espn::is_board_sport(g.sport) || this->idle_snaps_.size() < 2)
    return;
  GameSnapshot &f = this->today_final_();
  if (f.valid && f.event_id != g.event_id &&
      ::espn::day_order(f.kickoff_epoch, f.time_tbd) > ::espn::day_order(g.kickoff_epoch, g.time_tbd))
    return;  // endpoint still names game 1 after game 2
  f = g;
}

const GameSnapshot *GamedayComponent::today_game_() const {
  if (this->time_ == nullptr || this->idle_snaps_.size() < 2)
    return nullptr;
  ESPTime t = this->time_->now();
  if (!t.is_valid())
    return nullptr;
  const ::espn::LocalDay day = local_today(t);
  std::vector<const GameSnapshot *> games;
  if (this->favorites_mode_()) {
    for (const auto &e : this->fav_)
      games.push_back(&e.game);
  } else if (!this->live_mode_() || this->live_fallback_) {
    games.push_back(&this->game_);  // the team's card
  }
  games.push_back(&this->today_final_());
  std::vector<::espn::TodayGame> list;
  for (const GameSnapshot *g : games) {
    ::espn::TodayGame tg;
    tg.valid = g->valid && !::espn::is_board_sport(g->sport) && !::espn::is_team_placeholder(*g);
    tg.state = g->state;
    tg.kickoff = g->kickoff_epoch;
    tg.tbd = g->time_tbd;
    list.push_back(tg);
  }
  const int i = ::espn::today_pick(list, day);
  return i >= 0 ? games[i] : nullptr;
}

// Live games: board scans bring today, so days only.
bool GamedayComponent::start_side_job_(uint32_t now, uint32_t due) {
  if (!this->scores_wanted_() || this->idle_.dim)
    return false;
  if (now - this->side_check_ms_ < IDLE_TICK)
    return false;
  this->side_check_ms_ = now;
  if (this->side_wait_ms_ != 0 && now - this->side_wait_since_ < this->side_wait_ms_)
    return false;
  // College days are large: only with the board far off.
  auto fits = [&](uint8_t league) {
    if (::espn::watch_by_event((League) league))
      return (int32_t) (due - now) >= (int32_t) SIDE_COLLEGE_MS;
    return this->watch_times_.fits(now, due, league, false);
  };
  Job &j = this->new_job_();
  j.mode = this->effective_mode_();
  j.side = true;
  if (!this->live_mode_()) {
    for (League l : this->scan_leagues_()) {
      const uint8_t i = (uint8_t) l;
      if ((this->scan_due_[i] != 0 && (int32_t) (now - this->scan_due_[i]) < 0) || !fits(i))
        continue;
      // A waiting host's leagues wait; the others go on.
      const ::espn::Source src = ::espn::league_source(l);
      if (src != ::espn::Source::ESPN && this->host_wait_[(size_t) src].waiting(now))
        continue;
      j.need_scan = true;
      j.want_finals = true;
      j.scan_leagues = {l};
      // Stamped now, so failed scans wait their turn too.
      this->scan_due_[i] = (now + SIDE_RETRY) | 1;
      this->watch_times_.started(i, false);
      this->start_worker_();
      return this->busy_;
    }
  }
  if (!this->recap_job_(j, (int64_t) this->time_->timestamp_now()) || !fits(j.recap_league))
    return false;
  this->start_worker_();
  return this->busy_;
}

void GamedayComponent::apply_side_job_() {
  Job &j = this->job_;
  const int64_t tnow = (int64_t) this->time_->timestamp_now();
  // Other hosts' reads here count in their own wait.
  for (size_t i = 1; i < ::espn::kSourceCount; i++) {
    const Job::HostJob &h = j.host[i];
    const uint32_t ms = ::espn::host_side_result(this->host_wait_[i], h.read, j.need_scan ? h.scan_failed : !j.recap_ok,
                                                 h.throttled, h.retry_after_ms, random_uint32(), millis());
    if (ms != 0)
      ESP_LOGW(TAG, "%s scores for the idle display not read: next try in %us", HOST_NAMES[i], (unsigned) (ms / 1000));
  }
  bool ok;
  const uint8_t league = j.scan_leagues.empty() ? 0 : (uint8_t) j.scan_leagues[0];
  if (j.need_scan) {
    ok = j.scan_ok || !j.live.empty();
    if (!::espn::watch_by_event((League) league))
      this->watch_times_.took(league, false, j.took_ms, ok);
    if (ok) {
      this->recap_absorb_(j, tnow);
      // Unfollowed live games end on their own: rescan.
      const uint32_t again = !j.live.empty() ? SIDE_LIVE_RESCAN
                                             : ::espn::no_live_rescan_ms(tnow, j.next_kickoff, NO_LIVE_RESCAN,
                                                                         NO_GAMES_RESCAN);
      this->scan_due_[league] = (millis() + again) | 1;
    }
  } else {
    ok = this->recap_day_(j);
  }
  if (ok) {
    this->side_wait_ms_ = 0;
    return;
  }
  // Keeps the rotation; the mode's timing is untouched.
  if (j.need_scan)
    this->scan_due_[league] = 0;  // the scan, once the wait is over
  // Another host's failure waits there; others go on.
  if (this->job_host_() != ::espn::Source::ESPN)
    return;
  this->side_wait_since_ = millis();
  this->side_wait_ms_ = std::max(SIDE_RETRY, j.retry_after_ms);
  ESP_LOGW(TAG, "Scores for the idle display not read; trying again in %u min",
           (unsigned) (this->side_wait_ms_ / MINUTE));
}

void GamedayComponent::set_chip_temp(float c) {
  const uint8_t before = this->heat_.step;
  if (!this->heat_.feed(c, millis()))
    return;
  if (this->heat_.step > before)
    ESP_LOGW(TAG, "Chip at %.1f C: dimming to %u%% of the brightness setting", c,
             (unsigned) ::espn::kHeatPercent[this->heat_.step]);
  else
    ESP_LOGI(TAG, "Chip at %.1f C: back to %u%% of the brightness setting", c,
             (unsigned) ::espn::kHeatPercent[this->heat_.step]);
  this->fire_action_("guard");  // pages/guard.yaml applies the brightness and the corner mark
}

void GamedayComponent::clear_power_cap() {
  this->load_power_log_();
  if (!::espn::power_log_clear(this->power_log_))
    return;
  this->save_power_log_();
  ESP_LOGI(TAG, "Power cap cleared by the owner");
  this->fire_action_("guard");
}

void GamedayComponent::set_follow_beta(bool on) {
#ifdef GAMEDAY_BETA_CHANNEL
  if (on == this->follow_beta())
    return;
  this->follow_beta_ = on ? 1 : 0;
  this->beta_pref_.save(&this->follow_beta_);
  ESP_LOGI(TAG, "Follow beta: %s", on ? "on" : "off");
  this->apply_channel_();
  // Read the new manifest now, so the page shows what this channel offers.
  if (this->beta_update_ != nullptr)
    this->beta_update_->update();
  this->rebuild_state_(&this->last_fields_);
#else
  (void) on;  // no beta channel in this build
#endif
}

#ifdef GAMEDAY_BETA_CHANNEL
void GamedayComponent::apply_channel_() {
#ifdef GAMEDAY_UPDATE_MIRROR
  this->on_mirror_ = false;
#endif
  if (this->beta_update_ != nullptr)
    this->beta_update_->set_source_url(this->follow_beta() ? this->beta_source_ : this->stable_source_);
}
#endif

#ifdef GAMEDAY_UPDATE_MIRROR
// Points the update entity back at the primary host for its channel.
void GamedayComponent::use_primary_() {
#ifdef GAMEDAY_BETA_CHANNEL
  this->apply_channel_();
#else
  this->on_mirror_ = false;
  this->mirror_update_->set_source_url(this->primary_source_);
#endif
}

// A failed manifest read or install on the primary gets one read from the
// mirror right away (update_mirror.h). Runs every loop: a refused connection
// fails in well under a second.
void GamedayComponent::mirror_tick_() {
  http_request::HttpRequestUpdate *u = this->mirror_update_;
  if (u == nullptr)
    return;
  ::espn::MirrorInputs in;
  in.on_mirror = this->on_mirror_;
  in.error = u->status_has_error();
  in.answered = this->mirror_answered_;
  in.waited_ms = millis() - this->mirror_since_ms_;
  // Only read when it matters: now() converts the time, and this runs every loop.
  in.clock_valid = in.error && this->time_ != nullptr && this->time_->now().is_valid();
  switch (::espn::mirror_step(in)) {
    case ::espn::MirrorStep::NONE:
      break;
    case ::espn::MirrorStep::NO_CLOCK:
      u->status_clear_error();
      break;
    case ::espn::MirrorStep::TRY_MIRROR: {
      const char *mirror = this->follow_beta() ? this->beta_mirror_source_ : this->mirror_source_;
      ESP_LOGW(TAG, "Update check or install failed; reading the mirror %s", mirror);
      this->on_mirror_ = true;
      this->mirror_answered_ = false;
      this->mirror_since_ms_ = millis();
      u->set_source_url(mirror);
      // So the mirror's own failure shows (FAILED below).
      u->status_clear_error();
      u->update();
      break;
    }
    case ::espn::MirrorStep::ANSWERED:
      // update_info keeps the mirror's firmware URL, so an install of this
      // update downloads from the mirror.
      ESP_LOGI(TAG, "Update mirror answered: %s at %s", u->update_info.latest_version.c_str(),
               u->update_info.firmware_url.c_str());
      this->use_primary_();
      break;
    case ::espn::MirrorStep::FAILED:
      // Cleared, or the next failure on the primary would look like this one.
      ESP_LOGW(TAG, "Update mirror failed too; the next check starts with the primary");
      u->status_clear_error();
      this->use_primary_();
      break;
    case ::espn::MirrorStep::TIMED_OUT:
      ESP_LOGW(TAG, "Update mirror didn't answer; the next check starts with the primary");
      this->use_primary_();
      break;
  }
}
#endif

// At boot, what became of the last install (auto_update.h, UpdateMark), and
// from now on, note every install as it starts.
void GamedayComponent::settle_update_mark_() {
#if defined(USE_UPDATE) && defined(ESPHOME_PROJECT_VERSION)
  if (App.get_updates().empty())
    return;
  this->update_mark_pref_ = global_preferences->make_preference<::espn::UpdateMark>(fnv1_hash("gameday_update_mark_v1"));
  if (!this->update_mark_pref_.load(&this->update_mark_))
    this->update_mark_ = ::espn::UpdateMark{};
  // Ended first so the copy the log lines below print is a string too.
  ::espn::update_mark_terminate(this->update_mark_);
  const ::espn::UpdateMark before = this->update_mark_;
  const std::string running = ESPHOME_PROJECT_VERSION;
  // A power cut in the new firmware's first minute rolls it back too, but
  // says nothing against the release, so that one is tried again.
  const bool rolled_back = esp_ota_get_last_invalid_partition() != nullptr && esp_reset_reason() != ESP_RST_POWERON;
  switch (::espn::update_mark_boot(this->update_mark_, running, rolled_back)) {
    case ::espn::UpdateOutcome::NONE:
      break;
    case ::espn::UpdateOutcome::PENDING:
      // Cleared only once this boot is past the point of rolling back; a
      // crash before then and the old firmware sees the mark instead.
      this->set_timeout("update_mark", UPDATE_GOOD_BOOT, [this]() {
        if (strncmp(this->update_mark_.installing, ESPHOME_PROJECT_VERSION, ::espn::kUpdateVersionLen) != 0)
          return;
        ESP_LOGI(TAG, "Update to %s took", ESPHOME_PROJECT_VERSION);
        this->update_mark_.installing[0] = '\0';
        this->update_mark_pref_.save(&this->update_mark_);
      });
      break;
    case ::espn::UpdateOutcome::ROLLED_BACK:
      ESP_LOGW(TAG, "Update to %s rolled back to %s; automatic installs will skip it", before.installing,
               running.c_str());
      break;
    case ::espn::UpdateOutcome::NOT_INSTALLED:
      ESP_LOGI(TAG, "Update to %s didn't finish; still on %s", before.installing, running.c_str());
      break;
  }
  if (memcmp(&before, &this->update_mark_, sizeof(before)) != 0)
    this->update_mark_pref_.save(&this->update_mark_);
  if (this->update_mark_.failed[0] != '\0')
    ESP_LOGW(TAG, "Update %s rolled back on this panel before", this->update_mark_.failed);
  App.get_updates()[0]->add_on_state_callback([this]() { this->update_state_changed_(); });
#endif
}

void GamedayComponent::update_state_changed_() {
#if defined(USE_UPDATE) && defined(ESPHOME_PROJECT_VERSION)
  update::UpdateEntity *u = App.get_updates()[0];
  if (u->state == update::UPDATE_STATE_INSTALLING) {
    if (this->update_installing_)
      return;  // a progress update
    // Written to flash before the download starts, since the next thing
    // this firmware may do is restart into the new one.
    this->update_installing_ = true;
    ::espn::update_mark_set(this->update_mark_.installing, u->update_info.latest_version);
    this->update_mark_pref_.save(&this->update_mark_);
    global_preferences->sync();
  } else if (this->update_installing_) {
    // Back from installing with no restart: it failed before the new image
    // was in place, and this firmware runs on. Not a rollback.
    ESP_LOGI(TAG, "Update to %s didn't install; still on %s", this->update_mark_.installing, ESPHOME_PROJECT_VERSION);
    this->update_installing_ = false;
    this->update_mark_.installing[0] = '\0';
    this->update_mark_pref_.save(&this->update_mark_);
  }
#endif
}

bool GamedayComponent::board_live() const {
  if (!this->game_.valid)
    return false;
  // A golf event is IN all week, overnight breaks included: only a round
  // being played counts, so the nights between rounds can still update. A
  // race weekend is the same: only a session on track, and not a race
  // stopped overnight.
  if (this->game_.sport == ::espn::Sport::GOLF)
    return this->game_.golf.phase == ::espn::GolfPhase::LIVE;
  if (this->game_.sport == ::espn::Sport::RACING)
    return ::espn::racing_on_track(this->game_,
                                   this->time_ != nullptr ? (int64_t) this->time_->timestamp_now() : 0);
  return this->game_.state == GameState::IN;
}

// A game the panel follows is in progress: the one on the board, which
// covers My team and the live modes, or any favorite's in Favorite teams.
bool GamedayComponent::followed_game_live_() const {
  if (this->board_live())
    return true;
  // Golf and racing are never favorites.
  if (this->game_.valid && ::espn::is_board_sport(this->game_.sport))
    return false;
  if (this->favorites_mode_())
    for (const auto &e : this->fav_)
      if (e.game.valid && e.game.state == GameState::IN)
        return true;
  return false;
}

void GamedayComponent::auto_update_tick_() {
#ifdef USE_UPDATE
  if (!this->auto_update() || this->time_ == nullptr || App.get_updates().empty())
    return;
  update::UpdateEntity *u = App.get_updates()[0];
  ESPTime now = this->time_->now();
  ::espn::AutoUpdateInputs in;
  in.enabled = true;
  in.clock_valid = now.is_valid();
  in.hour = now.hour;
  in.minute = now.minute;
  in.today = ::espn::auto_update_day(now.year, now.day_of_year);
  in.tried_day = this->prefs6_.tried_day;
  in.checked_day = this->auto_update_checked_;
  uint8_t mac[6];
  get_mac_address_raw(mac);
  in.slot = ::espn::auto_update_slot(mac[4], mac[5]);
  in.check_done = millis() - this->auto_update_check_ms_ >= AUTO_UPDATE_CHECK_WAIT;
  in.available = u->state == update::UPDATE_STATE_AVAILABLE;
  in.newer = ::espn::version_newer(u->update_info.latest_version, u->update_info.current_version);
  in.live = this->followed_game_live_();
  in.failed = ::espn::update_failed_here(this->update_mark_, u->update_info.latest_version);
#ifdef GAMEDAY_BAR
  in.now = in.available && ::espn::install_now_listed(u->update_info.summary, this->hostname()) &&
           fnv1_hash(u->update_info.latest_version) != this->now_tried_;
#endif
  switch (::espn::auto_update_step(in)) {
    case ::espn::AutoUpdateStep::IDLE:
      // So the next wait for a game is logged too (an install-now one comes
      // at any hour, not once a night).
      this->auto_update_waiting_ = false;
      break;
    case ::espn::AutoUpdateStep::CHECK:
      ESP_LOGI(TAG, "Auto update: checking for a new release");
      this->auto_update_checked_ = in.today;
      this->auto_update_check_ms_ = millis();
      this->auto_update_waiting_ = false;
      u->check();
      break;
    case ::espn::AutoUpdateStep::WAIT_GAME:
      if (!this->auto_update_waiting_)
        ESP_LOGI(TAG, "Auto update: %s is out, waiting for the live game to end",
                 u->update_info.latest_version.c_str());
      this->auto_update_waiting_ = true;
      break;
    case ::espn::AutoUpdateStep::SKIP_FAILED:
      if (this->auto_update_skipped_ != in.today)
        ESP_LOGI(TAG, "Auto update: %s rolled back on this panel, not installing it on its own",
                 u->update_info.latest_version.c_str());
      this->auto_update_skipped_ = in.today;
      break;
    case ::espn::AutoUpdateStep::INSTALL:
      // Saved, and written to flash now, before the install starts: a power
      // cut partway through boots the old firmware, which must not try again
      // until tomorrow night.
      ESP_LOGI(TAG, "Auto update: installing %s (running %s)", u->update_info.latest_version.c_str(),
               u->update_info.current_version.c_str());
      this->prefs6_.tried_day = in.today;
      this->pref6_.save(&this->prefs6_);
      global_preferences->sync();
      u->perform();
      break;
    case ::espn::AutoUpdateStep::INSTALL_NOW:
#ifdef GAMEDAY_BAR
      // Same as above, but the try is for this version, not tonight: a
      // failed one is left to the nightly rule, which tries once in the
      // next 3 to 5 AM window (the same night, if it failed inside one).
      ESP_LOGI(TAG, "Auto update: installing %s now, as the beta channel asks (running %s)",
               u->update_info.latest_version.c_str(), u->update_info.current_version.c_str());
      this->now_tried_ = fnv1_hash(u->update_info.latest_version);
      this->bar_pref_.save(&this->now_tried_);
      global_preferences->sync();
      u->perform();
#endif
      break;
  }
#endif
}

void GamedayComponent::apply_timezone_() {
#ifdef GAMEDAY_TZ_PARSED
  // ESPHome 2026.9+: no POSIX parser on the device, hand the clock the
  // pre-parsed rules (scripts/build_timezones.py keeps the table in step).
  const ::espn::TzParsed &p = ::espn::kTimezonesParsed[this->prefs_.tz_index];
  auto rule = [](const ::espn::TzRule &r) {
    time::DSTRule d{};
    d.time_seconds = r.time_seconds;
    d.day = r.day;
    d.type = (time::DSTRuleType) r.type;
    d.month = r.month;
    d.week = r.week;
    d.day_of_week = r.day_of_week;
    return d;
  };
  time::ParsedTimezone tz{};
  tz.std_offset_seconds = p.std_offset_seconds;
  tz.dst_offset_seconds = p.dst_offset_seconds;
  tz.dst_start = rule(p.dst_start);
  tz.dst_end = rule(p.dst_end);
  time::set_global_tz(tz);
#else
  if (this->time_ != nullptr)
    this->time_->set_timezone(::espn::kTimezones[this->prefs_.tz_index].posix);
#endif
}

// A failed read with only the team change's logo on the board: drop it, so
// emit_ draws an empty board and says why ("Can't reach ESPN") instead of a
// game layout with no opponent.
void GamedayComponent::drop_team_placeholder_() {
  if (::espn::is_team_placeholder(this->game_))
    this->game_ = GameSnapshot{};
}

void GamedayComponent::reset_game_() {
  this->rebuild_favorites_(true);
  this->follow_ = ::espn::FollowPin{};  // the pin goes with the board
  this->lingered_event_.clear();
  this->linger_backoff_ms_ = 0;
  this->live_away_id_ = 0;
  this->live_started_ms_ = 0;
  this->live_unread_ = false;
  this->live_none_ = false;
  this->live_rescan_ms_ = NO_LIVE_RESCAN;
  this->live_fallback_ = false;
  // The finals and the walks stay: they are per league, not per mode.
  this->recap_on_ = false;
  this->recap_cards_.clear();
  this->recap_step_ = 0;
  this->recap_step_ms_ = 0;
  this->recap_scan_next_ = 0;
  // Idle scores restart; My team today forgets the final.
  this->side_on_ = false;
  this->a2_scores_ = false;  // and the second area's (a2_tick_)
  this->scan_due_now_();
  this->side_wait_ms_ = 0;
  if (this->idle_snaps_.size() >= 2)
    this->today_final_() = GameSnapshot{};
  this->schedule_ = Schedule{};
  this->schedule_fetched_ms_ = 0;
  this->comps_.clear();
  this->comp_next_ = -1;
  this->done_event_.clear();
  this->force_schedule_ = false;
  this->upcoming_.clear();
  this->upcoming_due_ = false;
  this->upcoming_after_poll_ = false;
  this->cache_unconfirmed_ = false;
  this->game_ = GameSnapshot{};
  this->prev_ = GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
  this->no_game_confirmed_ = false;
  this->board_stale_reads_ = 0;
  this->drop_visit_();
  this->watch_reset_();
}

void GamedayComponent::restart_board_() {
  this->reset_game_();
  this->generation_++;  // a scan in flight read what the board followed before
  this->emit_({});      // clear the board while the next fetch runs
  this->fire_action_("board_pending");  // the boot screen holds (gameday-common.yaml)
  this->end_backoff_wait_();
  this->schedule_next_(0);
  // Somebody changed what the panel follows, so they are looking at it.
  this->fire_action_("user_pick");
}

// Without this the finals and the "No live ... games" wait from the old
// leagues stayed up until the next scheduled scan, up to three hours.
bool GamedayComponent::live_leagues_changed_(const std::vector<League> &before) {
  if (!::espn::live_scan_changed(this->effective_mode_(), before, this->live_leagues_()))
    return false;
  ESP_LOGI(TAG, "Live games leagues changed: scanning again");
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->restart_board_();
  return true;
}

std::shared_ptr<http_request::HttpContainer> GamedayComponent::open_(const std::string &url) {
  std::vector<http_request::Header> headers = {
      {"User-Agent", USER_AGENT},
      {"Accept", "application/json"},
  };
  // Each host's answers and refusals are tracked apart.
  const ::espn::Source src = ::espn::is_mlb_url(url)  ? ::espn::Source::MLB_STATS
                             : ::espn::is_ht_url(url) ? ::espn::Source::HOCKEYTECH
                                                      : ::espn::Source::ESPN;
  const bool espn = src == ::espn::Source::ESPN;
  Job::HostJob &side = this->job_.host[(size_t) src];
  (espn ? this->job_.read_espn : side.read) = true;
  this->espn_requests_++;
  auto container = this->http_->get(url, headers, {"retry-after"});
  (espn ? this->espn_status_ : this->host_status_[(size_t) src]) = container == nullptr ? 0 : container->status_code;
  (espn ? this->espn_answer_ms_ : this->host_answer_ms_[(size_t) src]) = millis() == 0 ? 1 : millis();
  if (container == nullptr) {
    ESP_LOGW(TAG, "Request failed: %s", url.c_str());
    return nullptr;
  }
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "HTTP %d for %s", container->status_code, url.c_str());
    if (::espn::is_throttle_status(container->status_code)) {
      // Worker task: the job is this task's until it reports done.
      (espn ? this->job_.throttled : side.throttled) = true;
      uint32_t ra = ::espn::parse_retry_after_ms(container->get_response_header("retry-after"));
      uint32_t &most = espn ? this->job_.retry_after_ms : side.retry_after_ms;
      if (ra > most)
        most = ra;
    }
    container->end();
    return nullptr;
  }
  this->espn_ok_reads_++;
  return container;
}

// ---- Sign-in Wi-Fi (captive.h) ---------------------------------------------

struct ProbeHeaders {
  std::string location;
  std::string content_type;
};

static esp_err_t probe_event(esp_http_client_event_t *evt) {
  if (evt->event_id != HTTP_EVENT_ON_HEADER || evt->user_data == nullptr || evt->header_key == nullptr ||
      evt->header_value == nullptr)
    return ESP_OK;
  auto *h = static_cast<ProbeHeaders *>(evt->user_data);
  // Only the host matters, and a portal's link can carry a long query.
  if (strcasecmp(evt->header_key, "Location") == 0)
    h->location.assign(evt->header_value, strnlen(evt->header_value, 256));
  else if (strcasecmp(evt->header_key, "Content-Type") == 0)
    h->content_type.assign(evt->header_value, strnlen(evt->header_value, 64));
  return ESP_OK;
}

// Probe task. Its own esp_http_client rather than http_request, which follows
// redirects: a portal's redirect is the answer, and following it to an HTTPS
// sign-in page with its own certificate would only fail.
void GamedayComponent::probe_task_(void *arg) {
  auto *self = static_cast<GamedayComponent *>(arg);
  ProbeHeaders h;
  esp_http_client_config_t cfg = {};
  cfg.url = self->probe_url_.load();
  cfg.timeout_ms = 10000;
  cfg.disable_auto_redirect = true;
  cfg.event_handler = &probe_event;
  cfg.user_data = &h;
  cfg.user_agent = USER_AGENT;
  int status = 0;
  std::string body;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client != nullptr) {
    esp_http_client_set_header(client, "Accept", "application/json");
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
      status = esp_http_client_get_status_code(client);
      char buf[256];
      int n = esp_http_client_read(client, buf, sizeof(buf));
      if (n > 0)
        body.assign(buf, n);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }
  ::espn::Probe r = ::espn::classify_probe(status, h.location, h.content_type, body);
  const bool redirect = status >= 300 && status < 400 && !h.location.empty();
  ESP_LOGI(TAG, "Wi-Fi check: %s (HTTP %d%s%s)", ::espn::probe_name(r), status, redirect ? ", to " : "",
           redirect ? ::espn::detail::url_host(h.location).c_str() : "");
  self->probe_result_ = (uint8_t) r;
  self->probe_done_ = true;
  PsramTasks::park();
}

void GamedayComponent::start_probe_() {
  this->probe_join_done_ = true;
  this->probe_ms_ = millis();
  this->probe_ok_mark_ = this->espn_ok_reads_.load();
  this->probe_done_ = false;
  this->probe_running_ = true;
  // Plain HTTP and a 256 byte read, no TLS: a small stack is plenty.
  if (!PsramTasks::start(&GamedayComponent::probe_task_, "gameday_probe", 6144, this)) {
    ESP_LOGW(TAG, "Could not start the Wi-Fi check (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->probe_running_ = false;
  }
}

void GamedayComponent::set_captive_(bool on) {
  if (on == this->captive_)
    return;
  this->captive_ = on;
  if (on)
    ESP_LOGW(TAG, "This Wi-Fi needs a sign-in: a sign-in page answered in ESPN's place");
  else
    ESP_LOGI(TAG, "Wi-Fi sign-in state cleared");
  this->emit_({});
  this->fire_action_(on ? "captive" : "captive_clear");  // boot.yaml shows or hides the screen
}

// Once per join, then again while on a sign-in network or while ESPN reads
// fail (captive.h, probe_due). Any ESPN answer over HTTPS ends the state, so
// a network that only looks like a portal to the probe never covers a board
// that is working.
void GamedayComponent::captive_tick_() {
  const uint32_t now = millis();
  if (this->probe_running_) {
    if (!this->probe_done_.load())
      return;
    this->probe_running_ = false;
    const auto r = (::espn::Probe) this->probe_result_.load();
    if (r == ::espn::Probe::OPEN || this->espn_ok_reads_.load() != this->probe_ok_mark_) {
      this->captive_suspect_ = false;
      this->set_captive_(false);
    } else if (r == ::espn::Probe::CAPTIVE && network::is_connected() && !this->demo_playing_()) {
      if (this->clock_set_() && this->fail_run_.streak == 0 && !this->captive_) {
        // Nothing says ESPN is out of reach yet (a DNS block page can look
        // like a portal to the probe): ask ESPN now and say so only if that
        // read fails too. With no clock nothing can be read to check.
        // Only ESPN counts: a sign-in page blocks it too.
        ESP_LOGI(TAG, "Wi-Fi check saw a sign-in page: reading ESPN now to check");
        this->captive_suspect_ = true;
        this->suspect_fails_mark_ = this->read_fails_;
        this->end_backoff_wait_();
        if (!this->busy_)
          this->schedule_next_(0);
      } else {
        this->set_captive_(true);
      }
    }
  }
  if (!network::is_connected() || this->demo_playing_()) {
    this->probe_join_ms_ = 0;
    this->probe_join_done_ = false;
    this->captive_suspect_ = false;
    this->set_captive_(false);
    return;
  }
  if (this->probe_join_ms_ == 0)
    this->probe_join_ms_ = now == 0 ? 1 : now;
  if ((this->captive_ || this->captive_suspect_) && this->espn_ok_reads_.load() != this->probe_ok_mark_) {
    ESP_LOGI(TAG, "ESPN answered: this Wi-Fi %s", this->captive_ ? "no longer needs a sign-in" : "is open");
    this->captive_suspect_ = false;
    this->set_captive_(false);
  } else if (this->captive_suspect_ && this->read_fails_ != this->suspect_fails_mark_) {
    this->captive_suspect_ = false;
    this->set_captive_(true);
  }
  // Nothing is due inside the shortest recheck wait.
  if (this->probe_join_done_ && now - this->probe_ms_ < ::espn::kProbeCaptiveRecheckMs)
    return;
  // A sign-in network usually blocks the time server too, and with no clock
  // nothing is read from ESPN, so a clock still unset 30 s after joining
  // counts as reads failing.
  const bool failing = this->fail_run_.streak > 0 || this->other_host_failing_() ||
                       (!this->clock_set_() && now - this->probe_join_ms_ >= 30000);
  if (::espn::probe_due(this->probe_join_done_, this->captive_, failing, now - this->probe_ms_))
    this->start_probe_();
}

// Mixed-host scans: one host's failure is its own.
static bool scan_mixed(const std::vector<League> &leagues) {
  for (League l : leagues)
    if (::espn::league_source(l) != ::espn::league_source(leagues[0]))
      return true;
  return false;
}

uint32_t GamedayComponent::back_off_() {
  this->read_fails_++;
  const ::espn::Source host = this->job_host_();
  if (host != ::espn::Source::ESPN) {
    ::espn::HostWait &w = this->host_wait_[(size_t) host];
    const Job::HostJob &h = this->job_.host[(size_t) host];
    uint32_t ms = w.fail(h.throttled, h.retry_after_ms, random_uint32(), millis());
    ESP_LOGW(TAG, "%u failed %s read(s) in a row%s: next try in %us", (unsigned) w.run.streak,
             HOST_NAMES[(size_t) host], w.run.throttled ? ", refused" : "", (unsigned) (ms / 1000));
    // The fallback rescan never waits for that host.
    if (this->live_fallback_ && !this->job_.need_scan && ms > this->rescan_left_())
      ms = this->rescan_left_();
    return ms;
  }
  if (!this->job_.read_espn) {
    // Two other hosts failed: wait for the first to end.
    uint32_t first = 0;
    for (size_t i = 1; i < ::espn::kSourceCount; i++) {
      uint32_t w = this->host_wait_[i].wait_ms;
      if (this->job_.host[i].read && w != 0 && (first == 0 || w < first))
        first = w;
    }
    if (first != 0)
      return first;
  }
  uint32_t ms = this->fail_run_.fail(this->job_.throttled, this->job_.retry_after_ms, random_uint32());
  ESP_LOGW(TAG, "%u failed read(s) in a row%s: next try in %us", (unsigned) this->fail_run_.streak,
           this->fail_run_.throttled ? ", ESPN is refusing" : "", (unsigned) (ms / 1000));
  this->backoff_since_ms_ = millis();
  this->backoff_ms_ = ms;
  return ms;
}

bool GamedayComponent::fetch_schedule_(const ::espn::Team *team, Schedule &out, const char *comp_slug) {
  if (::espn::is_mlb_stats(team->league))
    return this->fetch_mlb_schedule_(team, out);
  if (::espn::is_hockeytech(team->league))
    return this->fetch_ht_schedule_(team, out);
  std::string url = comp_slug != nullptr ? ::espn::team_url(team->league, team->espn_id, comp_slug)
                                         : ::espn::team_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching schedule: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  Schedule s;
  // Seeds count only for a pro team's league read.
  bool ok = ::espn::parse_team(reader, s, comp_slug == nullptr && !::espn::is_college(team->league));
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Schedule parse failed after %u bytes", (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Schedule: event %s kickoff %lld group %u (%u bytes)", s.event_id.c_str(),
           (long long) s.kickoff_epoch, (unsigned) s.group, (unsigned) reader.total());
  out = std::move(s);
  return true;
}

bool GamedayComponent::needs_lookahead_(League league) const {
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  return info != nullptr && info->sport != ::espn::Sport::FOOTBALL && !::espn::is_board_sport(info->sport) &&
         !info->season_list;
}

int GamedayComponent::fetch_team_day_(const ::espn::Team *team, uint32_t group, int64_t day_epoch,
                                      const std::string &skip_event, Schedule &out, const char *comp_slug) {
  std::string url = ::espn::team_day_url(team->league, group, day_epoch, comp_slug);
  ESP_LOGD(TAG, "Looking ahead: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return -1;
  ContainerReader reader(container, this->espn_bytes_);
  int found = ::espn::parse_team_game(reader, team->espn_id, skip_event, out);
  container->end();
  return found;
}

// ESPN's team endpoint names a team's next game as soon as the current one
// starts (MIL and LAD on 2026-10-12; soccer the same, CRU and BRA in Brazil's
// Serie A that night, and a cup's team endpoint is the same endpoint; college
// football too, FLA @ TEX on 2026-10-17). So a
// read during a game (a boot, a team pick, the six-hour read, a cup read)
// checks today's scoreboard for one on now. Its date is taken 9 hours back
// (team_day_url's 5 plus 4 here), so a late West Coast game still finds its
// own day after midnight Eastern.
void GamedayComponent::take_game_on_now_(const ::espn::Team *team, Schedule &s, const std::string &done_event,
                                         const char *comp_slug) {
  // Minor league team reads list today's games themselves.
  if (::espn::league_source(team->league) != ::espn::Source::ESPN)
    return;
  int64_t now = (int64_t) this->time_->timestamp_now();
  if (!s.event_id.empty() && s.kickoff_epoch <= now)
    return;
  Schedule today;
  if (this->fetch_team_day_(team, s.group, now - 4 * 3600, done_event, today, comp_slug) > 0 &&
      ::espn::today_game_first(s, today)) {
    ESP_LOGI(TAG, "%s is playing now: event %s", team->abbr, today.event_id.c_str());
    s.event_id = today.event_id;
    s.kickoff_epoch = today.kickoff_epoch;
    s.time_tbd = today.time_tbd;
    // The team endpoint's opponent belongs to the next game.
    s.opp_id = 0;
    s.opp_abbr.clear();
  }
}

bool GamedayComponent::fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out,
                                       Schedule *read) {
  if (::espn::is_mlb_stats(team->league)) {
    Schedule s;
    return this->fetch_mlb_schedule_(team, s, &out);
  }
  if (::espn::is_hockeytech(team->league)) {
    Schedule s;
    return this->fetch_ht_schedule_(team, read != nullptr ? *read : s, &out);
  }
  const ::espn::LeagueInfo *info = ::espn::league_info(team->league);
  if (info != nullptr && !info->season_list) {
    out.clear();  // the season schedule is megabytes in this league: no Up next list
    return true;
  }
  std::string url = ::espn::schedule_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching upcoming games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  // Four: the game on the board plus the three after it.
  bool ok = ::espn::parse_upcoming(reader, team->espn_id, 4, out, ::espn::is_college(team->league));
  container->end();
  ESP_LOGI(TAG, "Upcoming: %u game(s) (%u bytes)", (unsigned) out.size(), (unsigned) reader.total());
  return ok;
}

bool GamedayComponent::fetch_live_games_(League league, std::vector<::espn::LiveGame> &out, int64_t &next_kickoff,
                                         std::vector<::espn::Final> *finals, std::vector<::espn::Pending> *pending,
                                         int64_t epoch) {
  const int64_t day = epoch != 0 ? epoch : (int64_t) this->time_->timestamp_now();
  const ::espn::Source src = ::espn::league_source(league);
  const bool mlb = src == ::espn::Source::MLB_STATS, ht = src == ::espn::Source::HOCKEYTECH;
  std::string url = mlb  ? ::espn::mlb_day_url(day)
                    : ht ? ::espn::ht_day_url(league, day)
                         : ::espn::scan_url(league, day);
  ESP_LOGD(TAG, "Scanning for live games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  size_t before = out.size();
  size_t finals_before = finals != nullptr ? finals->size() : 0;
  size_t pending_before = pending != nullptr ? pending->size() : 0;
  int64_t next = 0;
  bool ok = mlb  ? ::espn::parse_mlb_day(reader, out, &next, finals, pending)
            : ht ? ::espn::parse_ht_day(reader, out, &next, finals, pending)
                 : ::espn::parse_live_games(reader, out, &next, finals, pending);
  for (size_t i = before; i < out.size(); i++)
    out[i].league = (uint8_t) league;
  if (finals != nullptr)
    for (size_t i = finals_before; i < finals->size(); i++)
      (*finals)[i].league = (uint8_t) league;
  if (pending != nullptr)
    for (size_t i = pending_before; i < pending->size(); i++)
      (*pending)[i].league = (uint8_t) league;
  container->end();
  ESP_LOGI(TAG, "Scan: %u live game(s), %u final(s), %u to come (%u bytes)", (unsigned) out.size(),
           (unsigned) (finals != nullptr ? finals->size() - finals_before : 0),
           (unsigned) (pending != nullptr ? pending->size() - pending_before : 0), (unsigned) reader.total());
  if (next != 0 && (next_kickoff == 0 || next < next_kickoff))
    next_kickoff = next;
  return ok;
}

bool GamedayComponent::fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out) {
  bool live = is_live_mode_(this->job_.mode) || this->job_.visit || this->job_.a2 != 0;
  League league = live || this->job_.fav_index >= 0 ? (League) schedule.league : team->league;
  // A per_event league reads the one game, 8 to 20 KB a poll. The others read
  // the day's scoreboard it sits in, up to 750 KB for college football.
  if (::espn::is_mlb_stats(league))
    return this->fetch_mlb_game_(schedule, out);
  if (::espn::is_hockeytech(league))
    return this->fetch_ht_game_(league, schedule, out);
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  bool per_event = info != nullptr && info->per_event;
  std::string url = per_event ? ::espn::event_url(league, schedule.event_id, schedule.comp_slug)
                              : ::espn::scoreboard_url(league, schedule.group, schedule.kickoff_epoch,
                                                       schedule.time_tbd);
  ESP_LOGD(TAG, "Fetching game: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  GameSnapshot g;
  // Keeps favorite players' leader lines and last-play athletes.
  const ::espn::PlayerWatch watch = ::espn::player_watch(this->job_.players.players, (uint8_t) league);
  bool ok = per_event ? ::espn::parse_event(reader, info->sport, this->our_id_(), g, schedule.event_id, &watch)
                      : ::espn::parse_scoreboard(reader, schedule.event_id, this->our_id_(), g);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Game %s not parsed (%u bytes)", schedule.event_id.c_str(),
             (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Game: %s %s %d - %s %d [%s] (%u bytes)", ::espn::state_name(g.state), g.team_abbr.c_str(),
           g.team_score, g.opp_abbr.c_str(), g.opp_score, g.short_detail.c_str(), (unsigned) reader.total());
  out = std::move(g);
  return true;
}

// ---- Minor league baseball (MLB Stats API, source_mlbstats.h) ----------------

bool GamedayComponent::fetch_mlb_schedule_(const ::espn::Team *team, Schedule &out,
                                           std::vector<::espn::Upcoming> *upcoming, Schedule *last_final) {
  const int64_t now = (int64_t) this->time_->timestamp_now();
  Schedule s;
  // Coming days, then next season's first game if none.
  for (int pass = 0; pass < (upcoming != nullptr ? 1 : 2); pass++) {
    std::string url = pass == 0 ? ::espn::mlb_team_url(team->espn_id, now)
                                : ::espn::mlb_far_url(team->espn_id, team->group, now);
    ESP_LOGD(TAG, "Fetching schedule: %s", url.c_str());
    auto container = this->open_(url);
    if (container == nullptr)
      return false;
    ContainerReader reader(container, this->espn_bytes_);
    int r = ::espn::parse_mlb_team(reader, team->espn_id, now, s, upcoming, 4, pass == 0 ? last_final : nullptr);
    container->end();
    if (r < 0) {
      ESP_LOGW(TAG, "Schedule parse failed after %u bytes", (unsigned) reader.total());
      return false;
    }
    ESP_LOGI(TAG, "Schedule: game %s kickoff %lld, %u upcoming (%u bytes)", r > 0 ? s.event_id.c_str() : "none",
             (long long) s.kickoff_epoch, (unsigned) (upcoming != nullptr ? upcoming->size() : 0),
             (unsigned) reader.total());
    if (r > 0)
      break;
  }
  out = std::move(s);
  return true;
}

bool GamedayComponent::fetch_mlb_game_(const Schedule &schedule, GameSnapshot &out) {
  std::string url = ::espn::mlb_game_url(schedule.event_id);
  ESP_LOGD(TAG, "Fetching game: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  GameSnapshot g;
  bool ok = ::espn::parse_mlb_game(reader, this->our_id_(), schedule.kickoff_epoch, g);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Game %s not parsed (%u bytes)", schedule.event_id.c_str(), (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Game: %s %s %d - %s %d [%s] (%u bytes)", ::espn::state_name(g.state), g.team_abbr.c_str(),
           g.team_score, g.opp_abbr.c_str(), g.opp_score, g.short_detail.c_str(), (unsigned) reader.total());
  out = std::move(g);
  return true;
}

// ---- Minor league hockey (HockeyTech, source_hockeytech.h) -------------------

bool GamedayComponent::fetch_ht_schedule_(const ::espn::Team *team, Schedule &out,
                                          std::vector<::espn::Upcoming> *upcoming, Schedule *last_final) {
  std::string url = ::espn::ht_team_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching schedule: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  Schedule s;
  // Up next: the board's game plus three more.
  int r = ::espn::parse_ht_team(reader, team->espn_id, (int64_t) this->time_->timestamp_now(), s, upcoming, 4,
                                last_final);
  container->end();
  if (r < 0) {
    ESP_LOGW(TAG, "Schedule parse failed after %u bytes", (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Schedule: game %s kickoff %lld, %u upcoming (%u bytes)", r > 0 ? s.event_id.c_str() : "none",
           (long long) s.kickoff_epoch, (unsigned) (upcoming != nullptr ? upcoming->size() : 0),
           (unsigned) reader.total());
  out = std::move(s);
  return true;
}

bool GamedayComponent::fetch_ht_game_(League league, const Schedule &schedule, GameSnapshot &out) {
  std::string url = ::espn::ht_game_url(league, schedule.event_id);
  ESP_LOGD(TAG, "Fetching game: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  GameSnapshot g;
  bool ok = ::espn::parse_ht_game(reader, league, this->our_id_(), g);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Game %s not parsed (%u bytes)", schedule.event_id.c_str(), (unsigned) reader.total());
    return false;
  }
  ::espn::ht_fill_from_schedule(g, schedule);
  ESP_LOGI(TAG, "Game: %s %s %d - %s %d [%s] (%u bytes)", ::espn::state_name(g.state), g.team_abbr.c_str(),
           g.team_score, g.opp_abbr.c_str(), g.opp_score, g.short_detail.c_str(), (unsigned) reader.total());
  out = std::move(g);
  return true;
}

int GamedayComponent::fetch_board_(League league, GameSnapshot &out) {
  std::string url = ::espn::header_url(league);
  ESP_LOGD(TAG, "Fetching header: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return -1;
  ContainerReader reader(container, this->espn_bytes_);
  GameSnapshot g;
  const bool racing = ::espn::is_racing(league);
  int r = racing ? ::espn::parse_racing(reader, g) : ::espn::parse_golf(reader, g);
  container->end();
  if (r < 0) {
    ESP_LOGW(TAG, "Header not parsed (%u bytes)", (unsigned) reader.total());
    return -1;
  }
  // Nothing listed still says which sport, for the calendar read after it.
  if (r == 0)
    g.sport = ::espn::league_sport(league);
  ESP_LOGI(TAG, "Header: %s %s, %u rows (%u bytes)", r > 0 ? g.event_id.c_str() : "no event",
           r > 0 ? g.short_detail.c_str() : "", (unsigned) (racing ? g.race.rows.size() : g.golf.rows.size()),
           (unsigned) reader.total());
  out = std::move(g);
  return r;
}

bool GamedayComponent::fetch_board_next_(League league, const std::string &after_event, int64_t after_start,
                                         int64_t now, ::espn::GolfNext &out) {
  out = ::espn::GolfNext{};
  // This season's calendar first, then next season's once this one is over.
  for (int pass = 0; pass < 2; pass++) {
    std::string url = ::espn::golf_calendar_url(league, now, pass == 1);
    ESP_LOGD(TAG, "Fetching calendar: %s", url.c_str());
    auto container = this->open_(url);
    if (container == nullptr)
      return false;
    ContainerReader reader(container, this->espn_bytes_);
    int r = ::espn::is_racing(league) ? ::espn::parse_racing_calendar(reader, now, after_event, after_start, out)
                                      : ::espn::parse_golf_calendar(reader, now, after_event, out);
    container->end();
    if (r < 0) {
      ESP_LOGW(TAG, "Calendar not parsed (%u bytes)", (unsigned) reader.total());
      return false;
    }
    ESP_LOGI(TAG, "Next event: %s (%u bytes)", r > 0 ? out.event_id.c_str() : "none", (unsigned) reader.total());
    if (r > 0)
      return true;
  }
  return true;  // nothing listed ahead in either season
}

uint32_t GamedayComponent::interval_for_phase_() const {
  uint32_t ms;
  if (this->game_.valid && ::espn::is_board_sport(this->game_.sport)) {
    ms = ::espn::board_poll_ms(this->game_, (int64_t) this->time_->timestamp_now());
  } else {
  switch (this->game_.state) {
    case GameState::IN:
      ms = IN_INTERVAL;
      break;
    case GameState::POST:
      ms = POST_INTERVAL;
      break;
    case GameState::PRE: {
      // A TBD game's kickoff is a midnight placeholder: nothing to close in on.
      int64_t now = (int64_t) this->time_->timestamp_now();
      int64_t until = this->game_.kickoff_epoch - now;
      ms = ::espn::pre_poll_near(until, this->game_.time_tbd, this->game_.sport == ::espn::Sport::BASEBALL)
               ? PRE_NEAR_INTERVAL
               : PRE_FAR_INTERVAL;
      break;
    }
    default:
      ms = SCHEDULE_INTERVAL;
      break;
  }
  }
  // The fallback card is only borrowing the board: a 15 minute PRE cycle, or
  // the 6 hours an out of season team gets, must never outlast the rescan.
  if (this->live_fallback_) {
    uint32_t left = this->rescan_left_();
    if (ms > left)
      ms = left;
  }
  return ms;
}

uint32_t GamedayComponent::linger_ms_() const {
  const ::espn::Team *t = this->current_team_();
  League league = t != nullptr ? t->league : League::NFL;
  if (this->live_mode_() && this->schedule_.valid)
    league = (League) this->schedule_.league;
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  // The team endpoint named this final again after the last linger and the
  // look-ahead found nothing or failed: back off (run_job_, apply_job_).
  if (this->linger_backoff_ms_ != 0 && !this->lingered_event_.empty() &&
      this->game_.event_id == this->lingered_event_ && this->needs_lookahead_(league))
    return this->linger_backoff_ms_;
  return (uint32_t) (info != nullptr ? info->linger_min : 30) * MINUTE;
}

// Once the worker has it, job_ is the worker's.
GamedayComponent::Job &GamedayComponent::new_job_() {
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  return this->job_;
}

// Main loop: decide what this cycle needs and hand it to the worker task.
void GamedayComponent::start_job_() {
  uint32_t now = millis();
  Job &j = this->new_job_();
  j.players = this->players_;
  j.team = this->current_team_();
  j.mode = this->effective_mode_();
  j.scan_leagues = this->scan_leagues_();
  if (this->visit_.active) {
    // Visit: poll the big play's game until the stay ends.
    j.visit = true;
    j.schedule = this->visit_.sched;
    j.our_id = this->visit_.away_id;
    this->start_worker_();
    return;
  }
  {
    // Skip a failing host's leagues while others remain.
    auto held = [this, now](League l) {
      ::espn::Source s = ::espn::league_source(l);
      return s != ::espn::Source::ESPN && this->host_wait_[(size_t) s].waiting(now);
    };
    if (std::any_of(j.scan_leagues.begin(), j.scan_leagues.end(), [&held](League l) { return !held(l); }))
      j.scan_leagues.erase(std::remove_if(j.scan_leagues.begin(), j.scan_leagues.end(), held), j.scan_leagues.end());
  }
  if (this->favorites_mode_()) {
    this->start_fav_job_(now);
    return;
  }
  if (j.team == nullptr && !this->live_mode_()) {
    this->schedule_next_(SCHEDULE_INTERVAL);
    return;
  }
  if (this->live_mode_() && this->recap_on_) {
    // The finals rotation is up: read the days it still needs one by one,
    // then rescan for a live game when the wait is over.
    if (this->force_schedule_) {
      this->force_schedule_ = false;  // Refresh Now: scan now
      this->live_started_ms_ = 0;
    }
    // Not under the idle display: only today's scans then.
    if (this->idle_view_ == ::espn::IdleView::BOARD && this->recap_job_(j, (int64_t) this->time_->timestamp_now())) {
      this->start_worker_();
      return;
    }
    uint32_t since = now - this->live_started_ms_;
    uint32_t scan_left =
        this->live_started_ms_ != 0 && since < this->live_rescan_ms_ ? this->live_rescan_ms_ - since : 0;
    // A failing host holds only its own leagues' days.
    bool scan_held = !j.scan_leagues.empty();
    for (League l : j.scan_leagues)
      scan_held = scan_held && this->host_left_(l, now) != 0;
    uint32_t wait = ::espn::held_next_ms(scan_left, this->soonest_host_left_(now), scan_held);
    if (wait != 0) {
      this->schedule_next_(wait);
      return;
    }
    j.need_scan = true;
    j.want_finals = true;
    this->start_worker_();
    return;
  }
  if (this->live_mode_()) {
    // Inside the fallback live_started_ms_ times the rescan instead of the
    // rotate: the board is on the owner's own team until a game kicks off.
    bool rescan_due = this->live_started_ms_ == 0 || (now - this->live_started_ms_) >= this->live_rescan_ms_;
    if (!this->live_fallback_ || rescan_due) {
      if (this->live_fallback_) {
        j.need_scan = true;
        // Stamped here, not when the scan lands, so a failed scan does not
        // fire another one on the retry a minute later.
        this->live_started_ms_ = now == 0 ? 1 : now;
      } else {
        // Follow: no switch or rescan while pinned.
        bool held = this->following_();
        bool rotate = !held && ::espn::rotate_turn_over(now, this->live_started_ms_, this->rotate_seconds());
        bool ended = !held && this->game_.valid && this->game_.state != GameState::IN;
        j.need_scan = !this->schedule_.valid || rotate || ended || this->live_none_;
      }
      // Nothing live: the scan keeps finals for the rotation.
      j.want_finals = this->live_recap_() || this->a2_scores_;
      j.schedule = this->schedule_;
      this->start_worker_();
      return;
    }
    // In the fallback with the rescan not due: fall through to the My team
    // path below, so the card is fetched and polled by that code and not by
    // a copy of it.
    // A minor league card waits out its host's backoff.
    uint32_t held = j.team != nullptr ? this->host_left_(j.team->league, now) : 0;
    if (held != 0) {
      this->schedule_next_(::espn::held_next_ms(this->rescan_left_(), held, false));
      return;
    }
  }
  if (j.team != nullptr && ::espn::is_leaderboard(j.team->league)) {
    // A golf tour or a racing series: one header read, plus the calendar
    // after a final (run_board_job_). None of the team machinery below
    // applies.
    j.need_board = true;
    this->start_worker_();
    return;
  }
  if (!this->schedule_.valid && !this->team_cache_tried_) {
    this->team_cache_tried_ = true;
    if (this->load_team_cache_(j.team)) {
      this->schedule_fetched_ms_ = now == 0 ? 1 : now;
      this->cache_unconfirmed_ = true;
      // Not upcoming_due_ yet: that would put the season download in front
      // of the scoreboard, which is what the cache is here to skip.
      this->upcoming_after_poll_ = true;
    }
  }
  j.need_schedule = ::espn::schedule_due(this->schedule_.valid, this->force_schedule_, now,
                                         this->schedule_fetched_ms_, SCHEDULE_INTERVAL);
  // Minor league team reads skip ahead; poll the final first.
  if (j.need_schedule && this->schedule_.valid && j.team != nullptr &&
      ::espn::team_read_waits(::espn::league_source(j.team->league), this->post_since_ms_, now, this->linger_ms_()))
    j.need_schedule = false;
  // Soccer: after each league read, one cup's team read per cycle.
  if (this->comp_next_ > 0 && this->comp_next_ < (int) this->comps_.size() && !j.need_schedule) {
    j.comp = this->comp_next_;
    j.comps = this->comps_;
    j.schedule = this->schedule_;
    j.need_game = this->game_.event_id.empty() || this->game_.event_id != this->schedule_.event_id;
    j.done_event = this->done_event_;
    this->start_worker_();
    return;
  }
  // The season schedule is ~200KB; it is fetched on a cycle of its own right
  // after the board has its game, never in front of it.
  if (this->upcoming_due_ && !j.need_schedule && this->schedule_.valid) {
    j.need_upcoming = true;
    j.schedule = this->schedule_;
    this->start_worker_();
    return;
  }
  if (this->post_since_ms_ != 0 && (now - this->post_since_ms_) >= this->linger_ms_()) {
    // Game is over and lingered: look for the next one. Re-reading the team
    // endpoint is not enough on its own, because it can keep pointing at a
    // game that finished hours ago. The season schedule lists only games that
    // have not started, so prefer it when it has one.
    int64_t tnow = (int64_t) this->time_->timestamp_now();
    const ::espn::Upcoming *next = nullptr;
    // MiLB re-reads the team: doubleheader game 2 looks past.
    const bool reread = j.team != nullptr && ::espn::is_mlb_stats(j.team->league);
    for (const auto &u : this->upcoming_) {
      if (reread)
        break;
      if (u.kickoff_epoch > tnow && u.event_id != this->game_.event_id) {
        next = &u;
        break;
      }
    }
    this->prev_ = GameSnapshot{};
    this->done_event_ = this->game_.event_id;
    this->lingered_event_ = this->game_.event_id;
    this->game_ = GameSnapshot{};
    this->post_since_ms_ = 0;
    if (next != nullptr) {
      ESP_LOGI(TAG, "Game is over: taking event %s from the season schedule", next->event_id.c_str());
      // group, colors and the record are team properties from the team
      // endpoint and stay valid; only the event moves.
      this->schedule_.event_id = next->event_id;
      this->schedule_.kickoff_epoch = next->kickoff_epoch;
      this->schedule_.time_tbd = next->time_tbd;
      this->schedule_.comp_slug = nullptr;  // the list is the league's own
      if (!this->comps_.empty()) {
        // A cup game can come before the league's next fixture.
        this->comps_[0] = this->schedule_;
        int pick = ::espn::pick_schedule(this->comps_, tnow, this->done_event_);
        if (pick > 0) {
          this->schedule_ = this->comps_[pick];
          ESP_LOGI(TAG, "A cup game is first: event %s in %s", this->schedule_.event_id.c_str(),
                   this->schedule_.comp_slug);
        }
      }
      this->schedule_fetched_ms_ = now == 0 ? 1 : now;
      this->upcoming_due_ = true;  // the list just lost an entry: refresh it
    } else {
      j.need_schedule = true;  // no list yet: a fresh boot or the end of a season
    }
  }
  j.schedule = this->schedule_;
  size_t cups = ::espn::league_cup_count(j.team->league);
  if (j.need_schedule && cups > 0) {
    j.comps = this->comps_;
    j.comps.resize(cups + 1);
  }
  if (j.need_schedule) {
    j.done_event = this->done_event_;
    // A routine six-hour read must not cut a lingering final short.
    j.no_lookahead = this->post_since_ms_ != 0;
    j.today_since = this->today_since_();
    j.today_have = this->today_have_();
  }
  this->start_worker_();
}

void GamedayComponent::start_worker_() {
  this->job_done_ = false;
  this->busy_ = true;
  this->busy_since_ms_ = millis() == 0 ? 1 : millis();
  // TLS plus a nested JSON parse needs a roomy stack; 16KB has headroom.
  // Stack in PSRAM; loop() frees it once parked.
  if (!PsramTasks::start(&GamedayComponent::worker_, "gameday_fetch", 16384, this)) {
    ESP_LOGW(TAG, "Could not start the fetch task (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->busy_ = false;
    this->schedule_next_(RETRY_INTERVAL);
  }
}

void GamedayComponent::worker_(void *arg) {
  auto *self = static_cast<GamedayComponent *>(arg);
  uint32_t mine = self->worker_seq_;
  const uint32_t start = millis();
  self->run_job_();
  // A worker the loop gave up on must not hand back a result: the Job it
  // was writing into belongs to a later cycle by now.
  if (self->worker_seq_ == mine) {
    self->job_.took_ms = millis() - start;
    self->job_done_ = true;
  }
  PsramTasks::park();
}

// Worker task: network and parsing only, no display or entity access.
void GamedayComponent::run_job_() {
  Job &j = this->job_;
  if (j.need_clock) {
    // Any status will do, a 404 or a 503 carries the date just the same.
    std::vector<http_request::Header> headers = {{"User-Agent", USER_AGENT}};
    this->espn_requests_++;
    auto container = this->http_->get(HTTP_CLOCK_URL, headers, {"date"});
    if (container == nullptr) {
      ESP_LOGW(TAG, "Request failed: %s", HTTP_CLOCK_URL);
      return;
    }
    j.clock_got_ms = millis();
    if (container->status_code > 0)
      j.clock_epoch = ::espn::parse_http_date(container->get_response_header("date"));
    container->end();
    return;
  }
  if (j.need_board) {
    this->run_board_job_();
    return;
  }
  if (j.need_watch) {
    this->run_watch_job_();
    return;
  }
  if (j.need_recap) {
    // One day's scoreboard: its finals going back, its first kickoff and that
    // game's matchup going forward. The live list is always empty for a day
    // that is not today.
    std::vector<::espn::LiveGame> live;
    j.recap_ok = this->fetch_live_games_((League) j.recap_league, live, j.next_kickoff,
                                         j.recap_forward ? nullptr : &j.finals,
                                         j.recap_forward ? &j.pending : nullptr, ::espn::espn_day_epoch(j.recap_day));
    return;
  }
  if (j.need_scan) {
    std::vector<::espn::Final> *finals = j.want_finals ? &j.finals : nullptr;
    std::vector<::espn::Pending> *pending = j.want_finals ? &j.pending : nullptr;
    j.scan_ok = true;
    const bool mixed = scan_mixed(j.scan_leagues);
    bool answered = false;
    for (League l : j.scan_leagues) {
      bool ok = this->fetch_live_games_(l, j.live, j.next_kickoff, finals, pending);
      answered = answered || ok;
      ::espn::Source src = ::espn::league_source(l);
      if (src != ::espn::Source::ESPN) {
        Job::HostJob &h = j.host[(size_t) src];
        h.scan_failed = h.scan_failed || !ok;
        if (mixed)
          continue;  // that host's own backoff (host_scan_result), not the scan's
      }
      j.scan_ok = ok && j.scan_ok;
    }
    // Nothing answered: the scan failed, whatever hosts it asked.
    j.scan_ok = j.scan_ok && answered;
    return;  // the main loop picks a game, then the next cycle polls it
  }
  if (j.need_upcoming) {
    // AHL/ECHL records come back in j.schedule.
    Schedule read;
    j.upcoming_ok = this->fetch_upcoming_(j.team, j.upcoming, &read);
    if (j.upcoming_ok && read.valid)
      j.schedule = std::move(read);
    return;
  }
  if (j.comp > 0) {
    this->run_cup_job_();
    return;
  }
  if (j.need_schedule) {
    Schedule s;
    // Minor league team reads list today's final too.
    Schedule last_final;
    const bool mlb = ::espn::is_mlb_stats(j.team->league), ht = ::espn::is_hockeytech(j.team->league);
    if (j.today_since > 0 && mlb)
      j.schedule_ok = this->fetch_mlb_schedule_(j.team, s, nullptr, &last_final);
    else if (j.today_since > 0 && ht)
      j.schedule_ok = this->fetch_ht_schedule_(j.team, s, nullptr, &last_final);
    else
      j.schedule_ok = this->fetch_schedule_(j.team, s);
    if (!j.schedule_ok)
      return;
    if (last_final.valid &&
        ::espn::want_today_final(j.today_since, true,
                                 ::espn::day_order(last_final.kickoff_epoch, last_final.time_tbd), j.today_have)) {
      j.today_final = std::make_unique<GameSnapshot>();
      if (!(mlb ? this->fetch_mlb_game_(last_final, *j.today_final)
                : this->fetch_ht_game_(j.team->league, last_final, *j.today_final)))
        j.today_final.reset();
    }
    j.schedule = s;
    ::espn::adopt_league(j.schedule, j.team->league);
    // ESPN can also still call a game live that the panel saw finish.
    bool known_over = !j.done_event.empty() && j.schedule.event_id == j.done_event;
    if ((j.schedule.next_final || known_over) && !j.no_lookahead && this->needs_lookahead_(j.team->league)) {
      // The look-ahead skips today's final, so read it first.
      if (::espn::want_today_final(j.today_since, j.schedule.next_final, j.schedule.kickoff_epoch, j.today_have)) {
        j.today_final = std::make_unique<GameSnapshot>();
        if (!this->fetch_game_(j.team, j.schedule, *j.today_final))
          j.today_final.reset();
      }
      // ESPN's team endpoint keeps naming a finished game until the next one
      // is near (the previous night's finals were still named at 11 AM
      // Eastern on 2026-10-08). Without a season list, read the next days'
      // scoreboards for this team's next game. A failed read ends the search
      // so a later day's game is never taken over an unread earlier one.
      int64_t now = (int64_t) this->time_->timestamp_now();
      j.lookahead = 2;
      for (int d = 0; d < LOOKAHEAD_DAYS; d++) {
        if (!j.fits(true)) {
          j.lookahead = -1;  // retried later, so the board's next read keeps time
          break;
        }
        Schedule next;
        int found = this->fetch_team_day_(j.team, j.schedule.group, now + (int64_t) d * 86400, j.done_event, next);
        if (found < 0) {
          j.lookahead = -1;
          break;
        }
        if (found > 0) {
          ESP_LOGI(TAG, "Next game for %s: event %s", j.team->abbr, next.event_id.c_str());
          j.schedule.event_id = next.event_id;
          j.schedule.kickoff_epoch = next.kickoff_epoch;
          j.schedule.time_tbd = next.time_tbd;
          j.schedule.next_final = false;
          j.schedule.opp_id = next.opp_id;
          j.schedule.opp_abbr = next.opp_abbr;
          j.lookahead = 1;
          break;
        }
      }
    } else if (j.fits(true)) {
      this->take_game_on_now_(j.team, j.schedule, j.done_event);
    }
    if (!j.comps.empty()) {
      // Soccer: the cup reads from the last sweep still count.
      j.comps[0] = j.schedule;
      ::espn::league_standing(j.comps);
      int pick = ::espn::pick_schedule(j.comps, (int64_t) this->time_->timestamp_now(), j.done_event);
      if (pick > 0)
        j.schedule = j.comps[pick];
    }
    if (j.schedule.event_id.empty()) {
      j.no_event = true;
      return;
    }
    // Both logos download while the scoreboard read below runs, so the
    // board usually has them the moment it shows.
    League league = j.team->league;
    this->logos_.prefetch(::espn::team_logo_url(league, j.team->espn_id, j.team->abbr));
    if (j.schedule.opp_id != 0)
      this->logos_.prefetch(::espn::team_logo_url(league, j.schedule.opp_id, j.schedule.opp_abbr.c_str()));
    if (!j.fits(false)) {
      j.game_skipped = true;  // its own read in the next gap
      return;
    }
  }
  j.game_ok = this->fetch_game_(j.team, j.schedule, j.game);
}

bool GamedayComponent::Job::fits(bool day) const {
  return this->deadline_ms == 0 ||
         ::espn::watch_read_fits(millis(), this->deadline_ms, day ? this->day_ms : this->event_ms);
}

// Main loop: fold the worker's result into the component state and render.
void GamedayComponent::apply_job_() {
  Job &j = this->job_;
  if (j.need_clock) {
    this->apply_clock_job_();  // whatever the board follows now, it needs the clock
    return;
  }
  if (j.a2 != 0) {
    this->apply_a2_job_();  // own generation: main team changes don't affect it
    return;
  }
  if (j.generation != this->generation_) {
    ESP_LOGD(TAG, "Discarding a fetch for the previous team");
    return;
  }
  if (j.side) {
    this->apply_side_job_();  // leaves the mode's own next read where it was
    return;
  }
  uint32_t now = millis();
  if (j.need_watch) {
    this->apply_watch_job_(now);
    return;
  }
  if (j.visit) {
    this->apply_visit_job_();
    return;
  }
  if (j.fav_index >= 0) {
    this->apply_fav_job_(now);
    return;
  }
  if (j.need_board) {
    this->apply_board_job_(now);
    return;
  }
  if (j.need_recap) {
    this->apply_recap_job_();
    return;
  }
  if (j.need_scan) {
    // Single-host scans: back_off_ counts against that host.
    const bool mixed = scan_mixed(j.scan_leagues);
    for (size_t i = 1; i < ::espn::kSourceCount; i++) {
      const Job::HostJob &h = j.host[i];
      uint32_t ms = ::espn::host_scan_result(this->host_wait_[i], h.read, h.scan_failed, mixed, h.throttled,
                                             h.retry_after_ms, random_uint32(), now);
      if (ms != 0)
        ESP_LOGW(TAG, "%s scan failed: next try in %us", HOST_NAMES[i], (unsigned) (ms / 1000));
    }
    if (this->following_()) {
      // Follow arrived mid-scan: keep the followed game up.
      this->schedule_next_(this->interval_for_phase_());
      return;
    }
    if (!j.scan_ok && j.live.empty()) {
      this->misses_ = ::espn::add_miss(this->misses_);
      if (this->recap_on_)
        this->live_started_ms_ = 0;  // retry the scan, not the long wait
      this->drop_team_placeholder_();  // "Looking for a live game", not a bare logo
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    if (j.want_finals)
      this->recap_absorb_(j, (int64_t) this->time_->timestamp_now());
    this->clear_backoff_();
    // Prefer a different game than the one we just showed.
    // Skip the second area's team: it has its own area.
    std::vector<size_t> pick;
    const ::espn::Team *a2 = this->a2_show_() == ::espn::SplitShow::TEAM && !this->a2_team_.empty()
                                 ? this->a2_team_[0].team
                                 : nullptr;
    for (int pass = 0; pass < 2 && pick.empty(); pass++)
      for (size_t i = 0; i < j.live.size(); i++) {
        const ::espn::LiveGame &l = j.live[i];
        const bool theirs = a2 != nullptr && l.league == (uint8_t) a2->league &&
                            (l.away_id == a2->espn_id || l.home_id == a2->espn_id);
        if (l.event_id != this->schedule_.event_id && (pass == 1 || !theirs))
          pick.push_back(i);
      }
    if (pick.empty() && !j.live.empty())
      pick.push_back(0);
    if (pick.empty()) {
      // A failed scan took the retry above, so this kickoff covers every
      // league the mode reads.
      this->live_rescan_ms_ = ::espn::no_live_rescan_ms((int64_t) this->time_->timestamp_now(), j.next_kickoff,
                                                        NO_LIVE_RESCAN, NO_GAMES_RESCAN);
      if (this->live_rescan_ms_ > NO_LIVE_RESCAN)
        ESP_LOGI(TAG, "Next scan in %u min", (unsigned) (this->live_rescan_ms_ / MINUTE));
      const ::espn::Team *saved = this->current_team_();
      if (saved != nullptr && !this->live_recap_()) {
        // Nothing live, but the owner has a team: show that team's card and
        // keep scanning. The mode they picked is untouched.
        bool already = this->live_fallback_;
        this->live_none_ = true;
        this->live_fallback_ = true;
        this->live_started_ms_ = now == 0 ? 1 : now;  // start the rescan clock
        // No freshness marking here on purpose. What is on the board in the
        // fallback is the saved team's card, and only a poll of that card can
        // say how old it is. Marking a scan good here reset misses_ every
        // rescan, so a failing game endpoint never reached the three misses
        // the ticker warns at, and stale_s stayed near zero under a frozen
        // score. The card's own poll below marks it good when it lands.
        if (already) {
          // The card is already on the board: leave it there, and let the My
          // team path keep polling it rather than reloading it every scan.
          ESP_LOGI(TAG, "Still no live games: staying on %s", saved->abbr);
          this->rebuild_state_(&this->last_fields_);
          this->schedule_next_(0);
          return;
        }
        ESP_LOGI(TAG, "No live games right now: showing %s instead", saved->abbr);
        this->live_away_id_ = 0;  // the card is oriented around the saved team
        // Clearing the schedule is what forces the re-read; zeroing the
        // timestamp alone would not, because the test is on schedule_.valid.
        this->schedule_ = Schedule{};
        this->schedule_fetched_ms_ = 0;
        this->game_ = GameSnapshot{};
        this->prev_ = GameSnapshot{};
        this->post_since_ms_ = 0;
        this->emit_({});
        this->schedule_next_(0);  // fetch the card now
        return;
      }
      // No team either: the finals rotation (recap.h) has the board until a
      // game kicks off. The scan clock times the next scan from here.
      bool already = this->recap_on_;
      if (!already)
        ESP_LOGI(TAG, "No live games right now: showing recent finals");
      this->live_none_ = true;
      this->live_fallback_ = false;
      this->live_started_ms_ = now == 0 ? 1 : now;
      this->schedule_ = Schedule{};
      this->prev_ = GameSnapshot{};
      // The scan itself came back clean, so there is no run of failures to
      // report: clear the miss count. last_good_ms_ stays where it was,
      // because no game was polled - finals are fetched once, not polled,
      // so they have no freshness to report. Nothing polls a game in this
      // state, so clearing the count here cannot hide a failing game poll.
      this->misses_ = 0;
      this->recap_on_ = true;
      if (!already) {
        this->recap_step_ = 0;
        this->recap_step_ms_ = 0;
      }
      this->recap_rebuild_();
      this->mark_board_ready_();
      this->schedule_next_(0);  // the days the rotation still needs, or the wait
      return;
    }
    const ::espn::LiveGame &g = j.live[pick[random_uint32() % pick.size()]];
    ESP_LOGI(TAG, "Following %s @ %s", g.away_abbr.c_str(), g.home_abbr.c_str());
    // Both logos download while the game is read, so its board can go up whole.
    this->logos_.want(::espn::team_logo_url((League) g.league, g.away_id, g.away_abbr.c_str()));
    this->logos_.want(::espn::team_logo_url((League) g.league, g.home_id, g.home_abbr.c_str()));
    this->live_none_ = false;
    this->live_fallback_ = false;
    this->recap_on_ = false;
    // The second area's scores keep this scan's cards.
    if (!this->a2_scores_)
      this->recap_cards_.clear();
    this->recap_step_ms_ = 0;
    this->schedule_ = Schedule{};
    this->schedule_.valid = true;
    this->schedule_.event_id = g.event_id;
    this->schedule_.group = g.group;
    this->schedule_.league = g.league;
    if (::espn::is_hockeytech((League) g.league))
      ::espn::ht_live_records(this->schedule_, g);  // HockeyTech's game read has no records
    this->schedule_.kickoff_epoch = (int64_t) this->time_->timestamp_now();
    this->schedule_fetched_ms_ = now == 0 ? 1 : now;  // 0 reads as "never fetched"
    this->live_away_id_ = g.away_id;
    this->live_started_ms_ = now == 0 ? 1 : now;
    this->live_unread_ = true;  // its first read restarts the clock (apply_job_)
    if (this->game_.event_id != g.event_id || this->game_.team_id != g.away_id) {
      // Same game and side keeps its poll for carry_football.
      this->game_ = GameSnapshot{};
      this->prev_ = GameSnapshot{};
    }
    this->watch_feed_(j.live);
    this->schedule_next_(0);  // poll the chosen game right away
    return;
  }
  if (j.need_upcoming) {
    this->upcoming_due_ = false;
    if (j.upcoming_ok) {
      this->upcoming_ = j.upcoming;
      // HockeyTech game reads lack records; cache drops them too.
      if (j.team != nullptr && ::espn::is_hockeytech(j.team->league) &&
          ::espn::ht_take_records(this->schedule_, j.schedule) && this->game_.valid &&
          this->game_.event_id == this->schedule_.event_id)
        ::espn::ht_fill_from_schedule(this->game_, this->schedule_);
    }
    this->rebuild_state_(&this->last_fields_);
    // The list is optional and waits for the next schedule read, but a
    // refusal still holds back the game poll that would come next.
    this->schedule_next_(this->job_refused_() ? this->back_off_() : this->interval_for_phase_());
    return;
  }
  if (j.comp > 0) {
    this->apply_cup_job_(now);
    return;
  }
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->misses_ = ::espn::add_miss(this->misses_);
      this->drop_team_placeholder_();
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    this->clear_backoff_();
    this->schedule_ = j.schedule;
    this->schedule_fetched_ms_ = now == 0 ? 1 : now;  // 0 reads as "never fetched"
    if (j.today_final != nullptr)
      this->note_final_(*j.today_final);  // My team today, after the look-ahead moved on
    // Nothing found ahead: check back hourly; a failed read: in 15 minutes.
    this->linger_backoff_ms_ = j.lookahead == 2 ? 60 * MINUTE : j.lookahead < 0 ? 15 * MINUTE : 0;
    this->force_schedule_ = false;
    this->upcoming_due_ = true;  // refreshed on the next cycle, once the board is drawn
    this->cache_unconfirmed_ = false;
    this->save_team_cache_(j.team);
    if (!j.comps.empty()) {
      this->comps_ = j.comps;
      this->comp_next_ = 1;
    }
    if (j.no_event) {
      ESP_LOGI(TAG, "No upcoming game for this team");
      this->no_game_confirmed_ = true;
      this->game_ = GameSnapshot{};
      // Same as the no-live-games case: the schedule fetch succeeded, so the
      // failure run is over, but no GameSnapshot was applied and the board
      // has no score whose age last_good_ms_ could describe.
      this->misses_ = 0;
      this->emit_({});
      this->mark_board_ready_();
      this->schedule_next_(0);
      return;
    }
  }
  if (!j.game_ok) {
    this->misses_ = ::espn::add_miss(this->misses_);
    if (this->cache_unconfirmed_ && !this->job_refused_()) {
      // The cache may be wrong; refusals back off instead.
      ESP_LOGI(TAG, "Cached game not found: reading the team again");
      this->cache_unconfirmed_ = false;
      this->force_schedule_ = true;
      this->schedule_next_(0);
      return;
    }
    this->drop_team_placeholder_();
    this->emit_({});
    this->schedule_next_(this->back_off_());
    return;
  }
  this->cache_unconfirmed_ = false;
  ::espn::carry_football(this->game_, j.game);
  this->prev_ = this->board_prev_(j.game);
  this->game_ = j.game;
  this->mark_good_poll_();
  if (this->live_unread_ && this->live_mode_() && !this->live_fallback_) {
    // Turn starts now, so slow reads don't eat it.
    this->live_unread_ = false;
    this->live_started_ms_ = now == 0 ? 1 : now;
  }
  if (this->upcoming_after_poll_) {
    this->upcoming_after_poll_ = false;
    this->upcoming_due_ = true;
  }
  ::espn::Splash splash = this->board_splash_(this->live_mode_() && !this->live_fallback_);
  if (this->game_.state == GameState::POST) {
    this->note_post_(now);
    if (!this->live_mode_() || this->live_fallback_)
      this->note_final_(this->game_);  // the team's own: My team today keeps it
  } else {
    this->post_since_ms_ = 0;
  }
  this->emit_(splash);
  this->mark_board_ready_();
  this->schedule_next_(this->upcoming_due_ ? 0 : this->interval_for_phase_());
}

// Worker: one cup's team read, then the pick across every read so far. The
// game is polled when the pick moves, or at the end of a sweep that left the
// board without one.
void GamedayComponent::run_cup_job_() {
  Job &j = this->job_;
  const char *slug = ::espn::league_cup(j.team->league, (size_t) (j.comp - 1));
  Schedule s;
  j.cup_ok = slug != nullptr && this->fetch_schedule_(j.team, s, slug);
  if (j.cup_ok) {
    ::espn::adopt_league(s, j.team->league);
    ::espn::adopt_comp(s, slug);
    if (j.fits(true))
      this->take_game_on_now_(j.team, s, j.done_event, slug);
    j.comps[j.comp] = s;
    ::espn::league_standing(j.comps);  // the cup's table isn't the league's
  }
  int pick = ::espn::pick_schedule(j.comps, (int64_t) this->time_->timestamp_now(), j.done_event);
  Schedule next = pick >= 0 ? j.comps[pick] : j.comps[0];
  bool moved = next.event_id != j.schedule.event_id;
  bool last = j.comp + 1 >= (int) j.comps.size();
  j.schedule = next;
  if (!moved && !(last && j.need_game))
    return;
  if (next.event_id.empty()) {
    j.no_event = true;
    return;
  }
  if (!j.fits(false)) {
    j.game_skipped = true;  // the second area's: its game in the next gap
    return;
  }
  j.polled = true;
  j.game_ok = this->fetch_game_(j.team, j.schedule, j.game);
}

// Main loop, My team: fold a cup read in. A new pick takes the board.
void GamedayComponent::apply_cup_job_(uint32_t now) {
  Job &j = this->job_;
  this->comps_ = j.comps;
  this->comp_next_ = j.comp + 1 < (int) j.comps.size() ? j.comp + 1 : -1;
  bool moved = j.schedule.event_id != this->schedule_.event_id;
  if (moved) {
    ESP_LOGI(TAG, "Next game: event %s in %s", j.schedule.event_id.c_str(),
             j.schedule.comp_slug != nullptr ? j.schedule.comp_slug : "the league");
    this->schedule_ = j.schedule;
    this->save_team_cache_(j.team);
    this->post_since_ms_ = 0;
  }
  if (j.no_event) {
    ESP_LOGI(TAG, "No upcoming game for this team");
    this->no_game_confirmed_ = true;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->misses_ = 0;
    this->emit_({});
    this->mark_board_ready_();
  } else if (j.polled && !j.game_ok) {
    this->misses_ = ::espn::add_miss(this->misses_);
    this->drop_team_placeholder_();
    this->emit_({});
  } else if (j.polled) {
    this->prev_ = this->board_prev_(j.game);
    if (moved)
      this->prev_ = GameSnapshot{};
    ::espn::carry_football(this->prev_, j.game);
    this->game_ = j.game;
    this->mark_good_poll_();
    if (this->game_.state == GameState::POST) {
      this->note_post_(now);
      this->note_final_(this->game_);
    } else {
      this->post_since_ms_ = 0;
    }
    this->emit_(this->board_splash_(false));
    this->mark_board_ready_();
  }
  if (::espn::cup_read_backs_off(j.cup_ok, j.polled, j.game_ok, j.throttled)) {
    this->schedule_next_(this->back_off_());
    return;
  }
  this->schedule_next_(this->comp_next_ > 0 || this->upcoming_due_ ? 0 : this->interval_for_phase_());
}

// Worker, a leaderboard: the league's header, and after a final, a canceled
// event or none listed, what comes next from the calendar.
void GamedayComponent::run_board_job_() {
  Job &j = this->job_;
  League league = j.team->league;
  int r = this->fetch_board_(league, j.game);
  j.game_ok = r >= 0;
  j.no_event = r == 0;
  if (!j.game_ok || (!j.no_event && j.game.state != GameState::POST))
    return;
  int64_t now = (int64_t) this->time_->timestamp_now();
  j.board_next_ok =
      this->fetch_board_next_(league, j.game.event_id, j.game.kickoff_epoch, now, ::espn::board_next(j.game));
}

// Main loop, a leaderboard: the read takes the board unless it is an older
// CDN copy.
void GamedayComponent::apply_board_job_(uint32_t now) {
  Job &j = this->job_;
  const ::espn::Sport sport = ::espn::league_sport(j.team->league);
  const int64_t tnow = (int64_t) this->time_->timestamp_now();
  const bool calendar_failed = (j.no_event || j.game.state == GameState::POST) && !j.board_next_ok;
  if (!j.game_ok || (j.no_event && calendar_failed)) {
    // A read failed: the same as every sport.
    this->misses_ = ::espn::add_miss(this->misses_);
    this->drop_team_placeholder_();
    this->emit_({});
    this->schedule_next_(this->back_off_());
    return;
  }
  this->force_schedule_ = false;  // Refresh Now is a plain re-read for a leaderboard
  if (j.no_event) {
    if (!::espn::board_next(j.game).valid) {
      // ESPN lists nothing, now or ahead
      ESP_LOGI(TAG, "No upcoming %s", sport == ::espn::Sport::RACING ? "race" : "event");
      this->clear_backoff_();
      this->no_game_confirmed_ = true;
      this->game_ = GameSnapshot{};
      this->prev_ = GameSnapshot{};
      this->misses_ = 0;
      this->emit_({});
      this->mark_board_ready_();
      this->schedule_next_(60 * MINUTE);
      return;
    }
    // the next event's card, from the calendar
    j.game = ::espn::board_next_snapshot(sport, ::espn::board_next(j.game));
  }
  // A failed calendar read keeps the next event this final already had.
  if (calendar_failed && this->game_.valid && this->game_.event_id == j.game.event_id)
    ::espn::board_next(j.game) = ::espn::board_next(this->game_);
  // With none to keep, what comes next is unknown, not "no upcoming event":
  // the final stays on the board and the calendar is read again on the
  // failure schedule, not in 6 hours.
  const bool next_unknown = calendar_failed && !::espn::board_next(j.game).valid;
  ::espn::board_next_unknown(j.game) = next_unknown;
  if (!next_unknown)
    this->clear_backoff_();
  // ESPN keeps a tour's last final in its header for weeks: once it is
  // stale, the board moves on to the next event.
  if (::espn::board_show_next(j.game, tnow))
    j.game = ::espn::board_next_snapshot(sport, ::espn::board_next(j.game));
  if (::espn::board_read_is_older(this->game_, j.game) && this->board_stale_reads_ < 2) {
    this->board_stale_reads_++;
    ESP_LOGI(TAG, "An older copy of the leaderboard, keeping the board");
    this->mark_good_poll_();
    this->rebuild_state_(&this->last_fields_);
    this->schedule_next_(next_unknown ? this->back_off_() : this->interval_for_phase_());
    return;
  }
  this->board_stale_reads_ = 0;
  // Golf's tee times in the panel's time zone
  for (auto &r : j.game.golf.rows)
    r.set_tee(r.tee_epoch > 0 ? ::espn::golf_tee_label(ESPTime::from_epoch_local((time_t) r.tee_epoch).to_c_tm())
                              : std::string());
  this->prev_ = this->board_prev_(j.game);
  this->game_ = std::move(j.game);  // leaves job_ without the rows
  this->no_game_confirmed_ = false;
  if (this->game_.state == GameState::POST)
    this->note_post_(now);
  else
    this->post_since_ms_ = 0;
  this->mark_good_poll_();
  ::espn::Splash splash = ::espn::board_splash(this->prev_, this->game_, this->wide_());
  this->prev_.golf.rows = {};  // only its event and phase are read later
  this->prev_.race.rows = {};
  this->emit_(splash);
  this->mark_board_ready_();
  this->schedule_next_(next_unknown ? this->back_off_() : this->interval_for_phase_());
}

// ---- The big-play watcher (watch.h) ----------------------------------------

// Off when no big play could switch the board.
bool GamedayComponent::watching_() const {
  // Other games' reads splash no finals and keep no players.
  const uint32_t plays =
      this->alerts_on() & ~(::espn::alert_bit(::espn::Alert::FINAL) | ::espn::alert_bit(::espn::Alert::PLAYER));
  if (!this->watch_on() || this->demo_playing_() || !this->board_ready_ || this->backing_off_() || plays == 0 ||
      !this->switch_allowed_() || (this->own_team_live_() && !this->watch_from_mine()))
    return false;
  if (this->live_mode_())
    return !this->recap_on_ && !this->live_fallback_ && this->schedule_.valid;
  return !this->fav_.empty();
}

bool GamedayComponent::watch_reads_() const {
  if (this->watching_())
    return true;
  return this->rotation_on_() && this->board_ready_ && !this->backing_off_();
}

uint32_t GamedayComponent::watch_key_() const {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(this->effective_mode_());
  mix(this->prefs7_.live_mask);
  mix(this->prefs_.league);
  mix(this->prefs_.team_id);
  mix(this->alerts_favorites_only());
  mix(this->alerts_on());
  // Its team's game is dropped by each read (watch_ctx_).
  mix(this->rotation_on_());
  for (const auto &e : this->fav_) {
    mix((uint32_t) e.team->league);
    mix(e.team->espn_id);
  }
  return h | 1;  // never 0, which watch_reset_ leaves
}

void GamedayComponent::watch_reset_() {
  std::vector<::espn::WatchGame>().swap(this->watch_);  // frees its memory too while nothing is watched
  this->watch_src_.clear();
  this->watch_key_last_ = 0;
  this->watch_cycle_ms_ = millis() - ::espn::kWatchCycleMs;  // a new cycle at the next look
}

void GamedayComponent::watch_sync_() {
  const uint32_t key = this->watch_key_();
  if (key == this->watch_key_last_)
    return;
  std::vector<const ::espn::Team *> favs;
  for (const auto &e : this->fav_)
    favs.push_back(e.team);
  // Rotation watches every sport with big plays.
  const bool all = this->rotation_on_();
  const ::espn::Team *team = this->current_team_();
  if (all && !this->live_mode_() && team != nullptr && !::espn::is_leaderboard(team->league))
    favs.push_back(team);
  this->watch_.clear();
  this->watch_src_ = ::espn::watch_sources(this->live_mode_(), this->scan_leagues_(), favs,
                                           all ? ::espn::kAlertsAll : this->alerts_on());
  this->watch_key_last_ = key;
}

uint8_t GamedayComponent::mode_league_() const {
  if (this->favorites_mode_())
    return this->fav_shown_ >= 0 && this->fav_shown_ < (int) this->fav_.size()
               ? (uint8_t) this->fav_[this->fav_shown_].team->league
               : 0;
  if (this->live_mode_() && !this->live_fallback_)
    return this->schedule_.league;
  const ::espn::Team *t = this->current_team_();
  return t != nullptr ? (uint8_t) t->league : 0;
}

// A kickoff during a visit counts live and ends it.
bool GamedayComponent::own_team_live_() const {
  const GameSnapshot &g = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  return this->mode_league_() == this->prefs_.league &&
         ::espn::watch_own_game(g, this->prefs_.league, this->prefs_.team_id) &&
         ::espn::watch_live(g, (int64_t) this->time_->timestamp_now());
}

::espn::WatchCtx GamedayComponent::watch_ctx_() const {
  ::espn::WatchCtx c;
  c.live = this->live_mode_();
  c.fav_only = this->alerts_favorites_only();
  c.team = {this->prefs_.league, this->prefs_.team_id};
  for (const auto &e : this->fav_)
    c.favs.emplace_back((uint8_t) e.team->league, e.team->espn_id);
  // Neither the mode's game nor a visit's is watched.
  const GameSnapshot &g = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  c.board_league = this->mode_league_();
  c.board_event = g.valid ? g.event_id : this->schedule_.event_id;
  c.board_live = ::espn::watch_live(g, (int64_t) this->time_->timestamp_now());
  if (this->visit_.active) {
    c.visit_league = this->visit_.sched.league;
    c.visit_event = this->visit_.sched.event_id;
  }
  c.mask = this->alerts_on();
  // Rotation big plays splash in the second area.
  c.all = this->rotation_on_();
  c.read_mask = ::espn::kAlertsAll;
  c.wide = c.all ? this->a2_wide_() : this->wide_();
  if (this->a2_show_() == ::espn::SplitShow::TEAM && !this->a2_team_.empty() && this->a2_team_[0].game.valid) {
    c.a2_league = (uint8_t) this->a2_team_[0].team->league;
    c.a2_event = this->a2_team_[0].game.event_id;
  }
  return c;
}

::espn::WatchSwitch GamedayComponent::watch_switch_(bool alert_fav) const {
  const uint32_t now = millis();
  ::espn::WatchSwitch s;
  s.allowed = this->switch_allowed_();
  s.splash = this->splash_ms_ != 0 && now - this->splash_ms_ < ::espn::kWatchSplashMs;
  s.own_live = this->own_team_live_();
  s.alert_fav = alert_fav;
  s.from_mine = this->watch_from_mine();
  s.away_long = this->visit_.active && now - this->visit_.first_ms >= this->watch_stay_ms();
  return s;
}

bool GamedayComponent::start_watch_job_(uint32_t board_due) {
  if (!network::is_connected() || !this->clock_set_() || !this->selects_published_ || !this->watch_reads_()) {
    // Reads from before a pause can't be compared.
    if (this->watch_key_last_ != 0)
      this->watch_reset_();
    return false;
  }
  const uint32_t now = millis();
  if (this->watch_hold_until_ != 0) {
    if ((int32_t) (now - this->watch_hold_until_) < 0)
      return false;
    this->watch_hold_until_ = 0;
  }
  if (now - this->watch_cycle_ms_ >= ::espn::kWatchCycleMs) {
    // Count elapsed cycles so old reads look old.
    this->watch_cycle_ += (now - this->watch_cycle_ms_) / ::espn::kWatchCycleMs;
    this->watch_cycle_ms_ = now;
    this->watch_idle_ = false;
    this->watch_sync_();
    ::espn::watch_board_moved(this->watch_src_, this->watch_board_, this->watch_ctx_());
  }
  if (this->watch_idle_)
    return false;
  const ::espn::WatchRead r = ::espn::watch_next(
      this->watch_src_, this->watch_, this->watch_cycle_, now,
      [&](const ::espn::WatchSource &s, bool event) { return this->watch_times_.fits(now, board_due, s.league, event); });
  if (r.source < 0) {
    if (r.waiting)
      this->watch_hold_until_ = (now + 250) | 1;  // look again in a moment, not every loop
    else
      this->watch_idle_ = true;  // until the next cycle
    return false;
  }
  ::espn::WatchSource &src = this->watch_src_[r.source];
  const bool event = r.game >= 0;
  this->watch_times_.started(src.league, event);
  Job &j = this->new_job_();
  j.need_watch = true;
  j.watch_key = this->watch_key_last_;
  ::espn::WatchJob &w = j.watch;
  w.league = src.league;
  w.group = src.group;
  w.cycle = this->watch_cycle_;
  w.ctx = this->watch_ctx_();
  w.games = this->watch_;
  // Bet-tied games get their new reads handed back.
  for (const auto &b : this->bets_.blob.bets)
    if (!::espn::bet_empty(b) && b.event != 0 && b.league == src.league)
      w.bet_events.push_back(b.event);
  if (event) {
    ::espn::WatchGame &g = this->watch_[r.game];
    g.tried_cycle = this->watch_cycle_;
    w.event_id = g.event_id;
    w.away_id = g.away_id;
  } else {
    src.read_cycle = this->watch_cycle_;
  }
  this->start_worker_();
  return true;
}

// Worker task: one watcher read.
void GamedayComponent::run_watch_job_() {
  ::espn::WatchJob &w = this->job_.watch;
  const uint32_t start = millis();
  const League league = (League) w.league;
  const ::espn::Sport sport = ::espn::league_sport(league);
  const int64_t now = (int64_t) this->time_->timestamp_now();
  // A conference's day goes 9 hours back, as take_game_on_now_'s does.
  const std::string url = !w.event_id.empty() ? ::espn::event_url(league, w.event_id)
                          : w.group != 0      ? ::espn::team_day_url(league, w.group, now - 4 * 3600)
                                              : ::espn::scan_url(league, now);
  ESP_LOGD(TAG, "Watching: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr) {
    w.took_ms = millis() - start;  // refusals and timeouts are timed too
    return;
  }
  ContainerReader reader(container, this->espn_bytes_);
  if (!w.event_id.empty()) {
    GameSnapshot g;
    w.ok = ::espn::parse_event(reader, sport, w.away_id, g, w.event_id);
    if (w.ok)
      ::espn::watch_take(w, g);
  } else {
    w.ok = ::espn::watch_read_day(reader, w);
  }
  container->end();
  w.took_ms = millis() - start;
  ESP_LOGI(TAG, "Watch: %u %s game(s) %s(%u bytes, %u ms)", (unsigned) w.fresh.size(), ::espn::league_key(league),
           w.alert.valid ? "with a big play " : "", (unsigned) reader.total(), (unsigned) w.took_ms);
}

void GamedayComponent::apply_watch_job_(uint32_t now) {
  Job &j = this->job_;
  const ::espn::WatchJob &w = j.watch;
  // The panel doesn't follow these games, so settle bets here.
  if (!w.bets.empty() && this->clock_set_()) {
    const int64_t tnow = (int64_t) this->time_->timestamp_now();
    const int64_t midnight = this->next_midnight_(tnow);
    for (const auto &b : w.bets)
      ::espn::bets_update(this->bets_, b, tnow, midnight);
    if (this->bets_.dirty) {
      this->bets_tick_();  // which saves them
      this->rebuild_state_(&this->last_fields_);
    }
  }
  if (j.watch_key != this->watch_key_last_)
    return;  // what the board follows changed while the read ran
  const bool event = !w.event_id.empty();
  this->watch_times_.took(w.league, event, w.took_ms, w.ok);
  if (!w.ok) {
    // Failed reads wait a cycle; refusals hold the watcher.
    if (event)
      ::espn::watch_failed(this->watch_, w.league, w.event_id);
    if (j.throttled) {
      this->watch_hold_until_ = (now + std::max<uint32_t>(j.retry_after_ms, 5 * MINUTE)) | 1;
      this->watch_refused_(now);
    }
    return;
  }
  ::espn::WatchScope scope;
  scope.kind = event ? ::espn::WatchScope::GAME : ::espn::WatchScope::SOURCE;
  scope.league = w.league;
  scope.group = w.group;
  scope.event_id = w.event_id.c_str();
  ::espn::watch_merge(this->watch_, w.fresh, scope);
  for (auto &s : this->watch_src_)
    if (!event && s.league == w.league && s.group == w.group)
      s.wait_until = ::espn::watch_wait_until(s, !w.fresh.empty(), now, (int64_t) this->time_->timestamp_now(),
                                              w.next_start);
  if (this->rotation_on_()) {
    // The main board never leaves its game for these.
    if (w.alert.valid && this->watch_on())
      this->a2_jump_(w.alert, w.league);
    else
      this->emit_a2_({});
    return;
  }
  if (!w.alert.valid)
    return;
  // Recheck: the switch may have changed mid-read.
  if (this->watching_() && ::espn::watch_may_switch(this->watch_switch_(w.alert.fav)))
    this->start_visit_(w.alert, w.league);
  else
    ESP_LOGI(TAG, "Big play in %s @ %s: the board stays on its game", w.alert.away_abbr.c_str(),
             w.alert.home_abbr.c_str());
}

void GamedayComponent::watch_feed_(const std::vector<::espn::LiveGame> &live) {
  if (!this->watch_on() && !this->rotation_on_())
    return;
  this->watch_sync_();
  const ::espn::WatchCtx ctx = this->watch_ctx_();
  std::vector<::espn::WatchGame> fresh;
  for (const auto &l : live) {
    const int wants = ::espn::watch_wants(ctx, l.league, l.event_id, l.away_id, l.home_id);
    bool known = false;
    for (const auto &g : this->watch_)
      known = known || ::espn::watch_same(g, l.league, l.event_id);
    if (wants == 0 || known || !::espn::watch_by_event((League) l.league))
      continue;
    ::espn::WatchGame g;  // unread: its first read is the baseline
    snprintf(g.event_id, sizeof(g.event_id), "%s", l.event_id.c_str());
    g.league = l.league;
    g.away_id = l.away_id;
    g.home_id = l.home_id;
    g.fav = wants == 2;
    ::espn::watch_teams(g, l.away_abbr, l.home_abbr);
    ::espn::watch_keep(fresh, g);
  }
  ::espn::watch_merge(this->watch_, fresh, ::espn::WatchScope{});
}

void GamedayComponent::start_visit_(const ::espn::WatchAlert &a, uint8_t league) {
  ESP_LOGI(TAG, "Big play in %s @ %s: showing it for %u s", a.away_abbr.c_str(), a.home_abbr.c_str(),
           (unsigned) (this->watch_stay_ms() / 1000));
  if (!this->visit_.active) {
    this->visit_.saved.reset(new GameSnapshot(this->game_));  // the mode's board, for the way back
    this->visit_.first_ms = millis();
  }
  this->visit_.active = true;
  this->visit_.fav = a.fav;
  this->visit_.first = true;
  this->visit_.fails = 0;
  this->visit_.since_ms = millis();
  this->visit_.away_id = a.away_id;
  this->visit_.splash = a.splash;
  Schedule &s = this->visit_.sched;
  s = Schedule{};
  s.valid = true;
  s.league = league;
  s.event_id = a.event_id;
  s.kickoff_epoch = (int64_t) this->time_->timestamp_now();
  this->logos_.want(::espn::team_logo_url((League) league, a.away_id, a.away_abbr.c_str()));
  this->logos_.want(::espn::team_logo_url((League) league, a.home_id, a.home_abbr.c_str()));
  this->schedule_next_(0);  // its first poll now, which puts up the splash
}

void GamedayComponent::apply_visit_job_() {
  Job &j = this->job_;
  if (!this->visit_.active || j.schedule.event_id != this->visit_.sched.event_id) {
    this->schedule_next_(0);  // the visit ended or moved on while the read ran
    return;
  }
  if (!j.game_ok) {
    // A refusal or second miss gives the board back.
    if (j.throttled)
      this->watch_refused_(millis());
    else if (++this->visit_.fails >= 2)
      this->end_visit_();
    else
      this->schedule_next_(IN_INTERVAL);
    return;
  }
  this->visit_.fails = 0;
  this->prev_ = this->visit_.first ? GameSnapshot{} : std::move(this->game_);
  this->game_ = std::move(j.game);
  ::espn::carry_football(this->prev_, this->game_);
  // The first poll splashes the play that brought it.
  const Splash splash = ::espn::visit_splash(this->visit_.first, this->visit_.splash, this->prev_, this->game_,
                                             this->alerts_on(), this->wide_(), this->visit_.sched.league,
                                             this->players_.players);
  this->visit_.first = false;
  this->mark_good_poll_();
  this->emit_(splash);
  this->schedule_next_(this->interval_for_phase_());
}

void GamedayComponent::end_visit_() {
  if (!this->visit_.active)
    return;
  ESP_LOGI(TAG, "Back to the board's own game");
  if (this->visit_.saved)
    this->game_ = std::move(*this->visit_.saved);
  // Time away doesn't count against the game's turn.
  const uint32_t now = millis();
  const uint32_t away = now - this->visit_.first_ms;
  if (this->live_mode_() && !this->live_fallback_ && !this->recap_on_)
    this->live_started_ms_ = ::espn::rotate_turn_resume(this->live_started_ms_, now, away);
  if (this->favorites_mode_() && this->fav_shown_ >= 0 && this->time_ != nullptr)
    this->fav_shown_since_ = ::espn::rotate_since_resume(this->fav_shown_since_,
                                                         (int64_t) this->time_->timestamp_now(), away / 1000);
  this->drop_visit_();
  this->prev_ = GameSnapshot{};
  this->visit_back_ = true;  // that read splashes only a final reached meanwhile (board_prev_)
  this->emit_({});
  this->schedule_next_(0);  // the mode's own read, now
}

void GamedayComponent::watch_refused_(uint32_t now) {
  this->end_visit_();
  const uint32_t ms = this->back_off_();
  if (::espn::watch_refusal_holds_board(this->favorites_mode_(), this->live_mode_(), this->mode_league_()) &&
      (int32_t) (this->next_fetch_ms_ - (now + ms)) < 0)
    this->schedule_next_(ms);
}

// After a visit, only a final reached meanwhile splashes.
GameSnapshot GamedayComponent::board_prev_(const GameSnapshot &cur) {
  if (this->visit_back_) {
    this->visit_back_ = false;
    return ::espn::watch_back_prev(std::move(this->game_), cur);
  }
  return std::move(this->game_);
}

// ---- Split mode: the second area (split.h) -----------------------------------

void GamedayComponent::set_split(int show) {
  if (show < 0 || show >= ::espn::kSplitCount || show == this->split_prefs_.show)
    return;
  const bool was_on = this->split_area().on;
  const bool was_rotating = this->rotation_on_();
  this->split_prefs_.show = (uint8_t) show;
  this->split_pref_.save(&this->split_prefs_);
  ESP_LOGI(TAG, "Second area: %d", show);
  this->a2_reset_();
  if (was_rotating != this->rotation_on_() && this->watch_key_last_ != 0)
    this->watch_reset_();  // the rotation watches every live game: new sources now
  if (this->rotation_on_() && this->visit_.active)
    this->end_visit_();  // rotation shows big plays elsewhere in the area only
  // Three or four wide, the board and area trade panels.
  if (was_on != this->split_area().on)
    this->fire_action_("layout");
  this->emit_({});
  this->emit_a2_({});
}

void GamedayComponent::set_split_team(uint8_t league, uint32_t id) {
  if (id == 0)
    league = 0;
  if (id != 0 && league == this->prefs_.league && id == this->prefs_.team_id) {
    ESP_LOGW(TAG, "The second team can't be the panel's own team");
    return;
  }
  if (league == this->split_prefs_.team_league && id == this->split_prefs_.team_id)
    return;
  this->split_prefs_.team_league = league;
  this->split_prefs_.team_id = id;
  this->split_pref_.save(&this->split_prefs_);
  this->a2_set_team_();
  ESP_LOGI(TAG, "Second team: %s", this->a2_team_.empty() ? "none" : this->a2_team_[0].team->abbr);
  if (!this->a2_team_.empty()) {
    const ::espn::Team *t = this->a2_team_[0].team;
    this->logos_.want(::espn::team_logo_url(t->league, t->espn_id, t->abbr));
  }
  this->a2_reset_();
  this->emit_a2_({});
  this->rebuild_state_(&this->last_fields_);
}

// Unknown, golf or racing teams clear the setting.
void GamedayComponent::a2_set_team_() {
  this->a2_team_.clear();
  this->a2_none_ = false;
  this->a2_final_ms_ = 0;
  this->a2_saw_live_ = false;
  if (this->split_prefs_.team_id == 0)
    return;
  for (size_t t = 0; t < ::espn::kTeamCount; t++) {
    const ::espn::Team &team = ::espn::kTeams[t];
    if ((uint8_t) team.league == this->split_prefs_.team_league && team.espn_id == this->split_prefs_.team_id &&
        !::espn::is_leaderboard(team.league)) {
      FavEntry e;
      e.team = &team;
      this->a2_team_.push_back(e);
      return;
    }
  }
  this->split_prefs_.team_league = 0;
  this->split_prefs_.team_id = 0;
}

void GamedayComponent::a2_reset_() {
  this->a2_gen_++;  // a read in flight was for the old view
  this->end_jump_();
  this->rot_event_.clear();
  this->rot_ms_ = 0;
  this->a2_fail_.clear();
  this->a2_misses_ = 0;
  this->a2_wait_ms_ = 0;
  this->a2_poll_ms_ = 0;
}

// Same game only, not the next opponent's other game.
bool GamedayComponent::a2_team_main_() const {
  if (this->a2_team_.empty())
    return false;
  const FavEntry &e = this->a2_team_[0];
  const std::string &event = e.sched.valid && !e.sched.event_id.empty() ? e.sched.event_id : e.game.event_id;
  const GameSnapshot &mg = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  return !event.empty() && mg.valid && mg.event_id == event && this->mode_league_() == (uint8_t) e.team->league;
}

// Live, or a fresh final within the league's wait.
static bool a2_team_on(const GameSnapshot &g, bool saw_live, uint32_t final_ms, uint8_t league) {
  if (!g.valid)
    return false;
  if (g.state == GameState::IN)
    return true;
  const ::espn::LeagueInfo *info = ::espn::league_info((League) league);
  const uint32_t linger = (uint32_t) (info != nullptr ? info->linger_min : 30) * MINUTE;
  return g.state == GameState::POST && saw_live && final_ms != 0 && millis() - final_ms < linger;
}

bool GamedayComponent::a2_busy_() const {
  if (this->jump_.active)
    return true;
  if (this->a2_show_() != ::espn::SplitShow::TEAM || this->a2_team_.empty() || this->a2_team_main_())
    return false;
  const FavEntry &e = this->a2_team_[0];
  return a2_team_on(e.game, this->a2_saw_live_, this->a2_final_ms_, (uint8_t) e.team->league);
}

uint8_t GamedayComponent::a2_league_() const {
  if (this->a2_view_ == ::espn::AreaView::TEAM && !this->a2_team_.empty())
    return (uint8_t) this->a2_team_[0].team->league;
  if (this->jump_.active)
    return this->jump_.league;
  return this->rot_league_;
}

::espn::AreaView GamedayComponent::a2_compute_() const {
  ::espn::AreaInputs in;
  in.show = this->a2_show_();
  if (in.show == ::espn::SplitShow::OFF)
    return ::espn::AreaView::NONE;
  if (this->demo_playing_())
    in.show = ::espn::SplitShow::CLOCK;  // the demo plays on the main board only
  in.night = this->idle_.night;
  in.main_clock = this->last_fields_.clock;
  const bool main_scores = (this->recap_on_ && this->idle_view_ == ::espn::IdleView::BOARD) ||
                           this->idle_view_ == ::espn::IdleView::SCORES;
  in.scores = ::espn::scores_step(this->recap_step_, this->recap_cards_.size(), main_scores) >= 0;
  in.team_set = !this->a2_team_.empty();
  if (in.team_set) {
    const FavEntry &e = this->a2_team_[0];
    in.team_main = this->a2_team_main_();
    in.team_card = e.game.valid || this->a2_none_;
    in.team_on = a2_team_on(e.game, this->a2_saw_live_, this->a2_final_ms_, (uint8_t) e.team->league);
  }
  const GameSnapshot &mg = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  in.rot_games = ::espn::rot_count(this->watch_, this->mode_league_(), mg.event_id) > 0;
  in.jump = this->jump_.active;
  return ::espn::area_view(in);
}

// Up to three: Up next, favorites' cards, or next kickoff.
std::string GamedayComponent::a2_clock_line_() const {
  if (this->a2_show_() == ::espn::SplitShow::TEAM && !this->demo_playing_()) {
    if (this->a2_team_.empty())
      return "Pick the second team on the device page";
    if (this->a2_team_main_())
      return std::string(this->a2_team_[0].team->abbr) + " is on the main board";
  }
  if (!this->clock_set_() || this->demo_playing_())
    return "";
  const int64_t tnow = (int64_t) this->time_->timestamp_now();
  const struct tm now = this->time_->now().to_c_tm();
  std::vector<std::pair<int64_t, std::string>> next;
  auto add = [&](const std::string &event, int64_t kick, bool tbd, const std::string &ours, const std::string &opp,
                 bool home) {
    if (kick <= tnow || event == this->game_.event_id)
      return;
    struct tm k = ESPTime::from_epoch_local((time_t) (tbd ? ::espn::tbd_day_epoch(kick) : kick)).to_c_tm();
    next.emplace_back(kick, ::espn::next_line(panel_text(ours), panel_text(opp), home, k, now, " ", tbd));
  };
  if (this->favorites_mode_()) {
    for (const auto &e : this->fav_)
      if (e.game.valid && e.game.state == GameState::PRE)
        add(e.game.event_id, e.game.kickoff_epoch, e.game.time_tbd, e.game.team_abbr, e.game.opp_abbr, e.game.team_home);
  } else if (this->live_mode_() && !this->live_fallback_) {
    std::string match;
    const int64_t kick = this->recap_next_kickoff_(&match);
    if (kick > tnow) {
      struct tm k = ESPTime::from_epoch_local((time_t) kick).to_c_tm();
      next.emplace_back(kick, ::espn::recap_next_text(match, ::espn::kickoff_label(k, now)));
    }
  } else if (const ::espn::Team *t = this->current_team_()) {
    for (const auto &u : this->upcoming_)
      add(u.event_id, u.kickoff_epoch, u.time_tbd, t->abbr, u.opp_abbr, u.home);
  }
  std::sort(next.begin(), next.end(),
            [](const std::pair<int64_t, std::string> &a, const std::pair<int64_t, std::string> &b) {
              return a.first < b.first;
            });
  std::string out;
  for (size_t i = 0; i < next.size() && i < 3; i++)
    out += (out.empty() ? "" : " | ") + next[i].second;
  return out;
}

void GamedayComponent::a2_fields_(::espn::AreaView v, const ::espn::Splash &splash, UpdateFields &f) {
  using ::espn::AreaView;
  if (v == AreaView::CLOCK || v == AreaView::DATE) {
    f.status_text = this->a2_clock_line_();
    return;
  }
  if (v == AreaView::SCORES) {
    const bool main = (this->recap_on_ && this->idle_view_ == ::espn::IdleView::BOARD) ||
                      this->idle_view_ == ::espn::IdleView::SCORES;
    const int step = ::espn::scores_step(this->recap_step_, this->recap_cards_.size(), main);
    GameSnapshot card;
    std::string message, ticker;
    this->recap_card_(step < 0 ? 0 : (size_t) step, true, card, message, ticker);
    this->game_fields_(card, {}, f);
    f.status_text = ticker;
    f.message = message;
    return;
  }
  const GameSnapshot *g = nullptr;
  GameSnapshot card;
  uint8_t league = 0;
  bool rotation = false;
  const GameSnapshot &mg = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  if (v == AreaView::TEAM) {
    g = &this->a2_team_[0].game;
    league = (uint8_t) this->a2_team_[0].team->league;
  } else if (this->jump_.active && this->jump_game_().valid) {
    g = &this->jump_game_();
    league = this->jump_.league;
  } else {
    // A rotation card, or a jump before its read.
    for (const auto &w : this->watch_)
      if (::espn::watch_same(w, this->rot_league_, this->rot_event_)) {
        card = ::espn::rot_card(w);
        g = &card;
        league = w.league;
        rotation = !this->jump_.active;
      }
  }
  if (g == nullptr || !g->valid) {
    f.game_state = "NOT_FOUND";
    f.message = v == AreaView::TEAM ? "No upcoming game" : "No other live games";
    f.status_text = f.message;
    f.splash_text = splash.text;
    f.splash_color = splash.color;
    return;
  }
  this->game_fields_(*g, splash, f);
  if (rotation) {
    f.status_text = ::espn::rot_ticker(this->watch_, this->rot_league_, this->rot_event_, this->mode_league_(),
                                       mg.event_id);
  } else {
    for (const std::string &line : this->player_lines_(*g, league))
      f.status_text += (f.status_text.empty() ? "" : " | ") + line;
    this->bets_ticker_(*g, league, f.status_text);
  }
  // No diamond in the area: outs go in text.
  if (f.sport == (uint8_t) ::espn::Sport::BASEBALL && g->state == GameState::IN && f.outs >= 0)
    f.down_distance = (f.situation.empty() ? std::string() : f.situation + " ") + std::to_string(f.outs) + " OUT";
  if (this->a2_misses_ >= 3)
    f.status_text += f.status_text.empty() ? "not updating" : " | not updating";
}

void GamedayComponent::emit_a2_(const ::espn::Splash &splash) {
  if (this->idle_snaps_.size() < 3)
    return;  // called before setup()
  const ::espn::AreaView v = this->a2_compute_();
  this->a2_view_ = v;
  // Tell the state document only when the view changes.
  const GameSnapshot &mg = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  const uint8_t n =
      this->rotation_on_() ? (uint8_t) ::espn::rot_count(this->watch_, this->mode_league_(), mg.event_id) : 0;
  if (v != this->a2_view_doc_ || n != this->a2_n_doc_) {
    this->a2_view_doc_ = v;
    this->a2_n_doc_ = n;
    this->rebuild_state_(&this->last_fields_);
  }
#ifdef USE_LVGL
  AreaView *av = area_view();
  if (v == ::espn::AreaView::NONE || av == nullptr) {
    this->a2_hold_ = {};
    return;
  }
  UpdateFields f;
  this->a2_fields_(v, splash, f);
  const bool clock = v == ::espn::AreaView::CLOCK || v == ::espn::AreaView::DATE;
  // The card before stays up until these logos arrive
  const bool fresh = !clock && f.splash_text.empty() &&
                     (f.team_logo != this->a2_logo_[0] || f.opponent_logo != this->a2_logo_[1]);
  const uint32_t now = millis();
  const uint32_t since = this->a2_hold_.since;
  const ::espn::HoldStep step = this->a2_hold_.step(fresh, fresh && this->logos_.waiting(f.team_logo),
                                                    fresh && this->logos_.waiting(f.opponent_logo), now);
  if (step == ::espn::HoldStep::WAIT) {
    this->a2_hold_logo_[0] = f.team_logo;
    this->a2_hold_logo_[1] = f.opponent_logo;
    return;
  }
  if (step == ::espn::HoldStep::DRAW_LATE) {
    ESP_LOGD(TAG, "Second area held %u ms for its logos", (unsigned) (now - since));
    // A held card still gets its whole turn
    if (v == ::espn::AreaView::LIVE && !this->jump_.active && this->rot_ms_ != 0)
      this->rot_ms_ = now | 1;
    if (v == ::espn::AreaView::SCORES && this->recap_step_ms_ != 0)
      this->recap_step_ms_ = now == 0 ? 1 : now;
  }
  if (!f.splash_text.empty()) {
    ESP_LOGI(TAG, "Second area splash: %s", f.splash_text.c_str());
    this->a2_splash_ms_ = millis() == 0 ? 1 : millis();
  }
  const UpdateFields &m = this->last_fields_;
  // Cached logos; a missing one shows its badge
  image::Image *team_logo = clock ? nullptr : this->logos_.get(f.team_logo);
  image::Image *opp_logo = clock ? nullptr : this->logos_.get(f.opponent_logo);
  const DrawResult r = area_draw(*av, f, clock, m.clock, clock_card_date(this->split_area().main_w, !m.next_line.empty()),
                                 team_logo, opp_logo);
  this->a2_logo_[0] = clock ? std::string() : f.team_logo;
  this->a2_logo_[1] = clock ? std::string() : f.opponent_logo;
  this->a2_badge_[0] = f.team_badge;
  this->a2_badge_[1] = f.opponent_badge;
  this->a2_show_logos_();
  if (r.ticker_changed || r.clock_card)
    this->fire_action_("a2");  // the ticker's scroll time and the clock (gameday-common.yaml)
  if (r.splash)
    this->set_timeout("a2_splash", A2_SPLASH_MS, []() {
      if (AreaView *a = area_view())
        area_hide_splash(*a);
    });
#endif
}

void GamedayComponent::a2_show_logos_() {
#ifdef USE_LVGL
  AreaView *av = area_view();
  if (av == nullptr || this->a2_view_ == ::espn::AreaView::NONE)
    return;
  this->logos_.pin_a2(this->a2_logo_[0], this->a2_logo_[1]);
  image::Image *team = this->logos_.get(this->a2_logo_[0]), *opp = this->logos_.get(this->a2_logo_[1]);
  board_logos(av->game, team, opp, this->a2_badge_);
  splash_logos(av->game, team, opp);
#endif
}

void GamedayComponent::a2_tick_() {
  if (this->a2_show_() == ::espn::SplitShow::OFF) {
    if (this->a2_view_ != ::espn::AreaView::NONE)
      this->emit_a2_({});
    this->a2_scores_ = false;
    return;
  }
  const uint32_t now = millis();
  bool redraw = false;
  if (this->jump_.active && (now - this->jump_.since_ms >= this->watch_stay_ms() || !this->rotation_on_())) {
    ESP_LOGI(TAG, "Second area: back to the rotation");
    this->end_jump_();
    redraw = true;
  }
  // Not at night or while Off or dim is dark.
  const ::espn::SplitShow show = this->a2_show_();
  const GameSnapshot &mg = ::espn::watch_mode_game(this->game_, this->visit_.saved.get());
  const size_t live = ::espn::rot_count(this->watch_, this->mode_league_(), mg.event_id);
  const bool scores = !this->demo_playing_() && !this->idle_.night && !this->idle_.dim &&
                      (show == ::espn::SplitShow::SCORES ||
                       (show == ::espn::SplitShow::LIVE && !this->jump_.active && live == 0));
  if (scores != this->a2_scores_) {
    this->a2_scores_ = scores;
    if (scores) {
      if (this->recap_days_.empty())
        this->scan_due_now_();
      this->recap_rebuild_();
    } else if (!this->recap_on_ && !this->side_on_ && !this->live_mode_() && show != ::espn::SplitShow::LIVE) {
      // Free internal RAM while nothing shows the scores.
      std::vector<::espn::RecapCard>().swap(this->recap_cards_);
      std::vector<::espn::RecapDay>().swap(this->recap_days_);
      this->recap_step_ms_ = 0;
      this->recap_scan_next_ = 0;
      for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
        this->recap_back_[i] = this->recap_fwd_[i] = 0;
        this->recap_fwd_next_[i] = 0;
        this->recap_fwd_match_[i].clear();
      }
    }
    redraw = true;
  }
  if (this->rotation_on_() && !this->jump_.active) {
    bool here = false;
    for (const auto &w : this->watch_)
      here = here || (::espn::watch_same(w, this->rot_league_, this->rot_event_) &&
                      ::espn::rot_ok(w, this->mode_league_(), mg.event_id));
    if (!here || this->rot_ms_ == 0 || now - this->rot_ms_ >= ::espn::kRotCardMs) {
      const int i = ::espn::rot_pick(this->watch_, this->rot_league_, this->rot_event_, this->mode_league_(),
                                     mg.event_id);
      const std::string was = this->rot_event_;
      if (i < 0) {
        this->rot_event_.clear();
      } else {
        const ::espn::WatchGame &w = this->watch_[i];
        this->rot_league_ = w.league;
        this->rot_event_ = w.event_id;
        // The next card's logos start downloading now.
        const int k = ::espn::rot_pick(this->watch_, w.league, w.event_id, this->mode_league_(), mg.event_id);
        if (k >= 0 && k != i) {
          const ::espn::WatchGame &n = this->watch_[k];
          this->logos_.want(::espn::team_logo_url((League) n.league, n.away_id, n.away_abbr));
          this->logos_.want(::espn::team_logo_url((League) n.league, n.home_id, n.home_abbr));
        }
      }
      this->rot_ms_ = now | 1;
      redraw = redraw || was != this->rot_event_ || !here;
    }
  }
  if (redraw || this->a2_compute_() != this->a2_view_)
    this->emit_a2_({});
}

// No gap reads while a logo downloads on tight RAM.
bool GamedayComponent::gap_ok_(uint32_t now) const {
  if (!network::is_connected() || !this->clock_set_() || !this->selects_published_ || this->busy_ ||
      this->demo_playing_())
    return false;
  if (this->worker_gave_up_ms_ != 0 && now - this->worker_gave_up_ms_ < RETRY_INTERVAL)
    return false;
  if (this->logos_.busy() && this->panels_ != nullptr && this->panels_->cols() * this->panels_->rows() > 2 &&
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 24 * 1024)
    return false;
  return true;
}

// Minor league second teams use their own host's wait.
bool GamedayComponent::espn_gap_ok_(uint32_t now) const {
  return !this->backing_off_() && !(this->gap_hold_until_ != 0 && (int32_t) (now - this->gap_hold_until_) < 0);
}

// A due second-area read that doesn't fit blocks others.
void GamedayComponent::start_gap_job_(uint32_t due) {
  const uint32_t now = millis();
  if (now - this->times_cycle_ms_ >= ::espn::kWatchCycleMs) {
    this->times_cycle_ms_ = now;
    this->watch_times_.cycle();  // the read times age whatever reads
  }
  if (!this->gap_ok_(now)) {
    // Reads from before a pause can't be compared.
    if (this->watch_key_last_ != 0 && !this->watch_reads_())
      this->watch_reset_();
    return;
  }
  if (this->board_ready_ && this->start_a2_job_(now, due) != 0)
    return;
  if (!this->espn_gap_ok_(now)) {
    if (this->watch_key_last_ != 0 && !this->watch_reads_())
      this->watch_reset_();
    return;
  }
  if (this->start_side_job_(now, due))
    return;
  this->start_watch_job_(due);
}

int GamedayComponent::start_a2_job_(uint32_t now, uint32_t due) {
  if (this->a2_wait_ms_ != 0 && now - this->a2_wait_since_ < this->a2_wait_ms_)
    return 0;
  Job &j = this->job_;
  if (this->jump_.active) {
    // Jumps are always ESPN's: the watcher reads only ESPN.
    if (!this->espn_gap_ok_(now) ||
        (this->jump_.poll_ms != 0 && now - this->jump_.poll_ms < ::espn::kJumpPollMs))
      return 0;
    if (!this->watch_times_.fits(now, due, this->jump_.league, true))
      return -1;
    this->watch_times_.started(this->jump_.league, true);
    this->new_job_();
    j.generation = this->a2_gen_;
    j.a2 = A2_JUMP;
    j.players = this->players_;
    j.our_id = this->jump_.away_id;
    j.schedule.valid = true;
    j.schedule.league = this->jump_.league;
    j.schedule.event_id = this->jump_.event_id;
    j.schedule.kickoff_epoch = (int64_t) this->time_->timestamp_now();
    this->jump_.poll_ms = now | 1;
    this->start_worker_();
    return this->busy_ ? 1 : 0;
  }
  if (this->a2_show_() != ::espn::SplitShow::TEAM || this->a2_team_.empty() || this->demo_playing_())
    return 0;
  FavEntry &e = this->a2_team_[0];
  const uint8_t league = (uint8_t) e.team->league;
  // Its host is backing off: wait; other reads go on.
  if (::espn::from_espn(e.team->league) ? !this->espn_gap_ok_(now) : this->host_waiting_(e.team->league))
    return 0;
  // After the final's wait, the endpoint names the next game.
  const ::espn::LeagueInfo *info = ::espn::league_info(e.team->league);
  const uint32_t linger = (uint32_t) (info != nullptr ? info->linger_min : 30) * MINUTE;
  if (e.game.valid && e.game.state == GameState::POST && this->a2_final_ms_ != 0 && !e.stale &&
      now - this->a2_final_ms_ >= linger && !(e.game.event_id == e.stuck_event && now - e.stuck_ms < e.stuck_for_ms))
    e.stale = true;
  const bool missing = !e.sched.valid || e.stale;
  const uint32_t age = now - e.fetched_ms;
  const bool refresh = (missing && (e.fetched_ms == 0 || age >= RETRY_INTERVAL)) || (!missing && age >= SCHEDULE_INTERVAL);
  bool poll = false;
  // Main board polls its game; its schedule refreshes here.
  if (!refresh && !missing && !e.sched.event_id.empty() && !this->a2_team_main_()) {
    const int64_t tnow = (int64_t) this->time_->timestamp_now();
    const GameSnapshot &g = e.game;
    const uint32_t every = ::espn::a2_poll_ms(g.valid ? g.state : GameState::PRE, g.kickoff_epoch - tnow,
                                              g.time_tbd, this->game_.valid && this->game_.state == GameState::IN);
    poll = this->a2_poll_ms_ == 0 || now - this->a2_poll_ms_ >= every;
  }
  // Soccer: one cup read per gap after each refresh.
  const bool cup = !refresh && !poll && !missing && e.comp_next > 0 && e.comp_next < (int) e.comps.size();
  if (!refresh && !poll && !cup)
    return 0;
  // Requests that don't fit wait for the next gap.
  const uint32_t one = ::espn::watch_expect_ms(this->watch_times_.ms[::espn::WatchTimes::kind(league, true)], true);
  if (!::espn::watch_read_fits(now, due, poll ? one : 2 * one + ::espn::kWatchMarginMs))
    return -1;
  this->new_job_();
  j.generation = this->a2_gen_;
  j.a2 = A2_TEAM;
  j.mode = this->effective_mode_();
  j.players = this->players_;
  j.team = e.team;
  j.our_id = e.team->espn_id;
  j.schedule = e.sched;
  j.need_schedule = refresh;
  j.deadline_ms = due;
  j.event_ms = one;
  j.day_ms = ::espn::watch_expect_ms(this->watch_times_.ms[::espn::WatchTimes::kind(league, false)], false);
  if (refresh || cup) {
    const size_t cups = ::espn::league_cup_count(e.team->league);
    if (cups > 0) {
      j.comps = e.comps;
      j.comps.resize(cups + 1);
    }
    if (e.game.valid && e.game.state == GameState::POST)
      j.done_event = e.game.event_id;
  }
  if (cup) {
    j.comp = e.comp_next;
    j.need_game = e.game.event_id.empty() || e.game.event_id != e.sched.event_id;
  } else if (refresh) {
    e.fetched_ms = now | 1;
  } else {
    this->a2_poll_ms_ = now | 1;
    this->watch_times_.started(league, true);
  }
  this->start_worker_();
  return this->busy_ ? 1 : 0;
}

// Waits double from 30 s; refusals back off that host.
void GamedayComponent::a2_failed_(const Job &j) {
  const uint32_t now = millis();
  const ::espn::Source host = this->job_host_();
  const bool refused = this->job_refused_();
  const uint32_t retry = host == ::espn::Source::ESPN ? j.retry_after_ms : j.host[(size_t) host].retry_after_ms;
  this->a2_misses_ = ::espn::add_miss(this->a2_misses_);
  this->a2_wait_since_ = now;
  this->a2_wait_ms_ = this->a2_fail_.fail(refused, retry, random_uint32());
  if (refused) {
    const uint32_t ms = this->back_off_();
    bool hold_board;
    if (host == ::espn::Source::ESPN) {
      this->gap_hold_until_ = (now + std::max<uint32_t>(retry, 5 * MINUTE)) | 1;
      hold_board = ::espn::watch_refusal_holds_board(this->favorites_mode_(), this->live_mode_(), this->mode_league_());
    } else {
      hold_board = !this->favorites_mode_() && !this->live_mode_() &&
                   ::espn::league_source((League) this->mode_league_()) == host;
    }
    if (hold_board && (int32_t) (this->next_fetch_ms_ - (now + ms)) < 0)
      this->schedule_next_(ms);
  }
  ESP_LOGW(TAG, "Second area: read failed, again in %u s", (unsigned) (this->a2_wait_ms_ / 1000));
}

// ESPN's failure run is the board's to clear.
void GamedayComponent::a2_answered_() {
  const ::espn::Source host = this->job_host_();
  if (host != ::espn::Source::ESPN)
    this->host_wait_[(size_t) host].clear();
}

void GamedayComponent::apply_a2_job_() {
  Job &j = this->job_;
  if (j.generation != this->a2_gen_)
    return;  // the area's choice or team changed while the read ran
  const uint32_t now = millis();
  if (j.a2 == A2_JUMP) {
    if (!this->jump_.active || j.schedule.event_id != this->jump_.event_id)
      return;
    this->watch_times_.took(this->jump_.league, true, j.took_ms, j.game_ok);
    if (!j.game_ok) {
      // A refusal or second miss ends the stay.
      if (j.throttled)
        this->a2_failed_(j);
      if (j.throttled || ++this->jump_.fails >= 2) {
        this->end_jump_();
        this->emit_a2_({});
      }
      return;
    }
    this->jump_.fails = 0;
    GameSnapshot prev = std::move(this->jump_game_());
    ::espn::carry_football(prev, j.game);
    // Each read becomes the watcher's, so plays never splash twice.
    ::espn::WatchGame *seen = nullptr;
    for (auto &w : this->watch_)
      if (::espn::watch_same(w, this->jump_.league, this->jump_.event_id))
        seen = &w;
    Splash splash;
    if (prev.valid)
      splash = ::espn::visit_splash(false, this->jump_.splash, prev, j.game, this->alerts_on(), this->a2_wide_(),
                                    this->jump_.league, this->players_.players);
    else if (seen != nullptr)
      splash = ::espn::watch_splash(*seen, j.game, this->alerts_on(), this->a2_wide_());
    if (seen != nullptr)
      ::espn::watch_sync(*seen, j.game, this->watch_cycle_);
    this->jump_game_() = std::move(j.game);
    this->emit_a2_(splash);
    return;
  }
  if (this->a2_team_.empty() || this->a2_team_[0].team != j.team)
    return;
  FavEntry &e = this->a2_team_[0];
  const uint8_t league = (uint8_t) e.team->league;
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->a2_failed_(j);
      this->emit_a2_({});
      return;
    }
    e.sched = j.schedule;
    ::espn::adopt_league(e.sched, e.team->league);
    e.stale = false;
    if (!j.comps.empty()) {
      e.comps = j.comps;
      e.comp_next = 1;
    }
    if (j.no_event) {
      ESP_LOGI(TAG, "Second area: no upcoming game for %s", e.team->abbr);
      this->a2_none_ = true;
      e.game = GameSnapshot{};
      this->a2_final_ms_ = 0;
      this->a2_answered_();
      this->a2_fail_.clear();
      this->a2_misses_ = 0;
      this->a2_wait_ms_ = 0;
      this->emit_a2_({});
      return;
    }
    this->a2_none_ = false;
    if (j.game_skipped) {
      this->a2_answered_();
      this->a2_poll_ms_ = 0;  // its game in the next gap
      return;
    }
  } else if (j.comp > 0) {
    // A cup's pick is the team's game now.
    e.comps = j.comps;
    e.comp_next = j.comp + 1 < (int) j.comps.size() ? j.comp + 1 : -1;
    e.sched = j.schedule;
    if (!j.polled) {
      if (::espn::cup_read_backs_off(j.cup_ok, false, false, j.throttled))
        this->a2_failed_(j);
      if (j.no_event) {
        this->a2_none_ = true;
        e.game = GameSnapshot{};
        this->a2_final_ms_ = 0;
      }
      if (j.game_skipped)
        this->a2_poll_ms_ = 0;  // its game in the next gap
      this->emit_a2_({});
      return;
    }
    this->a2_none_ = false;
  } else {
    this->watch_times_.took(league, true, j.took_ms, j.game_ok);
  }
  if (!j.game_ok) {
    this->a2_failed_(j);
    this->emit_a2_({});
    return;
  }
  this->a2_answered_();
  this->a2_fail_.clear();
  this->a2_misses_ = 0;
  this->a2_wait_ms_ = 0;
  GameSnapshot old = e.game;
  ::espn::carry_football(old, j.game);
  e.game = std::move(j.game);
  const bool same = old.valid && old.event_id == e.game.event_id;
  // A repeated final waits before the next refresh.
  if (j.need_schedule && same && e.game.state == GameState::POST &&
      ::espn::league_sport(e.team->league) != ::espn::Sport::FOOTBALL) {
    e.stuck_event = e.game.event_id;
    e.stuck_ms = now | 1;
    e.stuck_for_ms = j.lookahead == 2 ? SCHEDULE_INTERVAL : 15 * MINUTE;
  }
  if (!same) {
    this->a2_final_ms_ = 0;
    this->a2_saw_live_ = false;
  }
  if (e.game.state == GameState::POST) {
    if (this->a2_final_ms_ == 0) {
      this->a2_final_ms_ = now | 1;
      this->a2_saw_live_ = same && old.state != GameState::POST;
    }
  } else {
    this->a2_final_ms_ = 0;
  }
  this->a2_poll_ms_ = now | 1;
  // Its splashes show only in its own area.
  const Splash splash = same ? ::espn::board_splash_with_players(old, e.game, this->opponent_splashes(), false,
                                                                 this->alerts_on(), this->a2_wide_(), league,
                                                                 this->players_.players)
                             : Splash{};
  this->bets_tick_();
  this->emit_a2_(splash);
}

// Only first-stay plays move it; favorites aren't replaced.
void GamedayComponent::a2_jump_(const ::espn::WatchAlert &a, uint8_t league) {
  const uint32_t now = millis();
  if (this->jump_.active && (now - this->jump_.first_ms >= this->watch_stay_ms() || (this->jump_.fav && !a.fav)))
    return;
  if (this->a2_splash_ms_ != 0 && now - this->a2_splash_ms_ < ::espn::kWatchSplashMs)
    return;
  ESP_LOGI(TAG, "Big play in %s @ %s: showing it in the second area for %u s", a.away_abbr.c_str(),
           a.home_abbr.c_str(), (unsigned) (this->watch_stay_ms() / 1000));
  if (!this->jump_.active)
    this->jump_.first_ms = now;
  this->jump_.active = true;
  this->jump_.fav = a.fav;
  this->jump_.fails = 0;
  this->jump_.league = league;
  this->jump_.away_id = a.away_id;
  this->jump_.since_ms = now;
  this->jump_.poll_ms = now | 1;  // the watcher's read was just now
  this->jump_.event_id = a.event_id;
  this->jump_.splash = a.splash;
  this->jump_game_() = GameSnapshot{};
  this->rot_league_ = league;
  this->rot_event_ = a.event_id;
  this->emit_a2_(a.splash);
}

void GamedayComponent::end_jump_() {
  if (!this->jump_.active)
    return;
  this->jump_ = A2Jump{};
  if (this->idle_snaps_.size() >= 3)
    this->jump_game_() = GameSnapshot{};
  this->rot_ms_ = 0;  // the rotation's next card now
}

std::string GamedayComponent::no_game_why_() const {
  if (this->game_.valid || this->demo_playing_())
    return "";
  // Name the board team's host, e.g. "Can't reach MLB".
  const ::espn::Team *t = this->current_team_();
  if (this->favorites_mode_())
    t = this->fav_shown_ >= 0 && this->fav_shown_ < (int) this->fav_.size() ? this->fav_[this->fav_shown_].team
                                                                            : this->fav_[0].team;
  const ::espn::Source source = t != nullptr ? ::espn::league_source(t->league) : ::espn::Source::ESPN;
  return ::espn::no_game_text(this->clock_set_(), this->live_mode_(), this->no_game_confirmed_, this->misses_,
                              this->captive_, source);
}

std::string GamedayComponent::standing_text_() const {
  const auto kind = (::espn::Standing) this->prefs8_.standing;
  if (kind == ::espn::Standing::RECORD || !this->game_.valid || this->live_mode_() || this->recap_on_ ||
      this->demo_playing_())
    return "";
  const ::espn::Team *team = this->current_team_();
  const Schedule *s = &this->schedule_;
  if (this->favorites_mode_()) {
    if (this->fav_shown_ < 0)
      return "";
    team = this->fav_[this->fav_shown_].team;
    s = &this->fav_[this->fav_shown_].sched;
  }
  if (team == nullptr || !s->valid || team->espn_id != this->game_.team_id || ::espn::is_college(team->league))
    return "";
  return ::espn::standing_text(kind, s->seed, s->place);
}

std::string GamedayComponent::kickoff_of_(const GameSnapshot &s) const {
  if (!s.valid || s.state != GameState::PRE || s.kickoff_epoch <= 0 || this->time_ == nullptr)
    return "";
  int64_t at = s.time_tbd ? ::espn::tbd_day_epoch(s.kickoff_epoch) : s.kickoff_epoch;
  struct tm kick = ESPTime::from_epoch_local((time_t) at).to_c_tm();
  struct tm now = this->time_->now().to_c_tm();
  return ::espn::kickoff_label(kick, now, s.time_tbd);
}

void GamedayComponent::emit_(const ::espn::Splash &splash) {
  // Games starting or ending take the panel back now.
  this->idle_update_();
  this->bets_tick_();  // bets tie to the new board's game
  const GameSnapshot &g = this->game_;
  UpdateFields f;
  this->game_fields_(g, splash, f);
  const bool board = g.valid && ::espn::is_board_sport(g.sport);
  const GameSnapshot &ha = this->ha_game_();
  f.ha_status_text = ::espn::ha_status_text(ha, board && &ha == &g ? f.kickoff : this->kickoff_of_(ha));
  f.ha_last_play = ha.last_play;
  // Mode's words first: HA and the page follow its game.
  this->mode_fields_(f, board);
  const bool idle = this->idle_view_ != ::espn::IdleView::BOARD && !this->demo_playing_();
  if (idle) {
    this->page_status_ = f.status_text;
    this->idle_fields_(f);
  }
  // The device page's address, for the hints below: the IP rather than the
  // .local name, which many Android phones can't open; the boot screen's QR
  // code holds the same address (setup_qr.h). Empty off Wi-Fi.
  auto page_ip = []() -> std::string {
    if (!network::is_connected())
      return std::string();
    for (auto &a : network::get_ip_addresses()) {
      if (a.is_set() && a.is_ip4()) {
        char buf[48];
        a.str_to(buf);
        return buf;
      }
    }
    return std::string();
  };
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->fav_.empty() && !this->demo_playing_() &&
      this->idle_view_ == ::espn::IdleView::BOARD) {
    const std::string ip = page_ip();
    const std::string fav = ip.empty() ? std::string("Favorites: add teams on the device page | ")
                                       : "Favorites: add teams at " + ip + " | ";
    f.status_text = fav + f.status_text;
    f.ha_status_text = fav + f.ha_status_text;
  }
  if (this->misses_ >= 3 && !this->demo_playing_()) {
    std::string stale;
    if (!this->ever_polled_good_()) {
      // Nothing has ever come back, so there is no age to quote. Saying "no
      // update for 1 min" here was wrong however long the panel had been
      // trying, on a screen someone is reading because something is broken.
      stale = "no update yet";
      f.ha_status_text += " | no update yet";
    } else {
      uint32_t mins = this->stale_seconds_() / 60;
      if (mins < 1)
        mins = 1;
      stale = "no update for " + std::to_string(mins) + " min";
      // Without the minutes, which would log a change every minute.
      f.ha_status_text += " | not updating";
    }
    // An empty ticker (the idle clock) gets the words alone.
    auto add = [&stale](std::string &line) { line += line.empty() ? stale : " | " + stale; };
    add(f.status_text);
    if (idle)
      add(this->page_status_);
  }
  // Until a team, mode or favorite has been picked once, the ticker asks for
  // one at the device page's address.
  if (!this->flag_(FLAG_SETUP) && !this->demo_playing_()) {
    const std::string ip = page_ip();
    if (!ip.empty()) {
      std::string pick = "Pick a team at " + ip + " | ";
      f.status_text = pick + f.status_text;
      f.ha_status_text = pick + f.ha_status_text;
      if (idle)
        this->page_status_ = pick + this->page_status_;
    }
  }
  // Panel and page only: HA would log it flapping.
  this->join_addr_shown_ = false;
  if (join_ticker_on(this->join_addr_ms_, millis()) && this->flag_(FLAG_SETUP) && !this->demo_playing_()) {
    const std::string line = join_ticker_text(page_ip(), this->hostname());
    if (!line.empty()) {
      // An empty ticker (the idle clock) gets the address alone.
      auto lead = [&line](std::string &text) { text = text.empty() ? line.substr(0, line.size() - 3) : line + text; };
      lead(f.status_text);
      if (idle)
        lead(this->page_status_);
      // Shown under messages too, which otherwise hide the ticker.
      f.join_line = true;
      this->join_addr_shown_ = true;
    }
  }

  if (!f.splash_text.empty()) {
    ESP_LOGI(TAG, "Splash: %s", f.splash_text.c_str());
    this->splash_ms_ = millis() == 0 ? 1 : millis();
  }
  this->last_fields_ = f;
  this->rebuild_state_(&f);
  // Second area: no duplicate clock or main game.
  if (this->a2_show_() != ::espn::SplitShow::OFF) {
    const ::espn::AreaView v = this->a2_compute_();
    if (v != this->a2_view_ || v == ::espn::AreaView::CLOCK || v == ::espn::AreaView::DATE)
      this->emit_a2_({});
  }
  if (this->hold_for_logos_(f))
    return;
  this->draw_(f);
}

void GamedayComponent::mode_fields_(UpdateFields &f, bool board) {
  const GameSnapshot &g = this->game_;
  // Before the Live mode prefixes, so a fallback team's Next line keeps "No
  // live X games, showing CHI |" in front of it.
  this->clock_fields_(f);
  // The leaderboard, or the card, unless the off-season clock has the board.
  if (f.clock)
    f.board_card = false;
  f.board = board && !f.clock && !f.board_card;
  // A golf tour or a racing series ESPN lists nothing for, now or ahead: an
  // event or a race, not a game.
  const ::espn::Team *followed = this->current_team_();
  if (!g.valid && this->no_game_confirmed_ && followed != nullptr && ::espn::is_leaderboard(followed->league) &&
      !this->favorites_mode_()) {
    f.status_text = ::espn::is_racing(followed->league) ? "No upcoming race" : "No upcoming event";
    f.ha_status_text = f.status_text;
    f.message = f.status_text;
  }
  if (this->live_mode_() && !this->demo_playing_()) {
    std::string league_word = this->live_word_();
    const ::espn::Team *saved = this->current_team_();
    if (this->recap_on_) {
      // The finals rotation follows no game, so Home Assistant gets one steady
      // line instead of a change on every card.
      f.status_text = this->recap_ticker_;
      f.ha_status_text = "No live " + league_word + "games right now";
      f.ha_last_play.clear();
      f.message = this->recap_message_;
    } else if (this->live_fallback_ && saved != nullptr) {
      std::string showing = "No live " + league_word + "games, showing " + saved->abbr + " | ";
      f.status_text = showing + f.status_text;
      f.ha_status_text = showing + f.ha_status_text;
    } else if (!g.valid || g.state == GameState::NOT_FOUND) {
      f.status_text = this->live_none_ ? "No live " + league_word + "games right now"
                                       : "Looking for a live " + league_word + "game";
      f.ha_status_text = f.status_text;
      f.message = this->live_none_ ? "No live " + league_word + "games" : "Looking for a live " + league_word + "game";
    }
  }
  // An empty board says why (espn_parse.h, no_game_text): the clock is not
  // set yet, or ESPN cannot be reached. "No upcoming game" is only right
  // once ESPN itself has said so; before that it would be a guess.
  std::string why = this->no_game_why_();
  if (!why.empty()) {
    f.status_text = why;
    f.ha_status_text = why;
    f.message = why;
  }
}

void GamedayComponent::idle_fields_(UpdateFields &f) {
  UpdateFields out;
  ::espn::IdleView v = this->idle_view_;
  if (v == ::espn::IdleView::SCORES) {
    if (this->recap_step_ms_ == 0)
      this->recap_prepare_();  // the rotation's first card
    if (!this->recap_cards_.empty()) {
      this->game_fields_(this->side_card_(), {}, out);
      out.status_text = this->recap_ticker_;
      out.message = this->recap_message_;
    } else {
      v = ::espn::IdleView::CLOCK;  // nothing read yet, or nothing to show
    }
  }
  if (v == ::espn::IdleView::TODAY) {
    const GameSnapshot *t = this->today_game_();
    if (t != nullptr) {
      // A single panel has no room for the records.
      this->game_fields_(*t, {}, out, true);
      if (t->state == GameState::PRE) {
        // e.g. "#5 TEX 7-1 | OKLA 6-2"
        std::string rec = ::espn::detail::records_line(*t, std::string());
        if (!rec.empty())
          out.status_text = out.status_text.empty() ? rec : out.status_text + " | " + rec;
      }
    } else {
      v = ::espn::IdleView::CLOCK;  // no game today
    }
  }
  if (v == ::espn::IdleView::CLOCK) {
    out.game_state = "NOT_FOUND";
    this->clock_fields_(out, true);
  }
  out.board = false;
  out.board_card = false;
  out.ha_status_text = std::move(f.ha_status_text);
  out.ha_last_play = std::move(f.ha_last_play);
  f = std::move(out);
}

void GamedayComponent::game_fields_(const GameSnapshot &g, const ::espn::Splash &splash, UpdateFields &f,
                                    bool countdown) {
  // Standing for the mode's own game, not idle cards.
  const std::string standing = &g == &this->game_ ? this->standing_text_() : std::string();
  f.game_state = g.valid ? ::espn::state_name(g.state) : "NOT_FOUND";
  f.team_abbr = g.team_abbr;
  f.team_score = g.team_score;
  f.opponent_abbr = g.opp_abbr;
  f.opponent_score = g.opp_score;
  f.team_logo = g.team_logo;
  f.opponent_logo = g.opp_logo;
  // At halftime ESPN keeps the last drive's down.
  const bool brk = ::espn::football_break(g);
  f.possession = brk ? 0 : g.possession;
  f.team_timeouts = g.team_timeouts;
  f.opponent_timeouts = g.opp_timeouts;
  f.team_record = g.team_record;
  f.opponent_record = g.opp_record;
  f.team_rank = g.team_rank;
  f.opponent_rank = g.opp_rank;
  f.last_play = g.last_play;
  // Clock text first: the record rows are sized from it.
  const int64_t now = countdown && this->time_ != nullptr ? (int64_t) this->time_->timestamp_now() : 0;
  f.clock_text = ::espn::board_clock_text(g, countdown, now);
  if (g.valid && g.state == GameState::IN && !brk) {
    f.down_distance = g.short_down_distance;
    f.is_red_zone = ::espn::in_red_zone(g);
  }
  f.sport = (uint8_t) g.sport;
  if (g.valid && g.state == GameState::IN && g.sport == ::espn::Sport::BASEBALL) {
    f.situation = ::espn::baseball_count(g);
    f.outs = g.mlb.outs;
    f.bases = g.mlb.bases;
    f.highlight = g.mlb.bases == (::espn::Baseball::FIRST | ::espn::Baseball::SECOND | ::espn::Baseball::THIRD);
  }
  if (g.valid && g.sport == ::espn::Sport::SOCCER) {
    // The latest goal on the situation row, red cards under the logos, and
    // records that fit the label under a logo.
    if (g.state == GameState::IN)
      f.situation = ::espn::soccer_situation(g);
    if (g.state != GameState::PRE) {
      f.team_marks = g.soc.team_reds;
      f.opp_marks = g.soc.opp_reds;
    }
    f.team_record = ::espn::soccer_board_record(g.team_record);
    f.opponent_record = ::espn::soccer_board_record(g.opp_record);
  }
  if (!standing.empty()) {
    // Standing beside the clock if it fits, else the record.
    const int clock_px = (int) f.clock_text.size() * ::espn::kClockCharPx;
    std::string board =
        ::espn::board_standing(standing, ::espn::record_row_chars(clock_px, this->board_px_()));
    if (!board.empty())
      f.team_record = std::move(board);
  }
  if (g.valid && g.state == GameState::IN && g.sport == ::espn::Sport::BASKETBALL)
    f.situation = ::espn::basketball_series(g);
  f.team_color = ::espn::parse_color(g.team_color);
  f.opponent_color = ::espn::parse_color(g.opp_color);
  if (g.valid && g.sport == ::espn::Sport::HOCKEY) {
    // Oversized AHL/ECHL logos aren't fetched: the badge shows.
    f.team_badge = !g.team_abbr.empty() && g.team_logo.empty();
    f.opponent_badge = !g.opp_abbr.empty() && g.opp_logo.empty();
  }
#ifdef GAMEDAY_DEMO
  this->demo_badges_(f);
#endif
  f.splash_text = splash.text;
  f.splash_color = splash.color;
  // Other games' reads may have the sides reversed.
  if (!splash.text.empty()) {
    if (splash.both)
      f.splash_side = 2;
    else if (!splash.abbr.empty() && splash.abbr == g.team_abbr)
      f.splash_side = 0;
    else if (!splash.abbr.empty() && splash.abbr == g.opp_abbr)
      f.splash_side = 1;
    f.splash_lead = splash.lead;
    f.splash_points = splash.points;
  }

  TickerOptions opts;
  opts.clock = false;  // the clock has its own row on the panel; never in the ticker
  opts.down_distance = this->ticker_down_distance();
  opts.baseball_count = this->ticker_baseball_count();
  opts.soccer_stats = this->ticker_soccer_stats();
  opts.last_play = this->ticker_last_play();
  opts.odds = this->ticker_odds();
  opts.team_standing = standing;

  std::string kickoff = this->kickoff_of_(g);
  const bool board = g.valid && ::espn::is_board_sport(g.sport);
  if (board) {
    std::string dates, card;
    if (g.sport == ::espn::Sport::RACING) {
      // The next session's day and time stand in for the kickoff, or the
      // dates: the weekend's own before it starts (from the calendar), the
      // next one's after the race or a canceled one.
      const ::espn::Racing &r = g.race;
      const bool over = r.phase == ::espn::RacePhase::FINAL || r.phase == ::espn::RacePhase::OFF;
      if (over)
        dates = r.next.valid ? this->racing_dates_(r.next.start_epoch, r.next.end_epoch) : std::string();
      else if (r.next_start > 0 && this->time_ != nullptr)
        dates = ::espn::racing_when(ESPTime::from_epoch_local((time_t) r.next_start).to_c_tm(),
                                    this->time_->now().to_c_tm());
      else if (r.phase == ::espn::RacePhase::PRE)
        dates = this->racing_dates_(g.kickoff_epoch, r.race_epoch);
      card = dates;
    } else {
      // The dates stand in for the kickoff: the event's own before it
      // starts, the next event's after a final or a canceled one.
      dates = g.golf.phase == ::espn::GolfPhase::PRE ? this->golf_dates_(g.kickoff_epoch, g.golf.end_epoch)
              : g.golf.next.valid ? this->golf_dates_(g.golf.next.start_epoch, g.golf.next.end_epoch)
                                  : std::string();
      card = g.golf.phase == ::espn::GolfPhase::PRE ? dates : std::string();
    }
    kickoff = dates;
    const bool wide = this->wide_();
    ::espn::BoardLines l = ::espn::board_lines(g, wide);
    f.board_title = std::move(l.title);
    f.board_status = std::move(l.status);
    f.board_status_color = l.status_color;
    f.board_value_narrow = l.value_narrow;
    f.board_pos = std::move(l.pos);
    f.board_name = std::move(l.name);
    f.board_value = std::move(l.value);
    f.board_detail = std::move(l.detail);
    f.board_value_color = std::move(l.value_color);
    f.board_name_color = std::move(l.name_color);
    // No leaderboard (before the event, canceled, the tour's name while it
    // loads): the card. Its pieces are panel-safe already (ascii_fold), and
    // panel_text() would turn its line break into a space.
    f.board_card = f.board_name.empty();
    if (f.board_card)
      f.next_line = ::espn::board_card_text(g, wide, card);
  }
  f.kickoff = kickoff;
  f.status_text = ::espn::status_text(g, opts, kickoff);
  // Players and bets only on the mode's own game.
  if (&g == &this->game_) {
    for (const std::string &line : this->player_lines_(g))
      f.status_text += (f.status_text.empty() ? "" : " | ") + line;
    this->bets_ticker_(g, this->board_league_(), f.status_text);
  }
}

// A board with different logos from the one up now waits for them, so it goes
// up whole instead of one logo and then the other a second later. loop() draws
// it from last_fields_, which later updates keep current, once both are in or
// the wait runs out (logo_lru.h, logo_hold_over). A splash goes up at once.
bool GamedayComponent::hold_for_logos_(const UpdateFields &f) {
  bool same = f.team_logo == this->shown_team_logo_ && f.opponent_logo == this->shown_opp_logo_;
  if (same || !f.splash_text.empty() || this->demo_playing_()) {
    this->logo_held_ = false;
    return false;
  }
  if (!this->logo_held_)
    this->logo_hold_ms_ = millis();
  this->logo_held_ = !this->logo_hold_done_();
  return this->logo_held_;
}

bool GamedayComponent::logo_hold_done_() {
  const UpdateFields &f = this->last_fields_;
  return ::espn::logo_hold_over(this->logos_.waiting(f.team_logo), this->logos_.waiting(f.opponent_logo),
                                this->logo_hold_ms_, millis());
}

void GamedayComponent::draw_(const UpdateFields &f) {
  if (this->logo_held_) {
    ESP_LOGD(TAG, "Board held %u ms for its logos", (unsigned) (millis() - this->logo_hold_ms_));
    this->logo_held_ = false;
  }
  this->shown_team_logo_ = f.team_logo;
  this->shown_opp_logo_ = f.opponent_logo;
  this->shown_badge_[0] = f.team_badge;
  this->shown_badge_[1] = f.opponent_badge;
  for (auto &cb : this->callbacks_)
    cb(f);
}

void GamedayComponent::show_logos() {
  this->logos_.pin(this->shown_team_logo_, this->shown_opp_logo_);
  image::Image *team = this->logos_.get(this->shown_team_logo_), *opp = this->logos_.get(this->shown_opp_logo_);
#ifdef USE_LVGL
  if (BoardView *v = board_view()) {
    board_logos(*v, team, opp, this->shown_badge_);
    splash_logos(*v, team, opp);
  }
#endif
}

// The off-season clock (offseason.h): the saved team between seasons (in My
// team, or as a Live mode's card when nothing is live), a Live mode whose
// rotation has no cards, or Favorites with no card at all. Team colors, or
// neutral gray with no team.
// force: the idle clock, any season, with next game.
void GamedayComponent::clock_fields_(UpdateFields &f, bool force) {
  if (this->demo_playing_() || !this->clock_set_() || this->visit_.active)
    return;
  const GameSnapshot &g = this->game_;
  const ::espn::Team *team = this->current_team_();
  std::string color;  // hex, from the team endpoint or the game
  const GameSnapshot *next = nullptr;  // the game whose kickoff goes under the time
  const int64_t tnow = (int64_t) this->time_->timestamp_now();
  if (this->live_mode_() && !this->live_fallback_) {
    // Nothing in the last week and no kickoff in the next (recap_show_).
    if (!force && (!this->recap_on_ || !this->recap_cards_.empty()))
      return;
    // No team card: gray, with the day's next game.
    team = nullptr;
    std::string match;
    const int64_t kick = force ? this->recap_next_kickoff_(&match) : 0;
    if (kick > 0) {
      struct tm k = ESPTime::from_epoch_local((time_t) kick).to_c_tm();
      std::string when = ::espn::kickoff_label(k, this->time_->now().to_c_tm());
      f.next_line = (match.empty() ? std::string("Next game") : "Next: " + match) + "\n" + when;
      f.status_text = ::espn::recap_next_text(match, when);
    }
  } else if (this->favorites_mode_()) {
    // Every favorite has been read, and none has a game.
    if (!force)
      for (const auto &e : this->fav_)
        if (!e.sched.valid || !e.sched.event_id.empty())
          return;
    const FavEntry *pick = &this->fav_[0];
    if (force)
      for (const auto &e : this->fav_)
        if (e.game.valid && e.game.state == GameState::PRE && e.game.kickoff_epoch > tnow &&
            (next == nullptr || e.game.kickoff_epoch < next->kickoff_epoch)) {
          next = &e.game;
          pick = &e;
        }
    team = pick->team;
    color = next != nullptr && !next->team_color.empty() ? next->team_color : pick->sched.team_color;
  } else if (team != nullptr && ::espn::is_leaderboard(team->league)) {
    // A golf tour or a racing series between seasons: the next event, when
    // one is known, under the time on a wide board and in the ticker.
    if (!force && !::espn::board_off_season(g, tnow))
      return;
    const bool racing = g.sport == ::espn::Sport::RACING;
    color = racing ? "E03030" : "30B050";  // racing red, golf green, lifted by clock_color
    const ::espn::GolfNext &next = racing ? g.race.next : g.golf.next;
    std::string name;
    int64_t start = 0, end = 0;
    if (racing ? g.race.phase == ::espn::RacePhase::PRE : g.golf.phase == ::espn::GolfPhase::PRE) {
      name = g.game_name;
      start = g.kickoff_epoch;
      end = racing ? g.race.race_epoch : g.golf.end_epoch;
    } else if (next.valid) {
      name = next.name;
      start = next.start_epoch;
      end = next.end_epoch;
    }
    if (!name.empty()) {
      std::string dates = racing ? this->racing_dates_(start, end) : this->golf_dates_(start, end);
      // 21 characters a line of spleen_12 on a wide board
      f.next_line = "Next: " + ::espn::board_event_name(g.sport, name, 15) + "\n" + dates;
      f.status_text = "Next: " + name + (dates.empty() ? "" : ", " + dates);
    } else {
      f.next_line.clear();
      f.status_text = std::string("No upcoming ") + team->name + (racing ? " race" : " event");
    }
  } else {
    if (team == nullptr)
      return;
    // The team endpoint named no game, as opposed to a card still loading.
    bool none = !g.valid && this->schedule_.valid && this->schedule_.event_id.empty();
    if (!force && !none && !g.valid)
      return;
    if (!force && !::espn::off_season(none ? GameState::NOT_FOUND : g.state, g.kickoff_epoch, tnow,
                                      ::espn::season_horizon(::espn::league_sport(team->league))))
      return;
    color = g.valid ? g.team_color : this->schedule_.team_color;
    next = &g;
  }
  if (next != nullptr && next->valid && next->state == GameState::PRE && next->kickoff_epoch > 0) {
    const GameSnapshot &n = *next;
    int64_t at = n.time_tbd ? ::espn::tbd_day_epoch(n.kickoff_epoch) : n.kickoff_epoch;
    struct tm kick = ESPTime::from_epoch_local((time_t) at).to_c_tm();
    struct tm now = this->time_->now().to_c_tm();
    std::string ours = panel_text(n.team_abbr), opp = panel_text(n.opp_abbr);
    f.next_line = ::espn::next_line(ours, opp, n.team_home, kick, now, "\n", n.time_tbd);
    // The ticker carries it on both layouts, since a single panel has no
    // room for it under the time.
    f.status_text = ::espn::next_line(ours, opp, n.team_home, kick, now, " ", n.time_tbd);
  }
  f.clock = true;
  f.team_color = ::espn::clock_color(team != nullptr, ::espn::parse_color(color));
}

// "Nov 12-15" for an event's first and last day (midnight Eastern, read at
// noon Eastern as a TBD game's day is), in the panel's time zone.
std::string GamedayComponent::golf_dates_(int64_t start, int64_t end) const {
  if (start <= 0)
    return "";
  struct tm a = ESPTime::from_epoch_local((time_t) ::espn::tbd_day_epoch(start)).to_c_tm();
  struct tm b = ESPTime::from_epoch_local((time_t) ::espn::tbd_day_epoch(end > 0 ? end : start)).to_c_tm();
  return ::espn::golf_dates(a, b);
}

// "Nov 20-22" for a race weekend in the panel's time zone. Racing's times are
// real (the header's, or the calendar's set right), so no noon shift.
std::string GamedayComponent::racing_dates_(int64_t start, int64_t end) const {
  if (start <= 0)
    return "";
  struct tm a = ESPTime::from_epoch_local((time_t) start).to_c_tm();
  struct tm b = ESPTime::from_epoch_local((time_t) (end > 0 ? end : start)).to_c_tm();
  return ::espn::golf_dates(a, b);
}

// ---- finals rotation ------------------------------------------------------------

// Main loop: the next day the rotation needs read, if any. Going back, per
// league, until it has kRecapMinFinals or has read the oldest day the
// rotation keeps (kRecapBackDays). Going forward, only when no game is left
// today, until a day has a kickoff. Each league counts days its own way
// (recap.h, espn_day).
bool GamedayComponent::recap_job_(Job &j, int64_t tnow) {
  const uint8_t done = ::espn::kRecapMaxDays + 1;
  const uint16_t leagues = this->recap_leagues_();
  // A failing host's days wait; others go on.
  auto held = [this](uint8_t i) {
    ::espn::Source s = ::espn::league_source((League) i);
    return s != ::espn::Source::ESPN && this->host_wait_[(size_t) s].waiting(millis());
  };
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)) || held(i))
      continue;
    int32_t today = ::espn::espn_day(tnow, i);
    uint8_t &w = this->recap_back_[i];
    if (w == done && this->recap_back_day_[i] != today && ::espn::recap_count(this->recap_days_, i) == 0)
      w = 0;  // a new day and still no finals: look again
    if (w == 0) {
      if (::espn::recap_count(this->recap_days_, i) >= ::espn::kRecapMinFinals) {
        w = done;  // today's scan already had enough
        this->recap_back_day_[i] = today;
        continue;
      }
      w = 1;
    }
    if (::espn::recap_back_reads(w)) {
      j.need_recap = true;
      j.recap_forward = false;
      j.recap_league = i;
      j.recap_day = today - w;
      return true;
    }
  }
  if (this->recap_scan_next_ != 0)
    return false;  // a game later today: that is the next kickoff
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)) || held(i))
      continue;
    int32_t today = ::espn::espn_day(tnow, i);
    uint8_t &w = this->recap_fwd_[i];
    // Found one and it has kicked off, or found none and it is a new day.
    if (w == done && this->recap_fwd_next_[i] <= tnow &&
        (this->recap_fwd_next_[i] != 0 || this->recap_fwd_day_[i] != today))
      w = 0;
    if (w == 0)
      w = 1;
    if (w <= ::espn::kRecapMaxDays) {
      j.need_recap = true;
      j.recap_forward = true;
      j.recap_league = i;
      j.recap_day = today + w;
      return true;
    }
  }
  return false;
}

void GamedayComponent::apply_recap_job_() {
  if (this->job_.recap_league >= ::espn::kLeagueCount)
    return;
  if (!this->recap_day_(this->job_)) {
    // The same day again after the backoff. The rotation keeps going with
    // what it has.
    // Other hosts' days are held by recap_job_ instead.
    const uint32_t ms = this->back_off_();
    this->schedule_next_(this->job_host_() != ::espn::Source::ESPN ? 0 : ms);
    return;
  }
  this->clear_backoff_();
  this->schedule_next_(0);
}

bool GamedayComponent::recap_day_(Job &j) {
  const uint8_t done = ::espn::kRecapMaxDays + 1;
  if (j.recap_league >= ::espn::kLeagueCount)
    return true;
  uint8_t i = j.recap_league;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int32_t today = ::espn::espn_day(tnow, i);
  if (!j.recap_ok)
    return false;
  if (j.recap_forward) {
    if (j.next_kickoff > tnow) {
      this->recap_fwd_next_[i] = j.next_kickoff;
      this->recap_fwd_[i] = done;
      this->recap_fwd_day_[i] = today;
      // The game the Next card names. The scan keeps a college day's ranked
      // games first, so the first kickoff may not be among them.
      this->recap_fwd_match_[i].clear();
      for (const auto &p : j.pending)
        if (p.kickoff_epoch == j.next_kickoff) {
          this->recap_fwd_match_[i] = ::espn::recap_matchup(p);
          break;  // the first one kept at that time: best ranked, then ESPN's order
        }
    } else if (++this->recap_fwd_[i] > ::espn::kRecapMaxDays) {
      ESP_LOGI(TAG, "No %s kickoff in the next week", ::espn::recap_league_word(i));
      this->recap_fwd_next_[i] = 0;
      this->recap_fwd_match_[i].clear();
      this->recap_fwd_[i] = done;
      this->recap_fwd_day_[i] = today;
    }
  } else {
    ::espn::RecapDay d;
    d.league = i;
    d.day = j.recap_day;
    d.finals.assign(j.finals.begin(), j.finals.end());
    ::espn::recap_merge(this->recap_days_, std::move(d));
    if (::espn::recap_count(this->recap_days_, i) >= ::espn::kRecapMinFinals ||
        !::espn::recap_back_reads(++this->recap_back_[i])) {
      this->recap_back_[i] = done;
      this->recap_back_day_[i] = today;
      ESP_LOGI(TAG, "Recent %s finals: %u", ::espn::recap_league_word(i),
               (unsigned) ::espn::recap_count(this->recap_days_, i));
    }
  }
  ::espn::recap_trim(this->recap_days_, tnow);
  this->recap_rebuild_();
  return true;
}

// A scan's finals and games to come go in as today's (recap.h).
void GamedayComponent::recap_absorb_(const Job &j, int64_t tnow) {
  uint16_t scanned = 0;
  for (League l : j.scan_leagues)
    scanned |= ::espn::recap_bit((uint8_t) l);
  ::espn::recap_absorb(this->recap_days_, j.finals, j.pending, tnow, scanned);
  // One league's scan keeps an earlier kickoff another league's found.
  const int64_t next = ::espn::recap_scan_next(j.next_kickoff, j.pending, tnow);
  const bool one = j.side && j.scan_leagues.size() == 1;
  if (!one || this->recap_scan_next_ <= tnow || (next != 0 && next < this->recap_scan_next_))
    this->recap_scan_next_ = next;
  this->recap_rebuild_();
}

int64_t GamedayComponent::recap_next_kickoff_(std::string *matchup) const {
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int64_t best = this->recap_scan_next_ > tnow ? this->recap_scan_next_ : 0;
  std::string match;
  const uint16_t leagues = this->recap_leagues_();
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)))
      continue;
    int64_t k = this->recap_fwd_next_[i];
    if (k > tnow && (best == 0 || k < best)) {
      best = k;
      match = this->recap_fwd_match_[i];
    }
  }
  if (best != 0 && best == this->recap_scan_next_)
    for (const auto &d : this->recap_days_)
      for (const auto &p : d.later)
        if (p.kickoff_epoch == best && (leagues & ::espn::recap_bit(d.league)))
          match = ::espn::recap_matchup(p);
  if (matchup != nullptr)
    *matchup = match;
  return best;
}

std::string GamedayComponent::live_word_() const {
  uint8_t m = this->effective_mode_();
  if (m == (uint8_t) Mode::LIVE_NCAA)
    return "college ";
  if (m == (uint8_t) Mode::LIVE_NFL)
    return "NFL ";
  if (m == (uint8_t) Mode::LIVE) {
    std::vector<League> leagues = this->live_leagues_();
    if (leagues.size() == 1)
      return std::string(::espn::recap_league_word((uint8_t) leagues[0])) + " ";
  }
  return "";
}

// The cards for the leagues this mode reads. The card on the board stays up
// until its time is over; a rotation with nothing up yet starts now.
void GamedayComponent::recap_rebuild_() {
  this->recap_cards_ = ::espn::recap_cards(this->recap_days_, this->recap_leagues_(), this->recap_next_kickoff_() != 0,
                                           (int64_t) this->time_->timestamp_now());
  if (!this->recap_on_ && !this->scores_wanted_())
    return;
  if (this->recap_step_ >= this->recap_cards_.size())
    this->recap_step_ = 0;
  if (this->recap_step_ms_ == 0 || this->recap_cards_.size() < 2)
    this->recap_show_();
}

void GamedayComponent::recap_tick_() {
  if (this->recap_cards_.size() < 2)
    return;
  if (this->recap_step_ms_ == 0) {
    // Area-only: its cards keep turning whatever stopped the board.
    if (this->a2_scores_ && !this->recap_on_ && !this->side_on_)
      this->recap_show_();
    return;
  }
  const ::espn::RecapCard &c = this->recap_cards_[this->recap_step_];
  if (millis() - this->recap_step_ms_ < ::espn::recap_card_ms(c.kind))
    return;
  this->recap_step_ = (this->recap_step_ + 1) % this->recap_cards_.size();
  this->recap_show_();
}

// Puts the current card on the board: a header or the next kickoff on the
// no-game screen, a final or a game later today on the scoreboard. The
// ticker on a final names the day it was played, on a game later today its
// kickoff and channel.
// Side cards omit "No live games": other games unfollowed.
void GamedayComponent::recap_prepare_() {
  // Timer stays 0 so first cards show once read.
  if (!this->recap_cards_.empty())
    this->recap_step_ms_ = millis() == 0 ? 1 : millis();
  const bool side = this->side_on_;
  GameSnapshot &card = side ? this->side_card_() : this->game_;
  if (!side) {
    this->prev_ = GameSnapshot{};
    this->post_since_ms_ = 0;
  }
  this->recap_card_(this->recap_step_, side, card, this->recap_message_, this->recap_ticker_);
}

void GamedayComponent::recap_card_(size_t step, bool side, GameSnapshot &card, std::string &message,
                                   std::string &ticker) {
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  std::string word = this->live_word_();
  std::string none = side ? std::string() : "No live " + word + "games right now";
  auto with_none = [&none](const std::string &label) { return none.empty() ? label : none + " | " + label; };
  if (this->recap_cards_.empty() || step >= this->recap_cards_.size()) {
    // Nothing in the last week and no kickoff in the next: the off season.
    card = GameSnapshot{};
    message = "No live " + word + "games";
    ticker = none;
    return;
  }
  const ::espn::RecapCard &c = this->recap_cards_[step];
  switch (c.kind) {
    case ::espn::RecapKind::HEADER:
      card = GameSnapshot{};
      message = ::espn::recap_header(this->recap_days_[c.day], tnow);
      ticker = none;
      break;
    case ::espn::RecapKind::FINAL: {
      card = ::espn::final_snapshot(this->recap_days_[c.day].finals[c.final]);
      message.clear();
      std::string detail = card.short_detail.empty() ? std::string("Final") : card.short_detail;
      ticker = ::espn::recap_header(this->recap_days_[c.day], tnow) + " | " + detail;
      break;
    }
    case ::espn::RecapKind::LATER_HEADER:
      card = GameSnapshot{};
      message = "Later today";
      ticker = none;
      break;
    case ::espn::RecapKind::LATER: {
      const ::espn::Pending &p = this->recap_days_[c.day].later[c.final];
      card = ::espn::pending_snapshot(p);
      message.clear();
      struct tm k = ESPTime::from_epoch_local((time_t) p.kickoff_epoch).to_c_tm();
      struct tm n = this->time_->now().to_c_tm();
      ticker = ::espn::recap_later_ticker(p, ::espn::kickoff_label(k, n));
      break;
    }
    case ::espn::RecapKind::NEXT: {
      card = GameSnapshot{};
      std::string match;
      int64_t kick = this->recap_next_kickoff_(&match);
      if (kick == 0) {
        // It kicked off since the cards were made; the scan due now finds it.
        message = side ? "Later today" : "No live " + word + "games";
        ticker = none;
        if (side)
          this->scan_due_now_();
        break;
      }
      struct tm k = ESPTime::from_epoch_local((time_t) kick).to_c_tm();
      struct tm n = this->time_->now().to_c_tm();
      std::string label = ::espn::recap_next_text(match, ::espn::kickoff_label(k, n));
      message = label;
      ticker = with_none(label);
      break;
    }
  }
}

static void recap_prefetch(LogoCache &logos, const std::vector<::espn::RecapDay> &days, const ::espn::RecapCard &c) {
  if (c.kind == ::espn::RecapKind::FINAL) {
    const ::espn::Final &f = days[c.day].finals[c.final];
    logos.want(::espn::team_logo_url((League) f.league, f.away_id, f.away_abbr.c_str()));
    logos.want(::espn::team_logo_url((League) f.league, f.home_id, f.home_abbr.c_str()));
  } else if (c.kind == ::espn::RecapKind::LATER) {
    const ::espn::Pending &p = days[c.day].later[c.final];
    logos.want(::espn::team_logo_url((League) p.league, p.away_id, p.away_abbr.c_str()));
    logos.want(::espn::team_logo_url((League) p.league, p.home_id, p.home_abbr.c_str()));
  }
}

// Board and area cards alternate half a loop apart.
void GamedayComponent::recap_show_() {
  const bool main = this->recap_on_ || this->side_on_;
  if (main) {
    this->recap_prepare_();
    this->emit_({});
  } else if (!this->recap_cards_.empty()) {
    this->recap_step_ms_ = millis() == 0 ? 1 : millis();
  }
  if (this->a2_scores_ || this->a2_view_ == ::espn::AreaView::SCORES)
    this->emit_a2_({});
  // Next cards' logos download during this one
  const int next = main ? ::espn::scores_next_logos(this->recap_cards_, this->recap_step_, false) : -1;
  if (next >= 0)
    recap_prefetch(this->logos_, this->recap_days_, this->recap_cards_[next]);
  const int a2 = this->a2_scores_ ? ::espn::scores_next_logos(this->recap_cards_, this->recap_step_, main) : -1;
  if (a2 >= 0)
    recap_prefetch(this->logos_, this->recap_days_, this->recap_cards_[a2]);
}

// ---- startup ------------------------------------------------------------------

// The first real scoreboard since boot or since the team changed is on the
// board: a polled game, or the mode deciding there is none. The boot screen
// hold (boot.yaml) ends on the board_ready action.
void GamedayComponent::mark_board_ready_() {
  if (this->board_ready_)
    return;
  this->board_ready_ = true;
  ESP_LOGI(TAG, "Board ready after %u ms (%s)", (unsigned) (millis() - this->board_pending_ms_),
           this->board_pending_ms_ == 0 ? "since boot" : "since the team change");
  this->fire_action_("board_ready");
}

// Only while it can still be right (startup.h): this team, saved in the last
// day, and a game that has not been over for hours.
bool GamedayComponent::load_team_cache_(const ::espn::Team *team) {
  ::espn::TeamCache c{};
  if (team == nullptr || !this->team_cache_pref_.load(&c))
    return false;
  Schedule s;
  if (!::espn::team_cache_load(c, team->league, team->espn_id, (int64_t) this->time_->timestamp_now(), s))
    return false;
  this->schedule_ = s;
  ESP_LOGI(TAG, "Team info from cache: event %s kickoff %lld", s.event_id.c_str(), (long long) s.kickoff_epoch);
  return true;
}

void GamedayComponent::save_team_cache_(const ::espn::Team *team) {
  ::espn::TeamCache c{};
  if (team == nullptr || !::espn::team_cache_save(this->schedule_, team->league, team->espn_id,
                                                  (int64_t) this->time_->timestamp_now(), c))
    return;
  this->team_cache_pref_.save(&c);
}

// ---- device page routes -----------------------------------------------------

static void put_team(JsonObject o, uint8_t league, uint32_t id) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == league && t.espn_id == id) {
      o["l"] = ::espn::league_key((League) league);
      o["id"] = id;
      o["abbr"] = t.abbr;
      o["name"] = t.name;
      return;
    }
  }
}

// Serialises everything the page shows into state_json_. Main loop only; the
// HTTP task copies the finished string under the mutex.
void GamedayComponent::rebuild_state_(const UpdateFields *f) {
  JsonDocument doc(::espn::detail::json_allocator());
  const GameSnapshot &g = this->game_;
  doc["name"] = this->hostname();
  doc["version"] = App.get_comment();
  doc["setup"] = this->flag_(FLAG_SETUP);
  // Columns, rows (2 for the 2x2), and wiring.
  doc["panels"] = this->panels_ != nullptr ? this->panels_->cols() : 0;
  doc["rows"] = this->panels_ != nullptr ? this->panels_->rows() : 1;
  doc["wiring"] = this->panels_ != nullptr ? this->panels_->wiring() : 0;
  JsonObject team = doc["team"].to<JsonObject>();
  put_team(team, this->prefs_.league, this->prefs_.team_id);
  doc["mode"] = this->prefs2_.mode;
  // "rotate" in minutes for older pages (1 means 30 s).
  doc["rotate"] = ::espn::rotate_minutes_of(this->rotate_seconds());
  doc["rotatesec"] = this->rotate_seconds();
  doc["lockon"] = this->prefs4_.lockon_minutes;
  doc["release"] = this->prefs4_.release_seconds;
  doc["collide"] = this->prefs4_.collide;
  // Always the four remote slots (older apps expect four), then the rest of
  // the list up to its last entry.
  uint8_t fav_n = 4;
  for (uint8_t i = 4; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      fav_n = i + 1;
  JsonArray favs = doc["favs"].to<JsonArray>();
  for (uint8_t i = 0; i < fav_n; i++) {
    JsonObject o = favs.add<JsonObject>();
    if (this->fav_id_(i) != 0)
      put_team(o, this->fav_league_(i), this->fav_id_(i));
  }
  doc["favmax"] = FAV_MAX;
  doc["today"] = this->today_only();
  JsonArray live = doc["live"].to<JsonArray>();
  for (League l : this->live_leagues_())
    live.add(::espn::league_key(l));
  doc["live_auto"] = this->prefs7_.live_mask == 0;
  doc["tz"] = this->prefs_.tz_index;
  doc["tz_name"] = ::espn::kTimezones[this->prefs_.tz_index].name;
  doc["tz_iana"] = ::espn::kTimezones[this->prefs_.tz_index].iana;
  doc["tz_auto"] = this->tz_auto();
  doc["down"] = this->ticker_down_distance();
  doc["count"] = this->ticker_baseball_count();
  doc["match"] = this->ticker_soccer_stats();
  doc["play"] = this->ticker_last_play();
  doc["odds"] = this->ticker_odds();
  doc["opp"] = this->opponent_splashes();
  doc["alerts"] = this->alerts_on();
  doc["alertfav"] = this->alerts_favorites_only();
  doc["watch"] = this->watch_on();
  doc["watchstay"] = this->watch_stay_ms() / 1000;
  doc["watchmine"] = this->watch_from_mine();
  doc["bootaddr"] = this->show_boot_address();
  doc["standing"] = this->prefs8_.standing;
  doc["clocksize"] = this->clock_large() ? 1 : 0;
  doc["textsize"] = this->text_large() ? 1 : 0;
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
  // Left out where nothing can be installed (the demo build) or there is no
  // choice (the bar build), so the page and the app hide the switch.
  doc["autoupd"] = this->auto_update();
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  // Left out in builds with no beta channel (bar, demo), which hides the switch.
  doc["beta"] = this->follow_beta();
#endif
#ifdef USE_UPDATE
  // A release that rolled back here, after a manual install or an automatic
  // one; left out when there is none.
  if (this->update_mark_.failed[0] != '\0')
    doc["update_failed"] = (const char *) this->update_mark_.failed;
#endif
#ifdef GAMEDAY_BAR
  // The device page shows the bar beta note and a "Something wrong?" link.
  doc["bar"]["report"] = this->report_email_;
#endif
  doc["misses"] = this->misses_;
  // The Wi-Fi wants a sign-in (captive.h); the page and the app say how to
  // get the panel onto a network that works.
  doc["captive"] = this->captive_;
  doc["espn_kb"] = (uint32_t) (this->espn_bytes() / 1000);
  doc["espn_reads"] = this->espn_requests();
  doc["stale_s"] = this->stale_seconds_();
  // The clock came from ESPN's Date header (to within a second or two) and no
  // time server has answered yet; left out once one has.
  if (this->clock_approx_)
    doc["clock_approx"] = true;
  doc["fallback"] = this->live_fallback_;
  doc["recap"] = this->recap_on_;
  doc["follow"] = this->follow_.on;  // Follow this game holds the board's game
  // visit_follow: false once the mode's game has ended.
  doc["visit"] = this->visit_.active;
  doc["visit_follow"] = this->visit_.active && this->follow_can_start_();
  // idle_view "" for none; dark: off until a game starts.
  doc["idle"] = this->idle_prefs_.idle;
  doc["idledim"] = this->idle_prefs_.dim;
  doc["night"] = this->idle_prefs_.night_on != 0;
  doc["nightstart"] = ::espn::clock_hhmm(this->idle_prefs_.night_start);
  doc["nightend"] = ::espn::clock_hhmm(this->idle_prefs_.night_end);
  doc["nightbri"] = this->idle_prefs_.night_bri;
  doc["idle_view"] = ::espn::idle_view_key(this->idle_view_);
  doc["night_now"] = this->idle_.night;
  doc["dark"] = this->idle_.dim && this->idle_prefs_.dim == 0;
  // "" for no second area, or nothing shown now.
  doc["split"] = this->split_prefs_.show;
  doc["split_at"] = this->panels_ != nullptr
                        ? ::espn::split_at_key(::espn::split_at(this->panels_->cols(), this->panels_->rows()))
                        : "";
  doc["split_now"] = ::espn::area_view_key(this->a2_view_);
  JsonObject split_team = doc["splitteam"].to<JsonObject>();
  put_team(split_team, this->split_prefs_.team_league, this->split_prefs_.team_id);
  if (this->rotation_on_())
    doc["split_n"] = this->a2_n_doc_;

  JsonObject game = doc["game"].to<JsonObject>();
  game["s"] = g.valid ? ::espn::state_name(g.state) : "NOT_FOUND";
  // The board's reason for being empty ("Can't reach ESPN"), so the page and
  // the app say what the panel says. Left out when the board has its default.
  std::string why = this->no_game_why_();
  if (!why.empty())
    game["why"] = why;
  game["l"] = ::espn::league_key((League) this->board_league_());
  game["ta"] = g.team_abbr;
  game["ti"] = g.team_id;
  game["ts"] = g.team_score;
  game["oa"] = g.opp_abbr;
  game["oi"] = g.opp_id;
  game["os"] = g.opp_score;
  const std::string standing = this->standing_text_();
  game["tr"] = standing.empty() ? g.team_record : standing;
  game["or"] = g.opp_record;
  if (g.team_rank != 0)
    game["trk"] = g.team_rank;
  if (g.opp_rank != 0)
    game["ork"] = g.opp_rank;
  const bool brk = ::espn::football_break(g);
  game["p"] = brk ? 0 : g.possession;
  game["tt"] = g.team_timeouts;
  game["ot"] = g.opp_timeouts;
  game["rz"] = ::espn::in_red_zone(g);
  game["tv"] = g.tv;
  game["venue"] = g.venue;
  game["gn"] = g.game_name;
  game["odds"] = g.odds;
  game["ou"] = g.over_under;
  game["detail"] = g.short_detail;
  game["done"] = g.completed;
  game["kick"] = g.kickoff_epoch;
  if (g.time_tbd)
    game["tbd"] = true;
  game["lp"] = g.last_play;
  game["dd"] = brk ? std::string() : g.down_distance;
  // Idle view: fields still come from the mode's game.
  const bool idle = this->idle_view_ != ::espn::IdleView::BOARD;
  if (f != nullptr) {
    game["c"] = idle ? std::string() : f->clock_text;
    game["d"] = idle ? std::string() : f->down_distance;
    game["k"] = idle ? this->kickoff_of_(g) : f->kickoff;
    char color[8];
    snprintf(color, sizeof(color), "%06X", (unsigned) (idle ? ::espn::parse_color(g.team_color) : f->team_color));
    game["tc"] = color;
    snprintf(color, sizeof(color), "%06X", (unsigned) (idle ? ::espn::parse_color(g.opp_color) : f->opponent_color));
    game["oc"] = color;
    doc["status"] = idle ? this->page_status_ : f->status_text;
    if (!idle) {
      const std::vector<std::string> lines = this->player_lines_(g);
      if (!lines.empty()) {
        JsonArray pl = game["pl"].to<JsonArray>();
        for (const auto &line : lines)
          pl.add(line);
      }
    }
    doc["clock"] = f->clock;
    doc["splash"] = f->splash_text;
    snprintf(color, sizeof(color), "%06X", (unsigned) f->splash_color);
    doc["splash_color"] = color;
  }
  game["id"] = g.event_id;
  if (g.sport != ::espn::Sport::FOOTBALL) {
    game["sport"] = (int) g.sport;
    if (f != nullptr && !idle) {
      game["sit"] = f->situation;
      game["outs"] = f->outs;
      game["bases"] = f->bases;
    }
  }
  if (g.valid && g.sport == ::espn::Sport::GOLF) {
    // The leaderboard for the page: up to kGolfRows rows, places without the
    // board's width limit ("T10").
    JsonObject golf = game["golf"].to<JsonObject>();
    golf["st"] = ::espn::golf_round_long(g);
    golf["ph"] = (int) g.golf.phase;
    golf["round"] = g.period;
    golf["start"] = g.kickoff_epoch;
    golf["end"] = g.golf.end_epoch;
    golf["loc"] = g.venue;
    JsonArray rows = golf["rows"].to<JsonArray>();
    for (const auto &r : g.golf.rows) {
      JsonObject o = rows.add<JsonObject>();
      o["p"] = ::espn::golf_place_text(r);
      o["n"] = (const char *) r.name;  // copied: a char array would be kept as a literal
      o["s"] = ::espn::golf_score_text(r);
      o["t"] = ::espn::golf_thru_text(r, g.golf.phase);
      if (r.winner)
        o["w"] = true;
    }
    if (g.golf.next.valid) {
      JsonObject n = golf["next"].to<JsonObject>();
      n["name"] = g.golf.next.name;
      n["start"] = g.golf.next.start_epoch;
      n["end"] = g.golf.next.end_epoch;
    }
  }
  if (g.valid && g.sport == ::espn::Sport::RACING) {
    // The shown session's order for the page: up to kRaceRows rows, gaps in
    // full ("+13.078", "+1 lap"), and the session to come.
    const ::espn::Racing &r = g.race;
    JsonObject race = game["race"].to<JsonObject>();
    race["st"] = ::espn::racing_state_long(g);
    race["ph"] = (int) r.phase;
    race["start"] = g.kickoff_epoch;
    race["end"] = r.race_epoch;
    race["loc"] = g.venue;
    if (r.next_start > 0) {
      race["nx"] = ::espn::racing_session_long(r.next_kind, r.next_number);
      race["at"] = r.next_start;
    }
    JsonArray rows = race["rows"].to<JsonArray>();
    for (const auto &row : r.rows) {
      JsonObject o = rows.add<JsonObject>();
      o["p"] = ::espn::racing_pos_text(row);
      o["n"] = (const char *) row.name;  // copied: a char array would be kept as a literal
      o["v"] = (const char *) row.num;
      o["d"] = ::espn::racing_detail_long(row);
      if (row.winner)
        o["w"] = true;
    }
    if (r.next.valid) {
      JsonObject n = race["next"].to<JsonObject>();
      n["name"] = r.next.name;
      n["start"] = r.next.start_epoch;
      n["end"] = r.next.end_epoch;
    }
  }
  JsonArray next = doc["next"].to<JsonArray>();
  if (this->favorites_mode_()) {
    // One row per favorite: its next game, or the one in progress.
    doc["shown"] = this->fav_shown_ >= 0 ? (int) this->fav_[this->fav_shown_].slot : 0;
    doc["locked"] = this->fav_locked_;
    for (const auto &e : this->fav_) {
      JsonObject o = next.add<JsonObject>();
      o["slot"] = e.slot;
      JsonObject t = o["t"].to<JsonObject>();
      put_team(t, (uint8_t) e.team->league, e.team->espn_id);
      if (!e.game.valid) {
        // Only a read that named no game means none is scheduled; before the
        // first answer the row has nothing to say yet.
        if (e.sched.valid && e.sched.event_id.empty())
          o["none"] = true;
        continue;
      }
      o["id"] = e.game.event_id;
      o["s"] = ::espn::state_name(e.game.state);
      o["kick"] = e.game.kickoff_epoch;
      if (e.game.time_tbd)
        o["tbd"] = true;
      o["oi"] = e.game.opp_id;
      o["oa"] = e.game.opp_abbr;
      if (e.game.team_rank != 0)
        o["trk"] = e.game.team_rank;
      if (e.game.opp_rank != 0)
        o["ork"] = e.game.opp_rank;
      o["ts"] = e.game.team_score;
      o["os"] = e.game.opp_score;
      o["tv"] = e.game.tv;
      // Live games match the board; ESPN's "Halftime" can lag.
      o["detail"] = e.game.state == GameState::IN ? ::espn::clock_text(e.game) : e.game.short_detail;
      o["done"] = e.game.completed;
    }
  }
  for (const auto &u : this->upcoming_) {
    JsonObject o = next.add<JsonObject>();
    o["id"] = u.event_id;
    o["kick"] = u.kickoff_epoch;
    if (u.time_tbd)
      o["tbd"] = true;
    o["oi"] = u.opp_id;
    o["oa"] = u.opp_abbr;
    o["on"] = u.opp_name;
    if (u.opp_rank != 0)
      o["ork"] = u.opp_rank;
    o["home"] = u.home;
    o["neutral"] = u.neutral;
    o["tv"] = u.tv;
  }
  // ts and os: its team's score first.
  JsonArray bets = doc["bets"].to<JsonArray>();
  const int64_t tnow = this->clock_set_() ? (int64_t) this->time_->timestamp_now() : 0;
  for (uint8_t i = 0; i < ::espn::kBetSlots; i++) {
    const ::espn::Bet &b = this->bets_.blob.bets[i];
    if (::espn::bet_empty(b))
      continue;
    JsonObject o = bets.add<JsonObject>();
    o["slot"] = i + 1;
    o["l"] = ::espn::league_key((League) b.league);
    o["id"] = b.team_id;
    o["abbr"] = ::espn::bet_team_abbr(b.league, b.team_id);
    o["type"] = ::espn::bet_type_key(b);
    o["half"] = b.line;
    o["text"] = ::espn::bet_label(b);
    ::espn::BetGame bg;
    const GameSnapshot *gs = this->bet_snapshot_(b, bg);
    o["st"] = ::espn::bet_status_key(::espn::bet_status(b, gs != nullptr ? &bg : nullptr, tnow));
    if (b.event == 0)
      continue;
    o["ev"] = std::to_string(b.event);
    o["kick"] = b.kickoff;
    if (gs != nullptr) {
      o["oa"] = gs->team_id == b.team_id ? gs->opp_abbr : gs->team_abbr;
    } else {
      o["oa"] = ::espn::bet_team_abbr(b.league, b.opp_id);
    }
    if (gs != nullptr && gs->state != GameState::PRE) {
      int us = 0, them = 0;
      ::espn::bet_score(b, bg, us, them);
      o["ts"] = us;
      o["os"] = them;
    } else if (b.result != (uint8_t) ::espn::BetResult::NONE) {
      o["ts"] = b.team_score;
      o["os"] = b.opp_score;
    }
  }
  doc["betmax"] = ::espn::kBetSlots;
  // Favorite players, in slot order.
  JsonArray players = doc["players"].to<JsonArray>();
  for (const auto &p : this->players_.players) {
    if (::espn::player_empty(p))
      continue;
    JsonObject o = players.add<JsonObject>();
    o["l"] = ::espn::league_key((League) p.league);
    o["id"] = p.athlete;
    o["team"] = p.team_id;
    o["name"] = (const char *) p.name;
  }
  doc["playermax"] = ::espn::kPlayerSlots;
  std::string out;
  serializeJson(doc, out);
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  this->state_json_ = std::move(out);
}

std::string GamedayComponent::current_state_json_() {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->state_json_;
}

std::string GamedayComponent::health_json_() {
  ::espn::Health h{};
  h.uptime_s = esp_timer_get_time() / 1000000;
  h.start = this->start_reason_;
  h.boots = boot_log.boots;
  h.fault = (::espn::Restart) boot_log.fault;
  h.fault_ago = ::espn::boot_log_fault_ago(boot_log, (int64_t) ::time(nullptr), h.uptime_s);
  h.heap_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f;
  h.block_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024.0f;
  h.psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0f;
#ifdef USE_WIFI
  auto *w = wifi::global_wifi_component;
  h.wifi = w != nullptr && w->is_connected();
  if (h.wifi)
    h.rssi = w->wifi_rssi();
#endif
  const uint32_t at = this->espn_answer_ms_.load();
  h.espn_status = this->espn_status_.load();
  h.espn_ago = at == 0 ? -1 : (int64_t) ((millis() - at) / 1000);
  const size_t mlb = (size_t) ::espn::Source::MLB_STATS, ht = (size_t) ::espn::Source::HOCKEYTECH;
  const uint32_t mlb_at = this->host_answer_ms_[mlb].load(), ht_at = this->host_answer_ms_[ht].load();
  h.mlb_status = this->host_status_[mlb].load();
  h.mlb_ago = mlb_at == 0 ? -1 : (int64_t) ((millis() - mlb_at) / 1000);
  h.ht_status = this->host_status_[ht].load();
  h.ht_ago = ht_at == 0 ? -1 : (int64_t) ((millis() - ht_at) / 1000);
#ifdef ESPHOME_PROJECT_VERSION
  h.version = ESPHOME_PROJECT_VERSION;
#else
  h.version = "";
#endif
  h.built = (int64_t) App.get_build_time();
#if defined(GAMEDAY_BAR)
  h.channel = "bar";
#elif defined(GAMEDAY_BETA_CHANNEL)
  h.channel = this->follow_beta() ? "beta" : "stable";
#elif defined(USE_UPDATE)
  h.channel = "stable";
#else
  h.channel = "none";
#endif
  char buf[::espn::kHealthJsonSize];
  int n = ::espn::health_json(buf, sizeof(buf), h);
  if (n <= 0 || n >= (int) sizeof(buf))
    return "{}";
  // The heat and power keys go in before its closing brace (guard.h)
  char guard[::espn::kGuardJsonSize];
  int g = ::espn::guard_json_keys(guard, sizeof(guard), this->heat_.last_c, this->heat_.step, this->power_log_);
  if (g <= 0 || g >= (int) sizeof(guard))
    return std::string(buf, n);
  std::string out(buf, n - 1);
  out.append(guard, g);
  out += '}';
  return out;
}

// Every loop for the link going down or up, every WIFI_SAMPLE_INTERVAL for
// the signal.
void GamedayComponent::wifi_tick_() {
#ifdef USE_WIFI
  auto *w = wifi::global_wifi_component;
  if (w == nullptr)
    return;
  const bool up = w->is_connected();
  const uint32_t now_ms = millis();
  const bool sample = now_ms - this->wifi_sample_ms_ >= WIFI_SAMPLE_INTERVAL;
  if (up == this->wifi_up_ && !sample)
    return;
  const uint32_t now_s = (uint32_t) (esp_timer_get_time() / 1000000);
  std::lock_guard<std::mutex> lock(this->wifi_mutex_);
  if (up != this->wifi_up_) {
    this->wifi_up_ = up;
    // The page shows the signal as soon as the link is back, not at the next sample.
    this->wifi_rssi_ = up ? w->wifi_rssi() : 0;
    if (up) {
      this->wifi_hist_.up(now_s);
      this->wifi_reason_seen_ = wifi_reason_seq.load();
    } else {
      // A link lost with no disconnect event (the IP went) has no cause.
      const int reason = wifi_reason_seq.load() != this->wifi_reason_seen_ ? wifi_reason.load() : 0;
      const ::espn::WifiCause cause = ::espn::wifi_cause(reason);
      this->wifi_hist_.down(now_s, cause);
      ESP_LOGI(TAG, "Wi-Fi lost: %s (reason %d), %u drop(s) since start", ::espn::wifi_cause_key(cause), reason,
               (unsigned) this->wifi_hist_.total());
    }
  }
  if (sample) {
    this->wifi_sample_ms_ = now_ms;
    this->wifi_rssi_ = up ? w->wifi_rssi() : 0;
    this->wifi_hist_.sample(now_s, up, this->wifi_rssi_);
  }
#endif
}

std::string GamedayComponent::wifi_json_() {
  const uint32_t now_s = (uint32_t) (esp_timer_get_time() / 1000000);
  std::lock_guard<std::mutex> lock(this->wifi_mutex_);
  return this->wifi_hist_.json(now_s, this->wifi_up_, this->wifi_rssi_);
}

bool GamedayComponent::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buf).starts_with("/gameday/");
}

void GamedayComponent::set_web_server_base(web_server_base::WebServerBase *base) {
  this->base_ = base;
  // Handlers run in add order; this precedes every setup().
  if (base != nullptr)
    base->add_handler(&this->hotspot_handler_);
}

// ---- The setup hotspot (hotspot.h) -------------------------------------------

bool GamedayComponent::HotspotHandler::canHandle(AsyncWebServerRequest *request) const {
#ifdef USE_CAPTIVE_PORTAL
  auto *portal = captive_portal::global_captive_portal;
  if (portal == nullptr || !portal->is_active())
    return false;
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return hotspot_route(true, network::is_connected(), request->method() == HTTP_GET, request->url_to(buf).str()) !=
         HotspotRoute::USUAL;
#else
  return false;
#endif
}

void GamedayComponent::HotspotHandler::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  if (panel_route(request->url_to(buf).str())) {
    this->parent_->handleRequest(request);
    return;
  }
#ifdef USE_CAPTIVE_PORTAL
  // Answering OS internet checks makes phones offer the page.
  captive_portal::global_captive_portal->handleRequest(request);
#endif
}

void GamedayComponent::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  if (url == StringRef("/gameday/state") && request->method() == HTTP_GET) {
    std::string body = this->current_state_json_();
    // The cached document is rebuilt on changes; the health and Wi-Fi
    // numbers move all the time, so they go in fresh at its end.
    if (body.size() >= 2 && body.back() == '}') {
      body.pop_back();
      body += ",\"health\":";
      body += this->health_json_();
      body += ",\"wifi\":";
      body += this->wifi_json_();
      body += '}';
    }
    request->send(200, "application/json", body.c_str());
    return;
  }
  // Free heap right now, in KiB, for bench and burn-in scripts. The Free Heap
  // sensors only read every 5 minutes so Home Assistant isn't logging them.
  if (url == StringRef("/gameday/heap") && request->method() == HTTP_GET) {
    char body[128];
    snprintf(body, sizeof(body), "{\"internal\":%.1f,\"block\":%.1f,\"psram\":%.1f}",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024.0f,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0f);
    request->send(200, "application/json", body);
    return;
  }
  if (url == StringRef("/gameday/set") && request->method() == HTTP_POST) {
    this->handle_set_(request);
    return;
  }
  if (url == StringRef("/gameday/action") && request->method() == HTTP_POST) {
    std::string name = request->arg("do");
    if (name.empty()) {
      request->send(400, "application/json", "{\"error\":\"do is required\"}");
      return;
    }
    bool known = name == "refresh" || name == "qc" || name == "power_ok";
#ifdef GAMEDAY_DEMO
    known = known || name == "demo" || name == "demo_stop";
#endif
    if (!known) {
      request->send(404, "application/json", "{\"error\":\"unknown action\"}");
      return;
    }
    this->defer([this, name]() {
      if (name == "refresh")
        this->refresh_now();
      else if (name == "power_ok")
        this->clear_power_cap();
#ifdef GAMEDAY_DEMO
      else if (name == "demo")
        this->start_demo((uint8_t) ::espn::DemoKind::PRO);
      else if (name == "demo_stop")
        this->stop_demo();
#endif
      for (auto &cb : this->action_callbacks_)
        cb(name);
    });
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
#ifdef GAMEDAY_DEMO
  if (url == StringRef("/gameday/demo")) {
    this->handle_demo_(request);
    return;
  }
#endif
#ifdef USE_GAMEDAY_SAVED_NETWORKS
  if (url == StringRef("/gameday/wifi")) {
    this->handle_wifi_(request);
    return;
  }
#endif
#ifdef USE_GAMEDAY_CRASH_REPORT
  auto *crash = crash_report::global_crash_report;
  if (url == StringRef("/gameday/crash") && crash != nullptr) {
    if (request->method() == HTTP_POST) {
      if (request->arg("clear") != "1") {
        request->send(400, "application/json", "{\"error\":\"clear=1 is required\"}");
        return;
      }
      crash->clear();
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    std::string body = crash->json();
    request->send(200, "application/json", body.c_str());
    return;
  }
#endif
  request->send(404, "application/json", "{\"error\":\"unknown route\"}");
}

#ifdef USE_GAMEDAY_SAVED_NETWORKS
// The saved networks, names only: a password never leaves the panel. The
// list is in the order the panel tries them, the last one that worked first.
void GamedayComponent::handle_wifi_(AsyncWebServerRequest *request) {
  auto *nets = saved_networks::global_saved_networks;
  if (nets == nullptr) {
    request->send(404, "application/json", "{\"error\":\"unknown route\"}");
    return;
  }
  if (request->method() == HTTP_POST) {
    std::string ssid = request->arg("forget");
    if (ssid.empty()) {
      request->send(400, "application/json", "{\"error\":\"forget is required\"}");
      return;
    }
    if (!nets->forget(ssid)) {
      request->send(404, "application/json", "{\"error\":\"not a saved network\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (request->method() != HTTP_GET) {
    request->send(405, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  JsonDocument doc;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (const auto &e : nets->list()) {
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = e.ssid;
    o["connected"] = e.connected;
  }
  doc["max"] = (int) saved_networks::SavedNetworks::max();
  std::string body;
  serializeJson(doc, body);
  request->send(200, "application/json", body.c_str());
}
#endif

void GamedayComponent::handle_set_(AsyncWebServerRequest *request) {
  std::vector<std::pair<std::string, std::string>> kv;
  for (const char *key : ::espn::kSetKeys)
    if (request->hasParam(key))
      kv.emplace_back(key, request->arg(key));
  if (kv.empty()) {
    request->send(400, "application/json", "{\"error\":\"nothing to set\"}");
    return;
  }
  this->defer([this, kv]() { this->apply_set_(kv); });
  request->send(200, "application/json", "{\"ok\":true}");
}

void GamedayComponent::apply_set_(const std::vector<std::pair<std::string, std::string>> &kv) {
  bool dirty = false;
  ::espn::SetOp op;
  for (const auto &p : kv) {
    if (!::espn::parse_set(p.first, p.second, op, FAV_MAX)) {
      ESP_LOGW(TAG, "Ignoring %s=%s", p.first.c_str(), p.second.c_str());
      continue;
    }
    switch (op.kind) {
      case ::espn::SetKind::TEAM:
        if (op.team.id != 0)
          this->select_team_id((League) op.team.league, op.team.id);
        else
          this->clear_team_();
        break;
      case ::espn::SetKind::MODE:
        this->select_mode(MODE_OPTIONS[op.value]);
        break;
      case ::espn::SetKind::ROTATE:
        this->set_rotate_seconds(op.value);
        break;
      case ::espn::SetKind::LOCKON:
        this->set_lockon_minutes(op.value);
        break;
      case ::espn::SetKind::RELEASE:
        this->set_release_seconds(op.value);
        break;
      case ::espn::SetKind::COLLIDE:
        this->set_collision_alternate(op.on);
        break;
      case ::espn::SetKind::TODAY:
        this->set_today_only(op.on);
        break;
      case ::espn::SetKind::LIVE:
        this->set_live_mask_((uint16_t) op.mask);
        break;
      case ::espn::SetKind::FAVS: {
        for (const auto &bad : op.refused)
          ESP_LOGW(TAG, "Bad favorite '%s'", bad.c_str());
        std::vector<std::pair<uint8_t, uint32_t>> list;
        for (const auto &t : op.teams)
          list.emplace_back(t.league, t.id);
        this->set_favorites_(list);
        break;
      }
      case ::espn::SetKind::FAV_SLOT:
        this->set_favorite_(op.slot, op.team.league, op.team.id);
        break;
      case ::espn::SetKind::TZ:
        this->select_timezone(::espn::kTimezones[op.value].name);
        break;
      case ::espn::SetKind::TZ_AUTO:
        this->set_tz_auto(op.on);
        dirty = true;
        break;
      case ::espn::SetKind::DOWN:
        this->set_ticker_down_distance(op.on);
        break;
      case ::espn::SetKind::COUNT:
        this->set_ticker_baseball_count(op.on);
        break;
      case ::espn::SetKind::MATCH:
        this->set_ticker_soccer_stats(op.on);
        break;
      case ::espn::SetKind::PLAY:
        this->set_ticker_last_play(op.on);
        break;
      case ::espn::SetKind::ODDS:
        this->set_ticker_odds(op.on);
        break;
      case ::espn::SetKind::OPP:
        this->set_opponent_splashes(op.on);
        break;
      case ::espn::SetKind::ALERTS:
        this->set_alerts(op.mask);
        break;
      case ::espn::SetKind::ALERTFAV:
        this->set_alerts_favorites_only(op.on);
        break;
      case ::espn::SetKind::WATCH:
        this->set_watch(op.on);
        break;
      case ::espn::SetKind::WATCHSTAY:
        this->set_watch_stay(op.value);
        break;
      case ::espn::SetKind::WATCHMINE:
        this->set_watch_from_mine(op.on);
        break;
      case ::espn::SetKind::BOOTADDR:
        this->set_show_boot_address(op.on);
        break;
      case ::espn::SetKind::AUTOUPD:
        this->set_auto_update(op.on);
        break;
      case ::espn::SetKind::BETA:
        this->set_follow_beta(op.on);
        break;
      case ::espn::SetKind::FOLLOW:
        this->set_follow(op.on);
        break;
      case ::espn::SetKind::STANDING:
        this->set_standing((uint8_t) op.value);
        break;
      case ::espn::SetKind::CLOCK_SIZE:
        this->set_clock_size(op.value);
        break;
      case ::espn::SetKind::TEXT_SIZE:
        this->set_text_size(op.value);
        break;
      case ::espn::SetKind::IDLE:
        this->set_idle(op.value);
        break;
      case ::espn::SetKind::IDLE_DIM:
        this->set_idle_dim(op.value);
        break;
      case ::espn::SetKind::NIGHT:
        this->set_night(op.on);
        break;
      case ::espn::SetKind::NIGHT_START:
        this->set_night_start(op.value);
        break;
      case ::espn::SetKind::NIGHT_END:
        this->set_night_end(op.value);
        break;
      case ::espn::SetKind::NIGHT_BRI:
        this->set_night_bri(op.value);
        break;
      case ::espn::SetKind::BET:
        this->set_bet_(op.slot, op.bet);
        break;
      case ::espn::SetKind::BET_DEL:
        this->remove_bet_(op.slot);
        break;
      case ::espn::SetKind::PLAYER:
        this->set_player_(op);
        break;
      case ::espn::SetKind::PLAYER_DEL:
        this->remove_player_(op.team.league, op.athlete);
        break;
      case ::espn::SetKind::PANELS:
        if (this->panels_ != nullptr)
          this->panels_->set_layout((uint8_t) op.value, (uint8_t) op.rows);
        break;
      case ::espn::SetKind::SPLIT:
        this->set_split(op.value);
        break;
      case ::espn::SetKind::SPLIT_TEAM:
        this->set_split_team(op.team.league, op.team.id);
        break;
      case ::espn::SetKind::WIRING:
        // Off a 2x2, no restart: the page needs new state.
        if (this->panels_ != nullptr && this->panels_->set_wiring((uint8_t) op.value))
          dirty = true;
        break;
    }
  }
  if (dirty)
    this->rebuild_state_(&this->last_fields_);
}

}  // namespace gameday
}  // namespace esphome
