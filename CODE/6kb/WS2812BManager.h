#ifndef WS2812BManager_h
#define WS2812BManager_h

/*
  WS2812B 灯效管理（六键键盘，6 颗灯，数据脚 D4，物理排布 2 行 × 3 列）

      0 1 2
      3 4 5

  设计约定：
    · 所有参数集中在 ledParams 结构体里（上位机下发 / EEPROM 保存都是这一份）；
    · 效果函数只接收“按键状态”参数、只往 wsLeds[] 填颜色，不读其它可变状态；
    · 每帧只 FastLED.show() 一次，帧率固定 50fps；
    · 全部整数运算（sin8 / beatsin8 / HeatColor / PROGMEM 小表），不用浮点；
    · 帧步进跟随 ledParams.speed（1 = 慢，20 = 快）。
*/

#include <FastLED.h>
#include "Universal.h"

// ---------------------------------------------------------------------------
// 灯效编号（上位机按这个编号下发，新增只能往后加，保持向后兼容）
// ---------------------------------------------------------------------------
#define LED_EFFECT_OFF          0   // 全灭
#define LED_EFFECT_SOLID        1   // 常亮（color1）
#define LED_EFFECT_PRESS        2   // 按下即亮、松开熄灭（原有效果）
#define LED_EFFECT_PRESS_FADE   3   // 按下亮，松开后余辉渐灭
#define LED_EFFECT_TYPING_HEAT  4   // 打字热度：越按越亮，之后慢慢冷却
#define LED_EFFECT_RAINBOW      5   // 彩虹流动
#define LED_EFFECT_BREATH       6   // 呼吸（color1）
#define LED_EFFECT_WAVE         7   // 波浪（按列扫过）
#define LED_EFFECT_SPARKLE      8   // 星光闪烁（随机点 + 拖尾）
#define LED_EFFECT_RIPPLE       9   // 涟漪：松开后从该键按物理相邻向外扩散
#define LED_EFFECT_COMET       10   // 跑马灯：color2 小灯珠带拖尾循环跑
#define LED_EFFECT_DUAL_BREATH 11   // 双色呼吸：color1 与 color2 之间渐变
#define LED_EFFECT_COUNT       12

// 灯带 / 排布
#define WS_PIN              4
#define WS_LED_COUNT        BUTTONCOUNT
#define WS_FRAME_MS         20      // 50fps
#define LED_KEY_COLS        3       // 每行 3 个
#define LED_KEY_ROWS        2       // 2 行

// 速度范围（上位机和 EEPROM 都用这个范围校验）
#define LED_SPEED_MIN       1
#define LED_SPEED_MAX       20

// ---------------------------------------------------------------------------
// 参数结构体：上位机下发、EEPROM 保存、灯效读取都用这一份
// 注意：字段顺序 / 大小就是 EEPROM 里的字节布局，改动要同步上位机
// ---------------------------------------------------------------------------
#define LED_PARAMS_MAGIC    0x5A

typedef struct LEDparams {
  uint8_t magic;          // LED_PARAMS_MAGIC
  uint8_t effect;         // LED_EFFECT_*
  uint8_t brightness;     // 0~255
  uint8_t speed;          // LED_SPEED_MIN ~ LED_SPEED_MAX
  uint8_t color1[3];      // 主色 RGB
  uint8_t color2[3];      // 副色 RGB（跑马灯 / 双色呼吸）
} LEDParams_t;            // 9 字节

// 唯一一份可变的灯效参数
LEDParams_t ledParams;

// 灯带缓冲（FastLED 直接写这块内存）
CRGB wsLeds[WS_LED_COUNT];

// 帧调度 + 效果内部状态（不进 EEPROM）
static uint32_t wsLastFrameMs  = 0;
static uint8_t  wsPhase        = 0;
static uint8_t  wsHeat[WS_LED_COUNT];
static uint8_t  wsCometPos     = 0;
static uint8_t  wsCometHold    = 0;
static uint16_t wsPrevKeys     = 0;   // 上一帧状态，用来找"刚松开"
static int8_t   wsRippleKey    = -1;  // 当前涟漪的波源按键
static uint8_t  wsRippleAge    = 0;

// 跑马灯拖尾亮度（PROGMEM，只占 3 字节 Flash，不占 RAM）
static const uint8_t wsCometTrail[3] PROGMEM = { 255, 90, 28 };

