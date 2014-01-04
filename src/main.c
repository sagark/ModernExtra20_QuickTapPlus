#include "pebble.h"
#include "common.h"
#include "QTPlus.h"


static Window* window;
static GBitmap *background_image_container;

static Layer *minute_display_layer;
static Layer *hour_display_layer;
static Layer *center_display_layer;
static Layer *second_display_layer;
static TextLayer *date_layer;
static char date_text[] = "Wed 13 ";
static bool bt_ok = false;
static uint8_t battery_level;
static bool battery_plugged;

static GBitmap *icon_battery;
static GBitmap *icon_battery_charge;
static GBitmap *icon_bt;

static Layer *battery_layer;
static Layer *bt_layer;

bool g_conserve = false;

#ifdef INVERSE
static InverterLayer *full_inverse_layer;
#endif

static Layer *background_layer;
static Layer *window_layer;

const GPathInfo MINUTE_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -70 }, { -4, -70 }, } };

const GPathInfo HOUR_HAND_PATH_POINTS = { 4, (GPoint[] ) { { -4, 15 },
				{ 4, 15 }, { 4, -50 }, { -4, -50 }, } };

static GPath *hour_hand_path;
static GPath *minute_hand_path;

static AppTimer *timer_handle;
#define COOKIE_MY_TIMER 1
static int my_cookie = COOKIE_MY_TIMER;
#define ANIM_IDLE 0
#define ANIM_START 1
#define ANIM_HOURS 2
#define ANIM_MINUTES 3
#define ANIM_SECONDS 4
#define ANIM_DONE 5
int init_anim = ANIM_DONE;
int32_t second_angle_anim = 0;
unsigned int minute_angle_anim = 0;
unsigned int hour_angle_anim = 0;

void handle_timer(void* vdata) {

	int *data = (int *) vdata;

	if (*data == my_cookie) {
		if (init_anim == ANIM_START) {
			init_anim = ANIM_HOURS;
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		} else if (init_anim == ANIM_HOURS) {
			layer_mark_dirty(hour_display_layer);
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		} else if (init_anim == ANIM_MINUTES) {
			layer_mark_dirty(minute_display_layer);
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		} else if (init_anim == ANIM_SECONDS) {
			layer_mark_dirty(second_display_layer);
			timer_handle = app_timer_register(50 /* milliseconds */,
					&handle_timer, &my_cookie);
		}
	}

}

void second_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	int32_t second_angle = t->tm_sec * (0xffff / 60);
	int second_hand_length = 70;
	GPoint center = grect_center_point(&GRECT_FULL_WINDOW);
	GPoint second = GPoint(center.x, center.y - second_hand_length);

	if (init_anim < ANIM_SECONDS) {
		second = GPoint(center.x, center.y - 70);
	} else if (init_anim == ANIM_SECONDS) {
		second_angle_anim += 0xffff / 60;
		if (second_angle_anim >= second_angle) {
			init_anim = ANIM_DONE;
			second =
					GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
							center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
		} else {
			second =
					GPoint(center.x + second_hand_length * sin_lookup(second_angle_anim)/0xffff,
							center.y + (-second_hand_length) * cos_lookup(second_angle_anim)/0xffff);
		}
	} else {
		second =
				GPoint(center.x + second_hand_length * sin_lookup(second_angle)/0xffff,
						center.y + (-second_hand_length) * cos_lookup(second_angle)/0xffff);
	}

	graphics_context_set_stroke_color(ctx, GColorWhite);

	graphics_draw_line(ctx, center, second);
}

void center_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	GPoint center = grect_center_point(&GRECT_FULL_WINDOW);
	graphics_context_set_fill_color(ctx, GColorBlack);
	graphics_fill_circle(ctx, center, 4);
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_circle(ctx, center, 3);
}

