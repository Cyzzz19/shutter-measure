//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//中景园电子
//店铺地址：http://shop73023976.taobao.com/?spm=2013.1.0.0.M4PqC2
//
//  文 件 名   : main.c
//  版 本 号   : v2.0
//  作    者   : Evk123
//  生成日期   : 2014-0101
//  最近修改   : 
//  功能描述   : 0.69寸OLED 接口演示例程(STM32F103ZE系列IIC)
//              说明: 
//              ----------------------------------------------------------------
//              GND   电源地
//              VCC   接5V或3.3v电源
//              SCL   接PD6（SCL）
//              SDA   接PD7（SDA）            
//              ----------------------------------------------------------------
//Copyright(C) 中景园电子2014/3/16
//All rights reserved
//////////////////////////////////////////////////////////////////////////////////�

#include "oled.h"
#include "stdlib.h"
#include "oledfont.h"
#include "stm32f1xx_hal.h"
// HAL I2C handle defined in main.c
extern I2C_HandleTypeDef hi2c1;

/* ---------------------------------------------------------------------------
 * Simple HAL‑based I2C helper functions replacing the original bit‑bang
 * implementation that was removed. These functions provide the minimal API
 * expected by the existing OLED driver code.
 * --------------------------------------------------------------------------- */

/* Write a single byte to the OLED via I2C. */
void Write_IIC_Data(unsigned char IIC_Data)
{
	uint8_t data[2];
	data[0] = 0x40;
	data[1] = IIC_Data;
	HAL_I2C_Master_Transmit(&hi2c1, 0x78, data, 2, 200);
}

//OLED的显存
//存放格式如下.
//[0]0 1 2 3 ... 127	
//[1]0 1 2 3 ... 127	
//[2]0 1 2 3 ... 127	
//[3]0 1 2 3 ... 127	
//[4]0 1 2 3 ... 127	
//[5]0 1 2 3 ... 127	
//[6]0 1 2 3 ... 127	
//[7]0 1 2 3 ... 127 			   
/**********************************************
//IIC Start
**********************************************/
/**********************************************
//IIC Start
**********************************************/
/* Bit‑bang I2C functions removed – HAL I2C1 is used instead */
// IIC Write byte

/* Bit‑bang byte transmission removed – not used with HAL I2C */
/**********************************************
// IIC Write Command
**********************************************/
void Write_IIC_Command(unsigned char IIC_Command)
{
	uint8_t data[2];
	data[0] = 0x00;
	data[1] = IIC_Command;
	HAL_I2C_Master_Transmit(&hi2c1, 0x78, data, 2, 200);
}


void OLED_WR_Byte(unsigned dat,unsigned cmd)
{
	if(cmd)
			{

   Write_IIC_Data(dat);
   
		}
	else {
   Write_IIC_Command(dat);
		
	}


}


/********************************************
// fill_Picture
********************************************/
void fill_picture(unsigned char fill_Data)
{
	unsigned char m,n;
	for(m=0;m<8;m++)
	{
		OLED_WR_Byte(0xb0+m,0);		//page0-page1
		OLED_WR_Byte(0x00,0);		//low column start address
		OLED_WR_Byte(0x10,0);		//high column start address
		for(n=0;n<128;n++)
			{
				OLED_WR_Byte(fill_Data,1);
			}
	}
}

/*---------------------------------------------------------------
 * Fill a rectangular area with a dot (1) or clear (0).
 * x_start, x_end: column range 0‑127
 * y_start, y_end: page range 0‑7 (each page = 8 vertical pixels)
 * dot: 1 to set pixel, 0 to clear.
 *---------------------------------------------------------------*/
void OLED_Fill(u8 x_start, u8 y_start, u8 x_end, u8 y_end, u8 dot)
{
    u8 page, col;
    for(page = y_start; page <= y_end; ++page)
    {
        // Set starting position for this page
        OLED_Set_Pos(x_start, page);
        for(col = x_start; col <= x_end; ++col)
        {
            OLED_WR_Byte(dot, OLED_DATA);
        }
    }
}

