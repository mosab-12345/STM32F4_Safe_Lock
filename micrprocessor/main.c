/* ───────────────────── ADC ───────────────────── */
/*MOSAB ABDELGADIR*/
static void ADC_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
    ADC1->CR2 = ADC_CR2_ADON;
    delay_ms(1);
    ADC1->CR2 |= RCC_CR2_RSTCAL;
    while (ADC1->CR2 & ADC_CR2_RSTCAL);
    ADC1->CR2 |= ADC_CR2_CAL;
    while (ADC1->CR2 & ADC_CR2_CAL);
}

static uint16_t ADC_Read(uint8_t ch) {
    ADC1->SQR3 = ch;
    ADC1->CR2 |= ADC_CR2_ADON;
    while (!(ADC1->SR & ADC_SR_EOC));
    return (uint16_t)ADC1->DR;
}

static uint8_t ADC_ToScale(uint16_t raw) {
    return (uint8_t)((raw * 10UL) / 4095);
}

static void ReadPots(void) {
    pot_val[0] = ADC_ToScale(ADC_Read(0));
    pot_val[1] = ADC_ToScale(ADC_Read(1));
    pot_val[2] = ADC_ToScale(ADC_Read(2));
}
/*Added ADC configuration for PA0, PA1, and PA2

Implemented ADC_Read subroutine for dynamic channel selection

Added ADC_ToScale function to map values from 0-4095 to 0-10

Implemented ReadPots subroutine to update active combo readings*/
/* ───────────────────── Servo (TIM1 CH1, PA8) ───────────────────── */
#define SERVO_0DEG   1800
#define SERVO_180DEG 3600

static void Servo_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_TIM1EN;
    GPIOA->CRH &= ~(0x0F);
    GPIOA->CRH |=  0x0B;   /* AF PP, 50 MHz */

    TIM1->PSC = 39;
    TIM1->ARR = 36000 - 1;
    TIM1->CCR1 = SERVO_0DEG;
    TIM1->CCMR1 = (6 << 4) | TIM_CCMR1_OC1PE;
    TIM1->CCER  = TIM_CCER_CC1E;
    TIM1->BDTR  = TIM_BDTR_MOE;
    TIM1->CR1   = TIM_CR1_CEN;
}

static void Servo_SetAngle(uint16_t angle) {
    TIM1->CCR1 = (angle == 0) ? SERVO_0DEG : SERVO_180DEG;
}

/* ───────────────────── GPIO LEDs ───────────────────── */
#define GREEN_LED_ON()   GPIOB->ODR |=  (1 << 12)
#define GREEN_LED_OFF()  GPIOB->ODR &= ~(1 << 12)
#define RED_LED_ON()     GPIOB->ODR |=  (1 << 13)
#define RED_LED_OFF()    GPIOB->ODR &= ~(1 << 13)
#define BLUE_LED_ON()    GPIOB->ODR |=  (1 << 14)
#define BLUE_LED_OFF()   GPIOB->ODR &= ~(1 << 14)
/*Configured TIM1 PWM registers for PA8 servo signal

Implemented Servo_SetAngle subroutine for lock control

Configured GPIO output pins for Status LEDs (PB12, PB13, PB14)

Added inline definitions for individual LED state toggling*/

/*AHMED BASEM ALARIQI*/

/*AHMED FATH ELRAHMAN*/
/* ───────────────────── I2C Configuration ───────────────────── */
#define LCD_ADDR  (0x27 << 1)
#define LCD_RS    0x01
#define LCD_EN    0x04
#define LCD_BL    0x08

static void I2C1_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* PB6 SCL, PB7 SDA – alternate function open-drain */
    GPIOB->CRL &= ~(0xFF000000);
    GPIOB->CRL |=  (0xFF000000); /* AF OD, 50 MHz */

    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = 36;           /* APB1 = 36 MHz */
    I2C1->CCR = 180;          /* 100 kHz */
    I2C1->TRISE = 37;
    I2C1->CR1 |= I2C_CR1_PE;
}

