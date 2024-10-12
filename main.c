#include "stm32f10x.h"
#include "GPIO_STM32F10x.h"

#define INC_BTN 		4
#define LED2 			5
#define CLOCKTIME_BTN 	6
#define ALARMTIME_BTN 	7

typedef struct time_tag{
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
} time;

static uint64_t TIM3_interrupts;
static uint8_t clockTimeSetting;
static uint8_t alarmTimeSetting;
static uint8_t clockTimeBtnClick;
static uint8_t alarmTimeBtnClick;
static uint8_t incrementBtnClick;
static uint8_t alarmIsOn;
static uint8_t mutex;
static uint8_t alarmSignal;
static uint8_t mode;
static time currentTime, alarmTime;

void SystemCoreClockConfigure(void) {
	RCC->CR |= ((uint32_t)RCC_CR_HSEBYP);                    // Включение HSE 
	while ((RCC->CR & RCC_CR_HSERDY) == 0);                  

	RCC->CFGR = RCC_CFGR_SW_HSE;                             // Источник тактирования - HSE 
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE);  

	RCC->CFGR |= RCC_CFGR_HPRE_DIV1;                         // Настройка частоты HCLK, APB1, APB2
	RCC->CFGR |= RCC_CFGR_PPRE1_DIV1;                        
	RCC->CFGR |= RCC_CFGR_PPRE2_DIV4;                        

	RCC->CR &= ~RCC_CR_PLLON;                                // Настройка PLL
															 
	RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
	RCC->CFGR |=  (RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL4);
	RCC->CR |= RCC_CR_PLLON;                               	 // PLL конфигурация: = HSE * 4 = 32 МГц
								 
	while((RCC->CR & RCC_CR_PLLRDY) == 0) __NOP();           

	RCC->CFGR &= ~RCC_CFGR_SW;                               // Выбор PLL в качестве источника тактирования
	RCC->CFGR |=  RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);  
}

void TIM3_Init() {															 
	RCC -> APB1ENR |= RCC_APB1ENR_TIM3EN; 				 	 // Настройка таймера TIM3 (прерывание срабатывает каждую секунду)
	TIM3 -> CR1 = TIM_CR1_CEN;										 

	TIM3->PSC = (uint16_t)(SystemCoreClock / 1000 - 1);	     // Определение значений предделителя (длительность одного тика таймера) 
	TIM3->ARR = 1000 - 1;									 // и регистра автоматичекой перезагрузки (количество тиков)
			
	TIM3->DIER |= TIM_DIER_UIE;								 // Включение прерываний
	NVIC_EnableIRQ (TIM3_IRQn);										 
}

void GPIO_Init(void) {            		 	 
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; // Настройка порта GPIO 
	
	GPIO_PinConfigure(GPIOA, LED2, GPIO_OUT_PUSH_PULL, GPIO_MODE_OUT10MHZ); // Настройка подачи сигнала на светодиод LD2 (порт PA5)
	
	GPIO_PinConfigure(GPIOA, INC_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); // Настройка подачи сигнала на порт PA4 (кнопка прибавления единицы к настраиваемому параметру (минуты или часы))
	GPIO_PinConfigure(GPIOB, CLOCKTIME_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); // Настройка подачи сигнала на порт PA6 (кнопка настройки текущего времени)
	GPIO_PinConfigure(GPIOC, ALARMTIME_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); // Настройка подачи сигнала на порт PA7 (кнопка настройки времени срабатывания будильника)

//	GPIOA->CRL &= ~(15ul << 4 * LED2);              	 
//	GPIOA->CRL |=  (1ul << 4 * LED2);
}

void NVIC_InputInit()
{
	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
	
	AFIO->EXTICR[1] |= AFIO_EXTICR2_EXTI4_PA | AFIO_EXTICR2_EXTI6_PA | AFIO_EXTICR2_EXTI7_PA; // interrupt source
	EXTI->IMR |= EXTI_IMR_MR4 | EXTI_IMR_MR6 | EXTI_IMR_MR7;
	EXTI->RTSR |= EXTI_RTSR_TR4 | EXTI_RTSR_TR6 | EXTI_RTSR_TR7;
	NVIC_EnableIRQ(EXTI4_IRQn);
	NVIC_EnableIRQ(EXTI9_5_IRQn);
}

