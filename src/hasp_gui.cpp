#include "Ticker.h"
#include "ArduinoLog.h"

#include "lv_conf.h"
#include "lvgl.h"
#include "lv_fs_if.h"
#include "TFT_eSPI.h"
//#include "lv_zifont.h"

#include "hasp_tft.h"
#include "hasp_debug.h"
#include "hasp_config.h"
#include "hasp_dispatch.h"
#include "hasp_gui.h"
#include "hasp_oobe.h"
#include "hasp.h"

//#include "lv_ex_conf.h"
//#include "tpcal.h"

#if HASP_USE_PNGDECODE
#include "png_decoder.h"
#endif

#ifndef TOUCH_DRIVER
#define TOUCH_DRIVER 0
#endif

#if HASP_USE_SPIFFS
#if defined(ARDUINO_ARCH_ESP32)
#include "SPIFFS.h"
#endif
#include <FS.h> // Include the SPIFFS library
#endif

#define BACKLIGHT_CHANNEL 15 // pwm channek 0-15

/* ---------- Screenshot Variables ---------- */
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
File pFileOut;
#endif
uint8_t guiSnapshot = 0;

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WebServer.h>
ESP8266WebServer * webClient; // for snatshot
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <WebServer.h>
WebServer * webClient; // for snatshot
#endif                 // ESP32
/* ------------------------------------------- */

// #define LVGL_TICK_PERIOD 30

#ifndef TFT_BCKL
#define TFT_BCKL -1 // No Backlight Control
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 0
#endif

static void IRAM_ATTR lv_tick_handler(void);

static bool guiShowPointer    = false;
static bool guiBacklightIsOn  = true;
static int8_t guiDimLevel     = -1;
static int8_t guiBacklightPin = TFT_BCKL;
static uint16_t guiSleepTime1 = 60;  // 1 second resolution
static uint16_t guiSleepTime2 = 120; // 1 second resolution
static uint8_t guiSleeping    = 0;   // 0 = off, 1 = short, 2 = long
static uint8_t guiTickPeriod  = 20;
static uint8_t guiRotation    = TFT_ROTATION;
#if ESP32 > 0 || ESP8266 > 0
static Ticker tick; /* timer for interrupt handler */
#else
static Ticker tick(lv_tick_handler,guiTickPeriod);
uint8_t serialInputIndex   = 0;    // Empty buffer
char serialInputBuffer[1024];

#endif
static TFT_eSPI tft; // = TFT_eSPI(); /* TFT instance */
static uint16_t calData[5] = {0, 65535, 0, 65535, 0};

static bool guiCheckSleep()
{
    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if(idle >= (guiSleepTime1 + guiSleepTime2) * 1000U) {
        if(guiSleeping != 2) {
            dispatchIdle(("LONG")); // Literal string
            guiSleeping = 2;
        }
        return true;
    } else if(idle >= guiSleepTime1 * 1000U) {
        if(guiSleeping != 1) {
            dispatchIdle(("SHORT")); // Literal string
            guiSleeping = 1;
        }
        return true;
    }
    if(guiSleeping != 0) {
        dispatchIdle(("OFF")); // Literal string
        guiSleeping = 0;
    }
    return false;
}

// static void gui_take_screenshot(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p)
// {
//     uint i = 0;
//     uint16_t c;
//     uint8_t pixel[1024];

//     for(int y = area->y1; y <= area->y2; y++) {
//         for(int x = area->x1; x <= area->x2; x++) {
//             /* Function for converting LittlevGL pixel format to RGB888 */
//             // data = DISP_IMPL_lvgl_formatPixel(*color_p);

//             // Complex 32 bpp
//             /* pixel[i++] = (LV_COLOR_GET_B(*color_p) * 263 + 7) >> 5;
//              pixel[i++] = (LV_COLOR_GET_G(*color_p) * 259 + 3) >> 6;
//              pixel[i++] = (LV_COLOR_GET_R(*color_p) * 263 + 7) >> 5;
//              pixel[i++] = 0xFF;*/

//             // Simple 32 bpp
//             // pixel[i++] = (LV_COLOR_GET_B(*color_p) << 3);
//             // pixel[i++] = (LV_COLOR_GET_G(*color_p) << 2);
//             // pixel[i++] = (LV_COLOR_GET_R(*color_p) << 3);
//             // pixel[i++] = 0xFF;

//             c = color_p->full;

//             // Simple 16 bpp
//             pixel[i++] = c & 0xFF;
//             pixel[i++] = (c >> 8) & 0xFF;

//             color_p++;

//             if(i + 4 >= sizeof(pixel)) {
//                 switch(guiSnapshot) {
//                     case 1:
//                         // Save to local file
//                         pFileOut.write(pixel, i);
//                         break;
//                     case 2:
//                         // Send to remote client
//                         if(webClient->client().write(pixel, i) != i) {
//                             Log.warning(F("GUI: Pixelbuffer not completely sent"));
//                             lv_disp_flush_ready(disp); /* tell lvgl that flushing is done */
//                             return;
//                         }
//                 }
//                 i = 0;
//             }
//         }
//     }

