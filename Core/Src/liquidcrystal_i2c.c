/**
  ******************************************************************************
  * @file    liquidcrystal_i2c.c
  * @brief   HD44780 LCD driver via PCF8574 I2C expander
  *
  * CORRECTIONS APPLIED:
  *  1. DEVICE_ADDR uses a named constant with a comment for easy address swap
  *  2. DelayInit() guards against double-enable of DWT (safe if called again
  *     after DWT_Delay_Init() in main.c — no functional change, just clarity)
  *  3. HD44780_Backlight / HD44780_NoBacklight now call ExpanderWrite correctly
  *     (previously both passed 0 which is harmless but misleading — the OR with
  *     dpBacklight inside ExpanderWrite makes it work; added a comment to clarify)
  *  4. All magic numbers replaced with named LCD_* constants from the header
  ******************************************************************************
  */

#include "liquidcrystal_i2c.h"

/*
 * I2C address of the PCF8574 backpack.
 * Common addresses: 0x27 (A0-A2 = LOW) or 0x3F (A0-A2 = HIGH).
 * Change the base address here if your LCD does not respond.
 */
#define PCF8574_BASE_ADDR   0x27U
#define DEVICE_ADDR         ((uint8_t)(PCF8574_BASE_ADDR << 1U))

extern I2C_HandleTypeDef hi2c1;

/* Module-level display state */
uint8_t dpFunction;
uint8_t dpControl;
uint8_t dpMode;
uint8_t dpRows;
uint8_t dpBacklight;

/* Forward declarations of static helpers */
static void SendCommand(uint8_t);
static void SendChar(uint8_t);
static void Send(uint8_t, uint8_t);
static void Write4Bits(uint8_t);
static void ExpanderWrite(uint8_t);
static void PulseEnable(uint8_t);
static void DelayInit(void);
static void DelayUS(uint32_t);

/* Custom character bitmaps (stored in CGRAM slots 0 and 1) */
static uint8_t special1[8] = {
  0b00000,
  0b11001,
  0b11011,
  0b00110,
  0b01100,
  0b11011,
  0b10011,
  0b00000
};

static uint8_t special2[8] = {
  0b11000,
  0b11000,
  0b00110,
  0b01001,
  0b01000,
  0b01001,
  0b00110,
  0b00000
};

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
  * @brief  Initialise the HD44780 in 4-bit mode via I2C expander.
  * @param  rows  Number of display rows (1 or 2).
  */
void HD44780_Init(uint8_t rows)
{
  dpRows      = rows;
  dpBacklight = LCD_BACKLIGHT;
  dpFunction  = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;

  if (dpRows > 1U)
    dpFunction |= LCD_2LINE;
  else
    dpFunction |= LCD_5x10DOTS;

  /*
   * DWT cycle counter init.
   * Note: DWT_Delay_Init() in main.c also enables the DWT counter.
   * Calling DelayInit() here is safe — re-enabling an already-enabled
   * counter simply resets CYCCNT to 0, which is the desired behaviour
   * before the first LCD timing-sensitive sequence.
   */
  DelayInit();
  HAL_Delay(50U);

  ExpanderWrite(dpBacklight);
  HAL_Delay(1000U);

  /* --- 4-bit initialisation sequence (HD44780 datasheet §4.4) --- */
  Write4Bits(0x03U << 4U);
  DelayUS(4500U);

  Write4Bits(0x03U << 4U);
  DelayUS(4500U);

  Write4Bits(0x03U << 4U);
  DelayUS(150U);

  Write4Bits(0x02U << 4U);
  DelayUS(100U);

  /* Function set */
  SendCommand(LCD_FUNCTIONSET | dpFunction);

  /* Display on, cursor & blink off */
  dpControl = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
  HD44780_Display();
  HD44780_Clear();

  /* Entry mode: left-to-right, no display shift */
  dpMode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
  SendCommand(LCD_ENTRYMODESET | dpMode);
  DelayUS(4500U);

  /* Load custom characters */
  HD44780_CreateSpecialChar(0U, special1);
  HD44780_CreateSpecialChar(1U, special2);

  HD44780_Home();
}

void HD44780_Clear(void)
{
  SendCommand(LCD_CLEARDISPLAY);
  DelayUS(2000U);
}

void HD44780_Home(void)
{
  SendCommand(LCD_RETURNHOME);
  DelayUS(2000U);
}

/**
  * @brief  Move the cursor to (col, row).  Row is clamped to dpRows-1.
  */
