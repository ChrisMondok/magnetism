#include <pebble.h>

static Window *s_window;
static Layer *s_graphics_layer;
static uint32_t s_minute_angle = 0;
static uint32_t s_hour_angle = 0;

static uint32_t s_inset_size = 32;

// "width" of "lump". A value of 1 means that half of the watch face is lumped
static uint8_t s_inv_lump_size = 6;
static uint32_t s_lump_height = PBL_DISPLAY_WIDTH/4;

static uint32_t s_outer_lump_height = 16; // must be less than s_inset_size

typedef struct AppSettings {
  GColor color_bg;
  GColor color_hour;
  GColor color_minute;
  uint8_t width_hour;
  uint8_t width_minute;
} AppSettings;

static AppSettings s_settings = {
  GColorBlack,
  PBL_IF_BW_ELSE(GColorWhite, GColorRed),
  GColorWhite,
  3,
  1
};

typedef struct LineSegment {
  GPoint p0;
  GPoint p1;
} LineSegment;


#ifdef PBL_RECT
GPoint get_rect_intersection(GRect rect, uint32_t angle) {
  GPoint center = grect_center_point(&rect);
  
  // NOTE: we're swapping sin and cos here to get "angle is clockwise from top"
  // instead of "angle is counter-clockwise from right". This is also why we
  // must negate intersect_y.
  int32_t sin_ratio = sin_lookup(angle);
  int32_t cos_ratio = cos_lookup(angle);
  
  int32_t dist_x, dist_y;
  
  if (sin_ratio != 0) {
    dist_x = (rect.size.w / 2 * TRIG_MAX_RATIO) / sin_ratio;
    if(sin_ratio < 1) dist_x *= -1;
  } else {
    dist_x = INT32_MAX;
  }

  if (cos_ratio != 0) {
    dist_y = (rect.size.h / 2 * TRIG_MAX_RATIO) / cos_ratio;
    if(cos_ratio < 1) dist_y *= -1;
  } else {
    dist_y = INT32_MAX;
  }

  int32_t length = (dist_x < dist_y) ? dist_x : dist_y;
  
  int32_t intersect_x = center.x + (length * sin_ratio) / TRIG_MAX_RATIO;
  int32_t intersect_y = center.y - (length * cos_ratio) / TRIG_MAX_RATIO;
  
  return GPoint(intersect_x, intersect_y);
}
#endif

static LineSegment get_minute_segment(GRect rect, uint8_t minute) {
  uint32_t angle = minute * TRIG_MAX_ANGLE / 60;

  int16_t minute_angle_delta = s_minute_angle - angle;
  int16_t hour_angle_delta = s_hour_angle - angle;

//  if( abs(hour_angle_delta) < TRIG_MAX_ANGLE / (s_inv_cluster_size * 4)) {
//    int32_t cluster_str = cos_lookup(hour_angle_delta * s_inv_cluster_size);
//    angle += hour_angle_delta * cluster_str / TRIG_MAX_RATIO;
//  }

  int32_t inset = s_inset_size;

  if(abs(minute_angle_delta) < TRIG_MAX_ANGLE / (s_inv_lump_size * 4)) {
    inset += s_lump_height * cos_lookup(minute_angle_delta * s_inv_lump_size) / TRIG_MAX_RATIO;
  }

  int32_t outer_inset = 0;

  if(abs(hour_angle_delta) < TRIG_MAX_ANGLE / (s_inv_lump_size * 4)) {
    outer_inset += s_outer_lump_height * cos_lookup(hour_angle_delta * s_inv_lump_size) / TRIG_MAX_RATIO;
  }

#ifdef PBL_RECT
  LineSegment seg = {
    get_rect_intersection(grect_crop(rect, inset), angle),
    get_rect_intersection(grect_crop(rect, outer_inset), angle)
  };
#else
  GPoint center = grect_center_point(&rect);

  uint16_t outer_radius = (rect.size.w > rect.size.h ? rect.size.w : rect.size.h)/2;
  uint16_t inner_radius = outer_radius - inset;
  outer_radius -= outer_inset;

  LineSegment seg = {
    GPoint(center.x + sin_lookup(angle) * inner_radius / TRIG_MAX_RATIO, center.y - cos_lookup(angle) * inner_radius / TRIG_MAX_RATIO),
    GPoint(center.x + sin_lookup(angle) * outer_radius / TRIG_MAX_RATIO, center.y - cos_lookup(angle) * outer_radius / TRIG_MAX_RATIO),
  };
#endif

  return seg;
}

static void update_graphics_layer(Layer *graphics_layer, GContext *ctx) {
  GRect bounds = layer_get_unobstructed_bounds(graphics_layer);
  for(int32_t m = 0; m < 60; m++) {
    LineSegment segment = get_minute_segment(bounds, m);

    graphics_context_set_stroke_width(ctx, m % 5 ? s_settings.width_minute : s_settings.width_hour);
    graphics_context_set_stroke_color(ctx, m % 5 ? s_settings.color_minute : s_settings.color_hour);

    graphics_draw_line(ctx, segment.p0, segment.p1);
  }
}

uint32_t s_uint32_getter(void *subject) {
  uint32_t *val = (uint32_t *)subject;
  return *val;
}

void s_uint32_setter(void *subject, uint32_t uint32) {
  uint32_t *val = (uint32_t *)subject;
  *val = uint32;
  layer_mark_dirty(s_graphics_layer);
}

