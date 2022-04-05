//YWROBOT
//last updated on 21/12/2011
//Tim Starling Fix the reset bug (Thanks Tim)
//wiki doc http://www.dfrobot.com/wiki/index.php?title=I2C/TWI_LCD1602_Module_(SKU:_DFR0063)
//Support Forum: http://www.dfrobot.com/forum/
//Compatible with the Arduino IDE 1.0
//Library version:1.1


#include "PinNames.h"
#include "basic_types.h"
#include "diag.h"
#include <osdep_api.h>

#include "i2c_api.h"
#include "pinmap.h"
#include "ex_api.h"
#include "LiquidCrystal_I2C.h"

#define MBED_I2C_MTR_SDA    PC_4
#define MBED_I2C_MTR_SCL    PC_5

#define MBED_I2C_BUS_CLK        100000  //hz


#if defined (__ICCARM__)
i2c_t   i2cmaster;
#else
volatile i2c_t   i2cmaster;
#endif

uint8_t _Addr;
uint8_t _displayfunction;
uint8_t _displaycontrol;
uint8_t _displaymode;
uint8_t _numlines;
uint8_t _cols;
uint8_t _rows;
uint8_t _backlightval;

#define printIIC(args)	i2c_send_byte(args)

void Wire_beginTransmission(uint8_t addr)
{
}

void Wire_endTransmission()
{
}


void Wire_begin()
{
}

void Wire_send(uint8_t arg)
{
    arg=arg;
}

void LiquidCrystal_I2C_write(uint8_t value) {
	LiquidCrystal_I2C_send(value, Rs);
}


void LiquidCrystal_I2C_print(char c[])
{
    char *p = c;
    while (*p!=0)
    {
        LiquidCrystal_I2C_send(*p, Rs);
        p++;
    }
}
// extern funciton
void delay(int ms) {
     vTaskDelay(ms);
}


// When the display powers up, it is configured as follows:
//
// 1. Display clear
// 2. Function set: 
//    DL = 1; 8-bit interface data 
//    N = 0; 1-line display 
//    F = 0; 5x8 dot character font 
// 3. Display on/off control: 
//    D = 0; Display off 
//    C = 0; Cursor off 
//    B = 0; Blinking off 
// 4. Entry mode set: 
//    I/D = 1; Increment by 1
//    S = 0; No shift 
//
// Note, however, that resetting the Arduino doesn't reset the LCD, so we
// can't assume that its in that state when a sketch starts (and the
// LiquidCrystal constructor is called).

void LiquidCrystal_I2C_LiquidCrystal_I2C(uint8_t lcd_Addr,uint8_t lcd_cols,uint8_t lcd_rows)
{
	i2c_init(&i2cmaster, MBED_I2C_MTR_SDA ,MBED_I2C_MTR_SCL);
    i2c_frequency(&i2cmaster,MBED_I2C_BUS_CLK);
    DBG_8195A("master write...\n");
    
  _Addr = lcd_Addr;
  _cols = lcd_cols;
  _rows = lcd_rows;
  _backlightval = LCD_NOBACKLIGHT;
}

void LiquidCrystal_I2C_init(){
	LiquidCrystal_I2C_init_priv();
}

void LiquidCrystal_I2C_init_priv()
{
	Wire_begin();
	_displayfunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;
	LiquidCrystal_I2C_begin(_cols, _rows, LCD_5x8DOTS);  
}

