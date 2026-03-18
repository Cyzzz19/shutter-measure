#ifndef __OLED_H
#define __OLED_H			  	 
#include "stm32f1xx_hal.h"
#include "stdlib.h"	    
#include <stdint.h>

// Define short type aliases for convenience, matching typical STM32 codebases
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define OLED_MODE 0
#define SIZE 8
#define XLevelL		0x00
#define XLevelH		0x10
#define Max_Column	128
#define Max_Row		64
#define	Brightness	0xFF 
#define X_WIDTH 	128
#define Y_WIDTH 	64	    						  
//-----------------OLED IIC端口定义----------------  					   

// I2C handled by HAL, GPIO definitions removed

 		     
#define OLED_CMD  0	//写命令
#define OLED_DATA 1	//写数据


//OLED控制用函数
void OLED_WR_Byte(unsigned dat,unsigned cmd);  
void OLED_Display_On(void);
void OLED_Display_Off(void);	   							   		    
void OLED_Init(void);
void OLED_Clear(void);
void OLED_DrawPoint(u8 x,u8 y,u8 t);
void OLED_Fill(u8 x1,u8 y1,u8 x2,u8 y2,u8 dot);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 Char_Size);
void OLED_ShowIcon(u8 x,u8 y,u8 c);
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size);
void OLED_ShowString(u8 x,u8 y, u8 *p,u8 Char_Size);	 
void OLED_Set_Pos(unsigned char x, unsigned char y);
void OLED_ShowCHinese(u8 x,u8 y,u8 no);
void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[]);
// Fill a rectangular area with a dot (1) or clear (0)
void OLED_Fill(u8 x_start, u8 y_start, u8 x_end, u8 y_end, u8 dot);
// UI helper functions
void OLED_ShowSplash(void);
void OLED_ShowMainSpeed(u32 speed, uint8_t trigger);
void OLED_ShowMenu(void);
void fill_picture(unsigned char fill_Data);
void Picture();
extern I2C_HandleTypeDef hi2c1; // I2C1 handle defined in main
void Write_IIC_Command(unsigned char IIC_Command);
void Write_IIC_Data(unsigned char IIC_Data);
void OLED_Invert_Rect(u8 x1, u8 y1, u8 x2, u8 y2);
void OLED_Invert_String(u8 x, u8 y, u8 *p, u8 Char_Size);
#endif  
	 



