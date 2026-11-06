# FriendlyARM mini2440 U-Boot 1.1.6 移植说明

> 基于 u-boot 1.1.6（2006-11）源码树，将 SMDK2410 的板级支持改造为 FriendlyARM mini2440（S3C2440A）。
> 本文档以工程当前实际代码为准（`fix.txt` 为对应补丁），记录了移植的所有改动、原理依据、验证结果与踩过的坑。

---

## 1. 目标平台

| 项目 | 参数 |
|---|---|
| CPU | Samsung S3C2440A（ARM920T，ARMv4T，5 级流水，16KB I/D Cache + MMU） |
| 主频 | FCLK 400 MHz / HCLK 100 MHz / PCLK 50 MHz |
| 晶振 | 12 MHz（XTIpll） |
| SDRAM | 64 MB × 1，起始 `0x3000_0000`（HY57V561620 × 2，32bit 位宽，13 行 9 列） |
| NOR Flash | 2 MB，起始 `0x0000_0000`（AM29LV160DB / SST39VF1601，16 bit） |
| NAND Flash | 板载有，但**本移植未启用**（无驱动，见 §9） |
| 网卡 | DM9000，`0x2000_0300`（16 bit） |
| 串口 | UART0，115200-8N1，`GPH2=TXD0 / GPH3=RXD0` |
| 调试口 | JTAG 20 pin（J-Link / J-Flash） |

### 1.1 关键地址表

| 用途 | 地址 | 说明 |
|---|---|---|
| U-Boot 运行地址 | `0x33F8_0000` | `TEXT_BASE`，距 SDRAM 顶部保留 512 KB |
| U-Boot 烧写地址 | `0x0000_0000` | NOR 起始（`.bin` 必须烧到 0x0） |
| 内核加载地址 | `0x3000_8000` | `CFG_LOAD_ADDR` |
| 内核传参区 | `0x3000_0100` | `bi_boot_params` |
| 环境变量 | `0x001E_0000` | 64 KB，位于 NOR 倒数第二个 64KB 扇区 |
| 环境变量长度 | `0x0001_0000` | `CFG_ENV_SIZE` |
| malloc 池 | 192 KB | `CFG_ENV_SIZE + 128KB` |
| 栈 | 128 KB | `CONFIG_STACKSIZE` |

> `u-boot.bin` 必须小于 **1.875 MB**（`0x1E0000`），否则会覆盖环境变量区。

---

## 2. 移植涉及的文件

| 文件 | 类型 | 内容 |
|---|---|---|
| `include/configs/frdArm_mini2440.h` | 重写 | 板级配置：串口、SDRAM、NOR、DM9000、命令集 |
| `board/frdArm_mini2440/frdArm_mini2440.c` | 重写 | `board_init()`：GPIO、时钟、Cache；`dram_init()` |
| `board/frdArm_mini2440/lowlevel_init.S` | 沿用 SMDK2410 | SDRAM 控制器 13 个寄存器初始化 |
| `board/frdArm_mini2440/u-boot.lds` | 沿用 | 链接脚本，`ENTRY(_start)`，段顺序 start.o 在最前 |
| `board/frdArm_mini2440/config.mk` | 沿用 | `TEXT_BASE = 0x33F80000` |
| `board/frdArm_mini2440/Makefile` | 改 | **去掉 `flash.o`**（改用通用 CFI 驱动） |
| `board/frdArm_mini2440/flash.c` | **删除** | 原为空文件，改用 `drivers/cfi_flash.c` |
| `cpu/arm920t/start.S` | 改 | 置 CP15 nF 位（异步总线模式） |
| `cpu/arm920t/s3c24x0/speed.c` | 改 | S3C2440 的 PLL/HCLK/PCLK 计算 |
| `include/s3c24x0.h` | 改 | `S3C24X0_CLOCK_POWER` 增加 `CAMDIVN` 字段 |
| `Makefile` | 改 | 注册 `frdArm_mini2440_cfg` 目标 |
| `tools/jlink/*.jlink` | 新增 | J-Link 诊断 / SDRAM 初始化脚本 |

---

## 3. 核心改动与依据

### 3.1 芯片宏：`CONFIG_S3C2400` → `CONFIG_S3C2410`（**最关键，曾经导致串口完全无输出**）