void LiquidCrystal_I2C_begin(uint8_t cols, uint8_t lines, uint8_t dotsize) {

    cols=cols;
	if (lines > 1) {

		_displayfunction |= LCD_2LINE;
	}
	_numlines = lines;

	// for some 1 line displays you can select a 10 pixel high font
	if ((dotsize != 0) && (lines == 1)) {
		_displayfunction |= LCD_5x10DOTS;
	}

	// SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
	// according to datasheet, we need at least 40ms after power rises above 2.7V
	// before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
	delay(50); 
  
	// Now we pull both RS and R/W low to begin commands
	LiquidCrystal_I2C_expanderWrite(_backlightval);	// reset expanderand turn backlight off (Bit 8 =1)
	delay(1000);

  	//put the LCD into 4 bit mode
	// this is according to the hitachi HD44780 datasheet
	// figure 24, pg 46
	
	  // we start in 8bit mode, try to set 4 bit mode
   LiquidCrystal_I2C_write4bits(0x03 << 4);
   delay(5); // wait min 4.1ms
   
   // second try
   LiquidCrystal_I2C_write4bits(0x03 << 4);
   delay(5); // wait min 4.1ms
   
   // third go!
   LiquidCrystal_I2C_write4bits(0x03 << 4); 
   delay(2);
   
   // finally, set to 4-bit interface
   LiquidCrystal_I2C_write4bits(0x02 << 4); 


	// set # lines, font size, etc.
	LiquidCrystal_I2C_command(LCD_FUNCTIONSET | _displayfunction);  
	
	// turn the display on with no cursor or blinking default
	_displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
	LiquidCrystal_I2C_display();
	
	// clear it off
	LiquidCrystal_I2C_clear();
	
	// Initialize to default text direction (for roman languages)
	_displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
	
	// set the entry mode
	LiquidCrystal_I2C_command(LCD_ENTRYMODESET | _displaymode);
	
	LiquidCrystal_I2C_home();
  
}

// ********** high level commands, for the user! 
void LiquidCrystal_I2C_clear(){
	LiquidCrystal_I2C_command(LCD_CLEARDISPLAY);// clear display, set cursor position to zero
	delay(2);  // this command takes a long time!
}

void LiquidCrystal_I2C_home(){
	LiquidCrystal_I2C_command(LCD_RETURNHOME);  // set cursor position to zero
	delay(2);  // this command takes a long time!
}

