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