//     if(i > 0) {
//         switch(guiSnapshot) {
//             case 1:
//                 // Save to local file
//                 pFileOut.write(pixel, i);
//                 break;
//             case 2:
//                 // Send to remote client
//                 if(webClient->client().write(pixel, i) != i) {
//                     Log.warning(F("GUI: Pixelbuffer not completely sent"));
//                 }
//         }
//     }
// }

/* Flush VDB bytes to a stream */
static void gui_take_screenshot(uint8_t * data_p, size_t len)
{
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
    size_t res = 0;
    switch(guiSnapshot) {
        case 1:
            res = pFileOut.write(data_p, len);
            break;
        case 2:
            res = webClient->client().write(data_p, len);
            break;
        default:
            res = 0; // nothing to do
    }
    if(res != len) {
        Log.warning(F("GUI: Pixelbuffer not completely sent"));
    }
#endif
}

/* Experimetnal Display flushing */
static void IRAM_ATTR tft_espi_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p)
{
    size_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1); /* Number of pixels */

    /* Update TFT */
    tft.startWrite();                                      /* Start new TFT transaction */
    tft.setWindow(area->x1, area->y1, area->x2, area->y2); /* set the working window */
    tft.setSwapBytes(true);                                /* set endianess */
    tft.pushPixels((uint16_t *)color_p, len);              /* Write words at once */
    tft.endWrite();                                        /* terminate TFT transaction */

    /* Send Screenshot data */
    if(guiSnapshot != 0) {
        gui_take_screenshot((uint8_t *)color_p, len * sizeof(lv_color_t)); /* Number of bytes */
    }

    /* Tell lvgl that flushing is done */
    lv_disp_flush_ready(disp);
}

/* Display flushing */
/*
void tft_espi_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p)
{
    uint16_t c;

    tft.startWrite(); // Start new TFT transaction
    tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1),
                      (area->y2 - area->y1 + 1)); // set the working window

    if(guiSnapshot != 0) {
        uint i = 0;
        uint8_t pixel[1024];

        for(int y = area->y1; y <= area->y2; y++) {
            for(int x = area->x1; x <= area->x2; x++) {
                // Function for converting LittlevGL pixel format to RGB888
                // data = DISP_IMPL_lvgl_formatPixel(*color_p);

                // Complex 32 bpp
                // pixel[i++] = (LV_COLOR_GET_B(*color_p) * 263 + 7) >> 5;
                // pixel[i++] = (LV_COLOR_GET_G(*color_p) * 259 + 3) >> 6;
                // pixel[i++] = (LV_COLOR_GET_R(*color_p) * 263 + 7) >> 5;
                // pixel[i++] = 0xFF;

                // Simple 32 bpp
                // pixel[i++] = (LV_COLOR_GET_B(*color_p) << 3);
                // pixel[i++] = (LV_COLOR_GET_G(*color_p) << 2);
                // pixel[i++] = (LV_COLOR_GET_R(*color_p) << 3);
                // pixel[i++] = 0xFF;

                c = color_p->full;
                tft.writeColor(c, 1); // also update tft

                // Simple 16 bpp
                pixel[i++] = c & 0xFF;
                pixel[i++] = (c >> 8) & 0xFF;

                color_p++;

                if(i + 4 >= sizeof(pixel)) {
                    switch(guiSnapshot) {
                        case 1:
                            // Save to local file
                            pFileOut.write(pixel, i);
                            break;
                        case 2:
                            // Send to remote client
                            if(webClient->client().write(pixel, i) != i) {
                                Log.warning(F("GUI: Pixelbuffer not completely sent"));
                                lv_disp_flush_ready(disp); // tell lvgl that flushing is done
                                return;
                            }
                    }
                    i = 0;
                }
            }
        }

        if(i > 0) {
            switch(guiSnapshot) {
                case 1:
                    // Save to local file
                    pFileOut.write(pixel, i);
                    break;
                case 2:
                    // Send to remote client
                    if(webClient->client().write(pixel, i) != i) {
                        Log.warning(F("GUI: Pixelbuffer not completely sent"));
                    }
            }
        }
    } else {
        for(int y = area->y1; y <= area->y2; y++) {
            for(int x = area->x1; x <= area->x2; x++) {
                c = color_p->full;
                tft.writeColor(c, 1);
                color_p++;
            }
        }
    }
    tft.endWrite(); // terminate TFT transaction

    lv_disp_flush_ready(disp); // tell lvgl that flushing is done
} */

/* Interrupt driven periodic handler */
static void IRAM_ATTR lv_tick_handler(void)
{
    // Serial.print(".");
    lv_tick_inc(guiTickPeriod);
}

#ifdef STM32_CORE_VERSION
void Update_IT_callback(void)
{
     Serial.print("?");
   lv_tick_inc(guiTickPeriod);
}
#endif

