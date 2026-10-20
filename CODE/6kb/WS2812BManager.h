#ifndef WS2812BManager_h
#define WS2812BManager_h

/*
  WS2812B 灯效管理（六键键盘，6 颗灯，数据脚 D4，物理排布 2 行 × 3 列）

      0 1 2
      3 4 5

  灯效按三组编号，上位机按组显示，新增只能往后加：
    0 ~  9   单色（只用 color1）
   10 ~ 14   炫彩（HSV 彩虹，不占 color1/color2）
   15 ~ 22   双色（color1 ↔ color2）

  设计约定：
    · 所有参数集中在 ledParams 结构体里（上位机下发 / EEPROM 保存都是这一份）；
    · 效果函数只接收“按键状态”参数、只往 wsLeds[] 填颜色；
    · 刷新率由 ledParams.fps 决定（10~200fps），速度 speed 只影响动画步进；
    · 全部整数运算（sin8 / beatsin8 / blend / PROGMEM 小表），不用浮点。
*/

#include <FastLED.h>
#include "Universal.h"

// ---------------------------------------------------------------------------
// 灯效编号（三组）
// ---------------------------------------------------------------------------
// ---- 单色（color1）----
#define LED_EFFECT_OFF             0   // 全灭
#define LED_EFFECT_SOLID           1   // 常亮
#define LED_EFFECT_PRESS           2   // 按下即亮、松开熄灭（原有效果）
#define LED_EFFECT_PRESS_FADE      3   // 按下亮，松开余辉渐灭
#define LED_EFFECT_TYPING_HEAT     4   // 打字热度（冷色 → 暖色）
#define LED_EFFECT_BREATH          5   // 呼吸
#define LED_EFFECT_WAVE            6   // 波浪（按列扫过）
#define LED_EFFECT_SPARKLE         7   // 星光
#define LED_EFFECT_RIPPLE          8   // 涟漪（按物理相邻扩散）
#define LED_EFFECT_COMET_MONO      9   // 单色跑马灯

// ---- 炫彩（彩虹，不占颜色参数）----
#define LED_EFFECT_RAINBOW        10   // 彩虹流动
#define LED_EFFECT_RAINBOW_BREATH 11   // 彩虹呼吸
#define LED_EFFECT_RAINBOW_WAVE   12   // 彩虹波浪
#define LED_EFFECT_RAINBOW_SPARKLE 13  // 彩虹星光
#define LED_EFFECT_COMET_RAINBOW  14   // 彩虹跑马灯

// ---- 双色（color1 ↔ color2）----
#define LED_EFFECT_DUAL_BREATH    15   // 双色呼吸
#define LED_EFFECT_DUAL_PRESS     16   // 按下 = color2，松开 = color1 常亮
#define LED_EFFECT_DUAL_PRESS_FADE 17  // 按下 = color2，松开渐回 color1
#define LED_EFFECT_DUAL_WAVE      18   // 双色波浪渐变
#define LED_EFFECT_DUAL_COMET     19   // 双色跑马（头 color2，尾 color1）
#define LED_EFFECT_DUAL_TYPING    20   // 双色打字热度（冷 color2 → 热 color1）
#define LED_EFFECT_DUAL_GRADIENT  21   // 左右渐变（左 color1 → 右 color2）
#define LED_EFFECT_DUAL_RIPPLE    22   // 双色涟漪（波前 color2）
#define LED_EFFECT_COUNT          23

// 分组边界（上位机显示用，和上面的编号保持一致）
#define LED_GROUP_MONO_END         9
#define LED_GROUP_RAINBOW_BEGIN   10
#define LED_GROUP_RAINBOW_END     14
#define LED_GROUP_DUAL_BEGIN      15

// 灯带 / 排布
#define WS_PIN              4
#define WS_LED_COUNT        BUTTONCOUNT
#define LED_KEY_COLS        3
#define LED_KEY_ROWS        2

// 参数范围
#define LED_SPEED_MIN       1
#define LED_SPEED_MAX       20
#define LED_FPS_MIN         10
#define LED_FPS_MAX         200
#define LED_FPS_DEFAULT     50

