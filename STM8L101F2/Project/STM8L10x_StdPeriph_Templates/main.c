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

#define  USE_FULL_ASSERT
/** @addtogroup STM8L15x_StdPeriph_Template
  * @{
  */

/* Private define ------------------------------------------------------------*/
#define LOAD_POWERED                  (u8)1
#define LOAD_NOT_POWERED              (u8)0
#define BLINK_REDLED(x)               {blink_redLED_times=(u8)x;   ((x==255)?(flag_blink_unlimited=TRUE):(flag_blink_unlimited=FALSE)); flag_blink_on_off=TRUE; cnt_state_redLED=0; LED_RED_ON; flag_blink_redLED=TRUE;}
#define BLINK_GREENLED(x)             {blink_greenLED_times=(u8)x; ((x==255)?(flag_blink_unlimited=TRUE):(flag_blink_unlimited=FALSE)); flag_blink_on_off=TRUE; cnt_state_greenLED=0; LED_GREEN_ON; flag_blink_greenLED=TRUE;}
#define BLINKSTOP_REDLED              {flag_blink_redLED=FALSE; LED_OFF;}
#define BLINKSTOP_GREENLED            {flag_blink_greenLED=FALSE; LED_OFF;}
#define ISBLINKING_REDLED             (flag_blink_redLED)
#define ISBLINKING_GREENLED           (flag_blink_greenLED)
#define HBRIDGE_CHARGE_TIME           (u16)1000  /* minimum H-Bridge capacitor charge time [ms] */
#define HBRIDGE_ON_TIME               (u8)100    /* H-Bridge conduction time [ms] */
#define BTN1_SET_NEW_TIME             (u16)6000  /* 3000ms */
#define TIMER_VAL_DEFAULT             (u16)600   /* 600 seconds - 10min*/
#define TIMER_VAL_PROGRAMMING_START   (u16)1     /* 1 minute */
#define BTN1_DOUBLECLICK_SPEED        (u16)200
#define TIMER_DISP_REM_TIME_PAUSE_B   (u16)600
#define TIMER_DISP_REM_TIME_PAUSE_A   (u16)300
#define TIMER_DISP_REM_TIME           (u16)5000
#define TIMER_DISP_REM_TIME_INTERCHAR (u16)650
#define PROG_MODE_VALUE_REP_TIME      (u16)1100
#define PROG_MODE_VALUE_INTERCHAR     (u16)650
#define READROM_U16(rom_adr)          (u16)(*((u16*)(rom_adr)))
#define ROM_LOCATIONS_TIMER           (u8)10