void LiquidCrystal_I2C_setCursor(uint8_t col, uint8_t row){
	int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
	if ( row > _numlines ) {
		row = _numlines-1;    // we count rows starting w/0
	}
	LiquidCrystal_I2C_command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

// Turn the display on/off (quickly)
void LiquidCrystal_I2C_noDisplay() {
	_displaycontrol &= ~LCD_DISPLAYON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidCrystal_I2C_display() {
	_displaycontrol |= LCD_DISPLAYON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turns the underline cursor on/off
void LiquidCrystal_I2C_noCursor() {
	_displaycontrol &= ~LCD_CURSORON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidCrystal_I2C_cursor() {
	_displaycontrol |= LCD_CURSORON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turn on and off the blinking cursor
void LiquidCrystal_I2C_noBlink() {
	_displaycontrol &= ~LCD_BLINKON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidCrystal_I2C_blink() {
	_displaycontrol |= LCD_BLINKON;
	LiquidCrystal_I2C_command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// These commands scroll the display without changing the RAM
void LiquidCrystal_I2C_scrollDisplayLeft(void) {
	LiquidCrystal_I2C_command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}
void LiquidCrystal_I2C_scrollDisplayRight(void) {
	LiquidCrystal_I2C_command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

// This is for text that flows Left to Right
void LiquidCrystal_I2C_leftToRight(void) {
	_displaymode |= LCD_ENTRYLEFT;
	LiquidCrystal_I2C_command(LCD_ENTRYMODESET | _displaymode);
}

// This is for text that flows Right to Left
void LiquidCrystal_I2C_rightToLeft(void) {
	_displaymode &= ~LCD_ENTRYLEFT;
	LiquidCrystal_I2C_command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'right justify' text from the cursor
void LiquidCrystal_I2C_autoscroll(void) {
	_displaymode |= LCD_ENTRYSHIFTINCREMENT;
	LiquidCrystal_I2C_command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'left justify' text from the cursor
void LiquidCrystal_I2C_noAutoscroll(void) {
	_displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
	LiquidCrystal_I2C_command(LCD_ENTRYMODESET | _displaymode);
}

// Allows us to fill the first 8 CGRAM locations
// with custom characters
void LiquidCrystal_I2C_createChar(uint8_t location, uint8_t charmap[]) {
    int i;
	location &= 0x7; // we only have 8 locations 0-7
	LiquidCrystal_I2C_command(LCD_SETCGRAMADDR | (location << 3));
	for (i=0; i<8; i++) {
		LiquidCrystal_I2C_write(charmap[i]);
	}
}

// Turn the (optional) backlight off/on
void LiquidCrystal_I2C_noBacklight(void) {
	_backlightval=LCD_NOBACKLIGHT;
	LiquidCrystal_I2C_expanderWrite(0);
}

void LiquidCrystal_I2C_backlight(void) {
	_backlightval=LCD_BACKLIGHT;
	LiquidCrystal_I2C_expanderWrite(0);
}



// *********** mid level commands, for sending data/cmds 

void LiquidCrystal_I2C_command(uint8_t value) {
	LiquidCrystal_I2C_send(value, 0);
}


// ************ low level data pushing commands **********

// write either command or data
void LiquidCrystal_I2C_send(uint8_t value, uint8_t mode) {
	uint8_t highnib=value&0xf0;
	uint8_t lownib=(value<<4)&0xf0;
       LiquidCrystal_I2C_write4bits((highnib)|mode);
	LiquidCrystal_I2C_write4bits((lownib)|mode); 
}

void LiquidCrystal_I2C_write4bits(uint8_t value) {
	LiquidCrystal_I2C_expanderWrite(value);
	LiquidCrystal_I2C_pulseEnable(value);
}

void LiquidCrystal_I2C_expanderWrite(uint8_t _data){
    char buf[1];
    buf[0] = _data | _backlightval;
    i2c_write(&i2cmaster, _Addr, &buf[0], 1, 1);
}

void LiquidCrystal_I2C_pulseEnable(uint8_t _data){
	LiquidCrystal_I2C_expanderWrite(_data | En);	// En high
	delay(1);		// enable pulse must be >450ns
	
	LiquidCrystal_I2C_expanderWrite(_data & ~En);	// En low
	delay(50);		// commands need > 37us to settle
} 


// Alias functions

void LiquidCrystal_I2C_cursor_on(){
	LiquidCrystal_I2C_cursor();
}

void LiquidCrystal_I2C_cursor_off(){
	LiquidCrystal_I2C_noCursor();
}

void LiquidCrystal_I2C_blink_on(){
	LiquidCrystal_I2C_blink();
}

void LiquidCrystal_I2C_blink_off(){
	LiquidCrystal_I2C_noBlink();
}

void LiquidCrystal_I2C_load_custom_character(uint8_t char_num, uint8_t *rows){
		LiquidCrystal_I2C_createChar(char_num, rows);
}

void LiquidCrystal_I2C_setBacklight(uint8_t new_val){
	if(new_val){
		LiquidCrystal_I2C_backlight();		// turn backlight on
	}else{
		LiquidCrystal_I2C_noBacklight();		// turn backlight off
	}
}

//void LiquidCrystal_I2C_printstr(const char c[]){
//	//This function is not identical to the function used for "real" I2C displays
//	//it's here so the user sketch doesn't have to be changed 
//	print(c);
//}


// unsupported API functions
void LiquidCrystal_I2C_off(){}
void LiquidCrystal_I2C_on(){}
void LiquidCrystal_I2C_setDelay (int cmdDelay,int charDelay) {cmdDelay=charDelay=0;}
uint8_t LiquidCrystal_I2C_status(){return 0;}
uint8_t LiquidCrystal_I2C_keypad (){return 0;}
uint8_t LiquidCrystal_I2C_init_bargraph(uint8_t graphtype){graphtype=graphtype; return 0;}
void LiquidCrystal_I2C_draw_horizontal_graph(uint8_t row, uint8_t column, uint8_t len,  uint8_t pixel_col_end){row=column=len=pixel_col_end=0;}
void LiquidCrystal_I2C_draw_vertical_graph(uint8_t row, uint8_t column, uint8_t len,  uint8_t pixel_row_end){row=column=len=pixel_row_end=0;}
void LiquidCrystal_I2C_setContrast(uint8_t new_val){ new_val=new_val;}

	
