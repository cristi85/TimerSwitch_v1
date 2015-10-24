/**
  ******************************************************************************
  * @file    Project/STM8L15x_StdPeriph_Template/main.c
  * @author  MCD Application Team
  * @version V1.6.0
  * @date    28-June-2013
  * @brief   Main program body
  ******************************************************************************
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */  
	
/* Includes ------------------------------------------------------------------*/
#include "board.h"
#include "config.h"
#include "delay.h"
#include "errors.h"
#include "stm8l10x_it.h"
#include "timeout.h"
#include "rtms.h"
/** @addtogroup STM8L15x_StdPeriph_Template
  * @{
  */

/* Private define ------------------------------------------------------------*/
#define LIGHTSTATE_ON              (u8)1
#define LIGHTSTATE_OFF             (u8)0
#define READROM_U8(adr)            (u8)(*((u8 *)(u32)(adr)))
#define READROM_U16(adr)           (u16)(*((u16 *)(u32)(adr)))
#define READROM_U32(adr)           (u32)(*((u32 *)(u32)(adr)))
#define BLINK_REDLED(x)            {blink_redLED_times=(u8)x;   ((x==255)?(flag_blink_unlimited=TRUE):(flag_blink_unlimited=FALSE)); flag_blink_on_off=TRUE; cnt_state_redLED=0; LED_RED_ON; flag_blink_redLED=TRUE;}
#define BLINK_GREENLED(x)          {blink_greenLED_times=(u8)x; ((x==255)?(flag_blink_unlimited=TRUE):(flag_blink_unlimited=FALSE)); flag_blink_on_off=TRUE; cnt_state_greenLED=0; LED_GREEN_ON; flag_blink_greenLED=TRUE;}
#define BLINKSTOP_REDLED           {flag_blink_redLED=FALSE; LED_OFF;}
#define BLINKSTOP_GREENLED         {flag_blink_greenLED=FALSE; LED_OFF;}
#define ISBLINKING_REDLED          (flag_blink_redLED)
#define ISBLINKING_GREENLED        (flag_blink_greenLED)
#define STATUS_LOWBATT             (u8)0x01
#define HBRIDGE_CHARGE_TIME        (u16)1000  /* minimum H-Bridge capacitor charge time [ms] */
#define HBRIDGE_ON_TIME            (u8)100    /* H-Bridge conduction time [ms] */
#define MAX_NUM_RECEIVERS          (u8)10
#define BTN1_DELETE_ID_THR         (u16)3000
/* Private typedef -----------------------------------------------------------*/
typedef union
{
  struct
  {
    u8 b0;
    u8 b1;
    u8 b2;
    u8 b3;
  } B;
  u32 DW;
} tBtoDW;
typedef enum States 
{
  ST_INIT                      = 0,
  ST_WAIT_INPUT_OR_FLAG        = 1,
  ST_NRF_RCVD_DATA             = 2,
  ST_SWITCH_LIGHT              = 3,
  ST_WAIT_TIMEOUT1_CAP_CHARGE  = 4,
  ST_WAIT_TIMEOUT1_HBRIDGE_ON  = 5
} StatesType;

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static u8 LightState = LIGHTSTATE_OFF;
static tBtoDW BtoDW;
static _Bool LrnModeActive = FALSE;
static _Bool FLAG_BTN1_lock = FALSE;
static _Bool FLAG_reset_LEDblink_error = FALSE;
static _Bool FLAG_check_nrf_reg = FALSE;
static u8 error_nrf24_reg_cnt = 0;
static volatile StatesType state = ST_INIT;
static volatile u8 program_status = 0;
static volatile u8 interrupt_status = 0;
static volatile u8 debug_registers[8];
static u8 debug_reg_idx = 0;
static u8 task_1000ms_cnt = 0;
static u16 init_delay = U16_MAX;
static volatile u8 flash_erase_timing_test;
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
void TASK_1000ms(void);
void TASK_500ms(void);
void TASK_250ms(void);
void BTN1_Released(void);
void LearnNewID(void);
_Bool IsLearnedID(void);