static const PropertyAnimationImplementation s_uint32_prop_anim_impl = {
  .base = {
    .update = (AnimationUpdateImplementation)property_animation_update_uint32,
  },
  .accessors = {
    .getter.uint32 = s_uint32_getter,
    .setter.uint32 = s_uint32_setter,
  },
};

Animation *create_uint32_animation(uint32_t *subject, uint32_t target) {
  uint32_t from = *subject;
  PropertyAnimation *prop_anim = property_animation_create(&s_uint32_prop_anim_impl, subject, &from, &target);
  property_animation_from(prop_anim, &from, sizeof(subject), true);
  property_animation_to(prop_anim, &target, sizeof(target), true);
  return property_animation_get_animation(prop_anim);
}

Animation *create_angle_animation(uint32_t *subject, uint32_t target) {
  uint32_t from = *subject;

  if(target > from && target - from > TRIG_MAX_ANGLE / 2)  {
    from += TRIG_MAX_ANGLE;
  } else if(from > target && from - target > TRIG_MAX_ANGLE / 2) {
    from -= TRIG_MAX_ANGLE;
  }

  return create_uint32_animation(subject, target);
}

static void prv_unobstructed_change(AnimationProgress progress, void *context) {
  layer_mark_dirty(s_graphics_layer);
}

static uint32_t get_minute_angle(struct tm *tick_time) {
  return tick_time->tm_min * TRIG_MAX_ANGLE / 60;
}

static uint32_t get_hour_angle(struct tm *tick_time) {
  return tick_time->tm_hour * TRIG_MAX_ANGLE / 12 + get_minute_angle(tick_time) / 12;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  Animation *anim1 = create_angle_animation(&s_minute_angle, get_minute_angle(tick_time));
  Animation *anim2 = create_angle_animation(&s_hour_angle, get_hour_angle(tick_time));
  Animation *spawn = animation_spawn_create(anim1, anim2, NULL);
  animation_schedule(spawn);
}

static Animation *create_intro_animation() {
  uint32_t target = s_inset_size;
  s_inset_size = 0;
  Animation *inset_anim = create_uint32_animation(&s_inset_size, target);
  animation_set_duration(inset_anim, 500);
  animation_set_curve(inset_anim, AnimationCurveEaseOut);

  target = s_lump_height;
  s_lump_height = 0;
  Animation *lump_anim = create_uint32_animation(&s_lump_height, target);
  animation_set_duration(lump_anim, 500);
  animation_set_curve(lump_anim, AnimationCurveEaseInOut);

  target = s_outer_lump_height;
  s_outer_lump_height = 0;
  Animation *outer_lump_anim = create_uint32_animation(&s_outer_lump_height, target);
  animation_set_duration(outer_lump_anim, 500);
  animation_set_curve(outer_lump_anim, AnimationCurveEaseInOut);


  return animation_sequence_create(
      inset_anim,
      animation_spawn_create(
        lump_anim,
        outer_lump_anim,
        NULL
        ),
      NULL
      );

  return inset_anim;
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_graphics_layer = layer_create(bounds);
  layer_set_update_proc(s_graphics_layer, update_graphics_layer);
  layer_add_child(window_layer, s_graphics_layer);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  UnobstructedAreaHandlers handlers = {
    .change = prv_unobstructed_change,
  };
  unobstructed_area_service_subscribe(handlers, NULL);

  animation_schedule(create_intro_animation());
}

static void window_unload(Window *window) {
  layer_destroy(s_graphics_layer);
}

static void on_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple* tuple = dict_read_first(iter);
  while(tuple) {
    if(tuple->key == MESSAGE_KEY_COLOR_BG) {
      s_settings.color_bg = GColorFromHEX(tuple->value->int32);
      window_set_background_color(s_window, s_settings.color_bg);
    } else if(tuple->key == MESSAGE_KEY_COLOR_MINUTE) {
      s_settings.color_minute = GColorFromHEX(tuple->value->int32);
    } else if(tuple->key == MESSAGE_KEY_COLOR_HOUR) {
      s_settings.color_hour = GColorFromHEX(tuple->value->int32);
    } else if(tuple->key == MESSAGE_KEY_STROKE_WIDTH_MINUTE) {
      s_settings.width_minute = tuple->value->uint8;
      APP_LOG(APP_LOG_LEVEL_DEBUG, "hour width is %d", tuple->value->uint8);
    } else if(tuple->key == MESSAGE_KEY_STROKE_WIDTH_HOUR) {
      s_settings.width_hour = tuple->value->uint8;
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Received unknown key %d", tuple->key);
    }
    tuple = dict_read_next(iter);
  }
  layer_mark_dirty(s_graphics_layer);
  persist_write_data(0, &s_settings, sizeof(AppSettings));
}

static void on_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", reason);
}

int main(void) {
  time_t temp = time(NULL);
  struct tm *current_time = localtime(&temp);
  s_minute_angle = get_minute_angle(current_time);
  s_hour_angle = get_hour_angle(current_time);
  
  if(persist_exists(0)) {
    persist_read_data(0, &s_settings, sizeof(AppSettings));
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(s_window, s_settings.color_bg);

  window_stack_push(s_window, /* animated */ false);
  
  app_message_register_inbox_received(on_inbox_received);
  app_message_register_inbox_dropped(on_inbox_dropped);
  app_message_open(128, 128);

  app_event_loop();

  window_destroy(s_window);
}
