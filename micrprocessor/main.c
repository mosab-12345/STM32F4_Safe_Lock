/* =====================================================================
   SMART SAVE CASE - STM32F103 (Blue Pill)
   ---------------------------------------------------------------------
   Hardware (from wiring table):
     Pot 1            -> PA0  (ADC1 IN0)
     Pot 2            -> PA1  (ADC1 IN1)
     Pot 3            -> PA2  (ADC1 IN2)
     Open Button      -> PB0  (active LOW, internal pull-up)
     Change Pwd Button-> PB1  (active LOW, internal pull-up)
     Servo Signal     -> PA8  (TIM1 CH1, PWM)
     Green LED        -> PB12
     Red LED          -> PB13
     Blue LED         -> PB14
     LCD SDA          -> PB7  (software I2C)
     LCD SCL          -> PB6  (software I2C)
     Buzzer           -> PB15
     Reset Button      -> PB8  (active LOW, internal pull-up)  *** confirm/move if your wiring differs ***

   LOGIC SUMMARY
   ---------------------------------------------------------------------
   NORMAL MODE:
     - LCD continuously shows live Pot1/Pot2/Pot3 values mapped 0-10.
     - Press OPEN button:
         * If door is CLOSED -> check pots against saved targets.
             - Correct  -> Green LED ON, happy beep, servo -> 90 deg (door OPEN)
             - Wrong    -> Red LED ON (briefly), sad beep, stays CLOSED
         * If door is OPEN -> servo back to 0 deg (door CLOSED), LEDs off.
     - Press CHANGE PASSWORD button -> enter SET PASSWORD mode.

   SET PASSWORD MODE (Blue LED ON):
     - Rotate Pot1 to desired value, press OPEN -> saves target1, confirm beep.
     - Rotate Pot2 to desired value, press OPEN -> saves target2, confirm beep.
     - Rotate Pot3 to desired value, press OPEN -> saves target3, confirm beep,
       exits back to NORMAL MODE, Blue LED OFF, long confirm beep.

   RESET BUTTON:
     - Forces door CLOSED (servo -> 0 deg), clears LEDs, cancels SET mode,
       returns to NORMAL MODE. Does NOT erase the saved password.

   Notes:
     - Register-level (CMSIS) code -> no HAL/SPL dependency, drop straight
       into a bare Keil "main.c" with startup file + system_stm32f1xx.
     - LCD is driven via SOFTWARE (bit-banged) I2C on PB6/PB7, talking to a
       PCF8574-based LCD1602 I2C backpack (default address 0x27, change
       LCD_ADDR below if yours is 0x3F).
   ===================================================================== */

#include "stm32f10x.h"                  // CMSIS device header (Keil STM32F1 pack)
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------
   USER CONFIG
   --------------------------------------------------------------------- */
#define LCD_ADDR        0x27           /* PCF8574 I2C address (7-bit). Try 0x3F if 0x27 doesn't work */
#define ADC_MAX         4095            /* 12-bit ADC max reading */
#define VALUE_MAX       10              /* displayed pot value range 0..10 */
#define DEFAULT_TARGET  5               /* default target for all pots before first password set */

/* ---------------------------------------------------------------------
   Simple blocking delay (based on SysTick-free busy loop, calibrated
   roughly for 72 MHz HSE/PLL system clock). For class-project purposes
   this is fine; swap in SysTick-based delay if you need more accuracy.
   --------------------------------------------------------------------- */
static void delay_us(uint32_t us)
{
    /* crude busy-wait, ~72 cycles per loop at -O0 on 72MHz core */
    volatile uint32_t count = us * 8;
    while (count--) { __NOP(); }
}

static void delay_ms(uint32_t ms)
{
    while (ms--) delay_us(1000);
}

/* =======================================================================
   SOFTWARE I2C (bit-banged) on PB6 = SCL, PB7 = SDA
   ======================================================================= */
#define I2C_SCL_PIN   6
#define I2C_SDA_PIN   7