/* Reading input device (simulated encoder here) */
/*bool read_encoder(lv_indev_drv_t * indev, lv_indev_data_t * data)
{
    static int32_t last_diff = 0;
    int32_t diff             = 0;                  // Dummy - no movement
    int btn_state            = LV_INDEV_STATE_REL; // Dummy - no press

    data->enc_diff = diff - last_diff;
    data->state    = btn_state;
    last_diff      = diff;
    return false;
}*/

// #define _RAWERR 20 // Deadband error allowed in successive position samples
// uint8_t validTouch(uint16_t * x, uint16_t * y, uint16_t threshold)
// {
//     uint16_t x_tmp, y_tmp, x_tmp2, y_tmp2;

//     // Wait until pressure stops increasing to debounce pressure
//     uint16_t z1 = 1;
//     uint16_t z2 = 0;
//     while(z1 > z2) {
//         z2 = z1;
//         z1 = tft.getTouchRawZ();
//         delay(1);
//     }

//     //  Serial.print("Z = ");Serial.println(z1);

//     if(z1 <= threshold) return false;

//     tft.getTouchRaw(&x_tmp, &y_tmp);

//     //  Serial.print("Sample 1 x,y = "); Serial.print(x_tmp);Serial.print(",");Serial.print(y_tmp);
//     //  Serial.print(", Z = ");Serial.println(z1);

//     delay(1); // Small delay to the next sample
//     if(tft.getTouchRawZ() <= threshold) return false;

//     delay(2); // Small delay to the next sample
//     tft.getTouchRaw(&x_tmp2, &y_tmp2);

//     //  Serial.print("Sample 2 x,y = "); Serial.print(x_tmp2);Serial.print(",");Serial.println(y_tmp2);
//     //  Serial.print("Sample difference = ");Serial.print(abs(x_tmp -
//     //  x_tmp2));Serial.print(",");Serial.println(abs(y_tmp - y_tmp2));

//     if(abs(x_tmp - x_tmp2) > _RAWERR) return false;
//     if(abs(y_tmp - y_tmp2) > _RAWERR) return false;

//     *x = x_tmp;
//     *y = y_tmp;

//     return true;
// }

// bool my_touchpad_read_raw(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
// {
// #ifdef TOUCH_CS
//     uint16_t touchX, touchY;

//     bool touched = validTouch(&touchX, &touchY, 600u / 2);
//     if(!touched) return false;

//     // if(touchCounter < 255) {
//     //     touchCounter++;

//     //     // Store the raw touches
//     //     if(touchCounter >= 8) {
//     //         touchPoints[touchCorner].x /= touchCounter;
//     //         touchPoints[touchCorner].y /= touchCounter;
//     //         touchCounter = 255;
//     //     } else {
//     //         touchPoints[touchCorner].x += touchX;
//     //         touchPoints[touchCorner].y += touchY;
//     //     }
//     // }

//     if(guiSleeping > 0) guiCheckSleep(); // update Idle

//     /*Save the state and save the pressed coordinate*/
//     // lv_disp_t * disp = lv_disp_get_default();
//     data->state   = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
//     data->point.x = touchX; // 20 + (disp->driver.hor_res - 40) * (touchCorner % 2);
//     data->point.y = touchY; // 20 + (disp->driver.ver_res - 40) * (touchCorner / 2);

//     Log.trace(F("Calibrate touch %u / %u"), touchX, touchY);

// #endif

//     return false; /*Return `false` because we are not buffering and no more data to read*/
// }

#if TOUCH_DRIVER == 2
#include "Touchscreen.h" // For Uno Shield or ADC based resistive touchscreens

boolean Touch_getXY(uint16_t * x, uint16_t * y, boolean showTouch)
{
    static const int coords[] = {3800, 500, 300, 3800}; // portrait - left, right, top, bottom
    static const int XP = 27, XM = 15, YP = 4, YM = 14; // default ESP32 Uno touchscreen pins
    static TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
    TSPoint p             = ts.getPoint();
    int z1                = analogRead(aXM);
    int z2                = analogRead(aYP);
    Serial.print(p.x);
    Serial.print(" - ");
    Serial.print(p.y);
    Serial.print(" - ");
    Serial.print(p.z);
    Serial.print(" - ");
    Serial.print(z1);
    Serial.print(" - ");
    Serial.println(z2);

    pinMode(aYP, OUTPUT); // restore shared pins
    pinMode(aXM, OUTPUT);
    digitalWrite(aYP, HIGH); // because TFT control pins
    digitalWrite(aXM, HIGH);
    // adjust pressure sensitivity - note works 'backwards'
#define MINPRESSURE 200
#define MAXPRESSURE 1000
    bool pressed = (p.z > MINPRESSURE && p.z < MAXPRESSURE);
    if(pressed) {

        switch(guiRotation) {
            case 0: // portrait
                *x = map(p.x, coords[0], coords[1], 0, tft.width());
                *y = map(p.y, coords[2], coords[3], 0, tft.height());
                break;
            case 1: // landscape
                *x = map(p.y, coords[1], coords[0], 0, tft.width());
                *y = map(p.x, coords[2], coords[3], 0, tft.height());
                break;
            case 2: // portrait inverted
                *x = map(p.x, coords[1], coords[0], 0, tft.width());
                *y = map(p.y, coords[3], coords[2], 0, tft.height());
                break;
            case 3: // landscape inverted
                *x = map(p.y, coords[0], coords[1], 0, tft.width());
                *y = map(p.x, coords[3], coords[2], 0, tft.height());
                break;
        }
        // if(showTouch) tft.fillCircle(*x, *y, 2, YELLOW);
    }
    return pressed;
}
#endif

