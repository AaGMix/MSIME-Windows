#pragma once

#include <windows.h>

namespace mvi_utils
{
int GetTaskbarHeight();
// 语音条锚定所用的显示器：前台窗口所在显示器。GetMonitorCoordinates 与
// GetForegroundMonitorScale 都以它为准，保证定位矩形与缩放同源。
HMONITOR GetForegroundMonitor();
RECT GetMonitorCoordinates();
// 目标显示器的有效缩放（DPI/96）。与候选框 ScaleFromMonitor 一致地使用
// GetDpiForMonitor(MDT_EFFECTIVE_DPI)，而非 GetDpiForWindow，避免分辨率切换时
// 取到陈旧或错误显示器的 DPI 导致尺寸/缩放不对。
float GetForegroundMonitorScale();
} // namespace mvi_utils
