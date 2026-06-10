#include "stm32f10x.h"
#include <stdint.h>

/* ============================================================
 *  PIN & CONSTANT DEFINITIONS
 * ============================================================ */

/* GPIOA pins */
#define PIN_TRIG           0       /* PA0 - HC-SR04 TRIG */
#define PIN_ECHO           1       /* PA1 - HC-SR04 ECHO */
#define PIN_POT_CH         2       /* PA2 - ADC1_IN2 */
#define PIN_LCD_RS         4       /* PA4 */
#define PIN_LCD_EN         5       /* PA5 */
#define PIN_LCD_D4         6       /* PA6 */
#define PIN_LCD_D5         7       /* PA7 */

/* GPIOB pins */
#define PIN_LED_SAFE       5       /* PB5 */
#define PIN_LED_WARN       6       /* PB6 */
#define PIN_LED_DANGER     7       /* PB7 */
#define PIN_LCD_D6         8       /* PB8 */
#define PIN_LCD_D7         9       /* PB9 */

static const uint8_t DAC_PIN_MAP[8] = { 0, 1, 3, 10, 11, 12, 13, 14 };
#define DAC_PIN_MASK       0x7C0B  /* PB14,13,12,11,10,3,1,0 */

#define SAMPLE_PERIOD_MS   50
#define DIST_SAFE_CM       50
#define DIST_DANGER_CM     20
#define DIST_DAC_MAX_CM    200
#define DIST_MAX_CM        400

#define BUZZER_DUTY_MIN    10
#define BUZZER_DUTY_MAX    90
#define BEEP_HALF_TICKS    5

/* ============================================================
 *  GLOBAL STATE
 * ============================================================ */

volatile uint32_t g_tim2_ovf      = 0;
volatile uint8_t  g_sample_ready  = 0;

volatile uint32_t g_echo_rise_us  = 0;
volatile uint32_t g_echo_fall_us  = 0;
volatile uint8_t  g_echo_state    = 0;   /* 0=idle, 1=rose, 2=complete */

volatile uint8_t  g_beep_tick     = 0;
volatile uint8_t  g_beep_on       = 0;

/* ============================================================
 *  TIMEBASE (TIM2 @ 1 MHz)
 * ============================================================ */

uint32_t micros(void)
{
    uint32_t ovf_a, ovf_b;
    uint16_t cnt;

    do {
        ovf_a = g_tim2_ovf;
        cnt   = TIM2->CNT;
        ovf_b = g_tim2_ovf;
    } while (ovf_a != ovf_b);

    return ovf_a * 65536UL + cnt;
}

void wait_us(uint16_t us)
{
    uint32_t start = micros();
    while ((micros() - start) < us) { ; }
}

void wait_ms(uint16_t ms)
{
    while (ms--) { wait_us(1000); }
}

void timer2_microsecond_init(void)
{
    RCC->APB1ENR |= (1 << 0);

    TIM2->PSC  = 72 - 1;
    TIM2->ARR  = 0xFFFF;
    TIM2->CNT  = 0;
    TIM2->SR   = 0;
    TIM2->DIER = (1 << 0);
    TIM2->CR1  = (1 << 0);

    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & 1) {
        TIM2->SR &= ~1;
        g_tim2_ovf++;
    }
}

/* ============================================================
 *  GPIO INIT
 * ============================================================ */

void gpio_init(void)
{
    RCC->APB2ENR |= (1 << 0) | (1 << 2) | (1 << 3);

    /* Disable JTAG, keep SWD (frees PB3, PB4) */
    AFIO->MAPR = (AFIO->MAPR & ~(7 << 24)) | (2 << 24);

    GPIOA->CRL = 0x33334043;
    /* PA8=AF push-pull (TIM1_CH1) */
    GPIOA->CRH = 0x4444444B;

    GPIOB->CRL = 0x33343433;

    GPIOB->CRH = 0x43333333;

    GPIOA->BRR = (1 << PIN_TRIG) | (1 << PIN_LCD_RS) | (1 << PIN_LCD_EN);
    GPIOB->BRR = (1 << PIN_LED_SAFE) | (1 << PIN_LED_WARN) | (1 << PIN_LED_DANGER)
               | DAC_PIN_MASK;
}

