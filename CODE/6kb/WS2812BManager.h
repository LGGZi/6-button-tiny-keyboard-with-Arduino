#ifndef WS2812BManager_h
#define WS2812BManager_h

/*
  WS2812B 灯效管理（六键键盘，6 颗灯，数据脚 D4）

  设计目标：
    · 所有灯效都是 O(按键数) 的整数运算，不用浮点、不用大查找表；
    · 每帧只调用一次 FastLED.show()，帧率固定在 50fps（20ms），
      而不是"每 N 轮循环 show 一次"——后者在 500Hz 主循环下会变成 200Hz 刷新，
      白白吃掉 USB 中断之外的 CPU 时间；
    · 每个效果只是往 wsLeds[] 里填颜色，切换效果不影响按键扫描；
    · RAM 占用：wsLeds 18B + wsHeat 6B + wsRippleLevel 6B ≈ 30B。

  用法（见 6kb.ino）：
    setup():  wsInit(); wsSetBaseColor(CRGB::Red); wsSetEffect(LED_EFFECT_PRESS);
    loop():   在 keyOperation(getKeyState()) 之后调用 wsTick();

  以后上位机可以通过 HID 指令调用 wsSetEffect() / wsSetBaseColor() / wsSetBrightness()。
*/

#include <FastLED.h>
#include "Universal.h"

// ---------------------------------------------------------------------------
// 灯效编号（上位机以后按这个编号下发，改动时保持向后兼容）
// ---------------------------------------------------------------------------
#define LED_EFFECT_OFF          0   // 全灭
#define LED_EFFECT_SOLID        1   // 常亮（基础色）
#define LED_EFFECT_PRESS        2   // 按下即亮、松开熄灭（原有效果）
#define LED_EFFECT_PRESS_FADE   3   // 按下亮，松开后余辉渐灭
#define LED_EFFECT_TYPING_HEAT  4   // 打字热度：越按越亮，之后慢慢冷却
#define LED_EFFECT_RAINBOW      5   // 彩虹流动
#define LED_EFFECT_BREATH       6   // 呼吸（亮度按正弦起伏）
#define LED_EFFECT_WAVE         7   // 波浪（每颗灯错相位）
#define LED_EFFECT_SPARKLE      8   // 星光闪烁（随机点 + 拖尾）
#define LED_EFFECT_RIPPLE       9   // 涟漪：松开时从该键向两侧扩散
#define LED_EFFECT_COUNT        10

// 灯带硬件参数
#define WS_PIN          4
#define WS_LED_COUNT    BUTTONCOUNT
#define WS_FRAME_MS     20      // 50fps
#define WS_MAX_BRIGHTNESS 255

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
CRGB wsLeds[WS_LED_COUNT];
static uint8_t  wsEffect        = LED_EFFECT_PRESS;
static CRGB     wsBaseColor     = CRGB::Red;
static uint8_t  wsBrightness    = 128;
static uint32_t wsLastFrameMs   = 0;
static uint8_t  wsPhase         = 0;                       // 彩虹 / 波浪相位
static uint8_t  wsHeat[WS_LED_COUNT];                      // 打字热度 / 余辉
static uint8_t  wsRippleLevel[WS_LED_COUNT];               // 涟漪亮度
static uint16_t wsPrevKeys      = 0;                       // 上一帧的按键状态

// ---------------------------------------------------------------------------
// 基础接口
// ---------------------------------------------------------------------------
void wsInit() {
  FastLED.addLeds<WS2812, WS_PIN, GRB>(wsLeds, WS_LED_COUNT);
  FastLED.setBrightness(wsBrightness);
  FastLED.clear(true);
  memset(wsHeat, 0, sizeof(wsHeat));
  memset(wsRippleLevel, 0, sizeof(wsRippleLevel));
  wsPrevKeys = 0;
}

void wsSetBrightness(uint8_t b) {
  wsBrightness = b;
  FastLED.setBrightness(b);
}

uint8_t wsGetBrightness() { return wsBrightness; }

void wsSetBaseColor(CRGB c) { wsBaseColor = c; }
void wsSetBaseColorRGB(uint8_t r, uint8_t g, uint8_t b) { wsBaseColor = CRGB(r, g, b); }

uint8_t wsGetEffect() { return wsEffect; }

// 切换效果：清掉上一份状态，避免切过去第一帧还是旧颜色
void wsSetEffect(uint8_t effect) {
  if (effect >= LED_EFFECT_COUNT) return;
  wsEffect = effect;
  wsPhase = 0;
  memset(wsHeat, 0, sizeof(wsHeat));
  memset(wsRippleLevel, 0, sizeof(wsRippleLevel));
  FastLED.clear(true);
}

