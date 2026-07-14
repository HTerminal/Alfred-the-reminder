#ifndef REMINDER_IMAGES_H
#define REMINDER_IMAGES_H

#ifdef __has_include
  #if __has_include("lvgl.h")
    #ifndef LV_LVGL_H_INCLUDE_SIMPLE
      #define LV_LVGL_H_INCLUDE_SIMPLE
    #endif
  #endif
#endif
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
  #include "lvgl.h"
#else
  #include "lvgl/lvgl.h"
#endif

LV_IMG_DECLARE(img_almonds_eat);
LV_IMG_DECLARE(img_almonds_eat_h);
LV_IMG_DECLARE(img_almonds_soak);
LV_IMG_DECLARE(img_almonds_soak_h);
LV_IMG_DECLARE(img_milk);
LV_IMG_DECLARE(img_milk_h);
LV_IMG_DECLARE(img_breakfast);
LV_IMG_DECLARE(img_breakfast_h);
LV_IMG_DECLARE(img_salad);
LV_IMG_DECLARE(img_salad_h);
LV_IMG_DECLARE(img_lunch);
LV_IMG_DECLARE(img_lunch_h);
LV_IMG_DECLARE(img_chaat);
LV_IMG_DECLARE(img_chaat_h);
LV_IMG_DECLARE(img_coconut);
LV_IMG_DECLARE(img_coconut_h);
LV_IMG_DECLARE(img_dinner);
LV_IMG_DECLARE(img_dinner_h);
LV_IMG_DECLARE(img_doorbell);
LV_IMG_DECLARE(img_doorbell_h);

#endif