void minute_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_min * 6 + t->tm_sec / 10;

	if (init_anim < ANIM_MINUTES) {
		angle = 0;
	} else if (init_anim == ANIM_MINUTES) {
		minute_angle_anim += 6;
		if (minute_angle_anim >= angle) {
			init_anim = ANIM_SECONDS;
		} else {
			angle = minute_angle_anim;
		}
	}

	gpath_rotate_to(minute_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorBlack);

	gpath_draw_filled(ctx, minute_hand_path);
	gpath_draw_outline(ctx, minute_hand_path);
}

void hour_display_layer_update_callback(Layer *me, GContext* ctx) {
	(void) me;

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	unsigned int angle = t->tm_hour * 30 + t->tm_min / 2;

	if (init_anim < ANIM_HOURS) {
		angle = 0;
	} else if (init_anim == ANIM_HOURS) {
		if (hour_angle_anim == 0 && t->tm_hour >= 12) {
			hour_angle_anim = 360;
		}
		hour_angle_anim += 6;
		if (hour_angle_anim >= angle) {
			init_anim = ANIM_MINUTES;
		} else {
			angle = hour_angle_anim;
		}
	}

	gpath_rotate_to(hour_hand_path, (TRIG_MAX_ANGLE / 360) * angle);

	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorBlack);

	gpath_draw_filled(ctx, hour_hand_path);
	gpath_draw_outline(ctx, hour_hand_path);
}

void draw_date() {

	time_t now = time(NULL);
	struct tm *t = localtime(&now);

	strftime(date_text, sizeof(date_text), "%a %d", t);

	text_layer_set_text(date_layer, date_text);
}

/*
 * Battery icon callback handler
 */
void battery_layer_update_callback(Layer *layer, GContext *ctx) {

  graphics_context_set_compositing_mode(ctx, GCompOpAssign);

  if (!battery_plugged) {
    graphics_draw_bitmap_in_rect(ctx, icon_battery, GRect(0, 0, 24, 12));
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(7, 4, (uint8_t)((battery_level / 100.0) * 11.0), 4), 0, GCornerNone);
  } else {
    graphics_draw_bitmap_in_rect(ctx, icon_battery_charge, GRect(0, 0, 24, 12));
  }
}



void battery_state_handler(BatteryChargeState charge) {
	battery_level = charge.charge_percent;
	battery_plugged = charge.is_plugged;
	layer_mark_dirty(battery_layer);
	if (!battery_plugged && battery_level < 20)
		conserve_power(true);
	else
		conserve_power(false);
}

/*
 * Bluetooth icon callback handler
 */
void bt_layer_update_callback(Layer *layer, GContext *ctx) {
  if (bt_ok)
  	graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  else
  	graphics_context_set_compositing_mode(ctx, GCompOpClear);
  graphics_draw_bitmap_in_rect(ctx, icon_bt, GRect(0, 0, 9, 12));
}

void bt_connection_handler(bool connected) {
	bt_ok = connected;
	layer_mark_dirty(bt_layer);
}

void draw_background_callback(Layer *layer, GContext *ctx) {
	graphics_context_set_compositing_mode(ctx, GCompOpAssign);
	graphics_draw_bitmap_in_rect(ctx, background_image_container,
			GRECT_FULL_WINDOW);
}

