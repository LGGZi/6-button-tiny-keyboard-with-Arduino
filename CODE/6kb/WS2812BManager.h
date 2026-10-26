#ifndef WS2812BManager_h
#define WS2812BManager_h

/*
  WS2812B 灯效管理（六键键盘，6 颗灯，数据脚 D4，物理排布 2 行 × 3 列）

      0 1 2
      3 4 5

  灯效按三组编号（新增只能往后加）：
    0 ~  9   单色（只用 color1）
   10 ~ 14   炫彩（HSV 彩虹，不用颜色参数）
   15 ~ 22   双色（color1 ↔ color2）

  动画全部按“时间”步进，不按帧数：
    · 每帧先算出 dt（毫秒），再把 speed 换算成这一帧该走多少；
    · dt 限幅在 8~60ms —— 刷新率低于 ~16fps 时动画会变慢，但不会跳变/闪黑；
    · 拖尾、余辉、涟漪的时长都由毫秒决定，所以调刷新率不会改变观感速度；
    · 彗星类效果每帧“清空 + 重画头尾”，不再靠 fadeToBlackBy 拖尾
      （旧写法在低刷新率下头会在移动到下一格之前就淡成黑色，看起来就是闪）。

  参数集中在 ledParams（LEDParams_t），EEPROM 里就是这 10 个字节。
*/

#include <FastLED.h>
#include "Universal.h"

// ---------------------------------------------------------------------------
// 灯效编号（三组）
// ---------------------------------------------------------------------------
// ---- 单色（color1）----
#define LED_EFFECT_OFF             0
#define LED_EFFECT_SOLID           1
#define LED_EFFECT_PRESS           2
#define LED_EFFECT_PRESS_FADE      3
#define LED_EFFECT_TYPING_HEAT     4
#define LED_EFFECT_BREATH          5
#define LED_EFFECT_WAVE            6
#define LED_EFFECT_SPARKLE         7
#define LED_EFFECT_RIPPLE          8
#define LED_EFFECT_COMET_MONO      9

// ---- 炫彩 ----
#define LED_EFFECT_RAINBOW        10
#define LED_EFFECT_RAINBOW_BREATH 11
#define LED_EFFECT_RAINBOW_WAVE   12
#define LED_EFFECT_RAINBOW_SPARKLE 13
#define LED_EFFECT_COMET_RAINBOW  14

// ---- 双色 ----
#define LED_EFFECT_DUAL_BREATH    15
#define LED_EFFECT_DUAL_PRESS     16
#define LED_EFFECT_DUAL_PRESS_FADE 17
#define LED_EFFECT_DUAL_WAVE      18
#define LED_EFFECT_DUAL_COMET     19
#define LED_EFFECT_DUAL_TYPING    20
#define LED_EFFECT_DUAL_GRADIENT  21
#define LED_EFFECT_DUAL_RIPPLE    22
#define LED_EFFECT_COUNT          23

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

// 时间步进的基准：20ms（50fps）时 speed=8 的效果“走一步”
#define WS_DT_REF_MS        20
// dt 限幅：太低刷新率时宁可动画变慢，也不要一帧跳很远
#define WS_DT_MIN_MS        8
#define WS_DT_MAX_MS        60

// ---------------------------------------------------------------------------
// 参数结构体（EEPROM 里就是这 10 个字节）
// ---------------------------------------------------------------------------
#define LED_PARAMS_MAGIC    0x5B
// 保存指令的确认字节：和结体内 magic 分开，改结构体布局时不会再把保存弄坏
#define LED_SAVE_CONFIRM    0x5A

typedef struct LEDparams {
  uint8_t magic;
  uint8_t effect;
  uint8_t brightness;
  uint8_t speed;
  uint8_t fps;
  uint8_t color1[3];
  uint8_t color2[3];
} LEDParams_t;

LEDParams_t ledParams;
CRGB wsLeds[WS_LED_COUNT];