#if TOUCH_DRIVER == 1

#include <Wire.h>
#include "Goodix.h"
#define INT_PIN (TOUCH_IRQ)
#define RST_PIN (TOUCH_RST) // -1 if pin is connected to VCC else set pin number

static Goodix touch = Goodix();
static int8_t GT911_num_touches;
static GTPoint * GT911_points;

void handleTouch(int8_t contacts, GTPoint * points)
{
    GT911_num_touches = contacts;
    GT911_points      = points;

    Log.trace("Contacts: %d", contacts);
    for(uint8_t i = 0; i < contacts; i++) {
        Log.trace("C%d: #%d %d,%d s:%d", i, points[i].trackId, points[i].x, points[i].y, points[i].area);
        yield();
    }
}

bool IRAM_ATTR GT911_getXY(uint16_t * touchX, uint16_t * touchY, bool debug)
{
    static GTPoint points[5];
    int16_t contacts = touch.readInput((uint8_t *)&points);
    if(contacts <= 0) return false;

    if(debug) {
        Serial.print(contacts);
        Serial.print(" : ");
        Serial.print(points[0].x);
        Serial.print(" x ");
        Serial.println(points[0].y);
    }

    *touchX = points[0].x;
    *touchY = points[0].y;
    return true;

    // ALTERNATE REGISTER READ METHOD
    // static uint8_t touchBuffer[6];

    // uint16_t first = 0x814E; // 8150
    // uint16_t last  = 0x8153;
    // uint16_t len   = first - last + 1;
    // uint8_t res = touch.read(first, touchBuffer, len);

    // if(res != GOODIX_OK || touchBuffer[0] - 128 == 0) return false;

    // *touchX = touchBuffer[2] + touchBuffer[3] * 256;
    // *touchY = touchBuffer[4] + touchBuffer[5] * 256;

    // if (debug) {
    //     Serial.print(touchBuffer[0] - 128);
    //     Serial.print(" : ");
    //     Serial.print(*touchX);
    //     Serial.print(" x ");
    //     Serial.println(*touchY);
    // }
    // return true;
}

void touchStart()
{
    if(touch.begin(INT_PIN, RST_PIN) != true) {
        Serial.println("! Module reset failed");
    } else {
        Serial.println("Module reset OK");
    }

    Serial.print("Check ACK on addr request on 0x");
    Serial.print(touch.i2cAddr, HEX);

    Wire.beginTransmission(touch.i2cAddr);
    int error = Wire.endTransmission();
    if(error == 0) {
        Serial.println(": SUCCESS");
    } else {
        Serial.print(": ERROR #");
        Serial.println(error);
    }
}

void GT911_setup()
{
    Wire.setClock(400000);
    Wire.begin();
    delay(300);

    touch.setHandler(handleTouch);
    touchStart();
    Log.verbose(F("Goodix GT911x touch driver started"));
}
#endif

bool IRAM_ATTR my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
    //#ifdef TOUCH_CS
    uint16_t touchX, touchY;

#if TOUCH_DRIVER == 0
    bool touched = tft.getTouch(&touchX, &touchY, 600);
#elif TOUCH_DRIVER == 1
    // return false;
    bool touched = GT911_getXY(&touchX, &touchY, true);
#else
    bool touched = Touch_getXY(&touchX, &touchY, false);
#endif

    if(!touched) return false;

    if(guiSleeping > 0) guiCheckSleep(); // update Idle

    // Ignore first press?

    if(touchX > tft.width() || touchY > tft.height()) {
        Serial.print(F("Y or y outside of expected parameters.. x: "));
        Serial.print(touchX);
        Serial.print(F("  / y: "));
        Serial.println(touchY);
    } else {
        /*Save the state and save the pressed coordinate*/
        data->state   = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        data->point.x = touchX;
        data->point.y = touchY;
        /* Serial.print("Data x");
           Serial.println(touchX);
           Serial.print("Data y");
           Serial.println(touchY);*/
    }
    //#endif

    return false; /*Return `false` because we are not buffering and no more data to read*/
}

void guiCalibrate()
{
#if TOUCH_DRIVER == 0
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println(PSTR("Touch corners as indicated"));

    tft.setTextFont(1);
    delay(500);
    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    for(uint8_t i = 0; i < 5; i++) {
        Serial.print(calData[i]);
        if(i < 4) Serial.print(", ");
    }

    tft.setTouch(calData);
    delay(500);
    lv_obj_invalidate(lv_disp_get_layer_sys(NULL));
#endif
}