void init() {

	// Window
	window = window_create();
	window_stack_push(window, true /* Animated */);
	window_layer = window_get_root_layer(window);

	// Background image
	background_image_container = gbitmap_create_with_resource(
			RESOURCE_ID_IMAGE_BACKGROUND);
	background_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(background_layer, &draw_background_callback);
	layer_add_child(window_layer, background_layer);

	// Date setup
	date_layer = text_layer_create(GRect(27, 100, 90, 21));
	text_layer_set_text_color(date_layer, GColorWhite);
	text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
	text_layer_set_background_color(date_layer, GColorClear);
	text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_layer, text_layer_get_layer(date_layer));

	draw_date();

	// Status setup
	icon_battery = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_ICON);
	icon_battery_charge = gbitmap_create_with_resource(RESOURCE_ID_BATTERY_CHARGE);
	icon_bt = gbitmap_create_with_resource(RESOURCE_ID_BLUETOOTH);

	BatteryChargeState initial = battery_state_service_peek();
	battery_level = initial.charge_percent;
	battery_plugged = initial.is_plugged;
	battery_layer = layer_create(GRect(50,56,24,12)); //24*12
	layer_set_update_proc(battery_layer, &battery_layer_update_callback);
	layer_add_child(window_layer, battery_layer);


	bt_ok = bluetooth_connection_service_peek();
	bt_layer = layer_create(GRect(83,56,9,12)); //9*12
	layer_set_update_proc(bt_layer, &bt_layer_update_callback);
	layer_add_child(window_layer, bt_layer);

	// Hands setup
	hour_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(hour_display_layer,
			&hour_display_layer_update_callback);
	layer_add_child(window_layer, hour_display_layer);

	hour_hand_path = gpath_create(&HOUR_HAND_PATH_POINTS);
	gpath_move_to(hour_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	minute_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(minute_display_layer,
			&minute_display_layer_update_callback);
	layer_add_child(window_layer, minute_display_layer);

	minute_hand_path = gpath_create(&MINUTE_HAND_PATH_POINTS);
	gpath_move_to(minute_hand_path, grect_center_point(&GRECT_FULL_WINDOW));

	center_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(center_display_layer,
			&center_display_layer_update_callback);
	layer_add_child(window_layer, center_display_layer);

	second_display_layer = layer_create(GRECT_FULL_WINDOW);
	layer_set_update_proc(second_display_layer,
			&second_display_layer_update_callback);
	layer_add_child(window_layer, second_display_layer);

	// Configurable inverse
#ifdef INVERSE
	full_inverse_layer = inverter_layer_create(GRECT_FULL_WINDOW);
	layer_add_child(window_layer, inverter_layer_get_layer(full_inverse_layer));
#endif

    qtp_setup();


}

void deinit() {

	window_destroy(window);
	gbitmap_destroy(background_image_container);
	gbitmap_destroy(icon_battery);
	gbitmap_destroy(icon_battery_charge);
	gbitmap_destroy(icon_bt);
	text_layer_destroy(date_layer);
	layer_destroy(minute_display_layer);
	layer_destroy(hour_display_layer);
	layer_destroy(center_display_layer);
	layer_destroy(second_display_layer);
	layer_destroy(battery_layer);
	layer_destroy(bt_layer);

#ifdef INVERSE
	inverter_layer_destroy(full_inverse_layer);
#endif

	layer_destroy(background_layer);
	layer_destroy(window_layer);

	gpath_destroy(hour_hand_path);
	gpath_destroy(minute_hand_path);

    qtp_app_deinit();
}

void handle_tick(struct tm *tick_time, TimeUnits units_changed) {

	if (init_anim == ANIM_IDLE) {
		init_anim = ANIM_START;
		timer_handle = app_timer_register(50 /* milliseconds */, &handle_timer,
				&my_cookie);
	} else if (init_anim == ANIM_DONE) {
		if (tick_time->tm_sec % 10 == 0) {
			layer_mark_dirty(minute_display_layer);

			if (tick_time->tm_sec == 0) {
				if (tick_time->tm_min % 2 == 0) {
					layer_mark_dirty(hour_display_layer);
					if (tick_time->tm_min == 0 && tick_time->tm_hour == 0) {
						draw_date();
					}
				}
			}
		}

		layer_mark_dirty(second_display_layer);
	}
}

void conserve_power(bool conserve) {
	if (conserve == g_conserve)
		return;
	g_conserve = conserve;
	if (conserve) {
		tick_timer_service_unsubscribe();
		tick_timer_service_subscribe(MINUTE_UNIT, &handle_tick);
		layer_set_hidden(second_display_layer, true);
	} else {
		tick_timer_service_unsubscribe();
		tick_timer_service_subscribe(SECOND_UNIT, &handle_tick);
		layer_set_hidden(second_display_layer, false);
	}
}


