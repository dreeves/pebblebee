#include <pebble.h>
#define DEBUGLOG 0
#define TRANSLOG 0

static Window * window;

static Layer * battery_layer;
static Layer * datetime_layer;
static TextLayer * date_layer;
static TextLayer * time_layer;
static TextLayer * week_layer;
static TextLayer * day_layer;
static Layer * calendar_layer;
static Layer * statusbar;
static Layer * slot_top;
static Layer * slot_bot;

static BitmapLayer *bmp_connection_layer;
static GBitmap *image_connection_icon;
static GBitmap *image_noconnection_icon;
static BitmapLayer *bmp_charging_layer;
static GBitmap *image_charging_icon;
static GBitmap *image_hourvibe_icon;
static TextLayer *text_connection_layer;
static TextLayer *text_battery_layer;

static InverterLayer *inverter_layer;
static InverterLayer *battery_meter_layer;

// battery info, instantiate to 'worst scenario' to prevent false hopes
static uint8_t battery_percent = 10;
static bool battery_charging = false;
static bool battery_plugged = false;
static uint8_t sent_battery_percent = 10;
static bool sent_battery_charging = false;
static bool sent_battery_plugged = false;
AppTimer *battery_sending = NULL;
// connected info
static bool bluetooth_connected = false;
// suppress vibration
static bool vibe_suppression = true;
static int8_t timezone_offset = 0;

// define the persistent storage key(s)
#define PK_SETTINGS      0
#define PK_LANG_GEN      1
#define PK_LANG_DATETIME 2

// define the appkeys used for appMessages
#define AK_STYLE_INV     0
#define AK_STYLE_DAY_INV 1
#define AK_STYLE_GRID    2
#define AK_VIBE_HOUR     3
#define AK_INTL_DOWO     4
//#define AK_INTL_FMT_DATE 5 // INCOMPLETE
#define AK_STYLE_AM_PM   6
#define AK_STYLE_DAY     7
#define AK_STYLE_WEEK    8
#define AK_INTL_FMT_WEEK 9
#define AK_VERSION       10 // UNUSED
#define AK_VIBE_PAT_DISCONNECT   11
#define AK_VIBE_PAT_CONNECT      12
#define AK_STRFTIME_FORMAT       13
#define AK_TRACK_BATTERY         14

#define AK_MESSAGE_TYPE          99
#define AK_SEND_BATT_PERCENT    100
#define AK_SEND_BATT_CHARGING   101
#define AK_SEND_BATT_PLUGGED    102
#define AK_TIMEZONE_OFFSET      103

// primary coordinates
#define DEVICE_WIDTH        144
#define DEVICE_HEIGHT       168
#define LAYOUT_STAT           0 // 20 tall
#define LAYOUT_SLOT_TOP      24 // 72 tall
#define LAYOUT_SLOT_BOT      96 // 72 tall, 4px gap above
#define LAYOUT_SLOT_HEIGHT   72
#define STAT_BATT_LEFT       96 // LEFT + WIDTH + NIB_WIDTH <= 143
#define STAT_BATT_TOP         4
#define STAT_BATT_WIDTH      44 // should be multiple of 10, after subtracting 4 (2 pixels/side for the 'border')
#define STAT_BATT_HEIGHT     15
#define STAT_BATT_NIB_WIDTH   3 // >= 3
#define STAT_BATT_NIB_HEIGHT  5 // >= 3
#define STAT_BT_ICON_LEFT    -2 // 0
#define STAT_BT_ICON_TOP      2
#define STAT_CHRG_ICON_LEFT  76
#define STAT_CHRG_ICON_TOP    2

// relative coordinates (relative to SLOTs)
#define REL_CLOCK_DATE_LEFT       0
#define REL_CLOCK_DATE_TOP       -9
#define REL_CLOCK_DATE_HEIGHT    30 // date/time overlap, due to the way text is 'positioned'
#define REL_CLOCK_TIME_LEFT       0
#define REL_CLOCK_TIME_TOP        7
#define REL_CLOCK_TIME_HEIGHT    60 // date/time overlap, due to the way text is 'positioned'
#define REL_CLOCK_SUBTEXT_TOP    56 // time/ampm overlap, due to the way text is 'positioned'

#define SLOT_ID_CLOCK_1  0
#define SLOT_ID_CALENDAR 1
#define SLOT_ID_WEATHER  2
#define SLOT_ID_CLOCK_2  3

// Create a struct to hold our persistent settings...
typedef struct persist {
  uint8_t version;                // version key
  uint8_t inverted;               // Invert display
  uint8_t day_invert;             // Invert colors on today's date
  uint8_t grid;                   // Show the grid
  uint8_t vibe_hour;              // vibrate at the top of the hour?
  uint8_t dayOfWeekOffset;        // first day of our week
  uint8_t date_format;            // date format
  uint8_t show_am_pm;             // Show AM/PM below time
  uint8_t show_day;               // Show day name below time
  uint8_t show_week;              // Show week number below time
  uint8_t week_format;            // week format (calculation, e.g. ISO 8601)
  uint8_t vibe_pat_disconnect;    // vibration pattern for disconnect
  uint8_t vibe_pat_connect;       // vibration pattern for connect
  char *strftime_format;          // custom date_format string (date_format = 255)
  uint8_t track_battery;          // track battery information
} __attribute__((__packed__)) persist;