/* Private typedef -----------------------------------------------------------*/
typedef enum States 
{
  ST_INIT               = 0,
  ST_WAIT_INPUT         = 1,
  ST_SWITCH_LOAD        = 2,
  ST_WAIT_CAP_CHARGE    = 3,
  ST_WAIT_HBRIDGE_ON    = 4
} StatesType;

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static u8 LoadStateRequest = LOAD_NOT_POWERED;
static u8 LoadState = LOAD_NOT_POWERED;
static _Bool FLAG_BTN1_lock = FALSE;
static _Bool FLAG_BTN1_long_lock = FALSE;
static _Bool FLAG_reset_LED_error = FALSE;
static _Bool FLAG_programming_mode = FALSE;
static u8 programming_mode_step = 0;
static u8 remaining_time_step = 0;
static volatile StatesType state = ST_INIT;
static u8 task_1000ms_cnt = 0;
static u16 timer_cnt_seconds = 0;
static u16 remaining_time = 0;
static u16 tmp_adr;
static _Bool FLAG_timer_on = FALSE;
static _Bool FLAG_first_click = FALSE;
static _Bool FLAG_continuous_load_operation = FALSE;
static _Bool FLAG_disp_rem_time = FALSE;
static u16 timer_value = TIMER_VAL_PROGRAMMING_START;
static const u16 timer_val_stored[ROM_LOCATIONS_TIMER] = {TIMER_VAL_DEFAULT, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const u16 timer_val_stored_default = TIMER_VAL_DEFAULT;
static u8 ROM_location_timer_idx = 0;
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
void Retrieve_Check_ROM_Timer_Val(void);
void Error_Handler(void);
void Task_1000ms(void);
void TimerSwitch_StateMachine(void);
void Button_Press_Manager(void);
void Programming_Mode_Manager(void);
void Program_Timer_Value(void);
void Btn1_LongPress_Event(void);
void Btn1_ShortRelease_Event(void);
void Btn1_ShortDoubleClickRelease_Event(void);
void Display_Remaining_Time(void);
// RUNTIME MEASUREMENT
RTMS_DECLARE(runtime_it_1ms);
/**
  * @brief  Main program.
  * @param  None
  * @retval None
  */
void main(void)
{
  u8 i, cnt;
  disableInterrupts();
  Config();
  HBRIDGE_OFF;
  Errors_Init();
  RTMS_INIT(runtime_it_1ms);
  enableInterrupts();
  LED_GREEN_ON;
  // Wait for power supply settling
  Timeout_SetTimeout2(200);
  while(!Timeout_IsTimeout2());
  // END Wait for power supply settling
  LED_OFF;
  // Handle RESET flags
  if(RST_GetFlagStatus(RST_FLAG_IWDGF)) {
    BLINK_REDLED(1);
  }
  else if(RST_GetFlagStatus(RST_FLAG_ILLOPF)) {
    BLINK_REDLED(2);
  }
  RST_ClearFlag(RST_FLAG_POR_PDR | RST_FLAG_SWIMF | RST_FLAG_ILLOPF | RST_FLAG_IWDGF);
  while(ISBLINKING_REDLED);
  // END Handle RESET flags
  Retrieve_Check_ROM_Timer_Val();
  Timeout_SetTimeout1(HBRIDGE_CHARGE_TIME);
  
  IWDG_Enable();
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_Prescaler_64);  /* 431.15ms for RL[7:0]= 0xFF */
  IWDG_SetReload(0xFF);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Disable);
  IWDG_ReloadCounter();
  
  LoadStateRequest = LOAD_NOT_POWERED;
  state = ST_WAIT_CAP_CHARGE;
  
  while (1)
  {
    TimerSwitch_StateMachine();
    Programming_Mode_Manager();
    Task_1000ms();
    Error_Handler();
    Display_Remaining_Time();
    IWDG_ReloadCounter();
  }
}

void Display_Remaining_Time()
{
  if(!FLAG_continuous_load_operation) {
    if(LoadState==LOAD_POWERED && !Errors_IsError() && !FLAG_programming_mode) {
      switch(remaining_time_step) {
        case 0: {
          Timeout_SetTimeout2(TIMER_DISP_REM_TIME);
          remaining_time_step = 1;
          break;
        }
        case 1: {
          if(Timeout_IsTimeout2()) {
            remaining_time_step = 2;
          }
          break;
        }
        case 2: {
          Timeout_SetTimeout2(TIMER_DISP_REM_TIME_PAUSE_B);
          FLAG_disp_rem_time = TRUE;
          LED_OFF;
          remaining_time_step = 3;
          break;
        }
        case 3: {
          if(Timeout_IsTimeout2()) {
            remaining_time_step = 4;
          }
          break;
        }
        case 4: {
          if(!Errors_CheckError(ERROR_FLASH_WRITE)) {
            tmp_adr = (u16)&(timer_val_stored[ROM_location_timer_idx]);
          }
          else {
            tmp_adr = (u16)&(timer_val_stored_default);
          }
          remaining_time = READROM_U16(tmp_adr) - timer_cnt_seconds;
          remaining_time /= 60;  // convert from seconds to minutes
          remaining_time++;
          if(remaining_time < 10) {
            if(remaining_time > 0) {
              BLINK_GREENLED(remaining_time);
            }
          }
          else if(remaining_time < 100) {
            BLINK_GREENLED(remaining_time / 10);            
          }
          remaining_time_step = 5;
          break;
        }
        case 5: {
          if(!ISBLINKING_GREENLED) {
            if(remaining_time < 10) {
              remaining_time_step = 9;
            }
            else if(remaining_time < 100) {
              Timeout_SetTimeout2(TIMER_DISP_REM_TIME_INTERCHAR);
              remaining_time_step = 6;
            }
          }
          break;
        }
        case 6: {
          if(Timeout_IsTimeout2()) {
            remaining_time_step = 7;
          }
          break;
        }
        case 7: {
          if(remaining_time % 10) {
            BLINK_GREENLED(remaining_time % 10);  // remainder is different than 0
          }
          else { 
            BLINK_GREENLED(10);                   // if remainder is 0 blink 10 times
          }
          remaining_time_step = 8;
          break;
        }
        case 8: {
          if(!ISBLINKING_GREENLED) {
            remaining_time_step = 9;
          }
          break;
        }
        case 9: {
          Timeout_SetTimeout2(TIMER_DISP_REM_TIME_PAUSE_A);
          remaining_time_step = 10;
          break;
        }
        case 10: {
          if(Timeout_IsTimeout2()) {
            FLAG_disp_rem_time = FALSE;
            remaining_time_step = 0;
          }
          break;
        }
        default: break;
      }
    }
    else { 
      remaining_time_step = 0;
      FLAG_disp_rem_time = FALSE;
    }
  }
  else {
    remaining_time_step = 0;
    FLAG_disp_rem_time = FALSE;
  }
  if(!ISBLINKING_REDLED && !ISBLINKING_GREENLED && !FLAG_programming_mode && !FLAG_disp_rem_time && !Errors_IsError()) {
    if(LoadState == LOAD_POWERED) {
      LED_GREEN_ON;  
    }
    else {
      LED_OFF; 
    }
  }
}