```c
-#define CONFIG_S3C2400      1
-// [TODO] #define CONFIG_SMDK2400     1
+#define CONFIG_S3C2410      1   /* for FriendlyArm mini2440, not CONFIG_S3C2400 !!!! */
+#define CONFIG_SMDK2410     1   /* for get_tbclk() */
```

u-boot 1.1.6 的 `serial.c` / `speed.c` / `interrupts.c` / `start.S` 全部按这个宏做条件编译，选不同的头文件与基址：

| 外设 | `s3c2400.h`（错误） | `s3c2410.h`（正确） | 2440 实际 |
|---|---|---|---|
| UART | `0x1500_0000` | `0x5000_0000` | `0x5000_0000` |
| CLOCK/POWER | `0x1480_0000` | `0x4C00_0000` | `0x4C00_0000` |
| TIMER | `0x1510_0000` | `0x5100_0000` | `0x5100_0000` |
| WATCHDOG | `0x1530_0000` | `0x5300_0000` | `0x5300_0000` |
| INTERRUPT | `0x1440_0000` | `0x4A00_0000` | `0x4A00_0000` |

**症状**：串口一个字节都没有（连乱码都没有），因为寄存器全写到未映射空间；更致命的是看门狗没被关掉（`WTCON` 复位值 `0x8021` 即使能），板子约 10 秒复位一次，表现为"不停重启"。反汇编 `u-boot.dis` 里可以直接验证：`0x15000000` 出现 7 次、`0x50000000` 出现 0 次。

**注意 `CONFIG_SMDK2410` 不能省**：`interrupts.c` 的 `get_tbclk()` 是三选一，去掉 `CONFIG_SMDK2400` 又不定义 2410，会直接 `#error "tbclk not configured"` 编译失败。

> u-boot 1.1.6 没有 `CONFIG_S3C2440` 这个概念，S3C2410/2440 共用同一套 `s3c24x0` 驱动，差异靠运行时判断。

### 3.2 GPIO 初始化

```c
gpio->GPACON = 0x007FFFFF;                       /* GPA0~GPA22 作地址线 */
gpio->GPBCON |= 1 << 10;                         /* GPB5 = 输出 */
gpio->GPBDAT &= ~(1 << 5);                       /* LED1 常亮 */

gpio->GPHCON &= ~((3 << 4) | (3 << 6));          /* 先清 GPH2/GPH3 */
gpio->GPHCON |=  ((2 << 4) | (2 << 6));          /* GPH2=TXD0, GPH3=RXD0 */
gpio->GPHUP  &= ~((1 << 2) | (1 << 3));          /* 清零 = 使能上拉 */
```

三个要点：

1. **GPHCON 必须配**。`CONFIG_S3C2400` 下 `S3C24X0_GPIO` 结构体里根本没有 `GPHCON` 字段，原作者写不出来；切到 2410 后字段才有（偏移 `0x70`）。不配则 TXD0 一直是复位后的输入态，不会有任何电平。
2. **用读-改-写**，不要整寄存器覆盖成 `0x002AAAAA`，否则会误改 GPH0/GPH1 和 GPH4~GPH9。
3. **`GPHUP` 是清零使能上拉**（不是置位），这点和很多 SoC 相反。

### 3.3 时钟配置

```c
#define S3C2440_MPLL_400MHZ  ((0x5c<<12)|(0x01<<4)|(0x01))   /* 0x5C011 */
#define S3C2440_UPLL_48MHZ   ((0x38<<12)|(0x02<<4)|(0x02))   /* 0x38022 */
#define S3C2440_CLKDIV      0x05                             /* 1:4:2 */
```

`board_init()` 中的顺序：

```
LOCKTIME = 0xFFFFFF
        ↓
CLKDIVN  = 0x05
        ↓
CP15 c1 |= 0xC0000000      （nF = 异步总线模式）
        ↓
MPLLCON  = 0x5C011         （400 MHz）
        ↓
UPLLCON  = 0x38022         （48 MHz，USB 专用）
        ↓
delay(10000)               （等 PLL 锁定）
        ↓
bi_arch_number = MACH_TYPE_S3C2440 (362)
```

**参数验算**（`m = MDIV+8, p = PDIV+2, s = SDIV`）：

