#include <common.h>
#include <s3c2410.h>

/* 查表标准：12M晶振，MPLL输出400MHz */
#define S3C2440_MPLL_400MHZ     ((0x5c<<12)|(0x01<<4)|(0x01))
/* UPLL 48MHz，USB专用，手册推荐 */
#define S3C2440_UPLL_48MHZ      ((0x38<<12)|(0x02<<4)|(0x02))
/* CLKDIVN=0x05：FCLK:HCLK:PCLK = 1:4:8，HCLK=100MHz，PCLK=50MHz */
#define S3C2440_CLKDIV          0x05

DECLARE_GLOBAL_DATA_PTR;

static inline void delay (unsigned long loops)
{
	__asm__ volatile ("1:\n"
			  "subs %0, %1, #1\n"
			  "bne 1b":"=r" (loops):"0" (loops));
}

int dram_init (void)
{
    gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
    gd->bd->bi_dram[0].size = PHYS_SDRAM_1_SIZE;

    return 0;
}

int board_init(void)
{
    S3C24X0_CLOCK_POWER* const clk_power = S3C24X0_GetBase_CLOCK_POWER();
    S3C24X0_GPIO* const gpio = S3C24X0_GetBase_GPIO();

    // addr、chip select 
    gpio->GPACON = 0x007FFFFF;

    // turn on led_1
    gpio->GPBCON |= 1 << 10;
    gpio->GPBDAT &= ~(1 << 5);

    // uart_0;
    gpio->GPHCON &= ~((3 << 4) | (3 << 6));
    gpio->GPHCON |= ((2 << 4) | (2 << 6));
    gpio->GPHUP &= ~((1 << 2) | (1 << 3)); 

    // ===== S3C2440 CLOCK & POWER MANAGEMENT =====
    // 修正后标准顺序：LOCKTIME → CLKDIVN → MPLLCON → UPLLCON → 等待锁定 → 异步总线模式
    /* 1. PLL锁定时间 */
    clk_power->LOCKTIME = 0xFFFFFF;

    /* 2. 先设置分频寄存器 CLKDIVN（重点！先分频，再开PLL） */
    clk_power->CLKDIVN = S3C2440_CLKDIV;

    /* 3. 开启异步总线模式（HCLK≠FCLK时必须开启） */
    __asm__(
        "mrc p15,0,r1,c1,c0,0\n"
        "orr r1,r1,#0xc0000000\n"
        "mcr p15,0,r1,c1,c0,0\n"
        :::"r1"
    );

    /* 4. 再配置 MPLL、UPLL */
    clk_power->MPLLCON = S3C2440_MPLL_400MHZ;
    clk_power->UPLLCON = S3C2440_UPLL_48MHZ;

    /* 5. 等待PLL锁定 */
    delay(10000);

    /* 6. 后续 */
    gd->bd->bi_arch_number = MACH_TYPE_S3C2440;
    gd->bd->bi_boot_params = 0x30000100;

    icache_enable();
    dcache_enable();

    return 0;
}