/* ============================================================
 *  TIM4: 50 ms SAMPLE TICK
 * ============================================================ */

void timer4_sampling_init(void)
{
    RCC->APB1ENR |= (1 << 2);

    TIM4->CR1  = 0;
    TIM4->PSC  = 7200 - 1;     /* 72MHz / 7200 = 10 kHz */
    TIM4->ARR  = 500 - 1;      /* 10kHz / 500  = 20 Hz -> 50ms */
    TIM4->CNT  = 0;
    TIM4->EGR  = 1;
    TIM4->SR   = 0;
    TIM4->DIER = (1 << 0);
    TIM4->CR1  = (1 << 0);

    NVIC_EnableIRQ(TIM4_IRQn);
}

void TIM4_IRQHandler(void)
{
    if (!(TIM4->SR & 1)) return;
    TIM4->SR &= ~1;

    g_sample_ready = 1;

    if (++g_beep_tick >= BEEP_HALF_TICKS) {
        g_beep_tick = 0;
        g_beep_on   = !g_beep_on;
    }
}

/* ============================================================
 *  ECHO CAPTURE (EXTI1)
 * ============================================================ */

void echo_exti_init(void)
{
    AFIO->EXTICR[0] &= ~(0xF << 4);    /* PA1 source */
    EXTI->RTSR |= (1 << 1);
    EXTI->FTSR |= (1 << 1);
    EXTI->PR    = (1 << 1);
    EXTI->IMR  |= (1 << 1);

    NVIC_EnableIRQ(EXTI1_IRQn);
}

void EXTI1_IRQHandler(void) /*state machine*/
{
    if (!(EXTI->PR & (1 << 1))) return;
    EXTI->PR = (1 << 1);

    if (GPIOA->IDR & (1 << PIN_ECHO)) {
        g_echo_rise_us = micros();
        g_echo_state   = 1;
    } else if (g_echo_state == 1) {
        g_echo_fall_us = micros();
        g_echo_state   = 2;
    }
}

/* ============================================================
 *  ADC1 (potentiometer on PA2)
 * ============================================================ */

void adc1_pot_init(void)
{
    RCC->APB2ENR |= (1 << 9);
    RCC->CFGR     = (RCC->CFGR & ~(3 << 14)) | (2 << 14);

    ADC1->CR1   = 0;
    ADC1->CR2   = 0;
    ADC1->SMPR2 = (7 << (PIN_POT_CH * 3));    /* 239.5 cycles */
    ADC1->SQR1  = 0;
    ADC1->SQR3  = PIN_POT_CH;

    ADC1->CR2 |= (1 << 0);
    wait_ms(1);

    ADC1->CR2 |= (1 << 3); while (ADC1->CR2 & (1 << 3)) { ; } 
    ADC1->CR2 |= (1 << 2); while (ADC1->CR2 & (1 << 2)) { ; }

    ADC1->CR2 |= (7 << 17) | (1 << 20);
}

uint16_t adc1_read_pot(void)
{
    ADC1->CR2 |= (1 << 22);
    while (!(ADC1->SR & (1 << 1))) { ; }
    return (uint16_t)(ADC1->DR & 0x0FFF);
}

/* ============================================================
 *  DAC0808 OUTPUT
 * ============================================================ */

void dac0808_write(uint8_t value)
{
    uint32_t set_mask = 0;
    uint8_t i;

    for (i = 0; i < 8; i++) {
        if (value & (1 << i)) {
            set_mask |= (1U << DAC_PIN_MAP[i]);
        }
    }

    GPIOB->BSRR = set_mask | ((uint32_t)DAC_PIN_MASK << 16);
    GPIOB->BSRR = set_mask;
}