// 帧调度 + 效果状态（不进 EEPROM）
static uint32_t wsLastFrameMs = 0;
static uint16_t wsDt          = WS_DT_REF_MS;   // 本帧经过的毫秒（限幅后）
static uint8_t  wsPhase       = 0;
static uint8_t  wsHue         = 0;
static uint8_t  wsHeat[WS_LED_COUNT];
static uint8_t  wsGlow[WS_LED_COUNT];            // 双色按下后的副色余量（0 = 纯主色）
static uint8_t  wsBreathPhase = 0;               // 双色呼吸相位（0~255 循环）
static uint8_t  wsCometPos    = 0;
static uint16_t wsCometAcc    = 0;              // 彗星走格累计毫秒
static uint16_t wsPrevKeys    = 0;
static int8_t   wsRippleKey   = -1;
static uint16_t wsRippleAge   = 0;              // 涟漪年龄（毫秒）

// 彗星亮度-距离曲线（PROGMEM，4 个采样点：0/1/2/3 格）
static const uint8_t wsCometCurve[4] PROGMEM = { 255, 140, 50, 0 };

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

// dt 换算：把“每 20ms 前进 1”的量，换算成这一帧的量（至少 1，避免卡住不动）
static inline uint8_t wsScaleByDt(uint8_t perRef) {
  uint16_t v = (uint16_t)perRef * wsDt / WS_DT_REF_MS;
  if (v < 1) v = 1;
  if (v > 255) v = 255;
  return (uint8_t)v;
}

// 相位步进：speed 1~20 -> 每 20ms 走 1~11
static inline uint8_t wsPhaseStep() { return wsScaleByDt(1 + (ledParams.speed >> 1)); }

// 渐隐量：每 20ms 淡掉 12~72，按 dt 缩放（上限 200，避免一帧直接清零）
static inline uint8_t wsFadeStep() {
  uint16_t v = (uint16_t)(12 + ledParams.speed * 3) * wsDt / WS_DT_REF_MS;
  if (v < 6) v = 6;
  if (v > 200) v = 200;
  return (uint8_t)v;
}

/*
  精确线性插值：t=0 取 a，t=255 取 b。
  不用 FastLED 的 blend()：AVR 上它走汇编实现，而且我们只需要“逐通道线性”这一种语义，
  自己算既确定又快（除以 255 编译器会转成乘移位）。
*/
static inline uint8_t wsMix8(uint8_t a, uint8_t b, uint8_t t) {
  if (t == 0)   return a;
  if (t == 255) return b;
  uint16_t inv = (uint16_t)(255 - t);
  return (uint8_t)(((uint16_t)a * inv + (uint16_t)b * t + 127) / 255);
}

static inline CRGB wsMix(CRGB a, CRGB b, uint8_t t) {
  return CRGB(wsMix8(a.r, b.r, t), wsMix8(a.g, b.g, t), wsMix8(a.b, b.b, t));
}

/*
  亮度乘一个系数（level=0 全黑，255 保持原亮度），不改色相。
  注意：CRGB::nscale8_video(x) 的 x 是“减暗量”，不是亮度系数，
  这里必须用自由函数 scale8_video(i, level)（乘 level/255）。
*/
static inline uint8_t wsScale(uint8_t v, uint8_t level) {
  if (level >= 255) return v;
  return scale8(v, level);        // scale8 会真正到 0；scale8_video 最低留 1，永远关不灭
}

static inline CRGB wsDim(CRGB c, uint8_t level) {
  c.r = wsScale(c.r, level);
  c.g = wsScale(c.g, level);
  c.b = wsScale(c.b, level);
  return c;
}

// ---------------------------------------------------------------------------
// 参数接口
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

void wsApplyParams() { FastLED.setBrightness(ledParams.brightness); }