static void i2c_scl_high(void) { GPIOB->BSRR = (1 << I2C_SCL_PIN); }
static void i2c_scl_low(void)  { GPIOB->BSRR = (1 << (I2C_SCL_PIN + 16)); }
static void i2c_sda_high(void) { GPIOB->BSRR = (1 << I2C_SDA_PIN); }
static void i2c_sda_low(void)  { GPIOB->BSRR = (1 << (I2C_SDA_PIN + 16)); }

static void i2c_init(void)
{
    /* PB6, PB7 as open-drain outputs, 2MHz, with external pull-ups
       (most I2C LCD backpacks already have 4.7k pull-ups onboard) */
    GPIOB->CRL &= ~((0xF << (4 * I2C_SCL_PIN)) | (0xF << (4 * I2C_SDA_PIN)));
    GPIOB->CRL |=  ((0x7 << (4 * I2C_SCL_PIN)) | (0x7 << (4 * I2C_SDA_PIN))); /* 0b0111 = open-drain, 2MHz */
    i2c_scl_high();
    i2c_sda_high();
}

static void i2c_start(void)
{
    i2c_sda_high();
    i2c_scl_high();
    delay_us(5);
    i2c_sda_low();
    delay_us(5);
    i2c_scl_low();
}

static void i2c_stop(void)
{
    i2c_sda_low();
    i2c_scl_high();
    delay_us(5);
    i2c_sda_high();
    delay_us(5);
}

static void i2c_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++)
    {
        if (data & 0x80) i2c_sda_high(); else i2c_sda_low();
        data <<= 1;
        delay_us(3);
        i2c_scl_high();
        delay_us(5);
        i2c_scl_low();
        delay_us(3);
    }
    /* ACK clock - we don't check it, just pulse */
    i2c_sda_high();
    delay_us(3);
    i2c_scl_high();
    delay_us(5);
    i2c_scl_low();
}

/* =======================================================================
   LCD1602 over PCF8574 I2C backpack
   PCF8574 bit mapping (typical / most common backpack wiring):
     P0 = RS, P1 = RW, P2 = EN, P3 = Backlight, P4..P7 = D4..D7
   ======================================================================= */
#define LCD_BACKLIGHT 0x08
#define LCD_EN        0x04
#define LCD_RW        0x02
#define LCD_RS        0x01

static void lcd_write4(uint8_t nibble, uint8_t rs)
{
    uint8_t data = (nibble & 0xF0) | LCD_BACKLIGHT | (rs ? LCD_RS : 0);
    i2c_start();
    i2c_write_byte((LCD_ADDR << 1));
    i2c_write_byte(data | LCD_EN);
    delay_us(1);
    i2c_write_byte(data & ~LCD_EN);
    i2c_stop();
    delay_us(50);
}

static void lcd_send_byte(uint8_t value, uint8_t rs)
{
    lcd_write4(value & 0xF0, rs);
    lcd_write4((value << 4) & 0xF0, rs);
}

static void lcd_cmd(uint8_t cmd)  { lcd_send_byte(cmd, 0); }
static void lcd_data(uint8_t ch)  { lcd_send_byte(ch, 1); }

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t row_offsets[] = { 0x00, 0x40 };
    lcd_cmd(0x80 | (col + row_offsets[row]));
}

static void lcd_print(const char *str)
{
    while (*str) lcd_data((uint8_t)*str++);
}

static void lcd_clear(void)
{
    lcd_cmd(0x01);
    delay_ms(2);
}

static void lcd_init(void)
{
    i2c_init();
    delay_ms(50);

    /* 4-bit init sequence (standard HD44780 power-on sequence) */
    lcd_write4(0x30, 0);
    delay_ms(5);
    lcd_write4(0x30, 0);
    delay_us(150);
    lcd_write4(0x30, 0);
    delay_us(150);
    lcd_write4(0x20, 0); /* switch to 4-bit mode */
    delay_us(150);

    lcd_cmd(0x28); /* 4-bit, 2 line, 5x8 font */
    lcd_cmd(0x0C); /* display ON, cursor OFF, blink OFF */
    lcd_cmd(0x06); /* increment cursor, no shift */
    lcd_clear();
}