/* Включение светодиода */
void ALARM_ON() {
	alarmSignal = 1;
	GPIOA->BSRR = 1ul << LED2;                                  
}

/* Выключение светодиода */
void ALARM_OFF() {
	alarmSignal = 0;
	alarmIsOn = 0;	
	GPIOA->BSRR = 1ul << (LED2 + 16);
}

/* Настройка обработчика прерываний для TIM3 */
void TIM3_IRQHandler() {													
	TIM3->SR &= ~TIM_SR_UIF;												// Снятие флага события обновления
	TIM3_interrupts++;														// Увеличение счетчика прерываний
	
	if(!clockTimeSetting)
	{
		currentTime.hours = (uint8_t)(TIM3_interrupts % 86400 / 3600);
		currentTime.minutes = (uint8_t)(TIM3_interrupts % 3600 / 60);
		currentTime.seconds = (uint8_t)(TIM3_interrupts % 60);
	}
}

void EXTI4_IRQHandler(void)
{
	incrementBtnClick = (!mutex) & (clockTimeSetting | alarmTimeSetting | alarmSignal);
	EXTI->PR |= EXTI_PR_PR4; // Очищаем флаг
}

void EXTI9_5_IRQHandler(void)
{
	if(!mutex) 
	{
		if (EXTI->PR & (1ul << CLOCKTIME_BTN)) //проверяем прерывание от EXTI6
		{ 
			clockTimeBtnClick = 1;
			EXTI->PR |= (1ul << CLOCKTIME_BTN);  // Очищаем флаг
		}
		  
		if (EXTI->PR & (1ul << ALARMTIME_BTN)) //проверяем прерывание от EXTI6
		{ 
			alarmTimeBtnClick = 1;
			EXTI->PR |= (1ul << ALARMTIME_BTN);  // Очищаем флаг
		}
	}
}

uint8_t compareTime(time *time_1, time *time_2)
{
	return (time_1->hours == time_2->hours) && (time_1->minutes == time_2->minutes);
}

void TimeSet(time *p_time, uint8_t *buttonClick)
{
	p_time->hours = 0;
	p_time->minutes = 0;
	
	mode = 0;
	*buttonClick = 0;
	while(1)
	{
		switch(mode)
		{
			case 0:
			{
				if(incrementBtnClick)
				{
					mutex = 1;
					{
						p_time->minutes = (p_time->minutes + 1) % 60;
						incrementBtnClick = 0;
					}
					mutex = 0;
				}
				
				if(*buttonClick)
				{
					mutex = 1;
					{
						mode = 1;
						*buttonClick = 0;
					}
					mutex = 0;
				}
			}
				break;
			case 1:
			{
				if(incrementBtnClick)
				{
					mutex = 1;
					{
						p_time->hours = (p_time->hours + 1) % 24;
						incrementBtnClick = 0;
					}
					mutex = 0;
				}
				
				if(*buttonClick)
				{
					mutex = 1;
					{
						mode = 2;
						*buttonClick = 0;
					}
					mutex = 0;
				}
			}
				break;
			default: 
			{
				return;
			}
		}
	}
}

int main (void){
	SystemCoreClockConfigure();                             
	SystemCoreClockUpdate();
			
	GPIO_Init();
	TIM3_Init();
	NVIC_InputInit();
	while (1) 
	{
		if(!alarmSignal && alarmIsOn && compareTime(&currentTime, &alarmTime))
		{
			ALARM_ON();
		}
		
		if(alarmSignal && incrementBtnClick && !clockTimeSetting && !alarmTimeSetting)
		{
			ALARM_OFF();
		}
		
		if(clockTimeBtnClick)
		{
			clockTimeSetting = 1;
			TimeSet(&currentTime, &clockTimeBtnClick);
			TIM3_interrupts = currentTime.hours * 3600 + currentTime.minutes * 60;
			clockTimeSetting = 0;
		}
		
		if(alarmTimeBtnClick)
		{
			alarmTimeSetting = 1;
			TimeSet(&alarmTime, &alarmTimeBtnClick);
			alarmIsOn = 1;
			alarmTimeSetting = 0;
		}
	}
}