void Error_Handler()
{
  if(Errors_IsError() && !FLAG_reset_LED_error) {
    LED_RED_ON;
    FLAG_reset_LED_error = TRUE;
  }
  else if(!Errors_IsError() && FLAG_reset_LED_error) {
    LED_OFF;
    FLAG_reset_LED_error = FALSE;
  }
}

void Retrieve_Check_ROM_Timer_Val()
{
  u8 cnt, i;
  /* Retrieve Timer Stored value index */
  cnt = 0;
  for(i=0; i<ROM_LOCATIONS_TIMER; i++) {
    tmp_adr = (u16)&(timer_val_stored[i]);
    if(READROM_U16(tmp_adr)) {
      ROM_location_timer_idx = i;
      cnt++;
      break;
    }
  }
  if(cnt != 1) {
    // timer_val_stored corrupt, all 0 values or more than one value != 0 were read
    tmp_adr = (u16)&(timer_val_stored[0]);
    FLASH_Unlock(FLASH_MemType_Program);
    FLASH_ProgramByte((u16)(tmp_adr)+0, (u8)(TIMER_VAL_DEFAULT >> 8));
    FLASH_ProgramByte((u16)(tmp_adr)+1, (u8)(TIMER_VAL_DEFAULT & (u16)0x00FF));
    if(READROM_U16(tmp_adr) != TIMER_VAL_DEFAULT) {
      Errors_SetError(ERROR_FLASH_WRITE);
    }
    for(i=1; i<ROM_LOCATIONS_TIMER; i++) {
      tmp_adr = (u16)&(timer_val_stored[i]);
      FLASH_ProgramByte((u16)(tmp_adr)+0, (u8)0x00);
      FLASH_ProgramByte((u16)(tmp_adr)+1, (u8)0x00);
      if(READROM_U16(tmp_adr) != 0) {
        Errors_SetError(ERROR_FLASH_WRITE);
      }
    }
    FLASH_Lock(FLASH_MemType_Program);
    ROM_location_timer_idx = 0;
    BLINK_GREENLED(5);
    while(ISBLINKING_GREENLED);
  }
  /* END Retrieve Timer Stored value index */
}

void Task_1000ms()
{
  if(FLAG_1000ms) {
    FLAG_1000ms = FALSE;
    task_1000ms_cnt++;
    if(FLAG_timer_on) timer_cnt_seconds++;
  }
}