/*
 * Draw a single pixel at (x, y).
 * t = 1 sets the pixel, t = 0 clears it.
 * The OLED controller uses page addressing: each page is 8 vertical pixels.
 * We set the position to the appropriate page and column, then write a
 * byte with the corresponding bit mask. For simplicity we write only the
 * mask; clearing writes the inverse mask which works for monochrome OLEDs.
 */
void OLED_DrawPoint(u8 x, u8 y, u8 t)
{
	if (x >= Max_Column || y >= Max_Row) return; // out of bounds guard
	u8 page = y / 8;
	u8 bit  = y % 8;
	u8 mask = (1 << bit);
	OLED_Set_Pos(x, page);
	if (t) {
		OLED_WR_Byte(mask, OLED_DATA);
	} else {
		OLED_WR_Byte(~mask, OLED_DATA);
	}
}


/***********************Delay****************************************/
void Delay_50ms(unsigned int Del_50ms)
{
	HAL_Delay(50);
}

void Delay_1ms(unsigned int Del_1ms)
{
	HAL_Delay(1);
}

//坐标设置

	void OLED_Set_Pos(unsigned char x, unsigned char y) 
{ 	OLED_WR_Byte(0xb0+y,OLED_CMD);
	OLED_WR_Byte(((x&0xf0)>>4)|0x10,OLED_CMD);
	OLED_WR_Byte((x&0x0f),OLED_CMD); 
}   	  
//开启OLED显示    
void OLED_Display_On(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC命令
	OLED_WR_Byte(0X14,OLED_CMD);  //DCDC ON
	OLED_WR_Byte(0XAF,OLED_CMD);  //DISPLAY ON
}
//关闭OLED显示     
void OLED_Display_Off(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC命令
	OLED_WR_Byte(0X10,OLED_CMD);  //DCDC OFF
	OLED_WR_Byte(0XAE,OLED_CMD);  //DISPLAY OFF
}		   			 
//清屏函数,清完屏,整个屏幕是黑色的!和没点亮一样!!!	  
void OLED_Clear(void)  
{  
	u8 i,n;		    
	for(i=0;i<8;i++)  
	{  
		OLED_WR_Byte (0xb0+i,OLED_CMD);    //设置页地址（0~7）
		OLED_WR_Byte (0x00,OLED_CMD);      //设置显示位置—列低地址
		OLED_WR_Byte (0x10,OLED_CMD);      //设置显示位置—列高地址   
		for(n=0;n<128;n++)OLED_WR_Byte(0,OLED_DATA); 
	} //更新显示
}
void OLED_On(void)  
{  
	u8 i,n;		    
	for(i=0;i<8;i++)  
	{  
		OLED_WR_Byte (0xb0+i,OLED_CMD);    //设置页地址（0~7）
		OLED_WR_Byte (0x00,OLED_CMD);      //设置显示位置—列低地址
		OLED_WR_Byte (0x10,OLED_CMD);      //设置显示位置—列高地址   
		for(n=0;n<128;n++)OLED_WR_Byte(1,OLED_DATA); 
	} //更新显示
}
//在指定位置显示一个字符,包括部分字符
//x:0~127
//y:0~63
//mode:0,反白显示;1,正常显示				 
//size:选择字体 16/12 
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 Char_Size)
{      	
	unsigned char c=0,i=0;	
		c=chr-' ';//得到偏移后的值			
		if(x>Max_Column-1){x=0;y=y+2;}
		if(Char_Size ==16)
			{
			OLED_Set_Pos(x,y);	
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i],OLED_DATA);
			OLED_Set_Pos(x,y+1);
			for(i=0;i<8;i++)
			OLED_WR_Byte(F8X16[c*16+i+8],OLED_DATA);
			}
			else {	
				OLED_Set_Pos(x,y);
				for(i=0;i<6;i++)
				OLED_WR_Byte(F6x8[c][i],OLED_DATA);
				
			}
}
//m^n函数
u32 oled_pow(u8 m,u8 n)
{
	u32 result=1;	 
	while(n--)result*=m;    
	return result;
}				  
//显示2个数字
//x,y :起点坐标	 
//len :数字的位数
//size:字体大小
//mode:模式	0,填充模式;1,叠加模式
//num:数值(0~4294967295);	 		  
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size2)
{         	
	u8 t,temp;
	u8 enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/oled_pow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size2/2)*t,y,' ',size2);
				continue;
			}else enshow=1; 
		 	 
		}
	 	OLED_ShowChar(x+(size2/2)*t,y,temp+'0',size2); 
	}
} 
//显示一个字符号串
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 Char_Size)
{
	unsigned char j=0;
	while (chr[j]!='\0')
	{		OLED_ShowChar(x,y,chr[j],Char_Size);
			x+=8;
		if(x>120){x=0;y+=2;}
			j++;
	}
}
//显示汉字
void OLED_ShowCHinese(u8 x,u8 y,u8 no)
{      			    
	u8 t,adder=0;
	OLED_Set_Pos(x,y);	
    for(t=0;t<16;t++)
		{
				OLED_WR_Byte(Hzk[2*no][t],OLED_DATA);
				adder+=1;
     }	
		OLED_Set_Pos(x,y+1);	
    for(t=0;t<16;t++)
			{	
				OLED_WR_Byte(Hzk[2*no+1][t],OLED_DATA);
				adder+=1;
      }					
}
/***********功能描述：显示显示BMP图片128×64起始点坐标(x,y),x的范围0～127，y为页的范围0～7*****************/
void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[])
{ 	
 unsigned int j=0;
 unsigned char x,y;
  
  if(y1%8==0) y=y1/8;      
  else y=y1/8+1;
	for(y=y0;y<y1;y++)
	{
		OLED_Set_Pos(x0,y);
    for(x=x0;x<x1;x++)
	    {      
	    	OLED_WR_Byte(BMP[j++],OLED_DATA);	    	
	    }
	}
} 