/* Initialize listeners to show and hide Quick Tap Plus as well as update data */
void qtp_setup() {
	qtp_is_showing = false;
	accel_tap_service_subscribe(&qtp_tap_handler);
	qtp_bluetooth_image = gbitmap_create_with_resource(RESOURCE_ID_QTP_IMG_BT);
	qtp_battery_image = gbitmap_create_with_resource(RESOURCE_ID_QTP_IMG_BAT);
	
	if (qtp_is_show_weather()) {
		qtp_setup_app_message();
	}
}

/* Handle taps from the hardware */
void qtp_tap_handler(AccelAxisType axis, int32_t direction) {
	if (qtp_is_showing) {
		qtp_hide();
	} else {
		qtp_show();
	}
	qtp_is_showing = !qtp_is_showing;
}

/* Subscribe to taps and pass them to the handler */
void qtp_click_config_provider(Window *window) {
	window_single_click_subscribe(BUTTON_ID_BACK, qtp_back_click_responder);
}

/* Unusued. Subscribe to back button to exit */
void qtp_back_click_responder(ClickRecognizerRef recognizer, void *context) {
	qtp_hide();
}

/* Update the text layer for the battery status */
void qtp_update_battery_status(bool mark_dirty) {
	BatteryChargeState charge_state = battery_state_service_peek();
	static char battery_text[] = "100%";
	snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);

	text_layer_set_text(qtp_battery_text_layer, battery_text);
	if (mark_dirty) {
		layer_mark_dirty(text_layer_get_layer(qtp_battery_text_layer));
	}
}

/* Update the weather icon. Destroy the current one if necessary */
void qtp_update_weather_icon(int icon_index, bool remove_old, bool mark_dirty) {
	const int icon_id = QTP_WEATHER_ICONS[icon_index];
	qtp_weather_icon = gbitmap_create_with_resource(icon_id);
	bitmap_layer_set_bitmap(qtp_weather_icon_layer, qtp_weather_icon);
	if (remove_old) {
		gbitmap_destroy(qtp_weather_icon);
	}
	if (mark_dirty) {
		layer_mark_dirty(bitmap_layer_get_layer(qtp_weather_icon_layer));
	}
}

/* Update the text layer for the bluetooth status */
void qtp_update_bluetooth_status(bool mark_dirty) {
	static char bluetooth_text[] = "Not Paired";

	if (bluetooth_connection_service_peek()) {
		snprintf(bluetooth_text, sizeof(bluetooth_text), "Paired");
	}

	text_layer_set_text(qtp_bluetooth_text_layer, bluetooth_text);
	if (mark_dirty) {
		layer_mark_dirty(text_layer_get_layer(qtp_bluetooth_text_layer));
	}
}

/* Update the text layer for the clock */
void qtp_update_time(bool mark_dirty) {
	static char time_text[10];
	clock_copy_time_string(time_text, sizeof(time_text));
	text_layer_set_text(qtp_time_layer, time_text);

	if (mark_dirty) {
		layer_mark_dirty(text_layer_get_layer(qtp_time_layer));
	}
}


/* Setup app message callbacks for weather */
void qtp_setup_app_message() {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: setting app message for weather");

	const int inbound_size = 100;
	const int outbound_size = 100;
	app_message_open(inbound_size, outbound_size);
	Tuplet initial_values[] = {
		TupletInteger(QTP_WEATHER_ICON_KEY, (uint8_t) 8),
		TupletCString(QTP_WEATHER_TEMP_F_KEY, "---\u00B0F"),
		TupletCString(QTP_WEATHER_TEMP_C_KEY, "---\u00B0F"),
		TupletCString(QTP_WEATHER_CITY_KEY, "Atlanta      "),
		TupletCString(QTP_WEATHER_DESC_KEY, "                       ")
	};
	APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather tuples intialized");

	app_sync_init(&qtp_sync, qtp_sync_buffer, sizeof(qtp_sync_buffer), initial_values, ARRAY_LENGTH(initial_values),
	  qtp_sync_changed_callback, qtp_sync_error_callback, NULL);
	APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather app message initialized");

}