void wsInit() {
  FastLED.addLeds<WS2812, WS_PIN, GRB>(wsLeds, WS_LED_COUNT);
  //关掉亮度抖动：亮度 < 255 时 FastLED 会逐帧加 1 LSB 的噪声，
  //在低亮度、渐变色上看起来像"闪一下别的颜色"
  FastLED.setDither(DISABLE_DITHER);
  FastLED.clear(true);
  memset(wsHeat, 0, sizeof(wsHeat));
  wsPrevKeys  = 0;
  wsRippleKey = -1;
  wsRippleAge = 0;
  wsCometPos  = 0;
  wsCometAcc  = 0;
  wsApplyParams();
}

void wsSetEffect(uint8_t effect) {
  if (effect >= LED_EFFECT_COUNT) return;
  //同一个效果只是改了颜色/亮度/速度时，保留相位和状态，不要重置（否则画面会跳一帧）
  if (effect == ledParams.effect) return;
  ledParams.effect = effect;
  wsPhase = 0;
  wsHue   = 0;
  memset(wsHeat, 0, sizeof(wsHeat));
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) wsGlow[i] = 0;
  wsBreathPhase = 0;
  wsRippleKey = -1;
  wsRippleAge = 0;
  wsCometPos  = 0;
  wsCometAcc  = 0;
  //注意：这里只清缓冲区，不 show()。show() 会先黑一帧，看起来就是"切换时闪一下"
  fill_solid(wsLeds, WS_LED_COUNT, CRGB::Black);   // memset 会被 FastLED 的 fl::memset 抢占，用 fill_solid
}

void wsSetBrightness(uint8_t b) { ledParams.brightness = b; FastLED.setBrightness(b); }

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
// 彗星（单色 / 炫彩 / 双色共用）
// 每帧清空重画，头永远最亮，尾巴固定两格；走格按毫秒累计。
// ---------------------------------------------------------------------------
static void wsDrawComet(uint8_t mode, uint16_t keyState) {
  uint16_t moveMs = 380 - (uint16_t)ledParams.speed * 15;

  //位置按毫秒累计；frac 是"距离下一格还剩多少"（0~255），用来做跨格插值
  wsCometAcc += wsDt;
  while (wsCometAcc >= moveMs) {
    wsCometAcc -= moveMs;
    wsCometPos = (wsCometPos + 1) % WS_LED_COUNT;
  }
  uint8_t frac = (uint8_t)((uint32_t)wsCometAcc * 255UL / moveMs);

  CRGB c1 = wsColor1(), c2 = wsColor2();
  int16_t head256 = (int16_t)wsCometPos * 256 + (int16_t)frac;

  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    //环形距离，单位 1/256 格
    int16_t d = (int16_t)i * 256 - head256;
    if (d < 0) d = -d;
    if (d > (int16_t)(WS_LED_COUNT * 128)) d = (int16_t)(WS_LED_COUNT * 256) - d;

    uint8_t level = 0;
    if (d < 768) {
      uint8_t seg = (uint8_t)(d >> 8);
      uint8_t f8  = (uint8_t)(d & 0xFF);
      uint8_t a = pgm_read_byte(&wsCometCurve[seg]);
      uint8_t b = pgm_read_byte(&wsCometCurve[seg + 1]);
      level = (uint8_t)((int16_t)a + (((int16_t)b - (int16_t)a) * (int16_t)f8) / 256);
    }

    if (mode == 0) {
      wsLeds[i] = wsDim(c1, level);
    } else if (mode == 1) {
      wsLeds[i] = CHSV(wsHue + (uint8_t)(d >> 5), 255, level);
    } else {
      wsLeds[i] = wsMix(c1, c2, level);       // 头 = 副色，尾巴过渡回主色
    }
  }

  //单色彗星：按下的键额外常亮
  if (mode == 0) {
    for (uint8_t i = 0; i < WS_LED_COUNT; i++)
      if ((keyState >> i) & 1) wsLeds[i] = c1;
  }

  wsHue += wsPhaseStep();
}