void guiSetup()
{
    /* TFT init */
    tft.begin();
    tft.setRotation(guiRotation); /* 1/3=Landscape or 0/2=Portrait orientation */
#if TOUCH_DRIVER == 0
    tft.setTouch(calData);
#endif

    /* Initialize the Virtual Device Buffers */
    size_t guiVDBsize = 0;
    lv_init();

#if defined(ARDUINO_ARCH_ESP32)
    /* allocate on iram (or psram ?) */
    guiVDBsize = 16 * 1024u; // 32 KBytes * 2
    static lv_disp_buf_t disp_buf;
    static lv_color_t * guiVdbBuffer1 =
        (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * guiVDBsize, MALLOC_CAP_8BIT);
    // static lv_color_t * guiVdbBuffer2 = (lv_color_t *)malloc(sizeof(lv_color_t) * guiVDBsize);
    // lv_disp_buf_init(&disp_buf, guiVdbBuffer1, guiVdbBuffer2, guiVDBsize);
    lv_disp_buf_init(&disp_buf, guiVdbBuffer1, NULL, guiVDBsize);
#elif defined(ARDUINO_ARCH_ESP8266)
    /* allocate on heap */
    static lv_disp_buf_t disp_buf;
    static lv_color_t guiVdbBuffer1[5 * 512u]; // 5 KBytes
    // static lv_color_t guiVdbBuffer2[3 * 1024u]; // 6 KBytes
    guiVDBsize = sizeof(guiVdbBuffer1) / sizeof(guiVdbBuffer1[0]);
    lv_disp_buf_init(&disp_buf, guiVdbBuffer1, NULL, guiVDBsize);
#else
    static lv_disp_buf_t disp_buf;
    static lv_color_t guiVdbBuffer1[16 * 512u]; // 16 KBytes
    static lv_color_t guiVdbBuffer2[16 * 512u]; // 16 KBytes
    guiVDBsize = sizeof(guiVdbBuffer1) / sizeof(guiVdbBuffer1[0]);
    lv_disp_buf_init(&disp_buf, guiVdbBuffer1, guiVdbBuffer2, guiVDBsize);
    //lv_disp_buf_init(&disp_buf, guiVdbBuffer1, NULL, guiVDBsize);
#endif

    /* Initialize PNG decoder */
#if HASP_USE_PNGDECODE != 0
    png_decoder_init();
#endif

    /* Initialize Filesystems */
#if LV_USE_FS_IF != 0
    lv_fs_init();    // lvgl File System
    lv_fs_if_init(); // auxilary file system drivers
#endif

    /* Dump TFT Cofiguration */
    tftSetup(tft);

    /* Load User Settings */
    // guiSetConfig(settings);
    /* Setup Backlight Control Pin */
    if(guiBacklightPin >= 0) {
        Log.verbose(F("LVGL: Backlight: Pin %d"), guiBacklightPin);

#if defined(ARDUINO_ARCH_ESP32)
        // pinMode(guiBacklightPin, OUTPUT);
        // configure LED PWM functionalitites
        ledcSetup(BACKLIGHT_CHANNEL, 20000, 10);
        // attach the channel to the GPIO to be controlled
        ledcAttachPin(guiBacklightPin, BACKLIGHT_CHANNEL);
#else
        pinMode(guiBacklightPin, OUTPUT);
#endif
    }

    Log.verbose(F("LVGL: Version  : %u.%u.%u %s"), LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                PSTR(LVGL_VERSION_INFO));
    Log.verbose(F("LVGL: Rotation : %d"), guiRotation);
#ifdef LV_MEM_SIZE
    Log.verbose(F("LVGL: MEM size : %d"), LV_MEM_SIZE);
#endif
    Log.verbose(F("LVGL: VFB size : %d"), (size_t)sizeof(lv_color_t) * guiVDBsize);

#if LV_USE_LOG != 0
    Log.verbose(F("LVGL: Registering lvgl logging handler"));
    lv_log_register_print_cb(debugLvgl); /* register print function for debugging */
#endif

    /* Initialize the display driver */
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = tft_espi_flush;
    disp_drv.buffer   = &disp_buf;
    if(guiRotation == 0 || guiRotation == 2 || guiRotation == 4 || guiRotation == 6) {
        /* 1/3=Landscape or 0/2=Portrait orientation */
        // Normal width & height
        disp_drv.hor_res = TFT_WIDTH;
        disp_drv.ver_res = TFT_HEIGHT;
    } else {
        // Swapped width & height
        disp_drv.hor_res = TFT_HEIGHT;
        disp_drv.ver_res = TFT_WIDTH;
    }
    lv_disp_drv_register(&disp_drv);

    /*Initialize the touch pad*/
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    // indev_drv.type = LV_INDEV_TYPE_ENCODER;
    // indev_drv.read_cb = read_encoder;
    indev_drv.type           = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb        = my_touchpad_read;
    lv_indev_t * mouse_indev = lv_indev_drv_register(&indev_drv);

    if(guiShowPointer) {
        lv_obj_t * label = lv_label_create(lv_layer_sys(), NULL);
        lv_label_set_text(label, "<");
        lv_indev_set_cursor(mouse_indev, label); // connect the object to the driver

        /*Set a cursor for the mouse*/
        // LV_IMG_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
        // lv_obj_t * cursor_obj =
        //     lv_img_create(lv_disp_get_scr_act(NULL), NULL); /*Create an image object for the cursor */
        // lv_img_set_src(cursor_obj, &mouse_cursor_icon);     /*Set the image source*/
        // lv_indev_set_cursor(mouse_indev, cursor_obj);       /*Connect the image  object to the driver*/
    }

    /*
        lv_obj_t * cursor = lv_obj_create(lv_layer_sys(), NULL); // show on every page
        lv_obj_set_size(cursor, 9, 9);
        static lv_style_t style_cursor;
        lv_style_copy(&style_cursor, &lv_style_pretty);
        style_cursor.body.radius     = LV_RADIUS_CIRCLE;
        style_cursor.body.main_color = LV_COLOR_RED;
        style_cursor.body.opa        = LV_OPA_COVER;
        lv_obj_set_style(cursor, &style_cursor);
        // lv_obj_set_click(cursor, false);
        lv_indev_set_cursor(mouse_indev, cursor); // connect the object to the driver
    */
    /* Initialize mouse pointer */
    /*// if(true) {
    debugPrintln(PSTR("LVGL: Initialize Cursor"));
    lv_obj_t * cursor;
    lv_obj_t * mouse_layer = lv_disp_get_layer_sys(NULL); // default display
    // cursor               = lv_obj_create(lv_scr_act(), NULL);
    cursor = lv_obj_create(mouse_layer, NULL); // show on every page
    lv_obj_set_size(cursor, 9, 9);
    static lv_style_t style_round;
    lv_style_copy(&style_round, &lv_style_plain);
    style_round.body.radius     = LV_RADIUS_CIRCLE;
    style_round.body.main_color = LV_COLOR_RED;
    style_round.body.opa        = LV_OPA_COVER;
    lv_obj_set_style(cursor, &style_round);
    lv_obj_set_click(cursor, false); // don't click on the cursor
    lv_indev_set_cursor(mouse_indev, cursor);
    // }*/

    /*Initialize the graphics library's tick*/
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    tick.attach_ms(guiTickPeriod, lv_tick_handler);
#else

/*
#if defined(TIM1)
    TIM_TypeDef * Instance = TIM1;
#else
    TIM_TypeDef * Instance = TIM2;
#endif
*/
    // Instantiate HardwareTimer object. Thanks to 'new' instanciation, HardwareTimer is not destructed when setup()
    // function is finished.
  /*  static HardwareTimer * MyTim = new HardwareTimer(Instance);
    MyTim->pause();
    MyTim->setPrescaleFactor(1);
    MyTim->setMode(0, TIMER_OUTPUT_COMPARE, NC);
    MyTim->setOverflow(1000 * guiTickPeriod, MICROSEC_FORMAT); //  MicroSec
    MyTim->setCount(0,MICROSEC_FORMAT);
    MyTim->refresh();
    MyTim->detachInterrupt();
    MyTim->attachInterrupt((void (*)(HardwareTimer *))lv_tick_handler);
    MyTim->detachInterrupt(0);
    MyTim->attachInterrupt(0,(void (*)(HardwareTimer *))lv_tick_handler);
    MyTim->resume();*/
    tick.start();
#endif

#if TOUCH_DRIVER == 1
    GT911_setup();
#endif
}