// RUNTIME MEASUREMENT
RTMS_DECLARE(runtime_it_1ms);
RTMS_DECLARE(runtime_it_RFrcv);
/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
void main(void)
{
  flash_erase_timing_test = 0;
  program_status = 1;
  disableInterrupts();
  Config();
  program_status = 2;
  Errors_Init();
  RTMS_INIT(runtime_it_1ms);
  RTMS_INIT(runtime_it_RFrcv);
  enableInterrupts();
  LED_GREEN_ON;
  /* Wait for power supply settling */
  Timeout_SetTimeout2(200);
  while(!Timeout_IsTimeout2());
  LED_OFF;
  program_status = 3;
  if(RST_GetFlagStatus(RST_FLAG_IWDGF))
  {
    BLINK_REDLED(1);
  }
  else if(RST_GetFlagStatus(RST_FLAG_ILLOPF))
  {
    BLINK_REDLED(2);
  }
  RST_ClearFlag(RST_FLAG_POR_PDR | RST_FLAG_SWIMF | RST_FLAG_ILLOPF | RST_FLAG_IWDGF);
  while(ISBLINKING_REDLED);
  program_status = 4;
  HBRIDGE_OFF;
  Timeout_SetTimeout1(HBRIDGE_CHARGE_TIME);
  
  IWDG_Enable();
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);  /* 431.15ms for RL[7:0]= 0xFF */
  IWDG_SetReload(0xFF);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Disable);
  IWDG_ReloadCounter();
  program_status = 5;
  while (1)
  {
    switch(state)
    {
      case ST_INIT:
      {
        state = ST_WAIT_INPUT_OR_FLAG;
        break;
      }
      case ST_WAIT_INPUT_OR_FLAG:
      {  
        //state = ST_WAIT_TIMEOUT1_CAP_CHARGE;
        if(FLAG_1000ms)
        {
          FLAG_1000ms = FALSE;
        }
        /* ============== PRESS BTN1 with key repetition lock ================= */
        if(BTN1_DEB_STATE == BTN_PRESSED && !FLAG_BTN1_lock)
        {
          FLAG_BTN1_lock = TRUE;
        }
        if(BTN1_DEB_STATE == BTN_DEPRESSED && FLAG_BTN1_lock)
        {
          FLAG_BTN1_lock = FALSE;
          BTN1_Released();
        }
        /* ============== END PRESS BTN1 with key repetition lock ================= */
        
        break;
      }
      case ST_SWITCH_LIGHT:
      {
        LED_GREEN_ON;
        LIGHT_ON;
        LightState = LIGHTSTATE_ON;

        /* set timeout for H-Bridge ON */
        Timeout_SetTimeout1(HBRIDGE_ON_TIME);
        state = ST_WAIT_TIMEOUT1_HBRIDGE_ON;
        break;
      }
      case ST_WAIT_TIMEOUT1_CAP_CHARGE:
      {
        if(Timeout_IsTimeout1())
        {
          state = ST_SWITCH_LIGHT;
        }
        break;
      }
      case ST_WAIT_TIMEOUT1_HBRIDGE_ON:
      {
        if(Timeout_IsTimeout1())
        {
          HBRIDGE_OFF;
          /* set timeout for H-Bridge capacitor to charge */
          Timeout_SetTimeout1(HBRIDGE_CHARGE_TIME);
          BLINKSTOP_GREENLED;
          state = ST_WAIT_INPUT_OR_FLAG;
        }
        break;
      }
      default: break;
    }
    IWDG_ReloadCounter();
    if(Errors_IsError() && !FLAG_reset_LEDblink_error)
    {
      BLINK_REDLED(255);
      FLAG_reset_LEDblink_error = TRUE;
    }
    else 
    {
      if(FLAG_reset_LEDblink_error)
      {
        BLINKSTOP_REDLED;
        FLAG_reset_LEDblink_error = FALSE;
      }
    }
  }
}

void BTN1_Released()
{
  if(BTN1_press_timer < BTN1_DELETE_ID_THR)
  {
  
  }
  BTN1_press_timer = 0;
}

void TASK_1000ms()
{
  task_1000ms_cnt++;
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *   where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
