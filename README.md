# Proximity Warning System on STM32

A bare-metal real-time proximity sensing and alerting system built on the **STM32F103C8T6 (Blue Pill)** microcontroller. No HAL, no middleware — every peripheral configured directly through CMSIS register access in Keil MDK.

---

## Features

- Ultrasonic distance measurement via HC-SR04 (EXTI edge-capture state machine)
- Three-zone LED indication: Safe / Warning / Danger
- PWM buzzer with potentiometer-adjustable duty cycle (10–90%)
- 8-bit analog output through DAC0808 (parallel GPIO bus)
- 2×16 HD44780 LCD display in 4-bit mode (split across GPIOA and GPIOB)
- 20 Hz sampling rate driven by TIM4 interrupt
- Global microsecond timebase via TIM2 @ 1 MHz

---

## Distance Zones

| Zone    | Range        | LED    | Buzzer                        |
|---------|--------------|--------|-------------------------------|
| Safe    | > 50 cm      | Green  | Off                           |
| Warning | 20 – 50 cm   | Yellow | Intermittent (250 ms toggle)  |
| Danger  | < 20 cm      | Red    | Continuous at pot-set duty    |

---

## Hardware

| Component         | Description                        |
|-------------------|------------------------------------|
| STM32F103C8T6     | Blue Pill — 72 MHz Cortex-M3 MCU   |
| HC-SR04           | Ultrasonic distance sensor         |
| DAC0808           | 8-bit parallel DAC                 |
| HD44780           | 2×16 character LCD                 |
| Potentiometer     | Buzzer duty cycle control (ADC)    |
| Passive Buzzer    | PWM-driven audio alert             |
| LEDs × 3          | Safe / Warning / Danger indicators |

---

## Pin Map

### GPIOA

| Pin | Function              |
|-----|-----------------------|
| PA0 | HC-SR04 TRIG (output) |
| PA1 | HC-SR04 ECHO (EXTI1)  |
| PA2 | Potentiometer (ADC1_IN2) |
| PA4 | LCD RS                |
| PA5 | LCD EN                |
| PA6 | LCD D4                |
| PA7 | LCD D5                |
| PA8 | Buzzer PWM (TIM1_CH1) |

### GPIOB

| Pin  | Function       |
|------|----------------|
| PB5  | LED Safe       |
| PB6  | LED Warning    |
| PB7  | LED Danger     |
| PB8  | LCD D6         |
| PB9  | LCD D7         |
| PB0  | DAC0808 bit 0  |
| PB1  | DAC0808 bit 1  |
| PB3  | DAC0808 bit 2  |
| PB10 | DAC0808 bit 3  |
| PB11 | DAC0808 bit 4  |
| PB12 | DAC0808 bit 5  |
| PB13 | DAC0808 bit 6  |
| PB14 | DAC0808 bit 7  |

---

## Peripheral Configuration

| Peripheral | Config                                      |
|------------|---------------------------------------------|
| TIM2       | PSC=71, ARR=0xFFFF — 1 MHz microsecond base |
| TIM4       | PSC=7199, ARR=499 — 20 Hz sample tick       |
| TIM1 CH1   | PSC=35, ARR=999 — PWM buzzer @ ~2 kHz       |
| ADC1       | IN2, 239.5-cycle sample time, software trigger |
| EXTI1      | Both edges, PA1 — echo pulse capture        |

---

## Build

- **IDE:** Keil MDK (µVision)
- **Target:** STM32F103C8T6
- **CMSIS:** stm32f10x.h
- **HAL:** None — bare register access only

Open the `.uvprojx` project file in Keil, build, and flash via ST-Link.

---

## Project Page

Full write-up with block diagram and annotated source: **[btunaucar.github.io](https://btunaucar.github.io)**

---

*Baran Tuna Uçar — Bilkent University, Electrical and Electronics Engineering*