//初始化SSD1306					    
void OLED_Init(void)
{ 	

OLED_WR_Byte(0xAE,OLED_CMD);//--display off
	OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
	OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
	OLED_WR_Byte(0x40,OLED_CMD);//--set start line address  
	OLED_WR_Byte(0xB0,OLED_CMD);//--set page address
	OLED_WR_Byte(0x81,OLED_CMD); // contract control
	OLED_WR_Byte(0xFF,OLED_CMD);//--128   
	OLED_WR_Byte(0xA1,OLED_CMD);//set segment remap 
	OLED_WR_Byte(0xA6,OLED_CMD);//--normal / reverse
	OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
	OLED_WR_Byte(0x3F,OLED_CMD);//--1/32 duty
	OLED_WR_Byte(0xC8,OLED_CMD);//Com scan direction
	OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset
	OLED_WR_Byte(0x00,OLED_CMD);//
	
	OLED_WR_Byte(0xD5,OLED_CMD);//set osc division
	OLED_WR_Byte(0x80,OLED_CMD);//
	
	OLED_WR_Byte(0xD8,OLED_CMD);//set area color mode off
	OLED_WR_Byte(0x05,OLED_CMD);//
	
	OLED_WR_Byte(0xD9,OLED_CMD);//Set Pre-Charge Period
	OLED_WR_Byte(0xF1,OLED_CMD);//
	
	OLED_WR_Byte(0xDA,OLED_CMD);//set com pin configuartion
	OLED_WR_Byte(0x12,OLED_CMD);//
	
	OLED_WR_Byte(0xDB,OLED_CMD);//set Vcomh
	OLED_WR_Byte(0x30,OLED_CMD);//
	
	OLED_WR_Byte(0x8D,OLED_CMD);//set charge pump enable
	OLED_WR_Byte(0x14,OLED_CMD);//
	
	OLED_WR_Byte(0xAF,OLED_CMD);//--turn on oled panel
}  

/*-------------------------------------------------------------------
 * UI helper functions
 *-------------------------------------------------------------------*/

