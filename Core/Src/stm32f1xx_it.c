/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32f1xx_it.c
 * @brief   Interrupt Service Routines.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
extern void PulseCapture_OnCapture(uint32_t capture_value);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* main.c - HardFault 调试 */
#include <stdio.h>

/**
 * @brief 硬故障处理函数 - 直接定位汇编地址版本
 * @param sp 堆栈指针（由汇编代码传入）
 * @note 此版本删除所有printf调用，直接通过断点和变量查看定位
 */
void HardFault_Handler_C(uint32_t *sp)
{
    // 1. 提取关键寄存器值 - 这些变量会保留在调试器中
    volatile uint32_t fault_r0 = sp[0];      // R0寄存器
    volatile uint32_t fault_r1 = sp[1];      // R1寄存器
    volatile uint32_t fault_r2 = sp[2];      // R2寄存器
    volatile uint32_t fault_r3 = sp[3];      // R3寄存器
    volatile uint32_t fault_r12 = sp[4];     // R12寄存器
    volatile uint32_t fault_lr = sp[5];      // 链接寄存器（返回地址）
    volatile uint32_t fault_pc = sp[6];      // 程序计数器（故障发生位置）- 重点关注！
    volatile uint32_t fault_xpsr = sp[7];    // xPSR程序状态寄存器
    
    // 2. 提取故障状态寄存器 - 帮助分析故障原因
    volatile uint32_t cfsr = *((uint32_t*)0xE000ED28);  // 可配置故障状态寄存器
    volatile uint32_t hfsr = *((uint32_t*)0xE000ED2C);  // 硬故障状态寄存器
    volatile uint32_t mmfar = *((uint32_t*)0xE000ED34); // MemManage故障地址寄存器
    volatile uint32_t bfar = *((uint32_t*)0xE000ED38);  // 总线故障地址寄存器
    
    // 3. 分析使用的堆栈类型
    volatile uint32_t current_lr;
    __asm volatile("MOV %0, LR" : "=r" (current_lr));
    
    volatile uint32_t sp_type;
    if (current_lr & 0x04) {
        sp_type = 1;  // 使用PSP（进程堆栈）
    } else {
        sp_type = 0;  // 使用MSP（主堆栈）
    }
    
    // 4. 保存堆栈内容用于分析（前16个字）
    volatile uint32_t stack_dump[16];
    for (int i = 0; i < 16; i++) {
        if ((uint32_t)(sp + i) >= 0x20000000 && (uint32_t)(sp + i) < 0x20020000) {
            stack_dump[i] = sp[i];
        } else {
            stack_dump[i] = 0xDEADBEEF;  // 无效堆栈标记
        }
    }
    
    // 5. 故障计数器 - 方便统计故障发生次数
    static volatile uint32_t fault_counter = 0;
    fault_counter++;
    
    // 6. 故障代码行号估计（如果需要，可以通过调试器查看 fault_pc）
    // 在调试器中，查看 fault_pc 的值，然后在map文件或反汇编中查找对应位置
    
    // 在这里设置断点！
    // 断点触发后，在调试器中查看以下变量：
    // - fault_pc: 故障发生的汇编地址（最重要）
    // - fault_lr: 返回地址
    // - fault_r0, fault_r1, fault_r2: 函数参数
    // - cfsr: 故障原因
    // - stack_dump: 堆栈内容
    // - sp: 堆栈指针
    
    while (1)
    {
        // 无限循环，便于调试器查看变量
        // 可以在循环中设置硬件断点，或使用单步执行
        __asm volatile("NOP");  // 添加NOP便于调试
    }
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
 * @brief This function handles Non maskable interrupt.
 */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
 * @brief This function handles Hard fault interrupt.
 */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  __asm volatile(
      "TST LR, #4 \n"
      "ITE EQ \n"
      "MRSEQ R0, MSP \n"
      "MRSNE R0, PSP \n"
      "B HardFault_Handler_C \n");
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
 * @brief This function handles Memory management fault.
 */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
 * @brief This function handles Prefetch fault, memory access fault.
 */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
 * @brief This function handles Undefined instruction or illegal state.
 */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
 * @brief This function handles Debug monitor.
 */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/**
 * @brief This function handles TIM1 update interrupt.
 */
void TIM1_UP_IRQHandler(void)
{
  /* USER CODE BEGIN TIM1_UP_IRQn 0 */

  /* USER CODE END TIM1_UP_IRQn 0 */
  HAL_TIM_IRQHandler(&htim1);
  /* USER CODE BEGIN TIM1_UP_IRQn 1 */

  /* USER CODE END TIM1_UP_IRQn 1 */
}

/**
 * @brief This function handles TIM2 global interrupt.
 */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/* USER CODE BEGIN 1 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  PulseCapture_OnCapture(TIM2->CCR3);
}
/* USER CODE END 1 */