| PLL | MDIV/PDIV/SDIV | 公式 | 结果 |
|---|---|---|---|
| MPLL（2440） | 92/1/1 → m=100, p=3, s=1 | `(2·m·Fin)/(p·2^s) = 2400/6` | **400 MHz** |
| UPLL（2410/2440 相同） | 56/2/2 → m=64, p=4, s=2 | `(m·Fin)/(p·2^s) = 768/16` | **48 MHz** |

> **S3C2410 与 S3C2440 的 MPLL 公式差一个 ×2**：2410 是 `(m·Fin)/(p·2^s)`，2440 是 `(2·m·Fin)/(p·2^s)`；而 **UPLL 两者都不乘 2**。这是 `speed.c` 必须按 PLL 类型分支的原因。

**nF 位的意义**：当 `HDIVN ≠ 0`（HCLK ≠ FCLK）时，必须先把 CP15 c1 的 bit30（nF）置 1，让 CPU 总线走异步模式，否则改分频瞬间总线时序错乱可能跑飞。

工程里有两处设置 nF：

- `cpu/arm920t/start.S`：在写 `CLKDIVN=3` 之前（严格正确的时机）
- `board/frdArm_mini2440.c`：在写 `CLKDIVN=5` 之后、`MPLLCON` 之前 —— 此时 FCLK 仍是 12 MHz，MPLL 尚未生效，所以实际安全，但与手册推荐顺序略有出入，属于可接受写法。

> 若后续维护，建议把 `board_init` 里那三行 `mrc/orr/mcr` 移到 `CLKDIVN` 赋值之前，与 start.S 保持一致。

### 3.4 `speed.c`：S3C2440 的时钟计算

三处改动：

**(1) 引入 gd 指针**

```c
DECLARE_GLOBAL_DATA_PTR;
```

**(2) `get_PLLCLK()` 对 2440 乘 2，且只对 MPLL 生效**

```c
if (pllreg == MPLL && gd->bd->bi_arch_number == MACH_TYPE_S3C2440)
        return ((CONFIG_SYS_CLK_FREQ * m * 2) / (p << s));
else
        return ((CONFIG_SYS_CLK_FREQ * m) / (p << s));
```

> 早先版本把 ×2 无条件加在返回值上，会让 `get_UCLK()` 把 48 MHz 报成 96 MHz。必须限定 `pllreg == MPLL`。
> 更早的错误版本用的是 ×8，会把 PCLK 算成 200 MHz → `UBRDIV = 107` → 实发约 28.9 kbps，接 115200 就是彻底收不到。

**(3) `get_HCLK()` / `get_PCLK()` 重写**

2410 只看 `CLKDIVN` 的 bit1/bit0 两位，2440 还要看 `CAMDIVN`（HCLK4_HALF / HCLK3_HALF），分频可能是 1/2/3/4/6/8：

```c
switch (clkdiv & S3C2440_CLKDIVN_HDIVN_MASK) {
case HDIVN_1:    hdiv = 1; break;
case HDIVN_2:    hdiv = 2; break;
case HDIVN_4_8:  hdiv = (camdiv & HCLK4_HALF) ? 8 : 4; break;
case HDIVN_3_6:  hdiv = (camdiv & HCLK3_HALF) ? 6 : 3; break;
}
```

两函数都用 `bi_arch_number == MACH_TYPE_SMDK2410 ? 旧公式 : 新公式` 做兼容，因此同一份 `s3c24x0` 目录仍可支持 2410 板。

> **顺序依赖很关键**：`bi_arch_number` 在 `board_init()` 里才被设成 362，而 `serial_init()`（依赖 `get_PCLK()`）在 `init_sequence` 中排在其后，所以 UBRDIV 能算对。若把 PCLK 相关的初始化提到 `board_init` 之前，会按 12 MHz 算出 `UBRDIV=5`（实发 125 kbps），串口又是乱码。

### 3.5 `s3c24x0.h`：补 `CAMDIVN`

```c
typedef struct {
    S3C24X0_REG32 LOCKTIME;   /* 0x00 */
    S3C24X0_REG32 MPLLCON;    /* 0x04 */
    S3C24X0_REG32 UPLLCON;    /* 0x08 */
    S3C24X0_REG32 CLKCON;     /* 0x0C */
    S3C24X0_REG32 CLKSLOW;    /* 0x10 */
    S3C24X0_REG32 CLKDIVN;    /* 0x14 */
    S3C24X0_REG32 CAMDIVN;    /* 0x18  ← 新增，S3C2440 独有 */
} S3C24X0_CLOCK_POWER;
```

