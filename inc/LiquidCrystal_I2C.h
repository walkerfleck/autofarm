//YWROBOT
#ifndef LiquidCrystal_I2C_h
#define LiquidCrystal_I2C_h

//#include "stdutils.h"
//#include "Print.h" 
//#include <Wire.h>

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// flags for backlight control
#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00

#define En 0b00000100  // Enable bit
#define Rw 0b00000010  // Read/Write bit
#define Rs 0b00000001  // Register select bit



void LiquidCrystal_I2C_LiquidCrystal_I2C(uint8_t lcd_Addr,uint8_t lcd_cols,uint8_t lcd_rows);

void LiquidCrystal_I2C_begin(uint8_t cols, uint8_t rows, uint8_t charsize);
void LiquidCrystal_I2C_clear();
void LiquidCrystal_I2C_home();
void LiquidCrystal_I2C_noDisplay();
void LiquidCrystal_I2C_display();
void LiquidCrystal_I2C_noBlink();
void LiquidCrystal_I2C_blink();
void LiquidCrystal_I2C_noCursor();
void LiquidCrystal_I2C_cursor();
void LiquidCrystal_I2C_scrollDisplayLeft();
void LiquidCrystal_I2C_scrollDisplayRight();
void LiquidCrystal_I2C_printLeft();
void LiquidCrystal_I2C_printRight();
void LiquidCrystal_I2C_leftToRight();
void LiquidCrystal_I2C_rightToLeft();
void LiquidCrystal_I2C_shiftIncrement();
void LiquidCrystal_I2C_shiftDecrement();
void LiquidCrystal_I2C_noBacklight();
void LiquidCrystal_I2C_backlight();
void LiquidCrystal_I2C_autoscroll();
void LiquidCrystal_I2C_noAutoscroll(); 
void LiquidCrystal_I2C_createChar(uint8_t, uint8_t[]);
void LiquidCrystal_I2C_setCursor(uint8_t, uint8_t); 
void LiquidCrystal_I2C_write(uint8_t);
void LiquidCrystal_I2C_command(uint8_t);
void LiquidCrystal_I2C_init();

////compatibility API function aliases
void LiquidCrystal_I2C_blink_on();						// alias for blink()
void LiquidCrystal_I2C_blink_off();       					// alias for noBlink()
void LiquidCrystal_I2C_cursor_on();      	 					// alias for cursor()
void LiquidCrystal_I2C_cursor_off();      					// alias for noCursor()
void LiquidCrystal_I2C_setBacklight(uint8_t new_val);				// alias for backlight() and nobacklight()
void LiquidCrystal_I2C_load_custom_character(uint8_t char_num, uint8_t *rows);	// alias for createChar()
void LiquidCrystal_I2C_printstr(const char[]);

////Unsupported API functions (not implemented in this library)
uint8_t LiquidCrystal_I2C_status();
void LiquidCrystal_I2C_setContrast(uint8_t new_val);
uint8_t LiquidCrystal_I2C_keypad();
void LiquidCrystal_I2C_setDelay(int,int);
void LiquidCrystal_I2C_on();
void LiquidCrystal_I2C_off();
uint8_t LiquidCrystal_I2C_init_bargraph(uint8_t graphtype);
void LiquidCrystal_I2C_draw_horizontal_graph(uint8_t row, uint8_t column, uint8_t len,  uint8_t pixel_col_end);
void LiquidCrystal_I2C_draw_vertical_graph(uint8_t row, uint8_t column, uint8_t len,  uint8_t pixel_col_end);
 

void LiquidCrystal_I2C_init_priv();
void LiquidCrystal_I2C_send(uint8_t, uint8_t);
void LiquidCrystal_I2C_write4bits(uint8_t);
void LiquidCrystal_I2C_expanderWrite(uint8_t);
void LiquidCrystal_I2C_pulseEnable(uint8_t);

void LiquidCrystal_I2C_print(char c[]);

#endif