typedef struct persist_datetime_lang { // 247 bytes
  char monthsNames[12][13];       // 156: 12 characters for each of 12 months
  char DaysOfWeek[7][13];         //  91: 12 characters for each of  7 weekdays
//                                   247 bytes
} __attribute__((__packed__)) persist_datetime_lang;

typedef struct persist_general_lang { // 101 bytes
  char statuses[2][10];           //  20:  9 characters for each of  2 statuses
  char abbrTime[2][6];            //  12:  5 characters for each of  2 abbreviations
  char abbrDaysOfWeek[7][3];      //  21:  2 characters for each of  7 weekdays abbreviations
  char abbrMonthsNames[12][4];    //  48:  3 characters for each of 12 months abbreviations
//                                   101 bytes
} __attribute__((__packed__)) persist_general_lang;

persist settings = {
  .version    = 10,
  .inverted   = 0, // no, dark
  .day_invert = 1, // yes
  .grid       = 1, // yes
  .vibe_hour  = 0, // no
  .dayOfWeekOffset = 1, // 0 - 6, Sun - Sat
  .date_format = 236, // Month DD, YYYY
  .show_am_pm  = 0, // no AM/PM       [0:Hide, 1:AM/PM, 2:TZ,    3:Week]
  .show_day    = 0, // no day name    [0:Hide, 1:Day,   2:Month, 3:TZ, 4:Week, 5:AM/PM]
  .show_week   = 0, // no week number [0:Hide, 1:Week,  2:TZ,    3:AM/PM
  .week_format = 0, // ISO 8601
  .vibe_pat_disconnect = 2, // double vibe
  .vibe_pat_connect = 0, // no vibe
  .strftime_format = "%Y-%m-%d",
  .track_battery = 0, // no battery tracking by default
};

persist_datetime_lang lang_datetime = {
  .monthsNames = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
  .DaysOfWeek = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
};

persist_general_lang lang_gen = {
  .statuses = { "", "NOLINK" },
  .abbrTime = { "AM", "PM" },
  .abbrDaysOfWeek = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" },
  .abbrMonthsNames = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
};

// How many days in the given month (0 for jan thru 11 for dec)
int daysInMonth(int mon, int year)
{
  if(mon==3 /*apr*/ || mon==5 /*jun*/ || mon==8 /*sep*/ || mon==10 /*nov*/ ) {
    return 30;
  } else if(mon==1 /*feb*/) {
    if     (year % 400 == 0) { return 29; } 
    else if(year % 100 == 0) { return 28; } 
    else if(year % 4   == 0) { return 29; } 
    else                     { return 28; }
  } else { return 31; }
}

struct tm *get_time()
{
  time_t tt = time(0);
  return localtime(&tt);
}