void IRAM_ATTR guiLoop()
{
#ifdef STM32_CORE_VERSION_MAJOR
    tick.update();

            while(Serial.available()) {
            char ch = Serial.read();
            Serial.print(ch);
            if (ch == 13 ||ch == 10) {
                serialInputBuffer[serialInputIndex] = 0;
                if (serialInputIndex>0) dispatchCommand(serialInputBuffer);
                serialInputIndex=0;
            }else{
                if(serialInputIndex < sizeof(serialInputBuffer) - 1) {
                    serialInputBuffer[serialInputIndex++] = ch;
                }
                serialInputBuffer[serialInputIndex] = 0;
                if (strcmp(serialInputBuffer,"jsonl=")==0){
                    dispatchJsonl(Serial);
                    serialInputIndex=0;
                }
            }
        }
#endif
    //lv_tick_handler();
    lv_task_handler(); /* let the GUI do its work */
    guiCheckSleep();

#if TOUCH_DRIVER == 1
    touch.loop();
#endif

}

void guiStop()
{}

bool guiGetBacklight()
{
    return guiBacklightIsOn;
}

void guiSetBacklight(bool lighton)
{
    guiBacklightIsOn = lighton;

    if(guiBacklightPin >= 0) {

#if defined(ARDUINO_ARCH_ESP32)
        ledcWrite(BACKLIGHT_CHANNEL, lighton ? map(guiDimLevel, 0, 100, 0, 1023) : 0); // ledChannel and value
#else
        analogWrite(guiBacklightPin, lighton ? map(guiDimLevel, 0, 100, 0, 1023) : 0);
#endif

    } else {
        guiBacklightIsOn = true;
    }
}