// ---------------------------------------------------------------------------
// 参数结构体（EEPROM 里就是这 10 个字节，顺序不能改）
// ---------------------------------------------------------------------------
#define LED_PARAMS_MAGIC    0x5B

typedef struct LEDparams {
  uint8_t magic;          // LED_PARAMS_MAGIC
  uint8_t effect;         // LED_EFFECT_*
  uint8_t brightness;     // 0~255
  uint8_t speed;          // LED_SPEED_MIN ~ LED_SPEED_MAX
  uint8_t fps;            // LED_FPS_MIN ~ LED_FPS_MAX
  uint8_t color1[3];      // 主色 RGB
  uint8_t color2[3];      // 副色 RGB
} LEDParams_t;            // 10 字节

LEDParams_t ledParams;
CRGB wsLeds[WS_LED_COUNT];

// 帧调度 + 效果内部状态（不进 EEPROM）
static uint32_t wsLastFrameMs = 0;
static uint8_t  wsPhase       = 0;
static uint8_t  wsHue         = 0;
static uint8_t  wsHeat[WS_LED_COUNT];
static uint8_t  wsCometPos    = 0;
static uint8_t  wsCometHold   = 0;
static uint16_t wsPrevKeys    = 0;
static int8_t   wsRippleKey   = -1;
static uint8_t  wsRippleAge   = 0;

// 跑马灯拖尾亮度（PROGMEM，3 字节 Flash，不占 RAM）
static const uint8_t wsCometTrail[3] PROGMEM = { 255, 90, 28 };

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
static inline uint8_t wsKeyCol(uint8_t k) { return k % LED_KEY_COLS; }
static inline uint8_t wsKeyRow(uint8_t k) { return k / LED_KEY_COLS; }

static inline uint8_t wsKeyDistance(uint8_t a, uint8_t b) {
  uint8_t ca = wsKeyCol(a), cb = wsKeyCol(b);
  uint8_t ra = wsKeyRow(a), rb = wsKeyRow(b);
  uint8_t dc = (ca > cb) ? (ca - cb) : (cb - ca);
  uint8_t dr = (ra > rb) ? (ra - rb) : (rb - ra);
  return dc + dr;
}

static inline CRGB wsColor1() { return CRGB(ledParams.color1[0], ledParams.color1[1], ledParams.color1[2]); }
static inline CRGB wsColor2() { return CRGB(ledParams.color2[0], ledParams.color2[1], ledParams.color2[2]); }

static inline uint8_t wsSpeedStep() { return 2 + (ledParams.speed >> 2); }
static inline uint8_t wsSpeedFade() { return 12 + ledParams.speed * 3; }

// 两色插值（t=0 取 a，t=255 取 b）
static inline CRGB wsMix(CRGB a, CRGB b, uint8_t t) { return blend(a, b, t); }

// 颜色按 t 变暗
static inline CRGB wsDim(CRGB c, uint8_t t) { c.nscale8_video(t); return c; }

// ---------------------------------------------------------------------------
// 参数接口（一次只改一个字段）
// ---------------------------------------------------------------------------
void ledParamsSetDefault() {
  ledParams.magic      = LED_PARAMS_MAGIC;
  ledParams.effect     = LED_EFFECT_PRESS;
  ledParams.brightness = 128;
  ledParams.speed      = 8;
  ledParams.fps        = LED_FPS_DEFAULT;
  ledParams.color1[0] = 255; ledParams.color1[1] = 0;   ledParams.color1[2] = 0;
  ledParams.color2[0] = 0;   ledParams.color2[1] = 120; ledParams.color2[2] = 255;
}

void wsApplyParams() {
  FastLED.setBrightness(ledParams.brightness);
}

void wsInit() {
  FastLED.addLeds<WS2812, WS_PIN, GRB>(wsLeds, WS_LED_COUNT);
  FastLED.clear(true);
  memset(wsHeat, 0, sizeof(wsHeat));
  wsPrevKeys  = 0;
  wsRippleKey = -1;
  wsRippleAge = 0;
  wsCometPos  = 0;
  wsCometHold = 0;
  wsApplyParams();
}