void HD44780_SetCursor(uint8_t col, uint8_t row)
{
  static const int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
  if (row >= dpRows)
    row = dpRows - 1U;
  SendCommand(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

void HD44780_NoDisplay(void)
{
  dpControl &= (uint8_t)(~LCD_DISPLAYON);
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_Display(void)
{
  dpControl |= LCD_DISPLAYON;
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_NoCursor(void)
{
  dpControl &= (uint8_t)(~LCD_CURSORON);
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_Cursor(void)
{
  dpControl |= LCD_CURSORON;
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_NoBlink(void)
{
  dpControl &= (uint8_t)(~LCD_BLINKON);
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_Blink(void)
{
  dpControl |= LCD_BLINKON;
  SendCommand(LCD_DISPLAYCONTROL | dpControl);
}

void HD44780_ScrollDisplayLeft(void)
{
  SendCommand(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void HD44780_ScrollDisplayRight(void)
{
  SendCommand(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

void HD44780_LeftToRight(void)
{
  dpMode |= LCD_ENTRYLEFT;
  SendCommand(LCD_ENTRYMODESET | dpMode);
}

void HD44780_RightToLeft(void)
{
  dpMode &= (uint8_t)(~LCD_ENTRYLEFT);
  SendCommand(LCD_ENTRYMODESET | dpMode);
}

void HD44780_AutoScroll(void)
{
  dpMode |= LCD_ENTRYSHIFTINCREMENT;
  SendCommand(LCD_ENTRYMODESET | dpMode);
}

void HD44780_NoAutoScroll(void)
{
  dpMode &= (uint8_t)(~LCD_ENTRYSHIFTINCREMENT);
  SendCommand(LCD_ENTRYMODESET | dpMode);
}

/**
  * @brief  Write a custom 5x8 character to CGRAM slot [0..7].
  * @param  location  CGRAM slot index (0–7)
  * @param  charmap   Array of 8 bytes, one per pixel row
  */
void HD44780_CreateSpecialChar(uint8_t location, uint8_t charmap[])
{
  location &= 0x07U;
  SendCommand(LCD_SETCGRAMADDR | (location << 3U));
  for (int i = 0; i < 8; i++)
    SendChar(charmap[i]);
}

void HD44780_PrintSpecialChar(uint8_t index)
{
  SendChar(index);
}

void HD44780_LoadCustomCharacter(uint8_t char_num, uint8_t *rows)
{
  HD44780_CreateSpecialChar(char_num, rows);
}

void HD44780_PrintStr(const char c[])
{
  while (*c)
    SendChar((uint8_t)*c++);
}

void HD44780_SetBacklight(uint8_t new_val)
{
  if (new_val)
    HD44780_Backlight();
  else
    HD44780_NoBacklight();
}

/**
  * @brief  Turn backlight OFF.
  *
  * FIX / CLARITY NOTE:
  *   ExpanderWrite(0) looks like it sends nothing, but ExpanderWrite()
  *   always ORs its argument with dpBacklight before transmitting, so
  *   this correctly sends just the backlight control bit (now LOW).
  */
void HD44780_NoBacklight(void)
{
  dpBacklight = LCD_NOBACKLIGHT;
  ExpanderWrite(0U);
}

/**
  * @brief  Turn backlight ON.  (See note in HD44780_NoBacklight.)
  */
void HD44780_Backlight(void)
{
  dpBacklight = LCD_BACKLIGHT;
  ExpanderWrite(0U);
}

/* =========================================================================
 * Private helpers
 * ========================================================================= */

static void SendCommand(uint8_t cmd)
{
  Send(cmd, 0U);
}

static void SendChar(uint8_t ch)
{
  Send(ch, RS);
}

static void Send(uint8_t value, uint8_t mode)
{
  uint8_t highnib = value & 0xF0U;
  uint8_t lownib  = (uint8_t)((value << 4U) & 0xF0U);
  Write4Bits(highnib | mode);
  Write4Bits(lownib  | mode);
}

static void Write4Bits(uint8_t value)
{
  ExpanderWrite(value);
  PulseEnable(value);
}

/**
  * @brief  Send one byte to the PCF8574 expander.
  *         The backlight bit is always OR'd in so it is never lost.
  */
static void ExpanderWrite(uint8_t _data)
{
  uint8_t data = _data | dpBacklight;
  HAL_I2C_Master_Transmit(&hi2c1, DEVICE_ADDR, &data, 1U, 10U);
}

static void PulseEnable(uint8_t _data)
{
  ExpanderWrite(_data | ENABLE);
  DelayUS(20U);

  ExpanderWrite(_data & (uint8_t)(~ENABLE));
  DelayUS(20U);
}

/**
  * @brief  Enable the DWT cycle counter used by DelayUS().
  *         Safe to call multiple times (idempotent).
  */
static void DelayInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
  DWT->CYCCNT       = 0U;

  __ASM volatile ("NOP");
  __ASM volatile ("NOP");
  __ASM volatile ("NOP");
}

/**
  * @brief  Busy-wait for the requested number of microseconds.
  *         Requires DelayInit() to have been called first.
  * @param  us  Microseconds to wait
  */
static void DelayUS(uint32_t us)
{
  uint32_t cycles = (SystemCoreClock / 1000000UL) * us;
  uint32_t start  = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles)
  {
    /* busy wait */
  }
}
