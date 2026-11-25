#pragma once

// The remapper speaks in Linux input event codes on every platform: they
// name the Xbox 360 pad's buttons and axes. On Windows, where the kernel
// header is missing, the few codes the shared code uses are defined here
// with the kernel's own values.

#ifdef _WIN32

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define SYN_REPORT 0

#define BTN_A 0x130
#define BTN_B 0x131
#define BTN_X 0x133
#define BTN_Y 0x134
#define BTN_TL 0x136
#define BTN_TR 0x137
#define BTN_SELECT 0x13a
#define BTN_START 0x13b
#define BTN_MODE 0x13c
#define BTN_THUMBL 0x13d
#define BTN_THUMBR 0x13e

#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_Z 0x02
#define ABS_RX 0x03
#define ABS_RY 0x04
#define ABS_RZ 0x05
#define ABS_HAT0X 0x10
#define ABS_HAT0Y 0x11

#else

#include <linux/input.h>

#endif
