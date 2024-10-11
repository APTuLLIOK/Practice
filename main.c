#include "stm32f10x.h"

#define INC_BTN 		4
#define LED2 			5
#define CURTIME_BTN 	6
#define ALARMTIME_BTN 	7

typedef struct time_tag{
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
} time;

static uint64_t TIM3_interrupts;
static uint8_t currentTimeSetting = 0;
static time currentTime, alarmTime;

void SystemCoreClockConfigure(void) {
	RCC->CR |= ((uint32_t)RCC_CR_HSEBYP);                    // ��������� HSE 
	while ((RCC->CR & RCC_CR_HSERDY) == 0);                  

	RCC->CFGR = RCC_CFGR_SW_HSE;                             // �������� ������������ - HSE 
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE);  

	RCC->CFGR |= RCC_CFGR_HPRE_DIV1;                         // ��������� ������� HCLK, APB1, APB2
	RCC->CFGR |= RCC_CFGR_PPRE1_DIV1;                        
	RCC->CFGR |= RCC_CFGR_PPRE2_DIV4;                        

	RCC->CR &= ~RCC_CR_PLLON;                                // ��������� PLL
															 
	RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
	RCC->CFGR |=  (RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL4);
	RCC->CR |= RCC_CR_PLLON;                               	 // PLL ������������: = HSE * 4 = 32 ���
								 
	while((RCC->CR & RCC_CR_PLLRDY) == 0) __NOP();           

	RCC->CFGR &= ~RCC_CFGR_SW;                               // ����� PLL � �������� ��������� ������������
	RCC->CFGR |=  RCC_CFGR_SW_PLL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);  
}

void TIM3_Init() {															 
	RCC -> APB1ENR |= RCC_APB1ENR_TIM3EN; 				 	 // ��������� ������� TIM3 (���������� ����������� ������ �������)
	TIM3 -> CR1 = TIM_CR1_CEN;										 

	TIM3->PSC = (uint16_t)(SystemCoreClock * 2 / 1000 - 1);	     // ����������� �������� ������������ (������������ ������ ���� �������) 
	TIM3->ARR = 1000 - 1;									 // � �������� ������������� ������������ (���������� �����)
			
	TIM3->DIER |= TIM_DIER_UIE;								 // ��������� ����������
	NVIC_EnableIRQ (TIM3_IRQn);										 
}

void GPIO_Init(void) {
	RCC->APB2ENR |= (1ul << 2);                		 	 // ��������� ����� GPIO 
	
	GPIOA->CRL &= ~(15ul << 4 * INC_BTN);              	 // ��������� ������ ������� �� ���� PA4 (������ ����������� ������� � �������������� ��������� (������ ��� ����))
	GPIOA->CRL |=  (8ul << 4 * INC_BTN);

	GPIOA->CRL &= ~(15ul << 4 * LED2);              	 // ��������� ������ ������� �� ��������� LD2 (���� PA5)
	GPIOA->CRL |=  (1ul << 4 * LED2);
	
	GPIOA->CRL &= ~(15ul << 4 * CURTIME_BTN);            // ��������� ������ ������� �� ���� PA6 (������ ��������� �������� �������)
	GPIOA->CRL |=  (8ul << 4 * CURTIME_BTN);
	
	GPIOA->CRL &= ~(15ul << 4 * ALARMTIME_BTN);          // ��������� ������ ������� �� ���� PA7 (������ ��������� ������� ������������ ����������)
	GPIOA->CRL |=  (8ul << 4 * ALARMTIME_BTN);
}

/* ��������� ���������� */
void LED_ON() {
	GPIOA->BSRR = 1ul << 5;                                  
}

/* ���������� ���������� */
void LED_OFF() {											 
	GPIOA->BSRR = 1ul << 21;
}

/* ��������� ����������� ���������� ��� TIM3 */
void TIM3_IRQHandler() {													
	TIM3->SR &= ~TIM_SR_UIF;												// ������ ����� ������� ����������
	TIM3_interrupts++;														// ���������� �������� ����������
	
	if(!currentTimeSetting)
	{
		currentTime.hours = (uint8_t)(TIM3_interrupts % 86400 / 3600);
		currentTime.minutes = (uint8_t)(TIM3_interrupts % 3600 / 60);
		currentTime.seconds = (uint8_t)(TIM3_interrupts % 60);
	}
}

uint8_t compareTime(time *time_1, time *time_2)
{
	return (time_1->hours == time_2->hours) && (time_1->minutes == time_2->minutes);
}

void Delay()
{
	uint32_t cnt = SystemCoreClock / 5;
	while(--cnt);
}

uint8_t ButtonIsPressed(uint8_t portNum)
{
	if(GPIOA->IDR & (1ul << portNum))
	{
		Delay();
		return ( (GPIOA->IDR & (1ul << portNum)) ? 1 : 0 );
	}
	return 0;
}

static void TimeSet(time *p_time, uint8_t button)
{
	uint8_t mode;
	p_time->hours = 0;
	p_time->minutes = 0;
	
	mode = 0;
	while(1)
	{
		switch(mode)
		{
			case 0:
			{
				if(ButtonIsPressed(INC_BTN))
					p_time->minutes = (p_time->minutes + 1) % 60;
				
				if(ButtonIsPressed(button))
					mode = 1;
			}
				break;
			case 1:
			{
				if(ButtonIsPressed(INC_BTN))
					p_time->hours = (p_time->hours + 1) % 24;
				
				if(ButtonIsPressed(button))
					mode = 2;
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
	while (1) 
	{	
		if(ButtonIsPressed(CURTIME_BTN))
		{
			currentTimeSetting = 1;
			TimeSet(&currentTime, CURTIME_BTN);
			TIM3_interrupts = currentTime.hours * 3600 + currentTime.minutes * 60;
			currentTimeSetting = 0;
		}
		
		if(ButtonIsPressed(ALARMTIME_BTN))
		{
			TimeSet(&alarmTime, ALARMTIME_BTN);
		}
		
		if(compareTime(&currentTime, &alarmTime))
		{
			LED_ON();
		}
		
		if(ButtonIsPressed(INC_BTN))
		{
			LED_OFF();
		}
	}
}