// ---------------------------------------------------------------------------
// 涟漪（单色 / 双色共用）
// 只做一次：松手那一帧启动，波前按物理距离延迟到达，亮度只和"年龄-距离"有关。
// 一次涟漪结束前不会再次触发（防止按键抖动造成来回闪）。
// ---------------------------------------------------------------------------
static void wsDrawRipple(bool dual, uint16_t keyState) {
  if (wsRippleKey < 0) {
    uint16_t released = wsPrevKeys & ~keyState;
    for (int8_t i = WS_LED_COUNT - 1; i >= 0; i--) {
      if ((released >> i) & 1) { wsRippleKey = i; wsRippleAge = 0; break; }
    }
  }

  CRGB c1 = wsColor1(), c2 = wsColor2();
  CRGB idle = CRGB::Black;      // 待机完全熄灭，涟漪结束后不残留

  //距离延迟和衰减时长都跟随 speed（1 → 78ms/格、1.75s；20 → 40ms/格、0.8s）
  uint16_t delayMs = 80 - (uint16_t)ledParams.speed * 2;
  uint16_t durMs   = 1800 - (uint16_t)ledParams.speed * 50;

  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t level = 0;
    if (wsRippleKey >= 0) {
      int16_t age = (int16_t)wsRippleAge - (int16_t)wsKeyDistance((uint8_t)wsRippleKey, i) * (int16_t)delayMs;
      if (age > 0) {
        int16_t v = 255 - (int16_t)((uint32_t)age * 255 / durMs);
        if (v > 0) level = (uint8_t)v;
      }
    }
    if (level > 0) wsLeds[i] = dual ? wsMix(c1, c2, level) : wsDim(c1, level);
    else           wsLeds[i] = idle;
    //单色涟漪保留"按住常亮"的反馈；双色涟漪不加，否则松手瞬间会从主色跳到副色
    if (!dual && ((keyState >> i) & 1)) wsLeds[i] = c1;
  }

  if (wsRippleKey >= 0) {
    wsRippleAge += wsDt;
    if (wsRippleAge > 1400) { wsRippleKey = -1; wsRippleAge = 0; }
  }
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
  uint8_t fadeBy = wsFadeStep();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsLeds[i] = c;
    else wsLeds[i] = wsDim(wsLeds[i], 255 - fadeBy);
  }
}

// 打字热度（单色）：按下累加热度，颜色 = 主色 × 热度；离开后慢慢冷却
static void wsTypingHeat(uint16_t keyState) {
  uint8_t hot  = wsScaleByDt(6 + ledParams.speed);          // 按住时每帧升温
  uint8_t cool = wsScaleByDt(1 + (ledParams.speed >> 1));   // 松开后每帧降温
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsHeat[i] = qadd8(wsHeat[i], hot);
    else wsHeat[i] = qsub8(wsHeat[i], cool);
    wsLeds[i] = wsDim(c, wsHeat[i]);
  }
}

// 三角波 0 -> 255 -> 0（端点连续，替代 beatsin8 内部的 millis 计时）
static inline uint8_t wsTriangle(uint8_t p) {
  return (p < 128) ? (uint8_t)(p * 2) : (uint8_t)((255 - p) * 2);
}

static void wsBreath() {
  uint8_t t = wsTriangle(wsBreathPhase);
  uint8_t level = (uint8_t)(16 + (uint16_t)t * 239 / 255);      // 16~255
  fill_solid(wsLeds, WS_LED_COUNT, wsColor1());
  nscale8(wsLeds, WS_LED_COUNT, level);
  wsBreathPhase += wsScaleByDt(1 + (ledParams.speed >> 2));
}

static void wsWave() {
  CRGB c = wsColor1();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    wsLeds[i] = wsDim(c, sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20));
  }
  wsPhase += wsPhaseStep();
}

static void wsSparkle(uint16_t keyState) {
  CRGB c = wsColor1();
  uint8_t fadeBy = wsFadeStep();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) wsLeds[i] = wsDim(wsLeds[i], 255 - fadeBy);
  if (random8() < 40) wsLeds[random8(WS_LED_COUNT)] = c;
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    if ((keyState >> i) & 1) wsLeds[i] = c;
}