/* ============================================================
 *  TIM1 PWM BUZZER (PA8 = TIM1_CH1)
 * ============================================================ */

void buzzer_init(void)
{
    RCC->APB2ENR |= (1 << 11);

    TIM1->PSC   = 36 - 1;
    TIM1->ARR   = 1000 - 1;
    TIM1->CCR1  = 0;
    TIM1->CCMR1 = (TIM1->CCMR1 & ~(7 << 4)) | (6 << 4) | (1 << 3);  /* PWM1 + preload */
    TIM1->CCER |= (1 << 0);
    TIM1->BDTR |= (1 << 15);
    TIM1->CR1  |= (1 << 7);    /* ARPE */
    TIM1->EGR  |= (1 << 0);
    TIM1->CR1  |= (1 << 0);
}

void buzzer_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    TIM1->CCR1 = ((TIM1->ARR + 1) * percent) / 100;
}

/* ============================================================
 *  LEDs & WARNING LOGIC
 * ============================================================ */

void leds_write(uint8_t safe, uint8_t warn, uint8_t danger)
{
    uint32_t set_mask   = (safe   ? (1U << PIN_LED_SAFE)   : 0)
                        | (warn   ? (1U << PIN_LED_WARN)   : 0)
                        | (danger ? (1U << PIN_LED_DANGER) : 0);
    uint32_t reset_mask = ((1U << PIN_LED_SAFE)
                        |  (1U << PIN_LED_WARN)
                        |  (1U << PIN_LED_DANGER)) & ~set_mask;

    GPIOB->BSRR = set_mask | (reset_mask << 16);
}

void apply_warning(uint32_t distance_cm, uint8_t buzzer_pct)
{
    if (distance_cm > DIST_SAFE_CM) {
        leds_write(1, 0, 0);
        buzzer_set(0);
    } else if (distance_cm > DIST_DANGER_CM) {
        leds_write(0, 1, 0);
        buzzer_set(g_beep_on ? buzzer_pct : 0);
    } else {
        leds_write(0, 0, 1);
        buzzer_set(buzzer_pct);
    }
}

/* ============================================================
 *  ULTRASONIC
 * ============================================================ */

void ultrasonic_trigger(void)
{
    g_echo_state = 0;

    GPIOA->BSRR = (1 << PIN_TRIG);
    wait_us(10);
    GPIOA->BRR  = (1 << PIN_TRIG);
}

uint32_t ultrasonic_read_cm(void)
{
    uint32_t pulse_us, distance;

    if (g_echo_state != 2) return DIST_MAX_CM;

    pulse_us = g_echo_fall_us - g_echo_rise_us;
    if (pulse_us < 100 || pulse_us > 40000) return DIST_MAX_CM;

    distance = pulse_us / 58;
    return (distance > DIST_MAX_CM) ? DIST_MAX_CM : distance;
}

/* ============================================================
 *  LCD (4-bit mode)
 * ============================================================ */

#define LCD_PA_DATA_MASK   ((1U << 6) | (1U << 7))           /* D4, D5 */
#define LCD_PB_DATA_MASK   ((1U << 8) | (1U << 9))           /* D6, D7 */

static void lcd_write_nibble(uint8_t nibble)
{
    uint32_t pa_set = ((nibble & 0x1) ? (1U << 6) : 0)
                    | ((nibble & 0x2) ? (1U << 7) : 0);
    uint32_t pb_set = ((nibble & 0x4) ? (1U << 8) : 0)
                    | ((nibble & 0x8) ? (1U << 9) : 0);

    GPIOA->BSRR = pa_set | (LCD_PA_DATA_MASK << 16);
    GPIOB->BSRR = pb_set | (LCD_PB_DATA_MASK << 16);

    wait_us(2);
    GPIOA->BSRR = (1 << PIN_LCD_EN);
    wait_us(2);
    GPIOA->BRR  = (1 << PIN_LCD_EN);
    wait_us(80);
}

