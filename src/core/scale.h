/*
 * The UI scale (uiScale, in percent), for high-resolution screens. Pixel
 * sizes in our own drawing and layout are written for 100% and go through
 * scaled(); fonts are scaled when they are loaded (x11/config.cpp). The
 * scale is set once at startup, before anything is created.
 */
#ifndef TTR_CORE_SCALE_H
#define TTR_CORE_SCALE_H

namespace ttr {

static const int k_min_scale = 100;
static const int k_max_scale = 300;

void set_ui_scale(int percent);
int ui_scale();

/* 'px' pixels at 100%, at the current scale, rounded. Never 0 unless px
 * is 0, so one pixel lines stay visible. */
int scaled(int px);

/* The other way: 'px' pixels at the current scale as pixels at 100%, for
 * sizes saved in settings that should mean the same at any scale. */
int unscaled(int px);

/* A line or bevel width: 1 pixel at 100%, growing slowly with the scale
 * (2 at 200%) so lines do not get heavy. */
int scaled_line(int px);

}

#endif