void setColors(GContext* ctx) {
  window_set_background_color(window, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_text_color(ctx, GColorWhite);
}

void setInvColors(GContext* ctx) {
  window_set_background_color(window, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_text_color(ctx, GColorBlack);
}

void calendar_layer_update_callback(Layer* me, GContext* ctx) {
  (void)me;
  struct tm * currentTime = get_time();

  int mon = currentTime->tm_mon;
  int year = currentTime->tm_year + 1900;
  int daysThisMonth = daysInMonth(mon, year);
  int specialDay = currentTime->tm_wday - settings.dayOfWeekOffset;
  if (specialDay < 0) { specialDay += 7; }
  /* We're going to build an array to hold the dates to be shown in the calendar.
   *
   * There are five 'parts' we'll calculate for this (though since we only display 3 weeks, we'll only ever see at most 4 of them)
   *
   *   daysVisPrevMonth = days from the previous month that are visible
   *   daysPriorToToday = days before today (including any days from previous month)
   *   ( today )
   *   daysAfterToday   = days after today (including any days from next month)
   *   daysVisNextMonth = days from the following month that are visible
   *
   *  daysPriorToToday + 1 + daysAfterToday = 21, since we display exactly 3 weeks.
   */
  int show_last = 1; // show last week?
  int show_next = 1; // show next week?
  int calendar[21];
  int cellNum = 0;   // address for current day table cell: 0-20
  int daysVisPrevMonth = 0;
  int daysVisNextMonth = 0;
  int daysPriorToToday = 7 + currentTime->tm_wday - settings.dayOfWeekOffset;
  int daysAfterToday   = 6 - currentTime->tm_wday + settings.dayOfWeekOffset;

  // tm_wday is based on Sunday being the startOfWeek, but Sunday may not be our startOfWeek.
  if (currentTime->tm_wday < settings.dayOfWeekOffset) { 
    if (show_last) {
      daysPriorToToday += 7; // we're <7, so in the 'first' week due to startOfWeek offset - 'add a week' before this one
    }
  } else {
    if (show_next) {
      daysAfterToday += 7;   // otherwise, we're already in the second week, so 'add a week' after
    }
  }

  if ( daysPriorToToday >= currentTime->tm_mday ) {
    // We're showing more days before today than exist this month
    int daysInPrevMonth = daysInMonth(mon - 1,year); // year only matters for February, which will be the same 'from' March

    // Number of days we'll show from the previous month
    daysVisPrevMonth = daysPriorToToday - currentTime->tm_mday + 1;

    for (int i = 0; i < daysVisPrevMonth; i++, cellNum++ ) {
      calendar[cellNum] = daysInPrevMonth + i - daysVisPrevMonth + 1;
    }
  }

  // optimization: instantiate i to a hot mess, since the first day we show this month may not be the 1st of the month
  int firstDayShownThisMonth = daysVisPrevMonth + currentTime->tm_mday - daysPriorToToday;
  for (int i = firstDayShownThisMonth; i < currentTime->tm_mday; i++, cellNum++ ) {
    calendar[cellNum] = i;
  }

  //int currentDay = cellNum; // the current day... we'll style this special
  calendar[cellNum] = currentTime->tm_mday;
  cellNum++;

  if ( currentTime->tm_mday + daysAfterToday > daysThisMonth ) {
    daysVisNextMonth = currentTime->tm_mday + daysAfterToday - daysThisMonth;
  }

  // add the days after today until the end of the month/next week, to our array...
  int daysLeftThisMonth = daysAfterToday - daysVisNextMonth;
  for (int i = 0; i < daysLeftThisMonth; i++, cellNum++ ) {
    calendar[cellNum] = i + currentTime->tm_mday + 1;
  }

  // add any days in the next month to our array...
  for (int i = 0; i < daysVisNextMonth; i++, cellNum++ ) {
    calendar[cellNum] = i + 1;
  }

// ---------------------------
// Now that we've calculated which days go where, we'll move on to the display logic.
// ---------------------------

  #define CAL_DAYS   7   // number of columns (days of the week)
  #define CAL_WIDTH  20  // width of columns
  #define CAL_GAP    1   // gap around calendar
  #define CAL_LEFT   2   // left side of calendar
  #define CAL_HEIGHT 18  // how tall rows should be depends on number of weeks

  int weeks  =  3;  // always display 3 weeks: previous, current, next
  if(!show_last) { weeks--; }
  if(!show_next) { weeks--; }
      
  GFont normal = fonts_get_system_font(FONT_KEY_GOTHIC_14); // fh = 16
  GFont bold   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); // fh = 22
  GFont current = normal;
  int font_vert_offset = 0;

  // generate a light background for the calendar grid
  setInvColors(ctx);
  graphics_fill_rect(ctx, GRect (CAL_LEFT + CAL_GAP, 
                                 CAL_HEIGHT - CAL_GAP, 
                                 DEVICE_WIDTH - 2 * (CAL_LEFT + CAL_GAP), 
                                 CAL_HEIGHT * weeks), 0, GCornerNone);
  setColors(ctx);
  for(int col = 0; col < CAL_DAYS; col++) {

    // Adjust labels by specified offset
    int weekday = col + settings.dayOfWeekOffset;
    if(weekday > 6) { weekday -= 7; }

    if(col == specialDay) {
      current = bold;
      font_vert_offset = -3;
    }
    // draw the cell background
    graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, 0, 
                                   CAL_WIDTH - CAL_GAP, 
                                   CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

    // draw the cell text
    graphics_draw_text(ctx, lang_gen.abbrDaysOfWeek[weekday], current, 
      GRect(CAL_WIDTH * col + CAL_LEFT + CAL_GAP, 
            CAL_GAP + font_vert_offset, 
            CAL_WIDTH, CAL_HEIGHT), 
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 
    if(col == specialDay) {
      current = normal;
      font_vert_offset = 0;
    }
  }

  // draw the individual calendar rows/columns
  int week = 0;
  for(int row = 1; row <= 3; row++) {
    if(row == 1 && !show_last) { continue; }
    if(row == 3 && !show_next) { continue; }
    week++;
    for(int col = 0; col < CAL_DAYS; col++) {
      if(row == 2 && col == specialDay) {
        setInvColors(ctx);
        current = bold;
        font_vert_offset = -3;
      }

      // draw the cell background
      graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, 
                                     CAL_HEIGHT * week, 
                                     CAL_WIDTH - CAL_GAP, 
                                     CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

      // draw the cell text
      char date_text[3];
      snprintf(date_text, sizeof(date_text), "%d", calendar[col+7*(row-1)]);
      graphics_draw_text(ctx, date_text, current, 
        GRect(CAL_WIDTH * col + CAL_LEFT, 
              CAL_HEIGHT * week - CAL_GAP + font_vert_offset, 
              CAL_WIDTH, 
              CAL_HEIGHT), 
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 

      if(row == 2 && col == specialDay) {
        setColors(ctx);
        current = normal;
        font_vert_offset = 0;
      }
    }
  }
}

void update_time_text() {
  // Need to be static because used by the system later.
  static char time_text[] = "00:00";

  char *time_format;

  if(clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }

  struct tm *currentTime = get_time();

  strftime(time_text, sizeof(time_text), time_format, currentTime);

  // Kludge to handle lack of non-padded hour format string
  // for twelve hour clock.
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  // would love to just use clock_copy_time_string, but it refuses to center 
  // properly in 12-hour time (see Kludge above)
  //clock_copy_time_string(time_text, sizeof(time_text));
  text_layer_set_text(time_layer, time_text);
}

void update_day_text(TextLayer *which_layer) {
  struct tm *currentTime = get_time();
  text_layer_set_text(which_layer, lang_datetime.DaysOfWeek[currentTime->tm_wday]);
}

void update_month_text(TextLayer *which_layer) {
  struct tm *currentTime = get_time();
  text_layer_set_text(which_layer, lang_datetime.monthsNames[currentTime->tm_mon]);
}

void update_week_text(TextLayer *which_layer) {
  struct tm *currentTime = get_time();
  static char week_text[] = "W00";
  if (settings.week_format == 0) {
    // ISO 8601 week number (00-53)
    strftime(week_text, sizeof(week_text), "W%V", currentTime);
  } else if (settings.week_format == 1) {
    // Week number with the first Sunday as the first day of week one (00-53)
    strftime(week_text, sizeof(week_text), "W%U", currentTime);
  } else if (settings.week_format == 2) {
    // Week number with the first Monday as the first day of week one (00-53)
    strftime(week_text, sizeof(week_text), "W%W", currentTime);
  }
  text_layer_set_text(which_layer, week_text);
}


void update_timezone_text(TextLayer *which_layer) {
  static char timezone_text[7];
  if (timezone_offset > 0) {
    snprintf(timezone_text, sizeof(timezone_text), "GMT-%d", timezone_offset);
  } else {
    snprintf(timezone_text, sizeof(timezone_text), "GMT+%d", abs(timezone_offset));
  }
  text_layer_set_text(which_layer, timezone_text);
}

void process_show_week() {
  switch(settings.show_week) {
  case 0: // Hide
    //layer_set_hidden(text_layer_get_layer(week_layer), true);
    return;
  case 1: // Show Week
    update_week_text(week_layer);
    break;
  case 2: // Show Timezone
    update_timezone_text(week_layer);
    break;
  }
}

void process_show_day() {
  switch ( settings.show_day ) {
  case 0: // Hide
    //layer_set_hidden(text_layer_get_layer(day_layer), true);
    return;
  case 1: // Show Day
    update_day_text(day_layer);
    break;
  case 2: // Show Month
    update_month_text(day_layer);
    break;
  case 3: // Show Timezone
    update_timezone_text(day_layer);
    break;
  case 4: // Show Week
    update_week_text(day_layer);
    break;
  }
}

void position_time_layer() {
  // potentially adjust the clock position, if we've added/removed the week, day, or AM/PM layers
  int time_offset = 0;
  if(!settings.show_day && !settings.show_week) {
    time_offset = 4;
    if(!settings.show_am_pm) {
      time_offset = 8;
    }
  }
  layer_set_frame( text_layer_get_layer(time_layer), 
    GRect(REL_CLOCK_TIME_LEFT, 
          REL_CLOCK_TIME_TOP + time_offset, 
          DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT) );
}

void update_datetime_subtext() {
    process_show_week();
    process_show_day();
    position_time_layer();
}

void datetime_layer_update_callback(Layer* me, GContext* ctx) {
    (void)me;
    setColors(ctx);
    update_time_text();
}

void statusbar_layer_update_callback(Layer *me, GContext* ctx) {
// XXX positioning tests... only valid if we leave statusbar's frame/bounds set to the whole watch...
/*
    setColors(ctx);
    graphics_draw_rect(ctx, GRect(0,  0, 144, 24)); // statusbar
    graphics_draw_rect(ctx, GRect(0, 24, 144, 72)); // top half
    graphics_draw_rect(ctx, GRect(0, 96, 144, 72)); // bottom half
*
    graphics_draw_rect(ctx, GRect(0, 50, 20, 20)); // linked
    graphics_draw_rect(ctx, GRect(0, 72, 20, 20)); // icon 2
    graphics_draw_rect(ctx, GRect(0, 50, 10, 42)); // battery l
    graphics_draw_rect(ctx, GRect(144-10, 50, 10, 42)); // battery r
    graphics_draw_rect(ctx, GRect(144-20, 50, 20, 20)); // icon 3
    graphics_draw_rect(ctx, GRect(144-20, 72, 20, 20)); // icon 4
    graphics_draw_rect(ctx, GRect(0, 46, 144, 50)); // targeting time
*/
}

void slot_top_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
}

void slot_bot_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
}

void battery_layer_update_callback(Layer *me, GContext* ctx) {
// simply draw the battery outline here - the text is a different layer, and we then 'fill' it with an inverterLayer
  setColors(ctx);
// battery outline
  graphics_draw_rect(ctx, GRect(STAT_BATT_LEFT, STAT_BATT_TOP, STAT_BATT_WIDTH, STAT_BATT_HEIGHT));
// battery 'nib' terminal
  graphics_draw_rect(ctx, GRect(STAT_BATT_LEFT + STAT_BATT_WIDTH - 1,
                                STAT_BATT_TOP + (STAT_BATT_HEIGHT - STAT_BATT_NIB_HEIGHT)/2,
                                STAT_BATT_NIB_WIDTH,
                                STAT_BATT_NIB_HEIGHT));
}

static void request_timezone() {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if(iter == NULL) {
    if(DEBUGLOG) { 
      app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
              "iterator is null: %d", result); 
    }
    return;
  }
  if(dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_TIMEZONE_OFFSET) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
}