static void lcd_write(uint8_t value, uint8_t is_data)
{
    if (is_data) GPIOA->BSRR = (1 << PIN_LCD_RS);
    else         GPIOA->BRR  = (1 << PIN_LCD_RS);

    lcd_write_nibble(value >> 4);
    lcd_write_nibble(value & 0x0F);

    if (!is_data && (value == 0x01 || value == 0x02)) wait_ms(2);
    else                                              wait_us(80);
}

#define lcd_command(c)  lcd_write((c), 0)
#define lcd_char(d)     lcd_write((d), 1)

void lcd_init(void)
{
    GPIOA->BRR = (1 << PIN_LCD_RS) | (1 << PIN_LCD_EN);
    wait_ms(40);

    lcd_write_nibble(0x03); wait_ms(5);
    lcd_write_nibble(0x03); wait_us(200);
    lcd_write_nibble(0x03); wait_us(200);
    lcd_write_nibble(0x02); wait_us(200);   /* set 4-bit mode */

    lcd_command(0x28);   /* 4-bit, 2-line, 5x8 */
    lcd_command(0x0C);   /* display on, cursor off */
    lcd_command(0x06);   /* entry mode: increment */
    lcd_command(0x01);   /* clear */
    wait_ms(2);
}

void lcd_goto(uint8_t row, uint8_t col)
{
    lcd_command(((row == 0) ? 0x80 : 0xC0) + col);
}

void lcd_print(const char *s)
{
    while (*s) lcd_char((uint8_t)*s++);
}

void lcd_print_uint(uint32_t value, uint8_t digits)
{
    char buf[10];
    int8_t i;

    buf[digits] = '\0';
    for (i = digits - 1; i >= 0; i--) {
        buf[i] = '0' + (value % 10);
        value /= 10;
    }
    lcd_print(buf);
}

void lcd_refresh(uint32_t distance_cm, uint8_t dac_value, uint8_t duty_pct)
{

    uint32_t saved_ccr = TIM1->CCR1;
    TIM1->CCR1 = 0;

    lcd_goto(0, 0);
    lcd_print("d=");
    lcd_print_uint(distance_cm, 3);
    lcd_print("cm Ts=");
    lcd_print_uint(SAMPLE_PERIOD_MS, 3);
    lcd_print("ms");

    lcd_goto(1, 0);
    lcd_print("DAC=");
    lcd_print_uint(dac_value, 3);
    lcd_print(" A=");
    lcd_print_uint(duty_pct, 2);
    lcd_print("%  ");

    TIM1->CCR1 = saved_ccr;
}

/* ============================================================
 *  MAIN
 * ============================================================ */

int main(void)
{
    gpio_init();
    timer2_microsecond_init();
    adc1_pot_init();
    buzzer_init();
    lcd_init();
    echo_exti_init();
    timer4_sampling_init();

    while (1) {
        uint32_t distance_cm;
        uint8_t  dac_value, buzzer_pct;
        uint16_t adc_raw;

        if (!g_sample_ready) continue;
        g_sample_ready = 0;

        distance_cm = ultrasonic_read_cm();

        adc_raw    = adc1_read_pot();
        buzzer_pct = BUZZER_DUTY_MIN
                   + (uint8_t)(((BUZZER_DUTY_MAX - BUZZER_DUTY_MIN) * (uint32_t)adc_raw) / 4095UL);

        {
            uint32_t d = (distance_cm > DIST_DAC_MAX_CM) ? DIST_DAC_MAX_CM : distance_cm;
            dac_value  = (uint8_t)((255UL * d) / DIST_DAC_MAX_CM);
            dac0808_write(dac_value);
        }

        apply_warning(distance_cm, buzzer_pct);

        lcd_refresh(distance_cm, dac_value, buzzer_pct);

        ultrasonic_trigger();
    }
}