void TimerSwitch_StateMachine()
{
  switch(state) {
    case ST_INIT: {
      state = ST_WAIT_INPUT;
      break;
    }
    case ST_WAIT_INPUT: {
      Button_Press_Manager();
      if(!Errors_CheckError(ERROR_FLASH_WRITE)) {
        tmp_adr = (u16)&(timer_val_stored[ROM_location_timer_idx]);
      }
      else {
        tmp_adr = (u16)&(timer_val_stored_default);
      }
      if(!FLAG_continuous_load_operation) {
        if( timer_cnt_seconds >= READROM_U16(tmp_adr) ) {
          LoadStateRequest = LOAD_NOT_POWERED;
          state = ST_WAIT_CAP_CHARGE;
        }
      }
      break;
    }
    case ST_SWITCH_LOAD: {
      switch(LoadStateRequest) {
        case LOAD_NOT_POWERED: {
          LOAD_OFF;
          LoadState = LOAD_NOT_POWERED;
          FLAG_timer_on = FALSE;
          timer_cnt_seconds = 0;
          break;
        }
        case LOAD_POWERED: {
          LOAD_ON;
          LoadState = LOAD_POWERED;
          if(!FLAG_continuous_load_operation) {
            FLAG_timer_on = TRUE;
          }
          timer_cnt_seconds = 0;
          break;
        }
        default: break;
      }
      Timeout_SetTimeout1(HBRIDGE_ON_TIME);  // set timeout for H-Bridge ON
      state = ST_WAIT_HBRIDGE_ON;
      break;
    }
    case ST_WAIT_CAP_CHARGE: {
      if(Timeout_IsTimeout1()) {
        state = ST_SWITCH_LOAD;
      }
      break;
    }
    case ST_WAIT_HBRIDGE_ON: {
      if(Timeout_IsTimeout1()) {
        HBRIDGE_OFF;
        Timeout_SetTimeout1(HBRIDGE_CHARGE_TIME);  // set timeout for H-Bridge capacitor to charge
        state = ST_WAIT_INPUT;
      }
      break;
    }
    default: break;
  }
}

void Btn1_LongPress_Event()
{
  if(FLAG_programming_mode) {
    FLAG_programming_mode = FALSE;
    BLINKSTOP_REDLED;
    Program_Timer_Value();
  }
  else {
    if(!Errors_IsError()) {
      FLAG_programming_mode = TRUE;
      programming_mode_step = 0;
      BLINKSTOP_GREENLED;
      remaining_time_step = 0;
      timer_value = TIMER_VAL_PROGRAMMING_START;
    }
  }
}

void Btn1_ShortRelease_Event()
{
  if(FLAG_programming_mode) {
    if(timer_value < 99) {
      timer_value++;
    }
  }
  else {
    if(LoadState == LOAD_NOT_POWERED) {
      FLAG_continuous_load_operation = FALSE;
      BLINKSTOP_GREENLED;
      LoadStateRequest = LOAD_POWERED;  
      state = ST_WAIT_CAP_CHARGE;
    }
    else {
      FLAG_continuous_load_operation = FALSE;
      BLINKSTOP_GREENLED;
      LoadStateRequest = LOAD_NOT_POWERED;
      state = ST_WAIT_CAP_CHARGE;
    }
  }
}

void Btn1_ShortDoubleClickRelease_Event()
{
  if(FLAG_programming_mode) {
    
  }
  else {
    if(LoadState == LOAD_NOT_POWERED) {
      FLAG_continuous_load_operation = TRUE;
      BLINKSTOP_GREENLED;
      LoadStateRequest = LOAD_POWERED;  
      state = ST_WAIT_CAP_CHARGE;
    }
    else {
      BLINKSTOP_GREENLED;
      LoadStateRequest = LOAD_NOT_POWERED;
      FLAG_continuous_load_operation = FALSE;
      state = ST_WAIT_CAP_CHARGE;
    }
  }
}