static void battery_status_send(void *data) {
  if(!settings.track_battery) {
    return; // if track battery setting's off, saves power w/ appmessages
  }
  if(  (battery_percent  == sent_battery_percent  )
     & (battery_charging == sent_battery_charging )
     & (battery_plugged  == sent_battery_plugged  )) {
    if(DEBUGLOG) { 
      app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
              "repeat battery reading"); 
    }
    battery_sending = NULL;
    return; // no need to resend the same value
  }
  DictionaryIterator *iter;

  AppMessageResult result = app_message_outbox_begin(&iter);

  if(iter == NULL) {
    if(DEBUGLOG) { 
      app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
              "iterator is null: %d", result); 
    }
    return;
  }

  if(result != APP_MSG_OK) {
    if(DEBUGLOG) { 
      app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
             "Dict write failed to open outbox: %d", (AppMessageResult) result);
    }
    return;
  }

  if(dict_write_uint8(iter, AK_MESSAGE_TYPE, AK_SEND_BATT_PERCENT) != DICT_OK) {
    return;
  }
  if(dict_write_uint8(iter, AK_SEND_BATT_PERCENT, battery_percent) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_BATT_CHARGING, battery_charging ? 1: 0) != DICT_OK) {
    return;
  }
  if (dict_write_uint8(iter, AK_SEND_BATT_PLUGGED, battery_plugged ? 1: 0) != DICT_OK) {
    return;
  }
  app_message_outbox_send();
  sent_battery_percent  = battery_percent;
  sent_battery_charging = battery_charging;
  sent_battery_plugged  = battery_plugged;
  battery_sending = NULL;
}