/* Handle incoming data from Javascript and update the view accordingly */
static void qtp_sync_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
	
	switch (key) {
		case QTP_WEATHER_TEMP_F_KEY:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather temp f received");
			if (qtp_is_showing && qtp_is_degrees_f()) {
				text_layer_set_text(qtp_temp_layer, new_tuple->value->cstring);
			}
			break;
		case QTP_WEATHER_TEMP_C_KEY:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather temp c received");
			if (qtp_is_showing && !qtp_is_degrees_f()) {
				text_layer_set_text(qtp_temp_layer, new_tuple->value->cstring);
			}
			break;
		case QTP_WEATHER_DESC_KEY:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather desc received: %s", new_tuple->value->cstring);
			if (qtp_is_showing) {
				text_layer_set_text(qtp_weather_desc_layer, new_tuple->value->cstring);
			}
			break;
		case QTP_WEATHER_ICON_KEY:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather icon received: %d", new_tuple->value->uint8);
			if (qtp_is_showing) {
				qtp_update_weather_icon(new_tuple->value->uint8, true, true);
			}
			break;

	}

}

/* Clear out the display on failure */
static void qtp_sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "QTP: weather app message error occurred: %d, %d", dict_error, app_message_error);
	if (DICT_NOT_ENOUGH_STORAGE == dict_error) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Not enough storage");
	}

	static char placeholder[] = "--\u00B0F";
	text_layer_set_text(qtp_temp_layer, placeholder);
}


/* Auto-hide the window after a certain time */
void qtp_timeout() {
	qtp_hide();
	qtp_is_showing = false;
}