void Button_Press_Manager()
{
  if(BTN1_DEB_STATE==BTN_PRESSED && BTN1_press_timer>=BTN1_SET_NEW_TIME && !FLAG_BTN1_long_lock) {
    FLAG_BTN1_long_lock = TRUE;
    //button long press
    Btn1_LongPress_Event();
  }
  if(BTN1_DEB_STATE == BTN_PRESSED && !FLAG_BTN1_lock) {
    FLAG_BTN1_lock = TRUE;
    //button short press
  }
  if(BTN1_DEB_STATE == BTN_DEPRESSED && FLAG_BTN1_lock) {
    if(FLAG_BTN1_long_lock) {
      FLAG_BTN1_long_lock = FALSE;
      FLAG_BTN1_lock = FALSE;
      // release button after a long press
    }
    else {
      FLAG_BTN1_lock = FALSE;
      // release button after a short press
      if(!FLAG_programming_mode) {
        if(!FLAG_first_click) {
          FLAG_first_click = TRUE;
          Timeout_SetTimeout3(BTN1_DOUBLECLICK_SPEED);
        }
        else {
          FLAG_first_click = FALSE;
          Btn1_ShortDoubleClickRelease_Event();
        }
      }
      else {
        Btn1_ShortRelease_Event();
      }
    }
  }
  if(!FLAG_programming_mode) {
    if(FLAG_first_click && Timeout_IsTimeout3()) {
      FLAG_first_click = FALSE;
      Btn1_ShortRelease_Event();
    }
  }
}

void Program_Timer_Value()
{
  u16 timer_value_sec;
  timer_value_sec = timer_value * 60; // convert to seconds, the user selected value in timer_value is in minutes
  if(!Errors_CheckError(ERROR_FLASH_WRITE)) {
    tmp_adr = (u16)&(timer_val_stored[ROM_location_timer_idx]);
    if(timer_value_sec != READROM_U16(tmp_adr))
    {
      FLASH_Unlock(FLASH_MemType_Program);
      // Clear old timer value position
      FLASH_ProgramByte( (u16)(tmp_adr)+0, (u8)0x00 );
      FLASH_ProgramByte( (u16)(tmp_adr)+1, (u8)0x00 );
      if(READROM_U16(tmp_adr) != 0) {
        Errors_SetError(ERROR_FLASH_WRITE);
      }
      ROM_location_timer_idx++;
      if(ROM_location_timer_idx >= ROM_LOCATIONS_TIMER) {
        ROM_location_timer_idx = 0;
      }
      tmp_adr = (u16)&(timer_val_stored[ROM_location_timer_idx]);
      // Write next location with new user data
      FLASH_ProgramByte( (u16)(tmp_adr)+0, (u8)(timer_value_sec >> 8) );
      FLASH_ProgramByte( (u16)(tmp_adr)+1, (u8)(timer_value_sec & (u16)0x00FF) );
      FLASH_Lock(FLASH_MemType_Program);
      // Check what was written
      if(READROM_U16(tmp_adr) != timer_value_sec) {
        Errors_SetError(ERROR_FLASH_WRITE);
      }
      BLINK_GREENLED(2);
    }
    else {
      BLINK_GREENLED(1);
    }
  }
}

void Programming_Mode_Manager()
{
  if(FLAG_programming_mode)
  {
    switch(programming_mode_step)
    {
      case 0:
      {
        if(timer_value < 10) {
          BLINK_REDLED(timer_value);
        }
        else if(timer_value < 100) {
          BLINK_REDLED(timer_value / 10);            
        }
        programming_mode_step = 1;
        break;
      }
      case 1: {
        if(!ISBLINKING_REDLED) {
          if(timer_value < 10) {
            Timeout_SetTimeout2(PROG_MODE_VALUE_REP_TIME);
            programming_mode_step = 5;
          }
          else if(timer_value < 100) {
            Timeout_SetTimeout2(PROG_MODE_VALUE_INTERCHAR);
            programming_mode_step = 2;
          }
        }
        break;
      }
      case 2: {
        if(Timeout_IsTimeout2()) {
          programming_mode_step = 3;
        }
        break;
      }
      case 3: {
        if(timer_value % 10) {
          BLINK_REDLED(timer_value % 10);  // remainder is different than 0
        }
        else { 
          BLINK_REDLED(10);                                // if remainder is 0 blink 10 times
        }
        programming_mode_step = 4;
        break;
      }
      case 4: {
        if(!ISBLINKING_REDLED) {
          Timeout_SetTimeout2(PROG_MODE_VALUE_REP_TIME);
          programming_mode_step = 5;
        }
        break;
      }
      case 5: {
        if(Timeout_IsTimeout2()) {
          programming_mode_step = 0;
        }
        break;
      }
      default: break;
    }
  }
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