void guiSetDim(int8_t level)
{
    if(guiBacklightPin >= 0) {
        guiDimLevel = level >= 0 ? level : 0;
        guiDimLevel = guiDimLevel <= 100 ? guiDimLevel : 100;

        if(guiBacklightIsOn) { // The backlight is ON
#if defined(ARDUINO_ARCH_ESP32)
            ledcWrite(BACKLIGHT_CHANNEL, map(guiDimLevel, 0, 100, 0, 1023)); // ledChannel and value
#else
            analogWrite(guiBacklightPin, map(guiDimLevel, 0, 100, 0, 1023));
#endif
        }

    } else {
        guiDimLevel = -1;
    }
}

int8_t guiGetDim()
{
    return guiDimLevel;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool guiGetConfig(const JsonObject & settings)
{
    bool changed = false;

    if(guiTickPeriod != settings[FPSTR(F_GUI_TICKPERIOD)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_GUI_TICKPERIOD)] = guiTickPeriod;

    if(guiSleepTime1 != settings[FPSTR(F_GUI_IDLEPERIOD1)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_GUI_IDLEPERIOD1)] = guiSleepTime1;

    if(guiSleepTime2 != settings[FPSTR(F_GUI_IDLEPERIOD2)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_GUI_IDLEPERIOD2)] = guiSleepTime2;

    if(guiBacklightPin != settings[FPSTR(F_GUI_BACKLIGHTPIN)].as<int8_t>()) changed = true;
    settings[FPSTR(F_GUI_BACKLIGHTPIN)] = guiBacklightPin;

    if(guiRotation != settings[FPSTR(F_GUI_ROTATION)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_GUI_ROTATION)] = guiRotation;

    if(guiShowPointer != settings[FPSTR(F_GUI_POINTER)].as<bool>()) changed = true;
    settings[FPSTR(F_GUI_POINTER)] = guiShowPointer;

    /* Check CalData array has changed */
    JsonArray array = settings[FPSTR(F_GUI_CALIBRATION)].as<JsonArray>();
    uint8_t i       = 0;
    for(JsonVariant v : array) {
        Log.verbose(F("GUI CONF: %d: %d <=> %d"), i, calData[i], v.as<uint16_t>());
        if(i < 5) {
            if(calData[i] != v.as<uint16_t>()) changed = true;
            v.set(calData[i]);
        } else {
            changed = true;
        }
        i++;
    }

    /* Build new CalData array if the count is not correct */
    if(i != 5) {
        array = settings[FPSTR(F_GUI_CALIBRATION)].to<JsonArray>(); // Clear JsonArray
        for(uint8_t i = 0; i < 5; i++) {
            array.add(calData[i]);
        }
    }

    if(changed) configOutput(settings);
    return changed;
}

/** Set GUI Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool guiSetConfig(const JsonObject & settings)
{
    configOutput(settings);
    bool changed = false;

    changed |= configSet(guiTickPeriod, settings[FPSTR(F_GUI_TICKPERIOD)], PSTR("guiTickPeriod"));
    changed |= configSet(guiBacklightPin, settings[FPSTR(F_GUI_BACKLIGHTPIN)], PSTR("guiBacklightPin"));
    changed |= configSet(guiSleepTime1, settings[FPSTR(F_GUI_IDLEPERIOD1)], PSTR("guiSleepTime1"));
    changed |= configSet(guiSleepTime2, settings[FPSTR(F_GUI_IDLEPERIOD2)], PSTR("guiSleepTime2"));
    changed |= configSet(guiRotation, settings[FPSTR(F_GUI_ROTATION)], PSTR("guiRotation"));

    if(!settings[FPSTR(F_GUI_POINTER)].isNull()) {
        if(guiShowPointer != settings[FPSTR(F_GUI_POINTER)].as<bool>()) {
            Log.trace(F("guiShowPointer set"));
        }
        changed |= guiShowPointer != settings[FPSTR(F_GUI_POINTER)].as<bool>();

        guiShowPointer = settings[FPSTR(F_GUI_POINTER)].as<bool>();
    }

    if(!settings[FPSTR(F_GUI_CALIBRATION)].isNull()) {
        bool status = false;
        int i       = 0;

        JsonArray array = settings[FPSTR(F_GUI_CALIBRATION)].as<JsonArray>();
        for(JsonVariant v : array) {
            if(i < 5) {
                if(calData[i] != v.as<uint16_t>()) status = true;
                calData[i] = v.as<uint16_t>();
            }
            i++;
        }

        if(calData[0] != 0 || calData[1] != 65535 || calData[2] != 0 || calData[3] != 65535) {
            Log.trace(F("calData set [%u, %u, %u, %u, %u]"), calData[0], calData[1], calData[2], calData[3],
                      calData[4]);
            oobeSetAutoCalibrate(false);
        } else {
            Log.notice(F("First Touch Calibration enabled"));
            oobeSetAutoCalibrate(true);
        }

        changed |= status;
    }

    return changed;
}

static void guiSetBmpHeader(uint8_t * buffer_p, int32_t data)
{
    *buffer_p++ = data & 0xFF;
    *buffer_p++ = (data >> 8) & 0xFF;
    *buffer_p++ = (data >> 16) & 0xFF;
    *buffer_p++ = (data >> 24) & 0xFF;
}

#if defined(ARDUINO_ARCH_ESP8266)
static void guiSendBmpHeader();

void guiTakeScreenshot(ESP8266WebServer & client)
#endif
#if defined(ARDUINO_ARCH_ESP32)
    static void guiSendBmpHeader();

void guiTakeScreenshot(WebServer & client)
#endif // ESP32{
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
{
    webClient        = &client;
    lv_disp_t * disp = lv_disp_get_default();

    webClient->setContentLength(122 + disp->driver.hor_res * disp->driver.ver_res * sizeof(lv_color_t));
    webClient->send(200, PSTR("image/bmp"), "");

    guiSnapshot = 2;
    guiSendBmpHeader();
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL); /* Will call our disp_drv.disp_flush function */
    guiSnapshot = 0;

    Log.verbose(F("GUI: Bitmap data flushed to webclient"));
}

