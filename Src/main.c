/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Auto-generated by STM32CubeIDE
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 ******************************************************************************
 */

// <stdint.h> je znotraj stm32f4xx.h
#include <stdio.h>
#include "stm32f4xx.h"
#include "printf.h"

void Delay_ms(int16_t ms);

uint16_t overflowCounter = 0;
//uint8_t interruptFlag = 0;

int main(void)
{
	/********************************** CLOCK CONFIG **********************************/

	SET_BIT(RCC -> AHB1ENR, RCC_AHB1ENR_GPIOEEN); 				// vklop ure GPIOE
	SET_BIT(RCC -> APB1ENR, RCC_APB1ENR_TIM4EN); 				// vklop ure TIM4 general purpose
	SET_BIT(RCC -> APB1ENR, RCC_APB1ENR_TIM7EN); 				// vklop ure TIM7 basic

	/********************************** TIMER CONFIG **********************************/

	/*************	Konfiguracija TIM4 - general purpose timer za 5 sekundno zakasnitev	*************/
	TIM4->CR1 &= ~TIM_CR1_DIR;						// DIR = 0 => Counter used as upcounter
	TIM4->PSC = 0;								// fCNT = fCLK/(PSC+1)/(ARR+1) = 16MHz/1/16000 = 1kHz => perioda T = 1/f = 1 ms
	TIM4->ARR = 16000-1;							// ARR doseze v 1 ms => overflow vsako 1 ms
	//TIM4->CR1 |= TIM_CR1_URS;						// URS = 1 => generate interrupt when overflow/underflow
	TIM4->DIER |= TIM_DIER_UIE;						// UIE = 1 => update interrupt enable
	TIM4->EGR |= TIM_EGR_UG;						// UG = 1 =>  re-initialize the counter and generate an update of registers; ročno sprozi dogodek
	TIM4->SR &= ~TIM_SR_UIF;						// Pocistimo UIF bit
	//TIM4->CR1 |= TIM_CR1_CEN;						// Enable timer -> naredimo v delay funkciji
	NVIC_EnableIRQ(TIM4_IRQn);						// Omogoci prekinitve

	/************* Konfiguracija TIM7 - basic timer za namen branja iz senzorja - counter used as upcounter *************/
	TIM7->PSC = 0;								// fCNT = fCLK/(PSC+1)/(ARR+1) = 16MHz/1/16000 = 1kHz => perioda T = 1/f = 1 ms
	TIM7->ARR = 16000-1;							// Ta ARR doseze v 1 ms, do te ne bo prestel med branjem (beremo pa v podrocju us)
	//TIM7->CR1 |= TIM_CR1_CEN;						// Enable timer -> naredimo med branjem

	/********************************** PIN CONFIG **********************************/

	// PE5 (SDA) na output (01)
	CLEAR_BIT(GPIOE->MODER, GPIO_MODER_MODER5_1);
	SET_BIT(GPIOE->MODER, GPIO_MODER_MODER5_0);

	/********************************** SENSOR CONFIG **********************************/

	uint64_t rawData = 0;

	for(;;)
	{
		Delay_ms(5000);	 						// 5 sekundni delay (zagon TIM4)
		//printf("Test delay\n");

		/********************************** Branje iz senzorja AM2302 **********************************/

		// PE5 (SDA) na output (01) => bit 2*5+1 = 11 na 0, bit 2*5 = 10 na 1
		CLEAR_BIT(GPIOE->MODER, GPIO_MODER_MODER5_1);
		SET_BIT(GPIOE->MODER, GPIO_MODER_MODER5_0);

		// Postavi na HIGH, nastavi PE5 na input (00), while(IDR == 1), podobno za 0 in 1
		SET_BIT(GPIOE->ODR,GPIO_ODR_ODR_5);
		// Z ODR na 0 1 ms delay	(PE5 = output)
		CLEAR_BIT(GPIOE->ODR,GPIO_ODR_ODR_5);

		Delay_ms(1);

		// Postavi na HIGH, nastavi PE5 na input (00), while(IDR == 1), podobno za 0 in 1
		SET_BIT(GPIOE->ODR,GPIO_ODR_ODR_5);
		CLEAR_BIT(GPIOE->MODER, GPIO_MODER_MODER5_1);
		CLEAR_BIT(GPIOE->MODER, GPIO_MODER_MODER5_0);

		while( READ_BIT(GPIOE->IDR,GPIO_IDR_IDR_5) != 0);		// čakaj na TLOW
		while( READ_BIT(GPIOE->IDR,GPIO_IDR_IDR_5) == 0);		// čakaj na THIGH (Treh)
		while( READ_BIT(GPIOE->IDR,GPIO_IDR_IDR_5) != 0);		// čakaj na TLOW

		for(uint8_t i = 0; i < 40; i++)					// beremo 40 bitov
		{
				while( READ_BIT(GPIOE->IDR,GPIO_IDR_IDR_5) == 0 );		// Preveri IDR in cakaj dokler IDR = 0 -> cakaj na THigh
				// s TIM7 beri cas HIGH, beri CNT, izracunaj cas THigh, doloci ali gre za 0 ali za 1, resetiraj counter CNT na 0
				TIM7->CNT = 0;							// Resetiraj counter na 0
				TIM7->CR1 |= TIM_CR1_CEN;					// Enable timer

				while( READ_BIT(GPIOE->IDR,GPIO_IDR_IDR_5) != 0 );		// cakaj na TLOW
				TIM7->CR1 &= ~TIM_CR1_CEN;					// Disable timer

				// Preverimo ali je poslan bit = 0  ali = 1, shrani vrednost v RawData spremenljivko
				if(READ_REG(TIM7->CNT) < 800)			// Poslan je bil bit 0 (Thigh < 50 us => TH0; CNT=800 => 50us [CNT = 15 999 => 1 ms])
				{
					rawData <<= 1;				// Dodaj bit 0 skrajno desno
				}
				else						// poslan je bil bit 1 (THigh > 50 us => TH1)
				{
					rawData = (rawData << 1) | 1;		// Dodaj bit 1 skrajno desno
				}
		}

		// Iz 64bit podatka s shiftanjem ipd. izvleci spremenljivki Vlaga16_t in Temp16_t
		uint8_t ParityBit = (uint8_t)rawData;				// skrajno desnih 8 bitov
		uint8_t TempLow = (uint8_t)(rawData>>8);			// naslednjih 8 bitov
		uint8_t TempHigh = (uint8_t)(rawData>>16);			// naslednjih 8 bitov
		uint8_t HumidityLow = (uint8_t)(rawData>>24);			// naslednjih 8 bitov
		uint8_t HumidityHigh = (uint8_t)(rawData>>32);			// zadnjih 8 bitov

		printf("ParityBit = %d\n",ParityBit);
		printf("Vsota = %d\n",(uint8_t)(HumidityHigh+HumidityLow+TempHigh+TempLow));

		// Preverimo pariteto (High humidity 8 + Low humidity 8 + High temp. 8 + Low temp. 8 = Parity bit)
		if( (uint8_t)(HumidityHigh+HumidityLow+TempHigh+TempLow) == ParityBit)			// Podatek je bil pravilno poslan
		{
				uint16_t Vlaga16_t = ((uint16_t)HumidityHigh<<8) | HumidityLow;
				uint16_t Temp16_t = ((uint16_t)TempHigh<<8) | TempLow;

				// Izpis vlage
				printf("Vlaga = %d.%1d %%RH\n",Vlaga16_t/10, Vlaga16_t%10);

				// Izpis temperature
				uint8_t sign = Temp16_t >> 15;						// MSb doloca predznak

				if(sign == 0)
				{
					printf("Temperatura = %d.%1d C\n",Temp16_t/10, Temp16_t%10);
				}
				else
				{
					Temp16_t &= ~(1<<15);						// Zbrisi bit, ki doloca predznak
					printf("Temperatura = -%d.%1d C\n",Temp16_t/10, Temp16_t%10);
				}
		}																				// konec if parity bit validation
		else
		{
			printf("Napaka pri preverjanju paritete\n");
		}
	}
}


void Delay_ms(int16_t ms)
{
	TIM4->CNT = 0;					// Resetiraj counter na 0
	overflowCounter = 0;
	TIM4->CR1 |= TIM_CR1_CEN;			// Enable timer
	while(overflowCounter < ms);			// Cakaj dokler se ne zgudi ms stevilo prekinitev (vsak overflow pomeni 1 ms)
	TIM4->CR1 &= ~TIM_CR1_CEN;			// Disable timer
}


void TIM4_IRQHandler(void)
{
	// Prekinitvena funkcija za delay
	if( READ_BIT(TIM4->SR,TIM_SR_UIF) == 1 )
	{						// razlog za prekinitev: timer je prestel do ARR (counter overflow)
		overflowCounter++;
		CLEAR_BIT(TIM4->SR,TIM_SR_UIF);		// Pocistimo UIF bit (drugace procesor ves cas klice to prekinitveno rutino)
	}
}