static void handle_battery(BatteryChargeState charge_state) {
  static char battery_text[] = "100";

  battery_percent = charge_state.charge_percent;
  uint8_t battery_meter = battery_percent/10*(STAT_BATT_WIDTH-4)/10;
  battery_charging = charge_state.is_charging;
  battery_plugged = charge_state.is_plugged;

  // fill it in with current power
  layer_set_bounds(inverter_layer_get_layer(battery_meter_layer), GRect(STAT_BATT_LEFT+2, STAT_BATT_TOP+2, battery_meter, STAT_BATT_HEIGHT-4));
  layer_set_hidden(inverter_layer_get_layer(battery_meter_layer), false);

  //if (DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "battery reading"); }
  if (battery_sending == NULL) {
    // multiple battery events can fire in rapid succession, we'll let it settle down before logging it
    battery_sending = app_timer_register(5000, &battery_status_send, NULL);
    if (DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "battery timer queued"); }
  }

  if (charge_state.is_charging) { // charging
    layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
    bitmap_layer_set_bitmap(bmp_charging_layer, image_charging_icon);
  } else { // not charging
    if (charge_state.is_plugged) { // plugged but not charging = charging complete...
      layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
    } else { // normal wear
      if (settings.vibe_hour) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
        bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
      } else {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
      }
    }
  }
  snprintf(battery_text, sizeof(battery_text), "%d", charge_state.charge_percent);
  text_layer_set_text(text_battery_layer, battery_text);
  layer_mark_dirty(battery_layer);
}

void generate_vibe(uint32_t vibe_pattern_number) {
  if (vibe_suppression) { return; }
  vibes_cancel();
  switch ( vibe_pattern_number ) {
  case 0: // No Vibration
    return;
  case 1: // Single short
    vibes_short_pulse();
    break;
  case 2: // Double short
    vibes_double_pulse();
    break;
  case 3: // Triple
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {200, 100, 200, 100, 200},
      .num_segments = 5
    } );
  case 4: // Long
    vibes_long_pulse();
    break;
  case 5: // Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {50, 200, 50, 200, 50, 200, 50},
      .num_segments = 7
    } );
    break;
  case 6: // Less Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {100, 200, 100, 200, 100, 200, 100},
      .num_segments = 7
    } );
    break;
  case 7: // Not Subtle
    vibes_enqueue_custom_pattern( (VibePattern) {
      .durations = (uint32_t []) {500, 250, 500, 250, 500, 250, 500},
      .num_segments = 7
    } );
    break;
  default: // No Vibration
    return;
  }
}

void update_connection() {
  text_layer_set_text(text_connection_layer, 
    bluetooth_connected ? lang_gen.statuses[0] : lang_gen.statuses[1]);
  if(bluetooth_connected) {
    generate_vibe(settings.vibe_pat_connect);  // no-op by default
    bitmap_layer_set_bitmap(bmp_connection_layer, image_connection_icon);
  } else {
    generate_vibe(settings.vibe_pat_disconnect);  // because, this is bad...
    bitmap_layer_set_bitmap(bmp_connection_layer, image_noconnection_icon);
  }
}

static void handle_bluetooth(bool connected) {
  bluetooth_connected = connected;
  update_connection();
}