/** Send Bitmap Header.
 *
 * Sends a header in BMP format for the size of the screen.
 *
 * @note: send header before refreshing the whole screen
 *
 **/
static void guiSendBmpHeader()
{
    uint8_t buffer[128];
    memset(buffer, 0, sizeof(buffer));

    lv_disp_t * disp = lv_disp_get_default();
    buffer[0]        = 0x42; // B
    buffer[1]        = 0x4D; // M

    buffer[10 + 0] = 122;      // full header size
    buffer[14 + 0] = 122 - 14; // dib header size
    buffer[26 + 0] = 1;        // number of color planes
    buffer[28 + 0] = 16;       // or 24, bbp
    buffer[30 + 0] = 3;        // compression, 0 = RGB / 3 = RGBA

    // file size
    guiSetBmpHeader(&buffer[2], 122 + disp->driver.hor_res * disp->driver.ver_res * buffer[28] / 8);
    // horizontal resolution
    guiSetBmpHeader(&buffer[18], disp->driver.hor_res);
    // vertical resolution
    guiSetBmpHeader(&buffer[22], -disp->driver.ver_res);
    // bitmap size
    guiSetBmpHeader(&buffer[34], disp->driver.hor_res * disp->driver.ver_res * buffer[28 + 0] / 8);
    // horizontal pixels per meter
    guiSetBmpHeader(&buffer[38], 2836);
    // vertical pixels per meter
    guiSetBmpHeader(&buffer[42], 2836);
    // R: 1111 1000 | 0000 0000
    guiSetBmpHeader(&buffer[54], 0xF800); // Red bitmask
    // G: 0000 0111 | 1110 0000
    guiSetBmpHeader(&buffer[58], 0x07E0); // Green bitmask
    // B: 0000 0000 | 0001 1111
    guiSetBmpHeader(&buffer[62], 0x001F); // Blue bitmask
    // A: 0000 0000 | 0000 0000
    guiSetBmpHeader(&buffer[66], 0x0000); // No Aplpha Mask

    // "Win
    buffer[70 + 3] = 0x57;
    buffer[70 + 2] = 0x69;
    buffer[70 + 1] = 0x6E;
    buffer[70 + 0] = 0x20;

    if(guiSnapshot == 1) {
        size_t len = pFileOut.write(buffer, 122);
        if(len != sizeof(buffer)) {
            Log.warning(F("GUI: Data written does not match header size"));
        } else {
            Log.verbose(F("GUI: Bitmap header written"));
        }

    } else if(guiSnapshot == 2) {
        if(webClient->client().write(buffer, 122) != 122) {
            Log.warning(F("GUI: Data sent does not match header size"));
        } else {
            Log.verbose(F("GUI: Bitmap header sent"));
        }
    }
}

/** Take Screenshot.
 *
 * Flush buffer into a binary file.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] pFileName   Output binary file name.
 *
 **/
void guiTakeScreenshot(const char * pFileName)
{
    pFileOut = SPIFFS.open(pFileName, "w");

    if(pFileOut == 0) {
        Log.warning(F("GUI: %s cannot be opened"), pFileName);
        return;
    }

    guiSnapshot = 1;
    guiSendBmpHeader();

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL); /* Will call our disp_drv.disp_flush function */
    guiSnapshot = 0;

    pFileOut.close();
    Log.verbose(F("[Display] data flushed to %s"), pFileName);
}
#endif