static void I2C_WriteByte(uint8_t addr, uint8_t data) {
    while (I2C1->SR2 & I2C_SR2_BUSY);
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB));
    I2C1->DR = addr;
    while (!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR2;
    I2C1->DR = data;static void LCD_SendNibble(uint8_t nibble, uint8_t rs) {
    uint8_t data = (nibble << 4) | LCD_BL | (rs ? LCD_RS : 0);
    I2C_WriteByte(LCD_ADDR, data | LCD_EN);
    delay_ms(1);
    I2C_WriteByte(LCD_ADDR, data & ~LCD_EN);
    delay_ms(1);
}

static void LCD_SendByte(uint8_t byte, uint8_t rs) {
    LCD_SendNibble(byte >> 4, rs);
    LCD_SendNibble(byte & 0x0F, rs);
}

static void LCD_Cmd(uint8_t cmd)  { LCD_SendByte(cmd,  0); }
static void LCD_Data(uint8_t c)   { LCD_SendByte(c,    1); }

static void LCD_Init(void) {
    delay_ms(50);
    LCD_SendNibble(0x03, 0); delay_ms(5);
    LCD_SendNibble(0x03, 0); delay_ms(1);
    LCD_SendNibble(0x03, 0); delay_ms(1);
    LCD_SendNibble(0x02, 0);            
    LCD_Cmd(0x28);                      
    LCD_Cmd(0x0C);                      
    LCD_Cmd(0x06);                      
    LCD_Cmd(0x01);                      
    delay_ms(2);
}2C_SR1_BTF));
    I2C1->CR1 |= I2C_CR1_STOP;
}
static void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? 0x80 : 0xC0;
    LCD_Cmd(addr + col);
}

static void LCD_Print(const char *s) {
    while (*s) LCD_Data((uint8_t)*s++);
}

static void LCD_Clear(void) { LCD_Cmd(0x01); delay_ms(2); }
static void LCD_ShowValues(void) {
    char buf[17];
    LCD_SetCursor(0, 0);
    snprintf(buf, sizeof(buf), "1:%2d 2:%2d 3:%2d  ",
             pot_val[0], pot_val[1], pot_val[2]);
    LCD_Print(buf);
}

static void LCD_ShowStatus(const char *line2) {
    char buf[17];
    LCD_SetCursor(1, 0);
    snprintf(buf, sizeof(buf), "%-16s", line2);
    LCD_Print(buf);
}
/*Azwad Nur Naveed*/
/* ───────────────────── GPIO Outputs Configuration ───────────────────── */
static void GPIO_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* PB12, PB13, PB14 LEDs – output push-pull 2 MHz */
    GPIOB->CRH &= ~(0x000FFF00);
    GPIOB->CRH |=  (0x00022200);

    /* PA5 Buzzer – output push-pull 2 MHz */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    GPIOA->CRL &= ~(0x00F00000);
    GPIOA->CRL |=  (0x00200000);

    /* Turn off all LEDs and Buzzer on boot */
    GPIOB->ODR &= ~((1 << 12) | (1 << 13) | (1 << 14));
    GPIOA->ODR &= ~(1 << 5);
}
#define GREEN_LED_ON()   GPIOB->ODR |=  (1 << 12)
#define GREEN_LED_OFF()  GPIOB->ODR &= ~(1 << 12)
#define RED_LED_ON()     GPIOB->ODR |=  (1 << 13)
#define RED_LED_OFF()    GPIOB->ODR &= ~(1 << 13)
#define BLUE_LED_ON()    GPIOB->ODR |=  (1 << 14)
#define BLUE_LED_OFF()   GPIOB->ODR &= ~(1 << 14)
#define BUZZER_ON()      GPIOA->ODR |=  (1 << 5)
#define BUZZER_OFF()     GPIOA->ODR &= ~(1 << 5)

static void Beep(uint32_t on_ms, uint32_t off_ms, uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        BUZZER_ON();
        delay_ms(on_ms);
        BUZZER_OFF();
        delay_ms(off_ms);
    }
}
static void Beep_Success(void) {
    Beep(80,  60, 1);
    Beep(120, 60, 1);
    Beep(200, 0,  1);
}

static void Beep_Error(void) {
    Beep(400, 100, 2);
}

static void Beep_Tick(void) {
    Beep(20, 0, 1);
}
static void LEDs_Off(void) {
    GREEN_LED_OFF();
    RED_LED_OFF();
    BLUE_LED_OFF();
}