// ---------------------------------------------------------------------------
// 炫彩组
// ---------------------------------------------------------------------------
static void wsRainbow() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = CHSV(wsHue + wsKeyCol(i) * 42 + wsKeyRow(i) * 85, 255, 255);
  wsHue += wsPhaseStep();
}

static void wsRainbowBreath() {
  uint8_t t = wsTriangle(wsBreathPhase);
  uint8_t level = (uint8_t)(20 + (uint16_t)t * 235 / 255);      // 20~255
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = CHSV(wsHue + wsKeyCol(i) * 30 + wsKeyRow(i) * 60, 255, level);
  wsBreathPhase += wsScaleByDt(1 + (ledParams.speed >> 2));
  wsHue += wsPhaseStep();
}

static void wsRainbowWave() {
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t hue = wsHue + wsKeyCol(i) * 50 + wsKeyRow(i) * 20;
    uint8_t v   = sin8(wsPhase + wsKeyCol(i) * 60 + wsKeyRow(i) * 20);
    wsLeds[i] = CHSV(hue, 255, v);
  }
  wsPhase += wsPhaseStep();
  wsHue   += 2;
}

static void wsRainbowSparkle(uint16_t keyState) {
  uint8_t fadeBy = wsFadeStep();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) wsLeds[i] = wsDim(wsLeds[i], 255 - fadeBy);
  if (random8() < 48) wsLeds[random8(WS_LED_COUNT)] = CHSV(random8(), 255, 255);
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    if ((keyState >> i) & 1) wsLeds[i] = CHSV(wsHue + i * 40, 255, 255);
  wsHue += 2;
}

// ---------------------------------------------------------------------------
// 双色组
// ---------------------------------------------------------------------------
/*
  双色呼吸：三角波 0 -> 255 -> 0，端点精确、严格单调，
  不依赖 sin 表的偏移/量化，也就不会在周期交界出现“闪一下别的颜色”。
*/
static void wsDualBreath() {
  uint8_t t = wsTriangle(wsBreathPhase);
  fill_solid(wsLeds, WS_LED_COUNT, wsMix(wsColor1(), wsColor2(), t));
  wsBreathPhase += wsScaleByDt(1 + (ledParams.speed >> 2));
}

static void wsDualPress(uint16_t keyState) {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++)
    wsLeds[i] = ((keyState >> i) & 1) ? c2 : c1;
}

/*
  按下 = 副色；松开后该键的“副色余量”按时间衰减回 0。
  背景（余量 0）永远是纯主色，亮度不会随余量变化，也不会污染别的键。
*/
static void wsDualPressFade(uint16_t keyState) {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  uint8_t fade = wsScaleByDt(4 + ledParams.speed);       // 每 20ms 5~24
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    //按下立刻满副色，松开后线性衰减；背景（余量 0）= 纯主色，亮度不变
    if ((keyState >> i) & 1) wsGlow[i] = 255;
    else wsGlow[i] = qsub8(wsGlow[i], fade);
    wsLeds[i] = wsMix(c1, c2, wsGlow[i]);
  }
}

static void wsDualWave() {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    //按列错开 85（一整圈的三分之一），行再错 30；sin8 直接输出 0~255
    uint8_t t = sin8(wsPhase + wsKeyCol(i) * 85 + wsKeyRow(i) * 30);
    wsLeds[i] = wsMix(c1, c2, t);
  }
  wsPhase += wsScaleByDt(2 + (ledParams.speed >> 1));
}