S3C2440 的 `CAMDIVN` 在 `0x4C00_0018`，不补这个字段 `get_HCLK()` 编译不过。

### 3.6 NOR Flash：删掉板级 `flash.c`，改用通用 CFI 驱动

```make
-COBJS	:= frdArm_mini2440.o flash.o
+COBJS	:= frdArm_mini2440.o
```

配置侧：

```c
#define CFG_FLASH_CFI          1
#define CFG_FLASH_CFI_DRIVER   1
#define CFG_FLASH_CFI_WIDTH    FLASH_CFI_16BIT
#define PHYS_FLASH_SIZE        0x00200000   /* 2MB */
```

用 `drivers/cfi_flash.c` 的通用实现，芯片型号由 CFI 查询自动识别，因此 AM29LV160DB 和 SST39VF1601 都能跑，无需为每颗片子写 `flash.c`。

> 不要同时定义 `CONFIG_AMD_LV800/LV400`，否则会走专用驱动分支，与 CFI 冲突。

### 3.7 串口

```c
#define CONFIG_SERIAL1      1        /* 使用 UART0 */
#define CONFIG_BAUDRATE     115200
```

驱动实际写入的寄存器与裸机版本等价：

| 寄存器 | 值 | 说明 |
|---|---|---|
| `GPHCON` bit[7:4] | `0b1010` | GPH2 = TXD0 |
| `GPHUP` bit2/3 | 0 | 使能上拉 |
| `ULCON0` | `0x03` | 8 数据位 / 无校验 / 1 停止位 |
| `UCON0` | `0x245` | PCLK 源、Tx 电平中断、Rx 脉冲中断、查询模式、**非 loopback**（bit5 = 0） |
| `UFCON0` | `0x07` | FIFO 使能 + 清空 |
| `UBRDIV0` | **26** | `50MHz/(16×115200)-1 = 26.1` → 实际 115740 bps |

`UBRDIV0 = 26` 是整条时钟链正确的最强证据：它反推出 PCLK ≈ 49.77 MHz，正好等于 `400/4/2`。

---

## 4. 启动流程

```
上电复位
  │
  ├─[0x0] stepping stone（NAND 启动时是 4KB SRAM；NOR 启动时直接在 NOR 执行）
  │
  ├── _start (cpu/arm920t/start.S)
  │     ├─ svc 模式、关 IRQ/FIQ
  │     ├─ 关看门狗        WTCON(0x53000000) = 0
  │     ├─ 屏蔽中断        INTMSK = 0xFFFFFFFF, INTSUBMSK = 0x3FF
  │     ├─ CP15 c1 |= 0xC0000000   （nF 异步总线）
  │     └─ CLKDIVN = 3            （临时 1:2:4）
  │
  ├── cpu_init_crit
  │     ├─ 清 I/D Cache、关 MMU
  │     └─ lowlevel_init：SDRAM 13 寄存器 → 0x48000000
  │
  ├── relocate：整份镜像拷贝到 0x33F80000
  │     （若已在 TEXT_BASE 运行则跳过，便于调试）
  │
  ├── 设栈（CFG_STACKSIZE=128KB）、清 .bss
  │
  └── start_armboot (lib_arm/board.c)
        ├─ 分配 gd / bd
        ├─ 遍历 init_sequence：
        │    cpu_init → board_init（GPIO/时钟/Cache）
        │    → interrupt_init（Timer4）→ env_init → init_baudrate
        │    → serial_init（此时 PCLK=50MHz，UBRDIV=26）
        │    → console_init_f → display_banner
        │    → dram_init → display_dram_config
        └─ main_loop（命令解释器，提示符 frdArm# ）
```

### 4.1 SDRAM 初始化参数（`lowlevel_init.S`，13 个字顺序写入）

