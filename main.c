#include "stm32f10x.h"
#include "GPIO_STM32F10x.h"

#define INC_BTN 		4	// Кнопка увеличения изменяемого параметра (минуты или секунды) на 1, также используется для отключения сигнала сработавшего будильника
#define LED2 			5	// Светодиод для подачи сигнала (светодиод загорается при срабатывании будильника)
#define CLOCKTIME_BTN 	6	// Кнопка перехода в режим настройки часов (текущего времени) ИЛИ кнопка подтверждения установленных минут или часов в этом режиме
#define ALARMTIME_BTN 	7	// Кнопка перехода в режим настройки будильника ИЛИ кнопка подтверждения установленных минут или часов в этом режиме

typedef struct time_tag{	// Структура для хранения времени часов и будильника
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
} time;

static uint64_t TIM3_interrupts;	// Счетчик прерываний таймера для счета секунд

/* 
*	Флаги режимов работы
*	По умолчанию принимают нулевые значения
*/
static uint8_t clockTimeSetting;	// Флаг режима настройки часов
static uint8_t alarmTimeSetting;	// Флаг режима настройки будильника
static uint8_t clockTimeBtnClick;	// Флаг нажатия на кнопку настройки часов
static uint8_t alarmTimeBtnClick;	// флаг нажатия на кнопку настройки будильника
static uint8_t incrementBtnClick;	// Флаг нажатия на кнопку инкремента 
static uint8_t alarmIsOn;			// Флаг включения режима дежурства (1 - будильник включен, 0 - будильник отключен); защищает от непреднамеренной подачи сигнала тревоги
static uint8_t alarmSignal;			// Флаг сигнала тревоги (1 - светодиод горит, 0 - светодиод не горит)
static uint8_t mode;				// Флаг режима настройки времени (0 - настройка минут, 1 - настройка часов, 2 - настройка завершена)

static time currentTime, alarmTime;	// Экземпляры времени часов и будильника

/* Настройка тактирования */
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

/* Настройка таймера TIM3 (прерывание срабатывает каждую секунду) */
void TIM3_Init() {															 
	RCC -> APB1ENR |= RCC_APB1ENR_TIM3EN; 				 	 // Разрешение тактирования
	TIM3 -> CR1 = TIM_CR1_CEN;								 // Включение счетчика

	TIM3->PSC = (uint16_t)(SystemCoreClock / 1000 - 1);	     // Определение значений предделителя (длительность одного тика таймера) 
	TIM3->ARR = 1000 - 1;									 // и регистра автоматичекой перезагрузки (количество тиков)
			
	TIM3->DIER |= TIM_DIER_UIE;								 // Включение прерываний
	NVIC_EnableIRQ (TIM3_IRQn);										 
}

/* Настройка порта ввода-вывода*/
void GPIO_Init(void) {            		 	 
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; 					 						// Включение тактирования порта GPIO 
	
	GPIO_PinConfigure(GPIOA, LED2, GPIO_OUT_PUSH_PULL, GPIO_MODE_OUT10MHZ); 		// Настройка подачи сигнала на светодиод LD2 (порт PA5)
	
	GPIO_PinConfigure(GPIOA, INC_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); 			// Настройка подачи сигнала на порт PA4 (кнопка прибавления единицы к настраиваемому параметру (минуты или часы))
	GPIO_PinConfigure(GPIOB, CLOCKTIME_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); 	// Настройка подачи сигнала на порт PA6 (кнопка настройки текущего времени)
	GPIO_PinConfigure(GPIOC, ALARMTIME_BTN, GPIO_IN_PULL_DOWN, GPIO_MODE_INPUT); 	// Настройка подачи сигнала на порт PA7 (кнопка настройки времени срабатывания будильника)
}

/* Настройка внешних прерываний */
void NVIC_InputInit()
{
	RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;															// Включение тактирования альтернативных функций
	
	AFIO->EXTICR[1] |= AFIO_EXTICR2_EXTI4_PA | AFIO_EXTICR2_EXTI6_PA | AFIO_EXTICR2_EXTI7_PA;   // Выбор источников прерывания (PA4, PA6, PA7)
	EXTI->IMR |= EXTI_IMR_MR4 | EXTI_IMR_MR6 | EXTI_IMR_MR7;									// Разрешение генерации прерываний в периферии
	EXTI->RTSR |= EXTI_RTSR_TR4 | EXTI_RTSR_TR6 | EXTI_RTSR_TR7;								// Настройка детектирования прерывания по восходящему фронту
	NVIC_EnableIRQ(EXTI4_IRQn);																	// Разрешение прерывания в NVIC
	NVIC_EnableIRQ(EXTI9_5_IRQn);
}

/* Включение светодиода */
void ALARM_ON() {
	alarmSignal = 1;			// Начало подачи сигнала тревоги
	GPIOA->BSRR = 1ul << LED2;  // Зажигание светодиода                            
}

/* Выключение светодиода */
void ALARM_OFF() {
	alarmSignal = 0;					// Завершение подачи сигнала тревоги
	alarmIsOn = 0;	            		// Отключение режима дежурства будильника (при совпадении времени на следующие сутки будильник не сработает, после срабатывания его нужно заводить заново)
	GPIOA->BSRR = 1ul << (LED2 + 16); 	// Потухание светодиода
}