static void window_load(Window *window) {

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  //statusbar = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  statusbar = layer_create(GRect(0,0,DEVICE_WIDTH,DEVICE_HEIGHT));
  layer_set_update_proc(statusbar, statusbar_layer_update_callback);
  layer_add_child(window_layer, statusbar);
  GRect stat_bounds = layer_get_bounds(statusbar);

  slot_top = layer_create(GRect(0,LAYOUT_SLOT_TOP,DEVICE_WIDTH,LAYOUT_SLOT_BOT));
  layer_set_update_proc(slot_top, slot_top_layer_update_callback);
  layer_add_child(window_layer, slot_top);
  GRect slot_top_bounds = layer_get_bounds(slot_top);

  slot_bot = layer_create(GRect(0,LAYOUT_SLOT_BOT,DEVICE_WIDTH,DEVICE_HEIGHT));
  layer_set_update_proc(slot_bot, slot_bot_layer_update_callback);
  layer_add_child(window_layer, slot_bot);
  GRect slot_bot_bounds = layer_get_bounds(slot_bot);

  bmp_connection_layer = bitmap_layer_create( GRect(STAT_BT_ICON_LEFT, STAT_BT_ICON_TOP, 20, 20) );
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_connection_layer));
  image_connection_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_LINKED_ICON);
  image_noconnection_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_NOLINK_ICON);

  bmp_charging_layer = bitmap_layer_create( GRect(STAT_CHRG_ICON_LEFT, STAT_CHRG_ICON_TOP, 20, 20) );
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer));
  image_charging_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGING_ICON);
  image_hourvibe_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HOURVIBE_ICON);
  if (settings.vibe_hour) {
    bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
  }

  battery_layer = layer_create(stat_bounds);
  layer_set_update_proc(battery_layer, battery_layer_update_callback);
  layer_add_child(statusbar, battery_layer);

  datetime_layer = layer_create(slot_top_bounds);
  layer_set_update_proc(datetime_layer, datetime_layer_update_callback);
  layer_add_child(slot_top, datetime_layer);

  calendar_layer = layer_create(slot_bot_bounds);
  layer_set_update_proc(calendar_layer, calendar_layer_update_callback);
  layer_add_child(slot_bot, calendar_layer);

  date_layer = text_layer_create( GRect(REL_CLOCK_DATE_LEFT, REL_CLOCK_DATE_TOP, DEVICE_WIDTH, REL_CLOCK_DATE_HEIGHT) );
  text_layer_set_text_color(date_layer, GColorWhite);
  text_layer_set_background_color(date_layer, GColorClear);
  text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(date_layer));

  time_layer = text_layer_create( GRect(REL_CLOCK_TIME_LEFT, REL_CLOCK_TIME_TOP, DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT) ); // see position_time_layer()
  text_layer_set_text_color(time_layer, GColorWhite);
  text_layer_set_background_color(time_layer, GColorClear);
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  position_time_layer(); // make use of our whitespace, if we have it...
  layer_add_child(datetime_layer, text_layer_get_layer(time_layer));

  week_layer = text_layer_create( GRect(4, REL_CLOCK_SUBTEXT_TOP, 140, 16) );
  text_layer_set_text_color(week_layer, GColorWhite);
  text_layer_set_background_color(week_layer, GColorClear);
  text_layer_set_font(week_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(week_layer, GTextAlignmentLeft);
  layer_add_child(datetime_layer, text_layer_get_layer(week_layer));
  if ( settings.show_week == 0 ) {
    layer_set_hidden(text_layer_get_layer(week_layer), true);
  }

  day_layer = text_layer_create( GRect(28, REL_CLOCK_SUBTEXT_TOP, DEVICE_WIDTH - 56, 16) );
  text_layer_set_text_color(day_layer, GColorWhite);
  text_layer_set_background_color(day_layer, GColorClear);
  text_layer_set_font(day_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(day_layer, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(day_layer));
  if ( settings.show_day == 0 ) {
    layer_set_hidden(text_layer_get_layer(day_layer), true);
  }

  update_datetime_subtext();

  text_connection_layer = text_layer_create( GRect(20+STAT_BT_ICON_LEFT, 0, 72, 22) );
  text_layer_set_text_color(text_connection_layer, GColorWhite);
  text_layer_set_background_color(text_connection_layer, GColorClear);
  text_layer_set_font(text_connection_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_connection_layer, GTextAlignmentLeft);
  text_layer_set_text(text_connection_layer, "NO LINK");
  layer_add_child(statusbar, text_layer_get_layer(text_connection_layer));

  text_battery_layer = text_layer_create( GRect(STAT_BATT_LEFT, STAT_BATT_TOP-2, STAT_BATT_WIDTH, STAT_BATT_HEIGHT) );
  text_layer_set_text_color(text_battery_layer, GColorWhite);
  text_layer_set_background_color(text_battery_layer, GColorClear);
  text_layer_set_font(text_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(text_battery_layer, GTextAlignmentCenter);
  text_layer_set_text(text_battery_layer, "?");

  layer_add_child(statusbar, text_layer_get_layer(text_battery_layer));

  // NOTE: No more adding layers below here - the inverter layers NEED to be the last to be on top!

  // hide battery meter, until we can fix the size/position later when subscribing
  battery_meter_layer = inverter_layer_create(stat_bounds);
  layer_set_hidden(inverter_layer_get_layer(battery_meter_layer), true);
  layer_add_child(statusbar, inverter_layer_get_layer(battery_meter_layer));

  // topmost inverter layer, determines dark or light...
  inverter_layer = inverter_layer_create(bounds);
  if (settings.inverted==0) {
    layer_set_hidden(inverter_layer_get_layer(inverter_layer), true);
  }
  layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));

}