| 偏移 | 寄存器 | 值 | 说明 |
|---|---|---|---|
| `0x48000000` | BWSCON | `0x2201D110` | bank0 由 OM 引脚锁存；B3=D(16bit+WAIT+UBLB)，B6/B7=2(32bit) |
| `0x48000004` | BANKCON0 | `0x00000700` | NOR，Tacc=14 clk |
| `0x48000008` | BANKCON1 | `0x00000700` | |
| `0x4800000C` | BANKCON2 | `0x00000700` | |
| `0x48000010` | BANKCON3 | `0x00001F4C` | Tcos=4, Tacc=14, Tcoh=1, Tacp=6 |
| `0x48000014` | BANKCON4 | `0x00000700` | |
| `0x48000018` | BANKCON5 | `0x00000700` | |
| `0x4800001C` | BANKCON6 | `0x00018001` | MT=11(SDRAM), Trcd=2clk, SCAN=9bit |
| `0x48000020` | BANKCON7 | `0x00018001` | 同上 |
| `0x48000024` | REFRESH | `0x008E04F5` | REFEN=1, Trc=7clk, Tchr=3clk, REFCNT=0x4F5（HCLK=100MHz） |
| `0x48000028` | BANKSIZE | `0x000000B1` | 64MB |
| `0x4800002C` | MRSRB6 | `0x00000020` | CL=2 |
| `0x48000030` | MRSRB7 | `0x00000020` | CL=2 |

`lowlevel_init` 用 `SMRDATA - TEXT_BASE` 计算物理地址，**这是为"镜像在 0x0、链接在 0x33F80000"这个场景设计的**。副作用：不能直接把 `u-boot.bin` 用 J-Link 下载到 `0x33F80000` 后运行（会跑到 `0x0000_xxxx` 去取 SDRAM 参数，取到的是 NOR 里的内容）。若要这么做，需把 `ldr r0, =SMRDATA` 改成 `adr r0, SMRDATA`。

### 4.2 定时器

`interrupt_init()` 用 Timer4：

- `TCFG0 = 0x0F00` → prescaler = 16
- 4 bit divider = 1/2
- Timer 计数频率 = `PCLK / 16 / 2 = 1.5625 MHz`
- `TCNTB4 = get_PCLK()/(2*16*100) = 15625` → 每 10 ms 溢出一次
- `CFG_HZ = 1562500` → 1 tick = 640 ns，供 `udelay` / `get_timer` 使用

---

## 5. 编译

```bash
# 清理（换配置后必须做）
make distclean

# 生成配置：注意目标名是 _cfg，不是 _config
make frdArm_mini2440_cfg

# 编译
make CROSS_COMPILE=arm-linux-          # 或 arm-none-linux-gnueabi-
```

产物：

| 文件 | 用途 |
|---|---|
| `u-boot.bin` | 烧写到 NOR `0x0000_0000` |
| `u-boot` | ELF，仅供调试/反汇编（**不要拿去烧**） |
| `System.map` | 符号表 |

`make frdArm_mini2440_cfg` 实际执行 `mkconfig frdArm_mini2440 arm arm920t frdArm_mini2440 NULL s3c24x0`，会生成 `include/config.h`、`include/asm`（指向 `asm-arm`）、`include/asm-arm/arch`（指向 `arch-s3c24x0`）。

---

## 6. J-Link 烧写

板载 NOR 在 `nGCS0`，必须烧到 **`0x0`**。

### 6.1 J-Flash 配置要点

| 项 | 值 |
|---|---|
| Device | Samsung → S3C2440A |
| Interface | JTAG，1000 kHz（不要用 Auto/4000） |
| Reset | Connect under reset |
| Flash base / size | `0x00000000` / `0x00200000` |
| Bus width | 16 bit（芯片型号按板上丝印选，不要用 Auto） |
| Data file | `u-boot.bin`，地址 **`0x00000000`** |
| Erase | 全片 |

**最容易错的两点**：

1. 用 `u-boot`（ELF）而不是 `u-boot.bin`。ELF 的装载地址是 `TEXT_BASE = 0x33F80000`，J-Flash 会判定超出 flash 范围。
2. **S2 拨码必须在 NOR 启动位**。若在 NAND 启动（`OM[1:0]=00`），bank0 总线位宽不是 16 位，CFI 擦除命令写不进去，表现为"能连上、能读、一擦就失败"。

### 6.2 辅助脚本

工程内 `tools/jlink/`：

| 脚本 | 用途 |
|---|---|
| `mini2440_diag.jlink` | 诊断：读 NOR 起始 / 强制 bank0 16bit / 重读对比 / 读 SDRAM / 读 WTCON |
| `mini2440_sdram_init.jlink` | 关看门狗 + 400/100/50 MHz + SDRAM 13 寄存器 + 写读自检 |