/* =======================================================================
   GPIO / Peripheral init
   ======================================================================= */
static void gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;

    /* PA0, PA1, PA2 -> analog input (mode 00, cnf 00) for ADC */
    GPIOA->CRL &= ~((0xF << (4*0)) | (0xF << (4*1)) | (0xF << (4*2)));

    /* PA8 -> TIM1_CH1 alternate function push-pull, 50MHz (mode 11, cnf 10) */
    GPIOA->CRH &= ~(0xF << (4*0));      /* PA8 is bit0 of CRH (pin8-pin8=0) */
    GPIOA->CRH |=  (0xB << (4*0));      /* 1011 = AF push-pull, 50MHz */

    /* PB0, PB1, PB8: input pull-up (mode 00, cnf 10) -- buttons, active LOW */
    GPIOB->CRL &= ~((0xF << (4*0)) | (0xF << (4*1)));
    GPIOB->CRL |=  ((0x8 << (4*0)) | (0x8 << (4*1)));   /* 1000 = input pull-up/down */
    GPIOB->ODR |=  (1 << 0) | (1 << 1);                  /* select pull-UP */

    GPIOB->CRH &= ~(0xF << (4*0));      /* PB8 is bit0 of CRH */
    GPIOB->CRH |=  (0x8 << (4*0));
    GPIOB->ODR |=  (1 << 8);

    /* PB12, PB13, PB14, PB15 -> output push-pull, 2MHz (mode 10, cnf 00) : LEDs + buzzer */
    GPIOB->CRH &= ~((0xF << (4*4)) | (0xF << (4*5)) | (0xF << (4*6)) | (0xF << (4*7)));
    GPIOB->CRH |=  ((0x2 << (4*4)) | (0x2 << (4*5)) | (0x2 << (4*6)) | (0x2 << (4*7)));
}

/* ---- ADC1 init: scan PA0/PA1/PA2 on demand (single conversion mode) ---- */
static void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->CFGR &= ~RCC_CFGR_ADCPRE;
    RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;   /* ADC clock = PCLK2/6 (<=14MHz) */

    ADC1->CR2 |= ADC_CR2_ADON;
    delay_us(10);
    ADC1->CR2 |= ADC_CR2_CAL;            /* run calibration */
    while (ADC1->CR2 & ADC_CR2_CAL);

    ADC1->SQR1 = 0; /* one conversion in the regular sequence at a time */
}

static uint16_t adc_read(uint8_t channel)
{
    ADC1->SQR3 = channel;
    ADC1->SMPR2 &= ~(7 << (3 * channel));
    ADC1->SMPR2 |=  (7 << (3 * channel)); /* longest sample time for stability */

    ADC1->CR2 |= ADC_CR2_ADON; /* start conversion */
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)(ADC1->DR);
}

/* ---- TIM1 PWM init for servo on PA8 (TIM1_CH1) ----
   Servo standard: 50Hz period (20ms), pulse 1ms (0deg) .. 2ms (90/180deg)
   Timer clock = 72MHz. Prescaler to get 1us tick -> ARR = 20000 for 20ms period. */
static void servo_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    TIM1->PSC = 72 - 1;     /* 72MHz / 72 = 1MHz -> 1 tick = 1us */
    TIM1->ARR = 20000 - 1;  /* 20000 us = 20ms period -> 50Hz */

    TIM1->CCMR1 &= ~TIM_CCMR1_OC1M;
    TIM1->CCMR1 |= (6 << 4);          /* OC1M = 110 : PWM mode 1 */
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE;   /* preload enable */

    TIM1->CCER |= TIM_CCER_CC1E;      /* enable channel 1 output */
    TIM1->CR1  |= TIM_CR1_ARPE;
    TIM1->BDTR |= TIM_BDTR_MOE;       /* Main Output Enable - REQUIRED for TIM1 (advanced timer) */

    TIM1->CCR1 = 1000;                /* start at 1ms pulse = 0 degrees */
    TIM1->CR1 |= TIM_CR1_CEN;         /* start timer */
}