// ---------------------------------------------------------------------------
// 内部小工具
// ---------------------------------------------------------------------------
static inline uint8_t wsKeyCol(uint8_t k) { return k % LED_KEY_COLS; }
static inline uint8_t wsKeyRow(uint8_t k) { return k / LED_KEY_COLS; }

// 两键在 2×3 布局上的曼哈顿距离（0~3）
static inline uint8_t wsKeyDistance(uint8_t a, uint8_t b) {
  uint8_t ca = wsKeyCol(a), cb = wsKeyCol(b);
  uint8_t ra = wsKeyRow(a), rb = wsKeyRow(b);
  uint8_t dc = (ca > cb) ? (ca - cb) : (cb - ca);
  uint8_t dr = (ra > rb) ? (ra - rb) : (rb - ra);
  return dc + dr;
}

// color1 / color2 取出来用（每帧各一次，避免在循环里反复拼 CRGB）
static inline CRGB wsColor1() { return CRGB(ledParams.color1[0], ledParams.color1[1], ledParams.color1[2]); }
static inline CRGB wsColor2() { return CRGB(ledParams.color2[0], ledParams.color2[1], ledParams.color2[2]); }

// 速度 -> 相位步进 / 衰减量
static inline uint8_t wsSpeedStep() { return 2 + (ledParams.speed >> 2); }
static inline uint8_t wsSpeedFade() { return 12 + ledParams.speed * 3; }

// ---------------------------------------------------------------------------
// 参数接口（原子：一次只改一个字段）
// ---------------------------------------------------------------------------
void ledParamsSetDefault() {
  ledParams.magic      = LED_PARAMS_MAGIC;
  ledParams.effect     = LED_EFFECT_PRESS;
  ledParams.brightness = 128;
  ledParams.speed      = 8;
  ledParams.color1[0] = 255; ledParams.color1[1] = 0;   ledParams.color1[2] = 0;
  ledParams.color2[0] = 0;   ledParams.color2[1] = 120; ledParams.color2[2] = 255;
}

// 把参数应用到硬件（亮度）
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

void wsSetColor1RGB(uint8_t r, uint8_t g, uint8_t b) {
  ledParams.color1[0] = r; ledParams.color1[1] = g; ledParams.color1[2] = b;
}

void wsSetColor2RGB(uint8_t r, uint8_t g, uint8_t b) {
  ledParams.color2[0] = r; ledParams.color2[1] = g; ledParams.color2[2] = b;
}

// ---------------------------------------------------------------------------
// 各灯效：只往 wsLeds[] 填颜色，按键状态由参数传入
// ---------------------------------------------------------------------------
static void wsFillOff() {
  fill_solid(wsLeds, WS_LED_COUNT, CRGB::Black);
}

static void wsFillSolid() {
  fill_solid(wsLeds, WS_LED_COUNT, wsColor1());
}

static void wsFillPress(uint16_t keyState) {
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = ((keyState >> i) & 1) ? c : CRGB::Black;
  }
}

static void wsFillPressFade(uint16_t keyState) {
  CRGB c = wsColor1();
  uint8_t fadeBy = wsSpeedFade();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsLeds[i] = c;
    else fadeToBlackBy(&wsLeds[i], 1, fadeBy);
  }
}

static void wsFillTypingHeat(uint16_t keyState) {
  uint8_t cool = 2 + (ledParams.speed >> 1);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsHeat[i] = 255;
    else wsHeat[i] = qsub8(wsHeat[i], cool);
    wsLeds[i] = HeatColor(wsHeat[i]);
  }
}

// 彩虹：按物理列/行错开色相，看起来是斜向流动
static void wsFillRainbow() {
  uint8_t step = wsSpeedStep();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t hue = wsPhase + wsKeyCol(i) * 42 + wsKeyRow(i) * 85;
    wsLeds[i] = CHSV(hue, 255, 255);
  }
  wsPhase += step;
}

static void wsFillBreath() {
  uint8_t bpm   = 6 + ledParams.speed * 3;
  uint8_t level = beatsin8(bpm, 16, 255);
  fill_solid(wsLeds, WS_LED_COUNT, wsColor1());
  nscale8(wsLeds, WS_LED_COUNT, level);
}

// 波浪：相位按列推进，同一列的灯同步
static void wsFillWave() {
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = c;
    wsLeds[i].nscale8_video(sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20));
  }
  wsPhase += 4 + (ledParams.speed >> 1);
}