```cmd
JLink.exe -CommanderScript tools\jlink\mini2440_diag.jlink
```

### 6.3 备选烧写方式

J-Link 不顺时的兜底：FriendlyARM 原厂 **SuperBoot + dnw（USB）**，或 H-JTAG + H-Flasher。

---

## 7. 验证清单

上电后串口（115200-8N1）应看到：

```
U-Boot 1.1.6 (Nov  1 2026 - ...)

DRAM:  64 MB
Flash:  2 MB
frdArm# 
```

| 检查点 | 命令 | 期望 |
|---|---|---|
| 串口基本通信 | 回车 | 回显 `frdArm# ` |
| SDRAM 完整性 | `md 0x30000000` | 可读，不报 Data Abort |
| Flash 识别 | `flinfo` | 显示 2MB / 16bit / CFI，扇区数正确 |
| 环境变量 | `printenv` / `saveenv` | 能读能写（写在 `0x1E0000`）|
| 定时器 | `sleep 1` 计时 | 约 1 秒 |
| 网卡 | `ping <PC>` | 见 §9，需先确认硬件 |

---

## 8. 命令集现状

```c
#define CONFIG_COMMANDS (CONFIG_CMD_DFL | CFG_CMD_CACHE | CFG_CMD_REGINFO | CFG_CMD_ELF)
```

`CONFIG_CMD_DFL` 定义为 `CFG_CMD_ALL & ~CFG_CMD_NONSTD`，其中 `NONSTD` 包含 `CFG_CMD_NAND`，所以：

- **已启用**：`md/mw/cp/cmp`、`tftp/bootp/ping`（NET 在 DFL 内）、`go`/`bootm`（`CFG_CMD_BOOTD`，在 DFL 内）、`bootelf`（由 `CFG_CMD_ELF` 打开）、`icache/dcache`、`reginfo`、`flinfo/erase/cp.b`
- **未启用**：`nand`、`dhcp`、`date`、`bmp`、`askenv` 等（均在 `NONSTD` 中）
- 无 `flash` 写保护相关高级命令，`flinfo`/`erase`/`cp.b` 可用

---

## 9. 已知限制与后续扩展

### 9.1 NAND 未支持

现状：

- `drivers/nand/` 只有通用 `nand_base.c` 等，**没有 `nand_s3c2410.c`**
- `cpu/arm920t/s3c24x0/` 目录下**没有 NAND 控制器驱动**
- 配置里没有 `CFG_NAND_*`，`CFG_CMD_NAND` 不在启用列表

影响：不能 `nand write` 内核/根文件系统，不能用 NAND 里的 YAFFS/JFFS2。**但 NOR 启动本身不需要 NAND 驱动**，所以当前 u-boot 功能完整可用。

要补的话，工作量主要在：

1. 新增 `drivers/nand/nand_s3c2410.c`，实现 `board_nand_init()`，填 `nand_chip` 的 `IO_ADDR_R/W`、`cmd_ctrl()`、`dev_ready()`、ECC（S3C2440 硬件 ECC 是 512B/4bit）
2. 配置里加 `CFG_NAND_BASE (0x4E000000)`、`CFG_NAND_WIDTH 8`、`CFG_NAND_SMALL_BLOCK` 或 `CONFIG_S3C2410_NAND_HWECC` 等
3. `CONFIG_COMMANDS` 里加 `CFG_CMD_NAND`
4. NAND 启动还需要把 u-boot 前 4KB 做成 `nand_spl`（工程里 `nand_spl/` 目录存在但未针对 mini2440 配置）

### 9.2 DM9000 未实测

配置已给出：`CONFIG_DM9000_BASE 0x20000300`、16 bit、`CONFIG_DM9000_NO_SROM`。但 mini2440 的 DM9000 接在 `nGCS4`，地址线偏移各版本略有差异，若 `ping` 不通，优先改 `CONFIG_DM9000_BASE`（常见还有 `0x20000300` / `0x20000000` 两种接法）。

### 9.3 其他