static void window_unload(Window *window) {
  // unload anything we loaded, destroy anything we created, remove anything we added
  layer_destroy(inverter_layer_get_layer(inverter_layer));
  layer_destroy(inverter_layer_get_layer(battery_meter_layer));
  layer_destroy(text_layer_get_layer(text_battery_layer));
  layer_destroy(text_layer_get_layer(text_connection_layer));
  layer_destroy(text_layer_get_layer(day_layer));
  layer_destroy(text_layer_get_layer(week_layer));
  layer_destroy(text_layer_get_layer(time_layer));
  layer_destroy(text_layer_get_layer(date_layer));
  layer_destroy(calendar_layer);
  layer_destroy(datetime_layer);
  layer_destroy(battery_layer);
  layer_remove_from_parent(bitmap_layer_get_layer(bmp_charging_layer));
  layer_remove_from_parent(bitmap_layer_get_layer(bmp_connection_layer));
  bitmap_layer_destroy(bmp_charging_layer);
  bitmap_layer_destroy(bmp_connection_layer);
  gbitmap_destroy(image_connection_icon);
  gbitmap_destroy(image_noconnection_icon);
  gbitmap_destroy(image_charging_icon);
  gbitmap_destroy(image_hourvibe_icon);
  layer_destroy(slot_bot);
  layer_destroy(slot_top);
  layer_destroy(statusbar);
}

static void deinit(void) {
  // deinit anything we init
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(window);
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  update_time_text();

  //if (units_changed & MONTH_UNIT) {
  //  update_date_text();
  //}

  if (units_changed & HOUR_UNIT) {
    request_timezone();
    update_datetime_subtext();
    if (settings.vibe_hour) {
      generate_vibe(settings.vibe_hour);
    }
  }

  if (units_changed & DAY_UNIT) {
    //layer_mark_dirty(datetime_layer);
    layer_mark_dirty(calendar_layer);
  }

  // calendar gets redrawn every time because time_layer is changed and all layers are redrawn together.
}

void my_out_sent_handler(DictionaryIterator *sent, void *context) {
// outgoing message was delivered
}
void my_out_fail_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
// outgoing message failed
  if (DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "AppMessage Failed to Send: %d", reason); }
}

