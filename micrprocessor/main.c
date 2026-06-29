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


/*AHMED BASEM ALARIQI*/