void wsSetEffect(uint8_t effect) {
  if (effect >= LED_EFFECT_COUNT) return;
  ledParams.effect = effect;
  wsPhase = 0;
  wsHue   = 0;
  memset(wsHeat, 0, sizeof(wsHeat));
  wsRippleKey = -1;
  wsRippleAge = 0;
  wsCometPos  = 0;
  wsCometHold = 0;
  FastLED.clear(true);
}

void wsSetBrightness(uint8_t b) {
  ledParams.brightness = b;
  FastLED.setBrightness(b);
}

void wsSetSpeed(uint8_t s) {
  if (s < LED_SPEED_MIN) s = LED_SPEED_MIN;
  if (s > LED_SPEED_MAX) s = LED_SPEED_MAX;
  ledParams.speed = s;
}

void wsSetFps(uint8_t f) {
  if (f < LED_FPS_MIN) f = LED_FPS_MIN;
  if (f > LED_FPS_MAX) f = LED_FPS_MAX;
  ledParams.fps = f;
}

void wsSetColor1RGB(uint8_t r, uint8_t g, uint8_t b) {
  ledParams.color1[0] = r; ledParams.color1[1] = g; ledParams.color1[2] = b;
}

void wsSetColor2RGB(uint8_t r, uint8_t g, uint8_t b) {
  ledParams.color2[0] = r; ledParams.color2[1] = g; ledParams.color2[2] = b;
}

// ---------------------------------------------------------------------------
// 单色组
// ---------------------------------------------------------------------------
static void wsOff() { fill_solid(wsLeds, WS_LED_COUNT, CRGB::Black); }

static void wsSolid() { fill_solid(wsLeds, WS_LED_COUNT, wsColor1()); }

static void wsPress(uint16_t keyState) {
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = ((keyState >> i) & 1) ? c : CRGB::Black;
}

static void wsPressFade(uint16_t keyState) {
  CRGB c = wsColor1();
  uint8_t fadeBy = wsSpeedFade();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsLeds[i] = c;
    else fadeToBlackBy(&wsLeds[i], 1, fadeBy);
  }
}

static void wsTypingHeat(uint16_t keyState) {
  uint8_t cool = 2 + (ledParams.speed >> 1);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsHeat[i] = 255;
    else wsHeat[i] = qsub8(wsHeat[i], cool);
    wsLeds[i] = HeatColor(wsHeat[i]);
  }
}

static void wsBreath() {
  uint8_t level = beatsin8(6 + ledParams.speed * 3, 16, 255);
  fill_solid(wsLeds, WS_LED_COUNT, wsColor1());
  nscale8(wsLeds, WS_LED_COUNT, level);
}

static void wsWave() {
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = c;
    wsLeds[i].nscale8_video(sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20));
  }
  wsPhase += 4 + (ledParams.speed >> 1);
}

static void wsSparkle(uint16_t keyState) {
  CRGB c = wsColor1();
  fadeToBlackBy(wsLeds, WS_LED_COUNT, wsSpeedFade());
  if (random8() < 40) wsLeds[random8(WS_LED_COUNT)] = c;
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    if ((keyState >> i) & 1) wsLeds[i] = c;
}

// 单色跑马灯
static void wsCometMono() {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, 48);
  CRGB c = wsColor1();
  wsLeds[wsCometPos] = c;
  for (uint8_t k = 1; k <= 2; k++) {
    int8_t idx = (int8_t)wsCometPos - (int8_t)k;
    if (idx < 0) idx += WS_LED_COUNT;
    wsLeds[idx] = wsDim(c, pgm_read_byte(&wsCometTrail[k - 1]));
  }
  wsCometHold++;
  if (wsCometHold >= (uint8_t)(11 - (ledParams.speed >> 1))) {
    wsCometHold = 0;
    wsCometPos = (wsCometPos + 1) % WS_LED_COUNT;
  }
}