// ---------------------------------------------------------------------------
// 各灯效：只负责往 wsLeds[] 填颜色
// ---------------------------------------------------------------------------
static void wsFillOff() {
  fill_solid(wsLeds, WS_LED_COUNT, CRGB::Black);
}

static void wsFillSolid() {
  fill_solid(wsLeds, WS_LED_COUNT, wsBaseColor);
}

// 原有效果：按物理按下状态点亮
static void wsFillPress() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = ((buttonbuf >> i) & 1) ? wsBaseColor : CRGB::Black;
  }
}

// 按下亮，松开后余辉渐灭（fadeToBlackBy：每帧减约 9%）
static void wsFillPressFade() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((buttonbuf >> i) & 1) wsLeds[i] = wsBaseColor;
    else fadeToBlackBy(&wsLeds[i], 1, 24);
  }
}

// 打字热度：按下的键冲到最亮，之后每帧冷却一点（HeatColor 是 FastLED 内置查表）
static void wsFillTypingHeat() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((buttonbuf >> i) & 1) wsHeat[i] = 255;
    else wsHeat[i] = qsub8(wsHeat[i], 4);
    wsLeds[i] = HeatColor(wsHeat[i]);
  }
}

// 彩虹流动
static void wsFillRainbow() {
  fill_rainbow(wsLeds, WS_LED_COUNT, wsPhase, 255 / WS_LED_COUNT);
  wsPhase += 2;
}

// 呼吸：beatsin8 是查表实现，比 sin() 快得多
static void wsFillBreath() {
  uint8_t level = beatsin8(12, 20, 255);
  fill_solid(wsLeds, WS_LED_COUNT, wsBaseColor);
  nscale8(wsLeds, WS_LED_COUNT, level);
}

// 波浪：相邻灯错开相位
static void wsFillWave() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = wsBaseColor;
    wsLeds[i].nscale8_video(sin8(wsPhase + i * 40));
  }
  wsPhase += 6;
}

// 星光：整体拖尾 + 随机点亮一颗，按住的键保持常亮
static void wsFillSparkle() {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, 40);
  if (random8() < 40) {
    wsLeds[random8(WS_LED_COUNT)] = wsBaseColor;
  }
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((buttonbuf >> i) & 1) wsLeds[i] = wsBaseColor;
  }
}

// 涟漪：松开某键时从它开始向两侧扩散并衰减
static void wsFillRipple() {
  uint16_t released = wsPrevKeys & ~buttonbuf;
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((released >> i) & 1) wsRippleLevel[i] = 255;
  }

  // 扩散 + 衰减（整数运算，先算扩散再衰减，避免同一帧来回震荡）
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t spread = wsRippleLevel[i] / 3;
    if (spread > 12) {
      if (i > 0)               wsRippleLevel[i - 1] = qadd8(wsRippleLevel[i - 1], spread);
      if (i + 1 < WS_LED_COUNT) wsRippleLevel[i + 1] = qadd8(wsRippleLevel[i + 1], spread);
    }
  }
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsRippleLevel[i] = qsub8(wsRippleLevel[i], 24);
    wsLeds[i] = wsBaseColor;
    wsLeds[i].nscale8_video(wsRippleLevel[i]);
    if ((buttonbuf >> i) & 1) wsLeds[i] = wsBaseColor;   // 按住的键始终常亮
  }
}

// ---------------------------------------------------------------------------
// 主刷新：每帧算一次颜色 + show 一次；主循环里随便调，内部自己限速
// ---------------------------------------------------------------------------
void wsTick() {
  uint32_t now = millis();
  if (now - wsLastFrameMs < WS_FRAME_MS) return;
  wsLastFrameMs = now;

  switch (wsEffect) {
    case LED_EFFECT_OFF:         wsFillOff();         break;
    case LED_EFFECT_SOLID:       wsFillSolid();       break;
    case LED_EFFECT_PRESS:       wsFillPress();       break;
    case LED_EFFECT_PRESS_FADE:  wsFillPressFade();   break;
    case LED_EFFECT_TYPING_HEAT: wsFillTypingHeat();  break;
    case LED_EFFECT_RAINBOW:     wsFillRainbow();     break;
    case LED_EFFECT_BREATH:      wsFillBreath();      break;
    case LED_EFFECT_WAVE:        wsFillWave();        break;
    case LED_EFFECT_SPARKLE:     wsFillSparkle();     break;
    case LED_EFFECT_RIPPLE:      wsFillRipple();      break;
    default:                     wsFillPress();       break;
  }

  FastLED.show();
  wsPrevKeys = buttonbuf;
}

#endif
