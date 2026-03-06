#pragma once

// Hub75S3 — Template-driven HUB75 driver for ESP32-S3 via LCD_CAM
//
// Usage:
//   #include <Hub75S3/Hub75S3.h>
//
//   using MyDisplay = Hub75S3Display<
//       128, 64, 1, 1,
//       Hub75S3::MatrixPortalS3Pins,
//       Hub75S3::PixelFormat::RGB888,
//       Hub75S3::BufferMode::Double,
//       Hub75S3::MemoryPolicy::PsramFramebuf
//   >;

#include "Config.h"
#include "PinMap.h"
#include "Framebuffer.h"
#include "BcmPacker.h"
#include "LcdCamDriver.h"
#include "Gfx.h"
#include "Display.h"