static void wsFillSparkle(uint16_t keyState) {
  CRGB c = wsColor1();
  fadeToBlackBy(wsLeds, WS_LED_COUNT, wsSpeedFade());
  if (random8() < 40) {
    wsLeds[random8(WS_LED_COUNT)] = c;
  }
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsLeds[i] = c;
  }
}

/*
  涟漪：松开某键时以它为波源，按物理距离（2×3 布局的曼哈顿距离）延迟到达，
  亮度只由"年龄 - 距离"决定，不做亮度叠加，所以一定会衰减到 0。
  之前"每帧把亮度往邻居推一点"的写法，推送量大于每帧衰减量，会自我维持不灭。
*/
static void wsFillRipple(uint16_t keyState) {
  uint16_t released = wsPrevKeys & ~keyState;
  if (released) {
    // 一帧内多个键同时松开时，取编号最大的那个当波源（多波源会互相叠加，没必要）
    for (int8_t i = WS_LED_COUNT - 1; i >= 0; i--) {
      if ((released >> i) & 1) { wsRippleKey = i; wsRippleAge = 0; break; }
    }
  }

  CRGB c = wsColor1();
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
    wsLeds[i] = c;
    wsLeds[i].nscale8_video(level);
    if ((keyState >> i) & 1) wsLeds[i] = c;      // 按住的键常亮，不参与涟漪
  }

  if (wsRippleKey >= 0) {
    wsRippleAge++;
    if (wsRippleAge > 40) { wsRippleKey = -1; wsRippleAge = 0; }
  }
}

// 跑马灯：color2 的小灯珠按顺序跑，后面拖两格渐暗的尾巴
static void wsFillComet() {
  fadeToBlackBy(wsLeds, WS_LED_COUNT, 48);
  CRGB c = wsColor2();
  wsLeds[wsCometPos] = c;

  // 拖尾：前两格，按速度给不同亮度
  uint8_t tail = 255 - (ledParams.speed * 8);
  for (uint8_t k = 1; k <= 2; k++) {
    int8_t idx = (int8_t)wsCometPos - (int8_t)k;
    if (idx < 0) idx += WS_LED_COUNT;
    uint8_t lv = (uint8_t)scale8(pgm_read_byte(&wsCometTrail[k - 1]), tail);
    wsLeds[idx] = c;
    wsLeds[idx].nscale8_video(lv);
  }

  // 速度 -> 每隔几帧走一格（1 = 最慢，20 = 最快）
  wsCometHold++;
  if (wsCometHold >= (uint8_t)(11 - (ledParams.speed >> 1))) {
    wsCometHold = 0;
    wsCometPos = (wsCometPos + 1) % WS_LED_COUNT;
  }
}

// 双色呼吸：在 color1 与 color2 之间来回渐变
static void wsFillDualBreath() {
  uint8_t bpm = 5 + ledParams.speed * 2;
  uint8_t t   = beatsin8(bpm, 0, 255);
  fill_solid(wsLeds, WS_LED_COUNT, blend(wsColor1(), wsColor2(), t));
}

// ---------------------------------------------------------------------------
// 主刷新：每帧算一次颜色 + show 一次；主循环里直接调，内部自己限速
// ---------------------------------------------------------------------------
void wsTick(uint16_t keyState) {
  uint32_t now = millis();
  if (now - wsLastFrameMs < WS_FRAME_MS) return;
  wsLastFrameMs = now;

  switch (ledParams.effect) {
    case LED_EFFECT_OFF:         wsFillOff();               break;
    case LED_EFFECT_SOLID:       wsFillSolid();             break;
    case LED_EFFECT_PRESS:       wsFillPress(keyState);     break;
    case LED_EFFECT_PRESS_FADE:  wsFillPressFade(keyState); break;
    case LED_EFFECT_TYPING_HEAT: wsFillTypingHeat(keyState);break;
    case LED_EFFECT_RAINBOW:     wsFillRainbow();           break;
    case LED_EFFECT_BREATH:      wsFillBreath();            break;
    case LED_EFFECT_WAVE:        wsFillWave();              break;
    case LED_EFFECT_SPARKLE:     wsFillSparkle(keyState);   break;
    case LED_EFFECT_RIPPLE:      wsFillRipple(keyState);    break;
    case LED_EFFECT_COMET:       wsFillComet();             break;
    case LED_EFFECT_DUAL_BREATH: wsFillDualBreath();        break;
    default:                     wsFillPress(keyState);     break;
  }

  FastLED.show();
  wsPrevKeys = keyState;
}

#endif