// Show splash screen with centered English title for 1 second
void OLED_ShowSplash(void)
{
	OLED_Clear();
	// Approximate centering: title length 22 characters, each 8px wide at size 16 => 176px, exceeds width.
	// Use a smaller font (size 12) to fit and start near left.
	const u8 title[] = "Shutter Speed Detector";
	OLED_ShowString(0, 2, (u8 *)title, 12);
	Delay_1ms(1000);
}

// Show main speed screen. "speed" is displayed as a four‑digit number.
// "trigger" indicates whether the top‑right indicator dot should be lit.
void OLED_ShowMainSpeed(u32 speed, uint8_t trigger)
{
	OLED_Clear();
	// Display speed as four digits using large font (size 16)
	OLED_ShowNum(0, 2, speed, 4, 16);
	// Append unit "fps" after the number
	const u8 unit[] = "fps";
	OLED_ShowString(4 * 8, 2, (u8 *)unit, 16);

	// Indicator dot at top‑right corner (approximate 2×2 pixels)
	if (trigger)
	{
		// Fill a small rectangle near the right edge
		OLED_Fill(120, 0, 127, 0, 1);
	}
	else
	{
		// Ensure the area is cleared when not triggered
		OLED_Fill(120, 0, 127, 0, 0);
	}
}

// Show menu screen with a list of options
void OLED_ShowMenu(void)
{
	OLED_Clear();
	const u8 *menu[] = {
		(u8 *)"Basic Measurement",
		(u8 *)"Front/Rear Curtain Sync",
		(u8 *)"Set Gear",
		(u8 *)"Stored Data",
		(u8 *)"Error Statistics",
		(u8 *)"Time Compensation",
		(u8 *)"Clear Data",
		(u8 *)"About"
	};
	for (uint8_t i = 0; i < 8; ++i)
	{
		OLED_ShowString(0, i * 2, (u8 *)menu[i], 12);
	}
}

/* 读取 OLED GRAM 数据（需要配置 OLED 支持读操作）*/
static uint8_t OLED_Read_Byte(void) {
    uint8_t data = 0;
    HAL_I2C_Mem_Read(&hi2c1, 0x78, 0x40, I2C_MEMADD_SIZE_8BIT, &data, 1, 10);
    return data;
}

/* 关键修复：反色矩形区域 */
void OLED_Invert_Rect(u8 x1, u8 y1, u8 x2, u8 y2) {
    if (x1 > x2 || y1 > y2) return;
    
    u8 page_start = y1 / 8;
    u8 page_end = y2 / 8;
    if (page_end > 7) page_end = 7;
    
    for (u8 page = page_start; page <= page_end; page++) {
        OLED_Set_Pos(x1, page);
        
        /* 发送命令：进入读 - 修改 - 写模式 */
        OLED_WR_Byte(0xE0, OLED_CMD);  // RMW 模式开始
        
        for (u8 x = x1; x <= x2; x++) {
            /* 读取当前像素数据 */
            uint8_t old_data = 0;
            HAL_I2C_Mem_Read(&hi2c1, 0x78, 0x40, I2C_MEMADD_SIZE_8BIT, &old_data, 1, 10);
            
            /* 反色 */
            uint8_t new_data = ~old_data;
            
            /* 写回反色数据 */
            OLED_WR_Byte(new_data, OLED_DATA);
        }
        
        OLED_WR_Byte(0xEE, OLED_CMD);  // RMW 模式结束
    }
}

/* 反色字符串 */
void OLED_Invert_String(u8 x, u8 y, u8 *p, u8 Char_Size) {
    if (!p) return;
    
    while (*p) {
        if (Char_Size == 16) {
            /* 反色 16x8 字符区域 */
            OLED_Invert_Rect(x, y, x + 8 - 1, y + 16 - 1);
            x += 8;
        } else {
            /* 反色 8x8 字符区域 */
            OLED_Invert_Rect(x, y, x + 6 - 1, y + 8 - 1);
            x += 6;
        }
        p++;
    }
}



