/* Настройка обработчика прерываний для TIM3 */
void TIM3_IRQHandler() {													
	TIM3->SR &= ~TIM_SR_UIF;												// Снятие флага события обновления
	TIM3_interrupts++;														// Увеличение счетчика прерываний
	
	if(!clockTimeSetting)													// Изменение времени в структуре в режиме настройки часов запрещено
	{
		currentTime.hours = (uint8_t)(TIM3_interrupts % 86400 / 3600);		// Вычисление часов, минут, секунд текущего времени
		currentTime.minutes = (uint8_t)(TIM3_interrupts % 3600 / 60);
		currentTime.seconds = (uint8_t)(TIM3_interrupts % 60);
	}
}

/* 
*	Обработка нажатия кнопки инкремента 
*	Нажатие кнопки фиксируется в следующих ситуациях:
*	1) в режимах настройки часов или будильника (для настройки минут или часов)
*	2) при подаче сигнала тревоги (для его выключения)
*   В обычном режиме работы нажатие игнорируется
*/
void EXTI4_IRQHandler(void)
{
	incrementBtnClick = clockTimeSetting | alarmTimeSetting | alarmSignal; 
	EXTI->PR |= EXTI_PR_PR4;	// Очистка флага
}

/* Обработка нажатия кнопок настройки часов и будильника */
void EXTI9_5_IRQHandler(void)
{
	clockTimeBtnClick = (EXTI->PR & (1ul << CLOCKTIME_BTN)); // Проверка срабатывания прерывания								
	alarmTimeBtnClick = (EXTI->PR & (1ul << ALARMTIME_BTN)); // Проверка срабатывания прерывания
	
	EXTI->PR |= (1ul << CLOCKTIME_BTN);						 // Очистка флагов
	EXTI->PR |= (1ul << ALARMTIME_BTN);  								
}

/* 
*	Функция сравнения времени 
*	Возвращает 1 при совпадени времени будильника и часов, в противном случае - 0
*/
uint8_t compareTime(time *time_1, time *time_2)
{
	return (time_1->hours == time_2->hours) && (time_1->minutes == time_2->minutes);
}

/*
*	Функция настройки времени для часов или будильника
*	Тип настраиваемого устройства (часы или будильник) определяется через указатель на кнопку p_buttonClick
*/
void TimeSet(time *p_time, uint8_t *p_buttonClick)
{
	p_time->hours = 0;		// Сброс времени
	p_time->minutes = 0;
	
	mode = 0;				// Установка режима настройки минут
	*p_buttonClick = 0;		// Обнуление нажатия кнопки настройки после перехода в режим настройки
	while(1)
	{
		switch(mode)		
		{
			case 0:			// Режим настройки минут
			{
				if(incrementBtnClick) 								// При нажатии кнопки инкремента
				{
					p_time->minutes = (p_time->minutes + 1) % 60;	// Увеличивается на 1 количество минут (диапазон: 0...59)
					incrementBtnClick = 0;							// Обнуляется флаг нажатия кнопки инкремента (клик считается обработанным)
				}
				
				if(*p_buttonClick)									// При нажатии кнопки настройки устройства
				{
					mode++;											// Происходит переход в следующий режим (в режим настройки часов)
					*p_buttonClick = 0;								// Обнуляется флаг нажатия кнопки настройки устройства (клик считается обработанным)
				}
			}
				break;
			case 1:			// Режим настройки часов
			{
				if(incrementBtnClick)								// При нажатии кнопки инкремента
				{
					p_time->hours = (p_time->hours + 1) % 24;		// Увеличивается количество часов (диапазон: 0...23)
					incrementBtnClick = 0;							// Обнуляется флаг нажатия кнопки инкремента (клик считается обработанным)
				}
				
				if(*p_buttonClick)									// При нажатии кнопки настройки устройства
				{
					mode++;											// Происходит переход в следующий режим (в режим завершения настройки)
					*p_buttonClick = 0;								// Обнуляется флаг нажатия кнопки настройки устройства (клик считается обработанным)
				}
			}
				break;
			default:        // Режим завершения настройки
			{
				return;		// Выход из функции
			}
		}
	}
}

int main (void){
	SystemCoreClockConfigure();     // Настройка тактирования                        
	SystemCoreClockUpdate();		// Обновление частоты
			
	GPIO_Init();					// Инициализация порта ввода-вывода, таймера и внешних прерываний
	TIM3_Init();
	NVIC_InputInit();
	while (1) 
	{
		/* Сигнал тревоги подается, если до этого он был выключен, будильник стоит на дежурстве и совпало время часов и будильника */
		if(!alarmSignal && alarmIsOn && compareTime(&currentTime, &alarmTime))
		{
			ALARM_ON();
		}
		
		/* Сигнал тревоги отключается, если до этого он был включен, нажата кнопка инкремента и устройство не находится в режиме настройки часов и будильника */
		if(alarmSignal && incrementBtnClick)
		{
			ALARM_OFF();
		}
		
		if(clockTimeBtnClick)														// При нажатии кнопки настройки часов
		{
			clockTimeSetting = 1;													// Устанавливается режим настройки часов
			TimeSet(&currentTime, &clockTimeBtnClick);								// Производится вызов функции настройки
			TIM3_interrupts = currentTime.hours * 3600 + currentTime.minutes * 60;  // После завершения настройки обновляется общее время на таймере
			clockTimeSetting = 0;													// Происходит возврат в обычный режим работы
		}
		
		if(alarmTimeBtnClick)														// При нажатии кнопки настройки будильника
		{
			alarmTimeSetting = 1;													// Устанавливается режим настройки будильника
			TimeSet(&alarmTime, &alarmTimeBtnClick);								// Производится вызов функции настройки 
			alarmIsOn = 1;															// После завершения настройки будильник ставится на дежурство
			alarmTimeSetting = 0;													// Происходит возврат в обычный режим работы
		}
	}
}
