// PxMatrix, as v1 drove it: double-buffered, refreshed every 4 ms, 1/16 scan,
// with the mux delay the SRYLED P4 panels need.
//
// v1 refreshed from a hardware timer interrupt inside a critical section.
// On Arduino core 3 every SPI call takes a mutex, which may not be taken
// there: FreeRTOS asserts on the first refresh and the device boot-loops.
// Running the refresh from the esp_timer task instead put it on core 0
// beside WiFi, which outranks it: refreshes ran late and the panel flickered.
//
// So the split is: the hardware timer interrupt (on core 1, like v1's) only
// wakes a task, and that task, pinned to core 1 at a priority above
// everything else there, does the SPI work. The timing is the timer's, the
// mutex is taken in a task, and WiFi on core 0 cannot delay it.
//
// The panel is lit in bursts, one per refresh, and on some builds the power
// supply sings at that rate. The period and the on-time are settings
// (g_px_period_us, g_px_on_us, Settings > Panel) and apply at once.
//
// Pins are Brian Lough's ESP32 Matrix Shield (Mini 32): R1 on GPIO13 (SPI
// MOSI) and CLK on 14 through the SPI peripheral, the rest as below.
#include "project/matrix/matrix.h"
#include "project/gs_state.h"

#include <Arduino.h>

#define double_buffer true   // PxMatrix reads this before its header
#include <PxMatrix.h>

#define PIN_LAT 22
#define PIN_OE   2
#define PIN_A   19
#define PIN_B   23
#define PIN_C   18
#define PIN_D    5

static PxMATRIX display(MATRIX_W, MATRIX_H, PIN_LAT, PIN_OE, PIN_A, PIN_B, PIN_C, PIN_D);
// Stack for display(): SPI calls and a few locals.
#define REFRESH_TASK_STACK  3072
#define REFRESH_TASK_CORE   1
#define REFRESH_TIMER_HZ    1000000   // the alarm counts microseconds

static hw_timer_t   *s_timer = nullptr;
static TaskHandle_t  s_task  = nullptr;
static volatile uint32_t s_refreshes = 0;

static void IRAM_ATTR onRefreshTimer() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(s_task, &woken);
  portYIELD_FROM_ISR(woken);
}

static void refreshTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    display.display((uint16_t)g_px_on_us);
    s_refreshes = s_refreshes + 1;
  }
}

uint32_t matrixRefreshCount() { return s_refreshes; }

// v1 waited 2 us after address line B only, which the SRYLED P4 panels
// needed. Some panels show the rows before an all-lines change (7 and 15 on
// a 1/16 scan) flickering: their decoder needs longer to settle there.
// g_px_mux_us adds a wait after every line, B never below v1's 2 us.
static void applyMuxDelay() {
  const uint8_t d = (uint8_t)g_px_mux_us;
  display.setMuxDelay(d, d > 2 ? d : 2, d, d, 0);
}

bool matrixBegin() {
  display.begin(16);                   // 1/16 scan
  applyMuxDelay();
  display.flushDisplay();
  display.setTextWrap(false);
  // Above the loop task and everything else an application runs on core 1;
  // below only the interrupt that wakes it.
  if (xTaskCreatePinnedToCore(refreshTask, "pxmatrix", REFRESH_TASK_STACK, nullptr,
                              configMAX_PRIORITIES - 1, &s_task, REFRESH_TASK_CORE) != pdPASS) {
    return false;
  }
  // Called from setup(), which runs on core 1: the interrupt lands there too.
  s_timer = timerBegin(REFRESH_TIMER_HZ);
  if (!s_timer) return false;
  timerAttachInterrupt(s_timer, &onRefreshTimer);
  timerAlarm(s_timer, (uint64_t)g_px_period_us, true, 0);
  return true;
}

// A new period or row settle time takes effect at once; the on-time is read
// on every refresh.
void matrixApplyTiming() {
  if (s_timer) timerAlarm(s_timer, (uint64_t)g_px_period_us, true, 0);
  applyMuxDelay();
}

void matrixShow() {
  const uint16_t *px = matrixCanvas().getBuffer();
  for (int16_t y = 0; y < MATRIX_H; y++)
    for (int16_t x = 0; x < MATRIX_W; x++)
      display.drawPixel(x, y, px[y * MATRIX_W + x]);
  display.showBuffer();
}

void matrixSetBrightness(uint8_t level) { display.setBrightness(matrixPerceptual(level)); }

const char *matrixDriverName() { return "PxMatrix"; }