- 未实现 `checkboard()`，`CONFIG_DISPLAY_BOARDINFO` 未开，启动不打印板名
- 未开 `CONFIG_DISPLAY_CPUINFO`，不打印 CPU 频率
- **未定义 `CONFIG_BOOTDELAY`**：1.1.6 的 `common/main.c` 里它是 `#if defined(...) && >= 0` 才启用，未定义时 `bootdelay = -1`，即**没有倒计时、没有自动启动**，上电直接停在 `frdArm#`。要恢复倒计时需在配置头里加 `#define CONFIG_BOOTDELAY 3`
- 未定义 `CONFIG_BOOTCOMMAND`，`bootcmd` 环境变量需自己 `setenv` 后 `saveenv`（可配合 `CONFIG_BOOTDELAY` 实现自动引导）

---

## 10. 踩坑记录

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | 串口完全无输出（无乱码） | `CONFIG_S3C2400` 导致 UART 基址 `0x15000000` | 改 `CONFIG_S3C2410` |
| 2 | 板子约 10 秒复位一次 | 看门狗地址 `0x15300000`，从未真正关闭 | 同上，地址自动变 `0x53000000` |
| 3 | 有输出但乱码 / 无输出 | `get_PLLCLK` 系数写成 ×8 → PCLK 算成 200MHz → `UBRDIV=107` | 改为 ×2，且**只对 MPLL** |
| 4 | 修好地址后仍无输出 | `S3C24X0_GPIO` 在 2400 分支下没有 `GPHCON` 字段，引脚从未配置 | 切 2410 后补读-改-写 |
| 5 | 编译报 `#error "tbclk not configured"` | `get_tbclk()` 三选一宏一个都没定义 | 加 `CONFIG_SMDK2410` |
| 6 | 编译报 `too many arguments to function GetBase_UART` | `s3c2400.h` 的 `GetBase_UART(nr)` 带参数 | 切到 `s3c2410.h` 后签名一致，自动解决 |
| 7 | J-Flash 报地址越界 | 用了 `u-boot`（ELF）而非 `u-boot.bin` | 下 `u-boot.bin` 到 `0x0` |
| 8 | J-Flash 能连能读但擦除失败 | S2 在 NAND 启动位，bank0 非 16 位 | 拨到 NOR 启动 |
| 9 | `make frdArm_mini2440_config` 报无此目标 | 1.1.6 的目标名是 `_cfg` | 用 `make frdArm_mini2440_cfg` |

### 关于 `fix.txt`

`fix.txt` 是本次移植的完整 diff，但有一处需要留意：**`cpu/arm920t/start.S` 的 hunk 只包含版权注释的编码变化**（`Grüger`/`Zöpke` 的 UTF-8 差异），而 nF 置位的三行 `mrc/orr/mcr` 在当前源码中存在于第 147 行附近。说明这几行在该 diff 的基线里已经存在；**若要在干净源码树上重放此补丁，需要单独补上这段**，否则 nF 会丢失（后果通常不立刻暴露，但在 `HDIVN≠0` 时有跑飞风险）。

---

## 附录 A：时钟树

```
12 MHz XTIpll
   │
   ├─ MPLL  ×33.33  →  FCLK = 400 MHz   (MPLLCON = 0x5C011)
   │     │
   │     ├─ HDIVN = /4 (CAMDIVN.HCLK4_HALF=0)
   │     │      └→ HCLK = 100 MHz       (CLKDIVN = 0x05)
   │     │            └─ PDIVN = /2
   │     │                  └→ PCLK = 50 MHz
   │     │
   │     └─ ARM920T 内核、Cache、总线
   │
   └─ UPLL  ×4      →  UCLK = 48 MHz    (UPLLCON = 0x38022，仅 USB)
```

派生频率：

| 外设 | 时钟源 | 频率 |
|---|---|---|
| UART | PCLK | 50 MHz → `UBRDIV=26` @115200 |
| Timer4 | PCLK/16/2 | 1.5625 MHz |
| SDRAM | HCLK | 100 MHz（REFRESH 的 REFCNT=0x4F5 依赖此值）|
| DM9000 | HCLK | 100 MHz |

## 附录 B：相关文档

- S3C2440A User's Manual, Chapter 6 (Clock & Power), Chapter 7 (I/O Ports), Chapter 11 (UART), Chapter 5 (Memory Controller)
- u-boot 1.1.6 `README`、`doc/README.arm-relocation`