static void servo_set_angle(uint8_t degrees)
{
    /* Map 0-180 degrees to 1000-2000 us pulse width. We only use 0 and 90. */
    uint32_t pulse_us = 1000 + ((uint32_t)degrees * 1000) / 180;
    TIM1->CCR1 = pulse_us;
}

/* =======================================================================
   Buzzer (simple ON/OFF tones on PB15, no PWM tone needed for beeps)
   ======================================================================= */
#define BUZZER_ON()   (GPIOB->BSRR = (1 << 15))
#define BUZZER_OFF()  (GPIOB->BSRR = (1 << (15 + 16)))

static void buzzer_beep(uint16_t on_ms, uint16_t off_ms)
{
    BUZZER_ON();
    delay_ms(on_ms);
    BUZZER_OFF();
    delay_ms(off_ms);
}

static void sound_correct(void)
{
    /* happy: two short rising beeps */
    buzzer_beep(100, 80);
    buzzer_beep(150, 0);
}

static void sound_wrong(void)
{
    /* sad: one long low beep */
    buzzer_beep(500, 0);
}

static void sound_saved(void)
{
    /* short confirmation chirp per saved pot */
    buzzer_beep(60, 0);
}

static void sound_password_done(void)
{
    /* longer triple beep when full new password is saved */
    buzzer_beep(80, 60);
    buzzer_beep(80, 60);
    buzzer_beep(200, 0);
}

/* =======================================================================
   LEDs
   ======================================================================= */
#define LED_GREEN_ON()   (GPIOB->BSRR = (1 << 12))
#define LED_GREEN_OFF()  (GPIOB->BSRR = (1 << (12 + 16)))
#define LED_RED_ON()     (GPIOB->BSRR = (1 << 13))
#define LED_RED_OFF()    (GPIOB->BSRR = (1 << (13 + 16)))
#define LED_BLUE_ON()    (GPIOB->BSRR = (1 << 14))
#define LED_BLUE_OFF()   (GPIOB->BSRR = (1 << (14 + 16)))

static void leds_all_off(void)
{
    LED_GREEN_OFF();
    LED_RED_OFF();
    LED_BLUE_OFF();
}

/* =======================================================================
   Buttons (active LOW, debounced with simple delay check)
   ======================================================================= */
#define BTN_OPEN_PRESSED()    (!(GPIOB->IDR & (1 << 0)))
#define BTN_CHPWD_PRESSED()   (!(GPIOB->IDR & (1 << 1)))
#define BTN_RESET_PRESSED()   (!(GPIOB->IDR & (1 << 8)))

/* Returns 1 once per press (rising edge of "pressed" state), with debounce */
static uint8_t button_pressed_edge(uint8_t (*read_fn)(void), uint8_t *last_state)
{
    uint8_t now = read_fn();
    if (now && !(*last_state))
    {
        delay_ms(20); /* debounce */
        if (read_fn())
        {
            *last_state = 1;
            return 1;
        }
    }
    if (!now) *last_state = 0;
    return 0;
}

static uint8_t read_open(void)  { return BTN_OPEN_PRESSED();  }
static uint8_t read_chpwd(void) { return BTN_CHPWD_PRESSED(); }
static uint8_t read_reset(void) { return BTN_RESET_PRESSED(); }

/* =======================================================================
   Helpers
   ======================================================================= */
static uint8_t adc_to_value(uint16_t raw)
{
    /* maps 0..4095 -> 0..10 */
    uint32_t v = ((uint32_t)raw * VALUE_MAX) / ADC_MAX;
    if (v > VALUE_MAX) v = VALUE_MAX;
    return (uint8_t)v;
}

static void lcd_show_values(uint8_t v1, uint8_t v2, uint8_t v3, const char *status)
{
    char line1[17];
    char line2[17];

    snprintf(line1, sizeof(line1), "P1:%2d P2:%2d P3:%2d", v1, v2, v3);
    snprintf(line2, sizeof(line2), "%-16s", status);

    lcd_set_cursor(0, 0);
    lcd_print(line1);
    lcd_set_cursor(0, 1);
    lcd_print(line2);
}

/* =======================================================================
   MAIN STATE MACHINE
   ======================================================================= */
