#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_ARM920T      1   /* This is an ARM920T core  */
#define CONFIG_S3C2410      1   /* [warning] for FriendlyArm mini2440, not CONFIG_S3C2400 !!!! */
#define CONFIG_SMDK2410     1   /* for get_tbclk() */

/* input clock of PLL */
#define CONFIG_SYS_CLK_FREQ 12000000 /* FriendlyArm mini2440  has 12 MHz input clock */

#define USE_920T_MMU		1
#undef CONFIG_USE_IRQ			/* we don't need IRQ/FIQ stuff */

/* Size of malloc() pool */
#define CFG_MALLOC_LEN		(CFG_ENV_SIZE + 128*1024)
#define CFG_GBL_DATA_SIZE	128	/* size in bytes reserved for initial data */

// SERIAL
#define CONFIG_SERIAL1          1	/* we use SERIAL 1(uart_0) on mini2440*/ 
#define CONFIG_BAUDRATE		115200
/* valid baudrates */
#define CFG_BAUDRATE_TABLE	{ 9600, 19200, 38400, 57600, 115200 }

/*
 * console
 */
#define CFG_PROMPT          "frndlyArmMini2440#"   /* 命令行提示符 */
#define CFG_CBSIZE          256                    /* 控制台输入缓冲区大小 */
#define CFG_PBSIZE          (CFG_CBSIZE + sizeof(CFG_PROMPT) + 16)
#define CFG_MAXARGS         32              /* 最大命令参数个数 */
#define CFG_BARGSIZE        CFG_CBSIZE      /* 启动参数缓冲区大小 */

/*----------------------------
 
 */
#define CONFIG_COMMANDS \
			(CONFIG_CMD_DFL	 | \
			CFG_CMD_CACHE	 | \
			CFG_CMD_REGINFO  | \
			CFG_CMD_ELF)

/* this must be included AFTER the definition of CONFIG_COMMANDS (if any) */
#include <cmd_confdefs.h>

/*-----------------------------------------------------------------------
 * Stack sizes
 *
 * The stack sizes are set up in start.S using the settings below
 */
#define CONFIG_STACKSIZE    (128*1024)  /* regular stack */
#ifdef CONFIG_USE_IRQ
#define CONFIG_STACKSIZE_IRQ    (4*1024)    /* IRQ stack */
#define CONFIG_STACKSIZE_FIQ    (4*1024)    /* FIQ stack */
#endif

// SDRAM
#define CONFIG_NR_DRAM_BANKS    1
#define PHYS_SDRAM_1		0x30000000 /* SDRAM Bank #1 */
#define PHYS_SDRAM_1_SIZE	0x04000000 /* 64 MB */

/*-------------------------- NOR FLASH and environment organization ---------------------*/
#define CFG_MAX_FLASH_BANKS   1
#define CFG_FLASH_BASE        0x00000000
#define CFG_MONITOR_BASE      CFG_FLASH_BASE

#define PHYS_FLASH_SIZE       0x00200000   /* AM29LV160DB 2MB */
#define CFG_MAX_FLASH_SECT    35           /* AM29LV160DB 扇区数 */
#define CFG_ENV_ADDR          (CFG_FLASH_BASE + 0x1E0000)
#define CFG_ENV_SIZE          0x10000      /* 64KB，与扇区对齐 */
#define CFG_ENV_IS_IN_FLASH   1

/*------------------dm9000-------------------*/
#define CONFIG_DRIVER_DM9000        1
#define CONFIG_DM9000_BASE          0x20000300
#define DM9000_IO                   CONFIG_DM9000_BASE
#define DM9000_DATA                 (CONFIG_DM9000_BASE+4)
#define CONFIG_DM9000_USE_16BIT     1
#define CONFIG_DM9000_NO_SROM       1

/* 走通用 CFI 驱动，不要定义 CONFIG_AMD_LV800/LV400 */
#define CFG_FLASH_CFI          1
#define CFG_FLASH_CFI_DRIVER   1
#define CFG_FLASH_CFI_WIDTH    FLASH_CFI_16BIT   /* AM29LV160DB 是 16 位 */

/**
 -----------------kernal load addr--------------------------- 
 **/
#define CFG_LOAD_ADDR       0x30008000

/*-----------------------------------------------------------------
 * the PWM TImer 4 uses a counter of 15625 for 10 ms, so we need */
/* it to wrap 100 times (total 1562500) to get 1 sec. */
#define	CFG_HZ			1562500

/*--------------------------------------------------------------------*/
#define CFG_MEMTEST_START	0x30000000	/* memtest works on	*/
#define CFG_MEMTEST_END		0x33F00000	/* 63 MB in DRAM	*/

#endif // CONFIG_H