// 涟漪：波源固定，亮度只由“年龄 − 物理距离”决定，必然衰减到 0
static void wsRipple(uint16_t keyState, bool dual) {
  uint16_t released = wsPrevKeys & ~keyState;
  if (released) {
    for (int8_t i = WS_LED_COUNT - 1; i >= 0; i--) {
      if ((released >> i) & 1) { wsRippleKey = i; wsRippleAge = 0; break; }
    }
  }

  CRGB c1 = wsColor1(), c2 = wsColor2();
  uint8_t fadeBy = 8 + (ledParams.speed * 2);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t level = 0;
    if (wsRippleKey >= 0) {
      int16_t age = (int16_t)wsRippleAge - (int16_t)wsKeyDistance((uint8_t)wsRippleKey, i) * 3;
      if (age >= 0) {
        int16_t v = 255 - age * fadeBy;
        if (v > 0) level = (uint8_t)v;
      }
    }
    if (dual) {
      wsLeds[i] = wsMix(c1, c2, level);
    } else {
      wsLeds[i] = wsDim(c1, level);
    }
    if ((keyState >> i) & 1) wsLeds[i] = c1;
  }

  if (wsRippleKey >= 0) {
    wsRippleAge++;
    if (wsRippleAge > 40) { wsRippleKey = -1; wsRippleAge = 0; }
  }
}

// ---------------------------------------------------------------------------
// 炫彩组
// ---------------------------------------------------------------------------
static void wsRainbow() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = CHSV(wsHue + wsKeyCol(i) * 42 + wsKeyRow(i) * 85, 255, 255);
  wsHue += wsSpeedStep();
}

static void wsRainbowBreath() {
  uint8_t level = beatsin8(6 + ledParams.speed * 3, 20, 255);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = CHSV(wsHue + wsKeyCol(i) * 30 + wsKeyRow(i) * 60, 255, level);
  wsHue += wsSpeedStep();
}

static void wsRainbowWave() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t hue = wsHue + wsKeyCol(i) * 50 + wsKeyRow(i) * 20;
    uint8_t v   = sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20);
    wsLeds[i] = CHSV(hue, 255, v);
  }
  wsPhase += 4 + (ledParams.speed >> 1);
  wsHue   += wsSpeedStep();
}

static void wsRainbowSparkle(uint16_t keyState) {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, wsSpeedFade());
  if (random8() < 48) wsLeds[random8(WS_LED_COUNT)] = CHSV(random8(), 255, 255);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    if ((keyState >> i) & 1) wsLeds[i] = CHSV(wsHue + i * 40, 255, 255);
  wsHue += 2;
}

static void wsCometRainbow() {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, 48);
  wsLeds[wsCometPos] = CHSV(wsHue, 255, 255);
  for (uint8_t k = 1; k <= 2; k++) {
    int8_t idx = (int8_t)wsCometPos - (int8_t)k;
    if (idx < 0) idx += WS_LED_COUNT;
    wsLeds[idx] = CHSV(wsHue + 10 * k, 255, pgm_read_byte(&wsCometTrail[k - 1]));
  }
  wsHue += 6;
  wsCometHold++;
  if (wsCometHold >= (uint8_t)(11 - (ledParams.speed >> 1))) {
    wsCometHold = 0;
    wsCometPos = (wsCometPos + 1) % WS_LED_COUNT;
  }
}

// ---------------------------------------------------------------------------
// 双色组（color1 ↔ color2）
// ---------------------------------------------------------------------------
static void wsDualBreath() {
  uint8_t t = beatsin8(6 + ledParams.speed * 3, 0, 255);
  fill_solid(wsLeds, WS_LED_COUNT, wsMix(wsColor1(), wsColor2(), t));
}

static void wsDualPress(uint16_t keyState) {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = ((keyState >> i) & 1) ? c2 : c1;
}

// 按下 = color2；松开后每帧往 color1 混一点
static void wsDualPressFade(uint16_t keyState) {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  uint8_t mixStep = 20 + ledParams.speed * 3;
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsLeds[i] = c2;
    else wsLeds[i] = wsMix(wsLeds[i], c1, mixStep);
  }
}

static void wsDualWave() {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t t = sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20);
    wsLeds[i] = wsMix(c1, c2, t);
  }
  wsPhase += 4 + (ledParams.speed >> 1);
}

