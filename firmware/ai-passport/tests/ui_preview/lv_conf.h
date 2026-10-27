#pragma once
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#ifdef PREVIEW_DEVICE_HEAP
#ifndef __ASSEMBLER__
#include <stdlib.h>
#endif
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
/* 主机指针宽度为设备的两倍，按指针宽度缩放受限堆。真机仍使用 24 KiB。 */
#define LV_MEM_SIZE (24 * 1024U * sizeof(void *) / 4)
#define LV_USE_ASSERT_MALLOC 1
#define LV_ASSERT_HANDLER abort();
#else
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#endif
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_QRCODE 1
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
#define LV_USE_THORVG_INTERNAL 0

#define LV_USE_FONT_COMPRESSED 1