/* Create the QTPlus Window and initialize the layres */
void qtp_init() {
	qtp_window = window_create();

	/* Time Layer */
	if (qtp_is_show_time()) {

		GRect time_frame = GRect( QTP_PADDING_X, QTP_PADDING_Y, QTP_SCREEN_WIDTH - QTP_PADDING_X, QTP_TIME_HEIGHT );
		qtp_time_layer = text_layer_create(time_frame);
		qtp_update_time(false);
		text_layer_set_text_alignment(qtp_time_layer, GTextAlignmentCenter);
		text_layer_set_font(qtp_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
		layer_add_child(window_get_root_layer(qtp_window), text_layer_get_layer(qtp_time_layer));
	}

	/* Setup weather if it is enabled */
	if (qtp_is_show_weather()) {

		/* Weather description layer */
		GRect desc_frame = GRect( QTP_PADDING_X + QTP_WEATHER_SIZE + 5, qtp_weather_y() + QTP_WEATHER_SIZE, QTP_SCREEN_WIDTH - QTP_PADDING_X, QTP_WEATHER_SIZE);
		qtp_weather_desc_layer = text_layer_create(desc_frame);
		text_layer_set_font(qtp_weather_desc_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
		text_layer_set_text_alignment(qtp_weather_desc_layer, GTextAlignmentLeft);
		const Tuple *desc_tuple = app_sync_get(&qtp_sync, QTP_WEATHER_DESC_KEY);
		if (desc_tuple != NULL) {
			text_layer_set_text(qtp_weather_desc_layer, desc_tuple->value->cstring);
		}
		layer_add_child(window_get_root_layer(qtp_window), text_layer_get_layer(qtp_weather_desc_layer));


		/* Temperature description layer */
		GRect temp_frame = GRect( QTP_PADDING_X + QTP_WEATHER_SIZE + 5, qtp_weather_y(), QTP_SCREEN_WIDTH, QTP_WEATHER_SIZE);
		qtp_temp_layer = text_layer_create(temp_frame);
		text_layer_set_text_alignment(qtp_temp_layer, GTextAlignmentLeft);
		const Tuple *temp_tuple;
		if (qtp_is_degrees_f()) {
			temp_tuple = app_sync_get(&qtp_sync, QTP_WEATHER_TEMP_F_KEY);
		} else {
			temp_tuple = app_sync_get(&qtp_sync, QTP_WEATHER_TEMP_C_KEY);
		}
		if (temp_tuple != NULL) {
			text_layer_set_text(qtp_temp_layer, temp_tuple->value->cstring);
		}
		text_layer_set_font(qtp_temp_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
		layer_add_child(window_get_root_layer(qtp_window), text_layer_get_layer(qtp_temp_layer));

		/* Weather icon layer */
		GRect weather_icon_frame = GRect( QTP_PADDING_X, qtp_weather_y(), QTP_WEATHER_SIZE, QTP_WEATHER_SIZE );
		qtp_weather_icon_layer = bitmap_layer_create(weather_icon_frame);
		bitmap_layer_set_alignment(qtp_weather_icon_layer, GAlignCenter);
		const Tuple *icon_tuple = app_sync_get(&qtp_sync, QTP_WEATHER_ICON_KEY);
		qtp_update_weather_icon(icon_tuple->value->uint8, false, false);
		layer_add_child(window_get_root_layer(qtp_window), bitmap_layer_get_layer(qtp_weather_icon_layer)); 

	}

	/* Bluetooth Logo layer */
	GRect battery_logo_frame = GRect( QTP_PADDING_X, qtp_battery_y(), QTP_BAT_ICON_SIZE, QTP_BAT_ICON_SIZE );
	qtp_battery_image_layer = bitmap_layer_create(battery_logo_frame);
	bitmap_layer_set_bitmap(qtp_battery_image_layer, qtp_battery_image);
	bitmap_layer_set_alignment(qtp_battery_image_layer, GAlignCenter);
	layer_add_child(window_get_root_layer(qtp_window), bitmap_layer_get_layer(qtp_battery_image_layer)); 

	/* Battery Status text layer */
	GRect battery_frame = GRect( 40, qtp_battery_y(), QTP_SCREEN_WIDTH - QTP_BAT_ICON_SIZE, QTP_BAT_ICON_SIZE );
	qtp_battery_text_layer =  text_layer_create(battery_frame);
	text_layer_set_font(qtp_battery_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
	qtp_update_battery_status(false);
	layer_add_child(window_get_root_layer(qtp_window), text_layer_get_layer(qtp_battery_text_layer));

	/* Bluetooth Logo layer */
	GRect bluetooth_logo_frame = GRect(QTP_PADDING_X, qtp_bluetooth_y(), QTP_BT_ICON_SIZE, QTP_BT_ICON_SIZE);
	qtp_bluetooth_image_layer = bitmap_layer_create(bluetooth_logo_frame);
	bitmap_layer_set_bitmap(qtp_bluetooth_image_layer, qtp_bluetooth_image);
	bitmap_layer_set_alignment(qtp_bluetooth_image_layer, GAlignCenter);
	layer_add_child(window_get_root_layer(qtp_window), bitmap_layer_get_layer(qtp_bluetooth_image_layer)); 


	/* Bluetooth Status text layer */
	GRect bluetooth_frame = GRect(40,qtp_bluetooth_y(), QTP_SCREEN_WIDTH - QTP_BT_ICON_SIZE, QTP_BT_ICON_SIZE);
	qtp_bluetooth_text_layer =  text_layer_create(bluetooth_frame);
	text_layer_set_font(qtp_bluetooth_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
	qtp_update_bluetooth_status(false);
	layer_add_child(window_get_root_layer(qtp_window), text_layer_get_layer(qtp_bluetooth_text_layer));

	/* Invert the screen */
	if (qtp_is_invert()) {
		GRect inverter_frame = GRect(0,0, QTP_SCREEN_WIDTH, QTP_SCREEN_HEIGHT);
		qtp_inverter_layer = inverter_layer_create(inverter_frame);
		layer_add_child(window_get_root_layer(qtp_window), inverter_layer_get_layer(qtp_inverter_layer));
	}

	/* Register for back button */
	window_set_click_config_provider(qtp_window, (ClickConfigProvider)qtp_click_config_provider);

}


/* Deallocate QTPlus items when window is hidden */
void qtp_deinit() {
	text_layer_destroy(qtp_battery_text_layer);
	text_layer_destroy(qtp_bluetooth_text_layer);
	bitmap_layer_destroy(qtp_bluetooth_image_layer);
	bitmap_layer_destroy(qtp_battery_image_layer);
	if (qtp_is_show_time()) {
		text_layer_destroy(qtp_time_layer);
	}
	if (qtp_is_show_weather()) {
		text_layer_destroy(qtp_temp_layer);
		text_layer_destroy(qtp_weather_desc_layer);
		bitmap_layer_destroy(qtp_weather_icon_layer);
		gbitmap_destroy(qtp_weather_icon);
	}
	if (qtp_is_invert()) {
		inverter_layer_destroy(qtp_inverter_layer);
	}
	window_destroy(qtp_window);
	if (qtp_is_autohide()) {
		app_timer_cancel(qtp_hide_timer);
	}
}

/* Deallocate persistent QTPlus items when watchface exits */
void qtp_app_deinit() {
	gbitmap_destroy(qtp_battery_image);
	gbitmap_destroy(qtp_bluetooth_image);
	app_sync_deinit(&qtp_sync);

}

/* Create window, layers, text. Display QTPlus */
void qtp_show() {
	qtp_init();
	window_stack_push(qtp_window, true);
	if (qtp_is_autohide()) {
		qtp_hide_timer = app_timer_register(QTP_WINDOW_TIMEOUT, qtp_timeout, NULL);
	}
}

/* Hide QTPlus. Free memory */
void qtp_hide() {
	window_stack_pop(true);
	qtp_deinit();
}


bool qtp_is_show_time() {
	return (qtp_conf & QTP_K_SHOW_TIME) == QTP_K_SHOW_TIME;
}
bool qtp_is_show_weather() {
	return (qtp_conf & QTP_K_SHOW_WEATHER) == QTP_K_SHOW_WEATHER;
}
bool qtp_is_autohide() {
	return (qtp_conf & QTP_K_AUTOHIDE) == QTP_K_AUTOHIDE;
}
bool qtp_is_degrees_f() {
	return (qtp_conf & QTP_K_DEGREES_F) == QTP_K_DEGREES_F;
}

bool qtp_is_invert() {
	return (qtp_conf & QTP_K_INVERT) == QTP_K_INVERT;
}

int qtp_battery_y() {
	if (qtp_is_show_time()) {
		return QTP_BATTERY_BASE_Y + QTP_TIME_HEIGHT + QTP_PADDING_Y;
	} else {
		return QTP_BATTERY_BASE_Y + QTP_PADDING_Y;
	}
}

int qtp_bluetooth_y() {
	if (qtp_is_show_time()) {
		return QTP_BLUETOOTH_BASE_Y + QTP_TIME_HEIGHT + QTP_PADDING_Y;
	} else {
		return QTP_BLUETOOTH_BASE_Y + QTP_PADDING_Y;
	}
}

int qtp_weather_y() {
	if (qtp_is_show_time()) {
		return QTP_WEATHER_BASE_Y + QTP_PADDING_Y + QTP_TIME_HEIGHT;

	} else {
		return QTP_WEATHER_BASE_Y + QTP_PADDING_Y;
	}
}

/*
 * Main - or main as it is known
 */
int main(void) {
	init();
	tick_timer_service_subscribe(SECOND_UNIT, &handle_tick);
	bluetooth_connection_service_subscribe(&bt_connection_handler);
	battery_state_service_subscribe	(&battery_state_handler);
	app_event_loop();
	deinit();
}