static void wsDualComet() {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, 48);
  CRGB c1 = wsColor1(), c2 = wsColor2();
  wsLeds[wsCometPos] = c2;
  for (uint8_t k = 1; k <= 2; k++) {
    int8_t idx = (int8_t)wsCometPos - (int8_t)k;
    if (idx < 0) idx += WS_LED_COUNT;
    wsLeds[idx] = wsDim(wsMix(c1, c2, pgm_read_byte(&wsCometTrail[k - 1])), pgm_read_byte(&wsCometTrail[k - 1]));
  }
  wsCometHold++;
  if (wsCometHold >= (uint8_t)(11 - (ledParams.speed >> 1))) {
    wsCometHold = 0;
    wsCometPos = (wsCometPos + 1) % WS_LED_COUNT;
  }
}

static void wsDualTyping(uint16_t keyState) {
  uint8_t cool = 2 + (ledParams.speed >> 1);
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsHeat[i] = 255;
    else wsHeat[i] = qsub8(wsHeat[i], cool);
    wsLeds[i] = wsMix(c2, c1, wsHeat[i]);      // 冷 = color2，热 = color1
  }
}

// 左右渐变：左列 color1，右列 color2，中间插值
static void wsDualGradient() {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t t = (uint8_t)((uint16_t)wsKeyCol(i) * 255 / (LED_KEY_COLS - 1));
    wsLeds[i] = wsMix(c1, c2, t);
  }
}

// ---------------------------------------------------------------------------
// 主刷新
// ---------------------------------------------------------------------------
void wsTick(uint16_t keyState) {
  uint32_t now = millis();
  uint16_t interval = (uint16_t)(1000UL / (ledParams.fps ? ledParams.fps : LED_FPS_DEFAULT));
  if (interval == 0) interval = 1;
  if (now - wsLastFrameMs < interval) return;
  wsLastFrameMs = now;

  switch (ledParams.effect) {
    // ---- 单色 ----
    case LED_EFFECT_OFF:              wsOff();               break;
    case LED_EFFECT_SOLID:            wsSolid();             break;
    case LED_EFFECT_PRESS:            wsPress(keyState);     break;
    case LED_EFFECT_PRESS_FADE:       wsPressFade(keyState); break;
    case LED_EFFECT_TYPING_HEAT:      wsTypingHeat(keyState);break;
    case LED_EFFECT_BREATH:           wsBreath();            break;
    case LED_EFFECT_WAVE:             wsWave();              break;
    case LED_EFFECT_SPARKLE:          wsSparkle(keyState);   break;
    case LED_EFFECT_RIPPLE:           wsRipple(keyState, false); break;
    case LED_EFFECT_COMET_MONO:       wsCometMono();         break;
    // ---- 炫彩 ----
    case LED_EFFECT_RAINBOW:          wsRainbow();           break;
    case LED_EFFECT_RAINBOW_BREATH:   wsRainbowBreath();     break;
    case LED_EFFECT_RAINBOW_WAVE:     wsRainbowWave();       break;
    case LED_EFFECT_RAINBOW_SPARKLE:  wsRainbowSparkle(keyState); break;
    case LED_EFFECT_COMET_RAINBOW:    wsCometRainbow();      break;
    // ---- 双色 ----
    case LED_EFFECT_DUAL_BREATH:      wsDualBreath();        break;
    case LED_EFFECT_DUAL_PRESS:       wsDualPress(keyState); break;
    case LED_EFFECT_DUAL_PRESS_FADE:  wsDualPressFade(keyState); break;
    case LED_EFFECT_DUAL_WAVE:        wsDualWave();          break;
    case LED_EFFECT_DUAL_COMET:       wsDualComet();         break;
    case LED_EFFECT_DUAL_TYPING:      wsDualTyping(keyState);break;
    case LED_EFFECT_DUAL_GRADIENT:    wsDualGradient();      break;
    case LED_EFFECT_DUAL_RIPPLE:      wsRipple(keyState, true); break;
    default:                          wsPress(keyState);     break;
  }

  FastLED.show();
  wsPrevKeys = keyState;
}

#endif