void in_timezone_handler(DictionaryIterator *received, void *context) {
    Tuple *tz_offset = dict_find(received, AK_TIMEZONE_OFFSET);
    if (tz_offset != NULL) {
      timezone_offset = tz_offset->value->int8;
      update_datetime_subtext();
    }
  if (DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Timezone received: %d", timezone_offset); }
}

void in_configuration_handler(DictionaryIterator *received, void *context) {
    // style_inv == inverted
    Tuple *style_inv = dict_find(received, AK_STYLE_INV);
    if (style_inv != NULL) {
      settings.inverted = style_inv->value->uint8;
      if (style_inv->value->uint8==0) {
        layer_set_hidden(inverter_layer_get_layer(inverter_layer), true); // hide inversion = dark
      } else {
        layer_set_hidden(inverter_layer_get_layer(inverter_layer), false); // show inversion = light
      }
    }

    // style_day_inv == day_invert
    Tuple *style_day_inv = dict_find(received, AK_STYLE_DAY_INV);
    if (style_day_inv != NULL) {
      settings.day_invert = style_day_inv->value->uint8;
    }

    // style_grid == grid
    Tuple *style_grid = dict_find(received, AK_STYLE_GRID);
    if (style_grid != NULL) {
      settings.grid = style_grid->value->uint8;
    }

    // AK_VIBE_HOUR == vibe_hour - vibration patterns for hourly vibration
    Tuple *vibe_hour = dict_find(received, AK_VIBE_HOUR);
    if (vibe_hour != NULL) {
      settings.vibe_hour = vibe_hour->value->uint8;
      if (settings.vibe_hour && !battery_plugged) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
        bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
      } else if (!battery_charging) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
      }
    }

    // INTL_DOWO == dayOfWeekOffset
    Tuple *INTL_DOWO = dict_find(received, AK_INTL_DOWO);
    if (INTL_DOWO != NULL) {
      settings.dayOfWeekOffset = INTL_DOWO->value->uint8;
    }

    // AK_INTL_FMT_DATE == date format (strftime + manual localization)
    //Tuple *FMT_DATE = dict_find(received, AK_INTL_FMT_DATE);
    //if (FMT_DATE != NULL) {
    //  settings.date_format = FMT_DATE->value->uint8;
    //  update_date_text();
    //}

    // AK_STYLE_WEEK
    Tuple *style_week = dict_find(received, AK_STYLE_WEEK);
    if (style_week != NULL) {
      settings.show_week = style_week->value->uint8;
      if ( settings.show_week ) {
        layer_set_hidden(text_layer_get_layer(week_layer), false);
      }  else {
        layer_set_hidden(text_layer_get_layer(week_layer), true);
      }
    }

    // AK_INTL_FMT_WEEK == week format (strftime)
    Tuple *FMT_WEEK = dict_find(received, AK_INTL_FMT_WEEK);
    if (FMT_WEEK != NULL) {
      settings.week_format = FMT_WEEK->value->uint8;
    }

    // AK_STYLE_DAY
    Tuple *style_day = dict_find(received, AK_STYLE_DAY);
    if (style_day != NULL) {
      settings.show_day = style_day->value->uint8;
      if ( settings.show_day ) {
        layer_set_hidden(text_layer_get_layer(day_layer), false);
      }  else {
        layer_set_hidden(text_layer_get_layer(day_layer), true);
      }
    }

    // now that we've received any changes, redraw the subtext (which processes week, day, and AM/PM)
    update_datetime_subtext();

    // AK_VIBE_PAT_DISCONNECT / AK_VIBE_PAT_CONNECT == vibration patterns for connect and disconnect
    Tuple *VIBE_PAT_D = dict_find(received, AK_VIBE_PAT_DISCONNECT);
    if (VIBE_PAT_D != NULL) {
      settings.vibe_pat_disconnect = VIBE_PAT_D->value->uint8;
    }
    Tuple *VIBE_PAT_C = dict_find(received, AK_VIBE_PAT_CONNECT);
    if (VIBE_PAT_C != NULL) {
      settings.vibe_pat_connect = VIBE_PAT_C->value->uint8;
    }

    // AK_TRACK_BATTERY == vibe_hour - vibration patterns for hourly vibration
    Tuple *track_battery = dict_find(received, AK_TRACK_BATTERY);
    if (track_battery != NULL) {
      settings.track_battery = track_battery->value->uint8;
      if (settings.track_battery) {
        battery_status_send(NULL); // either it was just turned on, or we'll get a bonus datapoint from running config.
      }
    }

    int result = 0;
    result = persist_write_data(PK_SETTINGS, &settings, sizeof(settings) );
    if(DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
                           "Wrote %d bytes into settings", result); }
    result = persist_write_data(PK_LANG_GEN, &lang_gen, sizeof(lang_gen) );
    if(DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
                           "Wrote %d bytes into lang_gen", result); }
    result = persist_write_data(PK_LANG_DATETIME, &lang_datetime, 
                                sizeof(lang_datetime) );
    if(DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
                           "Wrote %d bytes into lang_datetime", result); }

    // ==== Implemented SDK ====
    // Battery
    // Connected
    // Persistent Storage
    // Screenshot Operation
    // ==== Available in SDK ====
    // Accelerometer
    // App Focus
    // ==== Waiting on / SDK gaps ====
    // Magnetometer
    // PebbleKit JS - more accurate location data
    // ==== Interesting SDK possibilities ====
    // PebbleKit JS - more information from phone
    // ==== Future improvements ====
    // Positioning - top, bottom, etc.
  if(1) { layer_mark_dirty(calendar_layer); }
  if(1) { layer_mark_dirty(datetime_layer); }

  //update_time_text(&currentTime);
}

void my_in_rcv_handler(DictionaryIterator *received, void *context) {
// incoming message received
  Tuple *message_type = dict_find(received, AK_MESSAGE_TYPE);
  if(message_type != NULL) {
    if(DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
                      "Message type %d received", message_type->value->uint8); }
    switch ( message_type->value->uint8 ) {
    case AK_TIMEZONE_OFFSET:
      in_timezone_handler(received, context);
      return;
    }
  } else {
    // default to configuration, which may not send the message type...
    in_configuration_handler(received, context);
  }
}

void my_in_drp_handler(AppMessageResult reason, void *context) {
// incoming message dropped
  if(DEBUGLOG) { app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, 
                         "AppMessage Dropped: %d", reason); }
}

static void app_message_init(void) {
  // Register message handlers
  app_message_register_inbox_received(my_in_rcv_handler);
  app_message_register_inbox_dropped(my_in_drp_handler);
  app_message_register_outbox_sent(my_out_sent_handler);
  app_message_register_outbox_failed(my_out_fail_handler);
  // Init buffers
  app_message_open(app_message_inbox_size_maximum(), 
                   app_message_outbox_size_maximum());
}

static void init(void) {
  app_message_init();

  if(persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, &settings, sizeof(settings) );
  }
  if(persist_exists(PK_LANG_GEN)) {
    persist_read_data(PK_LANG_GEN, &lang_gen, sizeof(lang_gen) );
  }
  if(persist_exists(PK_LANG_DATETIME)) {
    persist_read_data(PK_LANG_DATETIME, &lang_datetime, sizeof(lang_datetime) );
  }

  request_timezone();

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  const bool animated = false;
  window_set_background_color(window, GColorBlack);
  window_stack_push(window, animated);

  //update_time_text();

  tick_timer_service_subscribe(MINUTE_UNIT, &handle_minute_tick);
  battery_state_service_subscribe(&handle_battery);
  handle_battery(battery_state_service_peek()); // initialize
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  handle_bluetooth(bluetooth_connection_service_peek()); // initialize
  vibe_suppression = false;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