typedef enum { MODE_NORMAL, MODE_SET_PASSWORD } SystemMode;

int main(void)
{
    /* --- init everything --- */
    gpio_init();
    adc_init();
    servo_init();
    lcd_init();

    leds_all_off();
    BUZZER_OFF();
    servo_set_angle(0); /* door closed at boot */

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("Smart Save Case");
    lcd_set_cursor(0, 1);
    lcd_print(" Booting OK...");
    delay_ms(1500);
    lcd_clear();

    /* --- password targets: defaults until first "Change Password" run --- */
    uint8_t target[3] = { DEFAULT_TARGET, DEFAULT_TARGET, DEFAULT_TARGET };

    SystemMode mode = MODE_NORMAL;
    uint8_t set_index = 0;          /* which pot we're currently setting (0,1,2) */
    uint8_t door_open = 0;

    uint8_t last_open = 0, last_chpwd = 0, last_reset = 0;

    while (1)
    {
        /* --- read pots every loop --- */
        uint16_t raw1 = adc_read(0);
        uint16_t raw2 = adc_read(1);
        uint16_t raw3 = adc_read(2);

        uint8_t v1 = adc_to_value(raw1);
        uint8_t v2 = adc_to_value(raw2);
        uint8_t v3 = adc_to_value(raw3);

        /* --- RESET button: highest priority, always works --- */
        if (button_pressed_edge(read_reset, &last_reset))
        {
            mode = MODE_NORMAL;
            set_index = 0;
            door_open = 0;
            servo_set_angle(0);
            leds_all_off();
            lcd_clear();
        }

        if (mode == MODE_NORMAL)
        {
            /* show live pot values */
            const char *status = door_open ? "Door: OPEN" : "Ready...";
            lcd_show_values(v1, v2, v3, status);

            /* Change Password button -> enter SET mode */
            if (button_pressed_edge(read_chpwd, &last_chpwd))
            {
                mode = MODE_SET_PASSWORD;
                set_index = 0;
                LED_BLUE_ON();
                lcd_clear();
            }

            /* Open button -> check combo / close door */
            if (button_pressed_edge(read_open, &last_open))
            {
                if (!door_open)
                {
                    if (v1 == target[0] && v2 == target[1] && v3 == target[2])
                    {
                        LED_GREEN_ON();
                        LED_RED_OFF();
                        sound_correct();
                        servo_set_angle(90);
                        door_open = 1;
                    }
                    else
                    {
                        LED_RED_ON();
                        LED_GREEN_OFF();
                        sound_wrong();
                        delay_ms(400);
                        LED_RED_OFF();
                    }
                }
                else
                {
                    /* door currently open -> close it */
                    servo_set_angle(0);
                    door_open = 0;
                    leds_all_off();
                }
            }
        }
        else /* MODE_SET_PASSWORD */
        {
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Set Pwd Pot %d", set_index + 1);
            uint8_t live_val = (set_index == 0) ? v1 : (set_index == 1) ? v2 : v3;
            snprintf(line2, sizeof(line2), "Val:%2d  [OPEN=OK]", live_val);

            lcd_set_cursor(0, 0);
            lcd_print(line1);
            lcd_print("    "); /* pad */
            lcd_set_cursor(0, 1);
            lcd_print(line2);

            /* Open button repurposed as "confirm/save" in this mode */
            if (button_pressed_edge(read_open, &last_open))
            {
                target[set_index] = live_val;
                sound_saved();
                set_index++;

                if (set_index >= 3)
                {
                    /* finished setting all 3 -> back to normal */
                    sound_password_done();
                    LED_BLUE_OFF();
                    mode = MODE_NORMAL;
                    set_index = 0;
                    lcd_clear();
                }
            }

            /* Allow cancel out of SET mode via Change-Password button again */
            if (button_pressed_edge(read_chpwd, &last_chpwd))
            {
                LED_BLUE_OFF();
                mode = MODE_NORMAL;
                set_index = 0;
                lcd_clear();
            }
        }

        delay_ms(80); /* main loop pacing - keeps LCD/buttons responsive but stable */
    }
}