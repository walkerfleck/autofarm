

ENTRY(Reset_Handler)

INCLUDE "export-rom_v02.txt"


MEMORY
{
  TCM (rwx) : ORIGIN = 0x1FFF0000, LENGTH = 65536
  ROM_USED_RAM (rwx)  : ORIGIN = 0x10000bc8, LENGTH = 21560
  //RECY_RAM (rwx)  : ORIGIN = 0x10002100, LENGTH = 16128
  BD_RAM (rwx)  : ORIGIN = 0x10006000, LENGTH = 434176
  RECY_RAM (rwx)  : ORIGIN = 0x10002100, LENGTH = 7936
  //BD_RAM (rwx)  : ORIGIN = 0x10004000, LENGTH = 442368
  SDRAM_RAM (rwx)  : ORIGIN = 0x30000000, LENGTH = 2M
}

/*
    ROM_USED_RAM : 0x10000bc8 ~ 0x10005fff  for boot loader


 */

SECTIONS
{
  __rom_bss_start__ = 0x10000300;
  __rom_bss_end__ = 0x10000bc8;

/*
  .ram.start.table :
  {
    __ram_image1_text_start__ = .;
    __ram_start_table_start__ = .;
    KEEP(*(SORT(.start.ram.data*)))
    __ram_start_table_end__ = .;
    
  } > ROM_USED_RAM
*/

/*  Add . to assign the start address of the section,                                     * 
  *  to prevent the change of the start address by ld doing section alignment */  


/* these 4 sections is used by ROM global variable */
  	/* Don't move them and never add RAM code variable to these sections */
/*
  .ram_image1.text . :
  {
    __image1_validate_code__ = .;
    KEEP(*(.image1.validate.rodata*))
    KEEP(*(.infra.ram.data*))
    KEEP(*(.timer.ram.data*))
    KEEP(*(.cutb.ram.data*))
    KEEP(*(.cutc.ram.data*))
    KEEP(*(.hal.ram.data*))
    __image1_bss_start__ = .;
    __image1_bss_end__ = .;
    __ram_image1_data_end__ = .;
    
    *(.hal.ram.text*)    
    *(.infra.ram.text*)
    __ram_image1_text_end__ = .;
  } > ROM_USED_RAM
*/
/*
  .tcm :
  {
    __tcm_start__ = .;
    *(.tcm.heap)
    *mem.o (.bss)
    *memp.o (.bss)
    __tcm_end__ = .;
  } > TCM
*/
  
  .bootloader :
  {
    KEEP(*(.loader.data*))
  } > ROM_USED_RAM
  
  OVERLAY 0x1FFF0000:
  {
  	.valid
  	{ 
	    *mem.o (.bss*)
	    *memp.o (.bss*)
	    *(.tcm.heap)
  	}
  	.dummy 
  	{ 
	  	__ram_image1_text_start__ = .;
	    __ram_start_table_start__ = .;
	    KEEP(*(SORT(.start.ram.data*)))
	    __ram_start_table_end__ = .;
		__image1_validate_code__ = .;
	    KEEP(*(.image1.validate.rodata*))
	    KEEP(*(.infra.ram.data*))
	    KEEP(*(.timer.ram.data*))
	    KEEP(*(.cutb.ram.data*))
	    KEEP(*(.cutc.ram.data*))
	    KEEP(*(.hal.ram.data*))
	    __image1_bss_start__ = .;
	    .ram_image1.bss$$Base = .;
    	__image1_bss_end__ = .;
    	.ram_image1.bss$$Limit = .;
    	__ram_image1_data_end__ = .;
	    
	    *(.hal.ram.text*)    
	    *(.infra.ram.text*)
  	}
  } > TCM

  .image2.start.table :
  {
    __ram_image2_text_start__ = .;
    __image2_entry_func__ = .;
    .image2.start.table1$$Base = .;
    KEEP(*(SORT(.image2.ram.data*)))
    __image2_validate_code__ = .;
    KEEP(*(.image2.validate.rodata*))
    KEEP(*(.custom.validate.rodata*))
  } > BD_RAM

  .ram_image2.text :
  {
    KEEP(*(.infra.ram.start*))
    *(.mon.ram.text*)
    *(.wlan.text*)
  } > BD_RAM

  .ram_image2.rodata :
  {
    *(.rodata*)
    *(.wlan.rodata*)
    *(.fwu.rodata*)
  } > BD_RAM

  .ram.data : 
  {
    __data_start__ = .;
    *(.wlan.data*)
    . = ALIGN(4);
    __data_end__ = .;
  	__ram_image2_text_end__ = .;    
  } > BD_RAM
  
  .ram.bss :
  {
    __bss_start__ = .;
    .ram.bss$$Base = .;
    *(.wlan.bss*)
    *(COMMON)
    *(.wlan.bdsram.data*)
    *(.bdsram.data*)
    __bss_end__ = .;
    .ram.bss$$Limit = .;
  } > BD_RAM

  .bf_data : 
  {
    __buffer_data_start__ = .;
    *(.bfsram.data*)
    __buffer_data_end__ = .;
    
  } > BD_RAM

  .bf_data2 : 
  {
    __buffer_data_start2__ = .;
    __buffer_data_end2__ = .;
    
  } > RECY_RAM

  .sdr_text :
  {
    __sdram_data_start__ = .;
    *(.hal.flash.text*)    
    *(.hal.sdrc.text*)
    *(.hal.gpio.text*)
    *(.sdram.text*)
    *(.p2p.text*)
    *(.wps.text*)
    *(.websocket.text*)
    *(.mdns.text*)
    *(.fwu.text*)
    *(.text*)
  } > SDRAM_RAM

  .sdr_rodata :
  {
    *(.sdram.rodata*)
    *(.p2p.rodata*)
    *(.wps.rodata*)
    *(.websocket.rodata*)
    *(.mdns.rodata*)
  } > SDRAM_RAM

  .sdr_data : 
  {
    *(.hal.flash.data*)    
    *(.hal.sdrc.data*)    
    *(.hal.gpio.data*)
    *(.sdram.data*)
    *(.p2p.data*)
    *(.wps.data*)    
    *(.websocket.data*)
    *(.mdns.data*)
    *(.data*)
    . = ALIGN(256);
    *(.fpb.remap*)
    . = ALIGN(4);
    __sdram_data_end__ = .;
  } > SDRAM_RAM

  .sdr_bss :
  {
    __sdram_bss_start__ = .;
    *(.sdram.bss*)
    *(.p2p.bss*)
    *(.wps.bss*)
    *(.websocket.bss*)
    *(.mdns.bss*)
    *(.fwu.data*)
    *(.bss*)
    __sdram_bss_end__ = .;
  } > SDRAM_RAM

 .heap :
 {
	__end__ = .;
	end = __end__;
	*(.heap*)
	__HeapLimit = .;
 } > BD_RAM

	/* .stack_dummy section doesn't contains any symbols. It is only
	 * used for linker to calculate size of stack sections, and assign
	 * values to stack symbols later */
	.stack_dummy :
	{
		*(.stack)
	} > BD_RAM

	/* Set stack top to end of RAM, and stack limit move down by
	 * size of stack_dummy section */
	__StackTop = ORIGIN(BD_RAM) + LENGTH(BD_RAM);
	__StackLimit = __StackTop - SIZEOF(.stack_dummy);
	PROVIDE(__stack = __StackTop);

	/* Check if data + heap + stack exceeds RAM limit */
	ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack")

}