/*
  双色打字：冷 = color2，热 = color1。
  和"双色按下+渐回"的区别：这里是**累积**——每按一次加一截热度，越按越接近主色，
  松手后慢慢冷却；按下渐变则是"按下立刻副色、松开线性回到主色"的一次性包络。
*/
static void wsDualTyping(uint16_t keyState) {
  uint8_t hot  = wsScaleByDt(6 + ledParams.speed);
  uint8_t cool = wsScaleByDt(1 + (ledParams.speed >> 1));
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    if ((keyState >> i) & 1) wsHeat[i] = qadd8(wsHeat[i], hot);
    else wsHeat[i] = qsub8(wsHeat[i], cool);
    wsLeds[i] = wsMix(c2, c1, wsHeat[i]);
  }
}

static void wsDualGradient() {
  CRGB c1 = wsColor1(), c2 = wsColor2();
  for (uint8_t i = 0; i < WS_LED_COUNT; i++) {
    uint8_t t = (uint8_t)((uint16_t)wsKeyCol(i) * 255 / (LED_KEY_COLS - 1));   // 0 / 127 / 255
    wsLeds[i] = wsMix(c1, c2, t);
  }
}

// ---------------------------------------------------------------------------
// 主刷新
// ---------------------------------------------------------------------------
void wsTick(uint16_t keyState) {
  uint32_t now = getFrameTime();
  uint32_t dtRaw = now - wsLastFrameMs;

  // 还没到刷新间隔
  uint16_t interval = (uint16_t)(1000UL / (ledParams.fps ? ledParams.fps : LED_FPS_DEFAULT));
  if (interval == 0) interval = 1;
  if (dtRaw < interval) return;

  wsLastFrameMs = now;
  if (dtRaw < WS_DT_MIN_MS) dtRaw = WS_DT_MIN_MS;
  if (dtRaw > WS_DT_MAX_MS) dtRaw = WS_DT_MAX_MS;
  wsDt = (uint16_t)dtRaw;

  switch (ledParams.effect) {
    // ---- 单色 ----
    case LED_EFFECT_OFF:              wsOff();                break;
    case LED_EFFECT_SOLID:            wsSolid();              break;
    case LED_EFFECT_PRESS:            wsPress(keyState);      break;
    case LED_EFFECT_PRESS_FADE:       wsPressFade(keyState);  break;
    case LED_EFFECT_TYPING_HEAT:      wsTypingHeat(keyState); break;
    case LED_EFFECT_BREATH:           wsBreath();             break;
    case LED_EFFECT_WAVE:             wsWave();               break;
    case LED_EFFECT_SPARKLE:          wsSparkle(keyState);    break;
    case LED_EFFECT_RIPPLE:           wsDrawRipple(false, keyState); break;
    case LED_EFFECT_COMET_MONO:       wsDrawComet(0, keyState); break;
    // ---- 炫彩 ----
    case LED_EFFECT_RAINBOW:          wsRainbow();            break;
    case LED_EFFECT_RAINBOW_BREATH:   wsRainbowBreath();      break;
    case LED_EFFECT_RAINBOW_WAVE:     wsRainbowWave();        break;
    case LED_EFFECT_RAINBOW_SPARKLE:  wsRainbowSparkle(keyState); break;
    case LED_EFFECT_COMET_RAINBOW:    wsDrawComet(1, keyState); break;
    // ---- 双色 ----
    case LED_EFFECT_DUAL_BREATH:      wsDualBreath();         break;
    case LED_EFFECT_DUAL_PRESS:       wsDualPress(keyState);  break;
    case LED_EFFECT_DUAL_PRESS_FADE:  wsDualPressFade(keyState); break;
    case LED_EFFECT_DUAL_WAVE:        wsDualWave();           break;
    case LED_EFFECT_DUAL_COMET:       wsDrawComet(2, keyState); break;
    case LED_EFFECT_DUAL_TYPING:      wsDualTyping(keyState); break;
    case LED_EFFECT_DUAL_GRADIENT:    wsDualGradient();       break;
    case LED_EFFECT_DUAL_RIPPLE:      wsDrawRipple(true, keyState); break;
    default:                          wsPress(keyState);      break;
  }

  FastLED.show();
  wsPrevKeys = keyState;
}

#endif
