/* Minimal STM32G474 register definitions.
 *
 * This is written by hand rather than pulling in CMSIS so the firmware has no
 * vendor dependency and every register touched is one somebody chose to touch.
 * Only the peripherals this board actually uses appear here.  Offsets and bit
 * positions are from RM0440 (STM32G4 reference manual); anything uncertain is
 * called out in docs/notes-on-the-spec.md rather than guessed at silently.
 */

#ifndef STM32G474_H
#define STM32G474_H

#include <stddef.h>
#include <stdint.h>

#define __IO volatile
#define __I volatile const

/* ------------------------------------------------------------ memory map */

#define FLASH_R_BASE 0x40022000u
#define RCC_BASE 0x40021000u
#define PWR_BASE 0x40007000u
#define SYSCFG_BASE 0x40010000u

#define GPIOA_BASE 0x48000000u
#define GPIOB_BASE 0x48000400u
#define GPIOC_BASE 0x48000800u
#define GPIOD_BASE 0x48000C00u
#define GPIOE_BASE 0x48001000u
#define GPIOF_BASE 0x48001400u
#define GPIOG_BASE 0x48001800u

#define USART1_BASE 0x40013800u
#define USART2_BASE 0x40004400u
#define USART3_BASE 0x40004800u

#define SPI1_BASE 0x40013000u
#define SPI2_BASE 0x40003800u
#define SPI3_BASE 0x40003C00u

#define ADC1_BASE 0x50000000u
#define ADC2_BASE 0x50000100u
#define ADC12_COMMON_BASE 0x50000300u

#define FDCAN1_BASE 0x40006400u
#define FDCAN2_BASE 0x40006800u
#define FDCAN3_BASE 0x40006C00u
#define FDCAN_RAM_BASE 0x4000A400u

#define UCPD1_BASE 0x4000A000u

#define SYSTICK_BASE 0xE000E010u
#define SCB_BASE 0xE000ED00u
#define DWT_BASE 0xE0001000u
#define COREDEBUG_BASE 0xE000EDF0u

/* ------------------------------------------------------------------ FLASH */

typedef struct {
    __IO uint32_t ACR;     /* 0x00 */
    __IO uint32_t PDKEYR;  /* 0x04 */
    __IO uint32_t KEYR;    /* 0x08 */
    __IO uint32_t OPTKEYR; /* 0x0C */
    __IO uint32_t SR;      /* 0x10 */
    __IO uint32_t CR;      /* 0x14 */
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *)FLASH_R_BASE)

#define FLASH_ACR_LATENCY_Msk 0xFu
#define FLASH_ACR_PRFTEN (1u << 8)
#define FLASH_ACR_ICEN (1u << 9)
#define FLASH_ACR_DCEN (1u << 10)

/* -------------------------------------------------------------------- PWR */

typedef struct {
    __IO uint32_t CR1; /* 0x00 */
    __IO uint32_t CR2; /* 0x04 */
    __IO uint32_t CR3; /* 0x08 */
    __IO uint32_t CR4; /* 0x0C */
    __IO uint32_t SR1; /* 0x10 */
    __IO uint32_t SR2; /* 0x14 */
    __IO uint32_t SCR; /* 0x18 */
    uint32_t RESERVED0[1];
    __IO uint32_t PUCRA; /* 0x20 */
    __IO uint32_t PDCRA;
    __IO uint32_t PUCRB;
    __IO uint32_t PDCRB;
    __IO uint32_t PUCRC;
    __IO uint32_t PDCRC;
    __IO uint32_t PUCRD;
    __IO uint32_t PDCRD;
    __IO uint32_t PUCRE;
    __IO uint32_t PDCRE;
    __IO uint32_t PUCRF;
    __IO uint32_t PDCRF;
    __IO uint32_t PUCRG;
    __IO uint32_t PDCRG;
    uint32_t RESERVED1[10];
    __IO uint32_t CR5; /* 0x80 */
} PWR_TypeDef;

#define PWR ((PWR_TypeDef *)PWR_BASE)

#define PWR_CR1_VOS_Pos 9
#define PWR_CR1_VOS_RANGE1 (1u << PWR_CR1_VOS_Pos)
#define PWR_CR1_DBP (1u << 8)
#define PWR_CR3_UCPD1_DBDIS (1u << 14) /* release dead-battery pull-downs */
#define PWR_SR2_VOSF (1u << 10)
#define PWR_CR5_R1MODE (1u << 8) /* 1 = normal, 0 = boost (>150 MHz) */

/* -------------------------------------------------------------------- RCC */

typedef struct {
    __IO uint32_t CR;      /* 0x00 */
    __IO uint32_t ICSCR;   /* 0x04 */
    __IO uint32_t CFGR;    /* 0x08 */
    __IO uint32_t PLLCFGR; /* 0x0C */
    uint32_t RESERVED0[2];
    __IO uint32_t CIER; /* 0x18 */
    __IO uint32_t CIFR; /* 0x1C */
    __IO uint32_t CICR; /* 0x20 */
    uint32_t RESERVED1[1];
    __IO uint32_t AHB1RSTR; /* 0x28 */
    __IO uint32_t AHB2RSTR; /* 0x2C */
    __IO uint32_t AHB3RSTR; /* 0x30 */
    uint32_t RESERVED2[1];
    __IO uint32_t APB1RSTR1; /* 0x38 */
    __IO uint32_t APB1RSTR2; /* 0x3C */
    __IO uint32_t APB2RSTR;  /* 0x40 */
    uint32_t RESERVED3[1];
    __IO uint32_t AHB1ENR; /* 0x48 */
    __IO uint32_t AHB2ENR; /* 0x4C */
    __IO uint32_t AHB3ENR; /* 0x50 */
    uint32_t RESERVED4[1];
    __IO uint32_t APB1ENR1; /* 0x58 */
    __IO uint32_t APB1ENR2; /* 0x5C */
    __IO uint32_t APB2ENR;  /* 0x60 */
    uint32_t RESERVED5[1];
    __IO uint32_t AHB1SMENR; /* 0x68 */
    __IO uint32_t AHB2SMENR;
    __IO uint32_t AHB3SMENR;
    uint32_t RESERVED6[1];
    __IO uint32_t APB1SMENR1; /* 0x78 */
    __IO uint32_t APB1SMENR2;
    __IO uint32_t APB2SMENR;
    uint32_t RESERVED7[1];
    __IO uint32_t CCIPR; /* 0x88 */
    uint32_t RESERVED8[1];
    __IO uint32_t BDCR;   /* 0x90 */
    __IO uint32_t CSR;    /* 0x94 */
    __IO uint32_t CRRCR;  /* 0x98 */
    __IO uint32_t CCIPR2; /* 0x9C */
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)RCC_BASE)

#define RCC_CR_HSION (1u << 8)
#define RCC_CR_HSIRDY (1u << 10)
#define RCC_CR_HSEON (1u << 16)
#define RCC_CR_HSERDY (1u << 17)
#define RCC_CR_HSEBYP (1u << 18)
#define RCC_CR_CSSON (1u << 19)
#define RCC_CR_PLLON (1u << 24)
#define RCC_CR_PLLRDY (1u << 25)

#define RCC_CFGR_SW_Msk 0x3u
#define RCC_CFGR_SW_HSI 0x1u
#define RCC_CFGR_SW_HSE 0x2u
#define RCC_CFGR_SW_PLL 0x3u
#define RCC_CFGR_SWS_Pos 2
#define RCC_CFGR_SWS_Msk (0x3u << 2)
#define RCC_CFGR_HPRE_Pos 4
#define RCC_CFGR_HPRE_Msk (0xFu << 4)
#define RCC_CFGR_HPRE_DIV1 (0x0u << 4)
#define RCC_CFGR_HPRE_DIV2 (0x8u << 4)
#define RCC_CFGR_PPRE1_Pos 8
#define RCC_CFGR_PPRE2_Pos 11
#define RCC_CFGR_MCOSEL_Pos 24
#define RCC_CFGR_MCOPRE_Pos 28

#define RCC_PLLCFGR_PLLSRC_HSI (0x2u << 0)
#define RCC_PLLCFGR_PLLSRC_HSE (0x3u << 0)
#define RCC_PLLCFGR_PLLM_Pos 4  /* divider is (M + 1) */
#define RCC_PLLCFGR_PLLN_Pos 8  /* multiplier is N */
#define RCC_PLLCFGR_PLLPEN (1u << 16)
#define RCC_PLLCFGR_PLLP (1u << 17)
#define RCC_PLLCFGR_PLLQEN (1u << 20)
#define RCC_PLLCFGR_PLLQ_Pos 21 /* 00=2 01=4 10=6 11=8 */
#define RCC_PLLCFGR_PLLREN (1u << 24)
#define RCC_PLLCFGR_PLLR_Pos 25 /* 00=2 01=4 10=6 11=8 */
#define RCC_PLLCFGR_PLLPDIV_Pos 27

#define RCC_CCIPR_FDCANSEL_Pos 24
#define RCC_CCIPR_FDCANSEL_HSE (0x0u << 24)
#define RCC_CCIPR_FDCANSEL_PLLQ (0x1u << 24)
#define RCC_CCIPR_FDCANSEL_PCLK1 (0x2u << 24)
#define RCC_CCIPR_ADC12SEL_Pos 28

/* enable bits used here */
#define RCC_AHB1ENR_FLASHEN (1u << 8)
#define RCC_AHB1ENR_CRCEN (1u << 12)
#define RCC_AHB2ENR_GPIOAEN (1u << 0)
#define RCC_AHB2ENR_GPIOBEN (1u << 1)
#define RCC_AHB2ENR_GPIOCEN (1u << 2)
#define RCC_AHB2ENR_GPIODEN (1u << 3)
#define RCC_AHB2ENR_GPIOFEN (1u << 5)
#define RCC_AHB2ENR_ADC12EN (1u << 13)
#define RCC_APB1ENR1_USART2EN (1u << 17)
#define RCC_APB1ENR1_FDCANEN (1u << 25)
#define RCC_APB1ENR1_PWREN (1u << 28)
#define RCC_APB1ENR2_UCPD1EN (1u << 8)
#define RCC_APB2ENR_SYSCFGEN (1u << 0)
#define RCC_APB2ENR_SPI1EN (1u << 12)
#define RCC_APB2ENR_USART1EN (1u << 14)

/* ------------------------------------------------------------------- GPIO */

typedef struct {
    __IO uint32_t MODER;   /* 0x00 */
    __IO uint32_t OTYPER;  /* 0x04 */
    __IO uint32_t OSPEEDR; /* 0x08 */
    __IO uint32_t PUPDR;   /* 0x0C */
    __I uint32_t IDR;      /* 0x10 */
    __IO uint32_t ODR;     /* 0x14 */
    __IO uint32_t BSRR;    /* 0x18 */
    __IO uint32_t LCKR;    /* 0x1C */
    __IO uint32_t AFR[2];  /* 0x20, 0x24 */
    __IO uint32_t BRR;     /* 0x28 */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE)
#define GPIOD ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOF ((GPIO_TypeDef *)GPIOF_BASE)

/* ------------------------------------------------------------------ USART */

typedef struct {
    __IO uint32_t CR1;  /* 0x00 */
    __IO uint32_t CR2;  /* 0x04 */
    __IO uint32_t CR3;  /* 0x08 */
    __IO uint32_t BRR;  /* 0x0C */
    __IO uint32_t GTPR; /* 0x10 */
    __IO uint32_t RTOR; /* 0x14 */
    __IO uint32_t RQR;  /* 0x18 */
    __I uint32_t ISR;   /* 0x1C */
    __IO uint32_t ICR;  /* 0x20 */
    __I uint32_t RDR;   /* 0x24 */
    __IO uint32_t TDR;  /* 0x28 */
    __IO uint32_t PRESC; /* 0x2C */
} USART_TypeDef;

#define USART1 ((USART_TypeDef *)USART1_BASE)
#define USART2 ((USART_TypeDef *)USART2_BASE)

#define USART_CR1_UE (1u << 0)
#define USART_CR1_RE (1u << 2)
#define USART_CR1_TE (1u << 3)
#define USART_ISR_RXNE (1u << 5)
#define USART_ISR_TC (1u << 6)
#define USART_ISR_TXE (1u << 7)

/* -------------------------------------------------------------------- SPI */

typedef struct {
    __IO uint32_t CR1;     /* 0x00 */
    __IO uint32_t CR2;     /* 0x04 */
    __IO uint32_t SR;      /* 0x08 */
    __IO uint32_t DR;      /* 0x0C */
    __IO uint32_t CRCPR;   /* 0x10 */
    __I uint32_t RXCRCR;   /* 0x14 */
    __I uint32_t TXCRCR;   /* 0x18 */
    __IO uint32_t I2SCFGR; /* 0x1C */
    __IO uint32_t I2SPR;   /* 0x20 */
} SPI_TypeDef;

#define SPI1 ((SPI_TypeDef *)SPI1_BASE)

#define SPI_CR1_CPHA (1u << 0)
#define SPI_CR1_CPOL (1u << 1)
#define SPI_CR1_MSTR (1u << 2)
#define SPI_CR1_BR_Pos 3
#define SPI_CR1_SPE (1u << 6)
#define SPI_CR1_SSI (1u << 8)
#define SPI_CR1_SSM (1u << 9)
#define SPI_CR2_DS_Pos 8 /* data size - 1 */
#define SPI_CR2_FRXTH (1u << 12)
#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_TXE (1u << 1)
#define SPI_SR_BSY (1u << 7)

/* -------------------------------------------------------------------- ADC */

typedef struct {
    __IO uint32_t ISR;   /* 0x00 */
    __IO uint32_t IER;   /* 0x04 */
    __IO uint32_t CR;    /* 0x08 */
    __IO uint32_t CFGR;  /* 0x0C */
    __IO uint32_t CFGR2; /* 0x10 */
    __IO uint32_t SMPR1; /* 0x14 */
    __IO uint32_t SMPR2; /* 0x18 */
    uint32_t RESERVED0[1];
    __IO uint32_t TR1; /* 0x20 */
    __IO uint32_t TR2;
    __IO uint32_t TR3;
    uint32_t RESERVED1[1];
    __IO uint32_t SQR1; /* 0x30 */
    __IO uint32_t SQR2;
    __IO uint32_t SQR3;
    __IO uint32_t SQR4;
    __I uint32_t DR; /* 0x40 */
} ADC_TypeDef;

typedef struct {
    __I uint32_t CSR; /* 0x00 */
    uint32_t RESERVED0[1];
    __IO uint32_t CCR; /* 0x08 */
    __I uint32_t CDR;  /* 0x0C */
} ADC_Common_TypeDef;

#define ADC1 ((ADC_TypeDef *)ADC1_BASE)
#define ADC12_COMMON ((ADC_Common_TypeDef *)ADC12_COMMON_BASE)

#define ADC_ISR_ADRDY (1u << 0)
#define ADC_ISR_EOC (1u << 2)
#define ADC_CR_ADEN (1u << 0)
#define ADC_CR_ADDIS (1u << 1)
#define ADC_CR_ADSTART (1u << 2)
#define ADC_CR_ADVREGEN (1u << 28)
#define ADC_CR_DEEPPWD (1u << 29)
#define ADC_CR_ADCALDIF (1u << 30)
#define ADC_CR_ADCAL (1u << 31)
#define ADC_CCR_CKMODE_Pos 16 /* 00 async, 01 HCLK/1, 10 HCLK/2, 11 HCLK/4 */
#define ADC_CCR_VREFEN (1u << 22)

/* ------------------------------------------------------------------ FDCAN */
/* ST's cut-down M_CAN: the message RAM start addresses are fixed per
 * instance, so there is no SIDFC/RXF0C/TXBC address programming -- RXGFC
 * replaces the filter and element-size registers. */

typedef struct {
    __I uint32_t CREL; /* 0x00 */
    __I uint32_t ENDN; /* 0x04 */
    uint32_t RESERVED0[1];
    __IO uint32_t DBTP; /* 0x0C */
    __IO uint32_t TEST; /* 0x10 */
    __IO uint32_t RWD;  /* 0x14 */
    __IO uint32_t CCCR; /* 0x18 */
    __IO uint32_t NBTP; /* 0x1C */
    __IO uint32_t TSCC; /* 0x20 */
    __IO uint32_t TSCV; /* 0x24 */
    __IO uint32_t TOCC; /* 0x28 */
    __IO uint32_t TOCV; /* 0x2C */
    uint32_t RESERVED1[4];
    __I uint32_t ECR;   /* 0x40 */
    __I uint32_t PSR;   /* 0x44 */
    __IO uint32_t TDCR; /* 0x48 */
    uint32_t RESERVED2[1];
    __IO uint32_t IR;  /* 0x50 */
    __IO uint32_t IE;  /* 0x54 */
    __IO uint32_t ILS; /* 0x58 */
    __IO uint32_t ILE; /* 0x5C */
    uint32_t RESERVED3[8];
    __IO uint32_t RXGFC; /* 0x80 */
    __IO uint32_t XIDAM; /* 0x84 */
    __I uint32_t HPMS;   /* 0x88 */
    uint32_t RESERVED4[1];
    __I uint32_t RXF0S;  /* 0x90 */
    __IO uint32_t RXF0A; /* 0x94 */
    __I uint32_t RXF1S;  /* 0x98 */
    __IO uint32_t RXF1A; /* 0x9C */
    uint32_t RESERVED5[8];
    __IO uint32_t TXBC;  /* 0xC0 */
    __I uint32_t TXFQS;  /* 0xC4 */
    __I uint32_t TXBRP;  /* 0xC8 */
    __IO uint32_t TXBAR; /* 0xCC */
    __IO uint32_t TXBCR; /* 0xD0 */
    __I uint32_t TXBTO;  /* 0xD4 */
    __I uint32_t TXBCF;  /* 0xD8 */
} FDCAN_TypeDef;

#define FDCAN1 ((FDCAN_TypeDef *)FDCAN1_BASE)

#define FDCAN_CCCR_INIT (1u << 0)
#define FDCAN_CCCR_CCE (1u << 1)
#define FDCAN_CCCR_CSR (1u << 4)
#define FDCAN_CCCR_CSA (1u << 3)
#define FDCAN_CCCR_MON (1u << 5)
#define FDCAN_CCCR_DAR (1u << 6)
#define FDCAN_CCCR_TEST (1u << 7)
#define FDCAN_CCCR_FDOE (1u << 8)
#define FDCAN_CCCR_BRSE (1u << 9)
#define FDCAN_TEST_LBCK (1u << 4)
#define FDCAN_TDCR_TDCF_Pos 0
#define FDCAN_TDCR_TDCO_Pos 8
#define FDCAN_DBTP_TDC (1u << 23)

#define FDCAN_RXGFC_ANFE_Pos 2 /* 00 = to FIFO 0 */
#define FDCAN_RXGFC_ANFS_Pos 4
#define FDCAN_RXGFC_LSS_Pos 16
#define FDCAN_RXGFC_LSE_Pos 24

#define FDCAN_PSR_LEC_Msk 0x7u
#define FDCAN_PSR_ACT_Pos 3
#define FDCAN_PSR_EP (1u << 5)
#define FDCAN_PSR_EW (1u << 6)
#define FDCAN_PSR_BO (1u << 7)

/* ------------------------------------------------------------------- UCPD */

typedef struct {
    __IO uint32_t CFG1; /* 0x00 */
    __IO uint32_t CFG2; /* 0x04 */
    uint32_t RESERVED0[1];
    __IO uint32_t CR;  /* 0x0C */
    __IO uint32_t IMR; /* 0x10 */
    __I uint32_t SR;   /* 0x14 */
    __IO uint32_t ICR; /* 0x18 */
    __IO uint32_t TX_ORDSET; /* 0x1C */
    __IO uint32_t TX_PAYSZ;  /* 0x20 */
    __IO uint32_t TXDR;      /* 0x24 */
    __IO uint32_t RX_ORDSET; /* 0x28 */
    __I uint32_t RX_PAYSZ;   /* 0x2C */
    __I uint32_t RXDR;       /* 0x30 */
    __IO uint32_t RX_ORDEXT1; /* 0x34 */
    __IO uint32_t RX_ORDEXT2; /* 0x38 */
} UCPD_TypeDef;

#define UCPD1 ((UCPD_TypeDef *)UCPD1_BASE)

#define UCPD_CFG1_HBITCLKDIV_Pos 0
#define UCPD_CFG1_IFRGAP_Pos 6
#define UCPD_CFG1_TRANSWIN_Pos 11
#define UCPD_CFG1_PSC_UCPDCLK_Pos 17
#define UCPD_CFG1_RXORDSETEN_Pos 20
#define UCPD_CFG1_UCPDEN (1u << 31)

#define UCPD_CR_CCENABLE_Pos 2 /* 11 = both CC lines active */
#define UCPD_CR_ANAMODE (1u << 4) /* 1 = sink */
#define UCPD_CR_ANASUBMODE_Pos 5
#define UCPD_CR_CCSEL (1u << 7)
#define UCPD_CR_PHYRXEN (1u << 8)
#define UCPD_CR_PHYCCSEL (1u << 9)

#define UCPD_SR_TYPEC_VSTATE_CC1_Pos 8
#define UCPD_SR_TYPEC_VSTATE_CC2_Pos 10

/* ---------------------------------------------------------- Cortex-M4 bits */

typedef struct {
    __IO uint32_t CSR;
    __IO uint32_t RVR;
    __IO uint32_t CVR;
    __I uint32_t CALIB;
} SysTick_TypeDef;

#define SysTick ((SysTick_TypeDef *)SYSTICK_BASE)
#define SysTick_CSR_ENABLE (1u << 0)
#define SysTick_CSR_TICKINT (1u << 1)
#define SysTick_CSR_CLKSOURCE (1u << 2)

typedef struct {
    __I uint32_t CPUID;
    __IO uint32_t ICSR;
    __IO uint32_t VTOR;
    __IO uint32_t AIRCR;
    __IO uint32_t SCR;
    __IO uint32_t CCR;
    __IO uint8_t SHP[12];
    __IO uint32_t SHCSR;
    __IO uint32_t CFSR;
    __IO uint32_t HFSR;
    __IO uint32_t DFSR;
    __IO uint32_t MMFAR;
    __IO uint32_t BFAR;
} SCB_TypeDef;

#define SCB ((SCB_TypeDef *)SCB_BASE)

/* ------------------------------------------------- register layout checks */

/* A hand-written register map goes wrong in exactly one way: a RESERVED array
 * off by one word, which silently shifts every register after it.  The result
 * is a peripheral that ignores half its configuration, and it is almost
 * impossible to spot by reading.  These assertions are checked at compile
 * time on every build, so the map cannot drift from RM0440 unnoticed. */

#define CHECK_OFFSET(type, field, expected) \
    _Static_assert(offsetof(type, field) == (expected), #type "." #field)

CHECK_OFFSET(RCC_TypeDef, PLLCFGR, 0x0C);
CHECK_OFFSET(RCC_TypeDef, AHB1RSTR, 0x28);
CHECK_OFFSET(RCC_TypeDef, AHB1ENR, 0x48);
CHECK_OFFSET(RCC_TypeDef, APB1ENR1, 0x58);
CHECK_OFFSET(RCC_TypeDef, APB1ENR2, 0x5C);
CHECK_OFFSET(RCC_TypeDef, APB2ENR, 0x60);
CHECK_OFFSET(RCC_TypeDef, CCIPR, 0x88);
CHECK_OFFSET(RCC_TypeDef, BDCR, 0x90);
CHECK_OFFSET(RCC_TypeDef, CCIPR2, 0x9C);

CHECK_OFFSET(PWR_TypeDef, CR5, 0x80);

CHECK_OFFSET(GPIO_TypeDef, BSRR, 0x18);
CHECK_OFFSET(GPIO_TypeDef, AFR, 0x20);
CHECK_OFFSET(GPIO_TypeDef, BRR, 0x28);

CHECK_OFFSET(USART_TypeDef, ISR, 0x1C);
CHECK_OFFSET(USART_TypeDef, TDR, 0x28);

CHECK_OFFSET(SPI_TypeDef, DR, 0x0C);
CHECK_OFFSET(SPI_TypeDef, I2SPR, 0x20);

CHECK_OFFSET(ADC_TypeDef, SMPR1, 0x14);
CHECK_OFFSET(ADC_TypeDef, SQR1, 0x30);
CHECK_OFFSET(ADC_TypeDef, DR, 0x40);
CHECK_OFFSET(ADC_Common_TypeDef, CCR, 0x08);

CHECK_OFFSET(FDCAN_TypeDef, DBTP, 0x0C);
CHECK_OFFSET(FDCAN_TypeDef, CCCR, 0x18);
CHECK_OFFSET(FDCAN_TypeDef, NBTP, 0x1C);
CHECK_OFFSET(FDCAN_TypeDef, ECR, 0x40);
CHECK_OFFSET(FDCAN_TypeDef, PSR, 0x44);
CHECK_OFFSET(FDCAN_TypeDef, TDCR, 0x48);
CHECK_OFFSET(FDCAN_TypeDef, IR, 0x50);
CHECK_OFFSET(FDCAN_TypeDef, RXGFC, 0x80);
CHECK_OFFSET(FDCAN_TypeDef, RXF0S, 0x90);
CHECK_OFFSET(FDCAN_TypeDef, RXF0A, 0x94);
CHECK_OFFSET(FDCAN_TypeDef, TXBC, 0xC0);
CHECK_OFFSET(FDCAN_TypeDef, TXFQS, 0xC4);
CHECK_OFFSET(FDCAN_TypeDef, TXBAR, 0xCC);

CHECK_OFFSET(UCPD_TypeDef, CR, 0x0C);
CHECK_OFFSET(UCPD_TypeDef, SR, 0x14);
CHECK_OFFSET(UCPD_TypeDef, TXDR, 0x24);
CHECK_OFFSET(UCPD_TypeDef, RXDR, 0x30);

#undef CHECK_OFFSET

/* ---------------------------------------------------------- Cortex-M intr */

static inline void __dsb(void) { __asm volatile("dsb 0xF" ::: "memory"); }
static inline void __isb(void) { __asm volatile("isb 0xF" ::: "memory"); }
static inline void __nop(void) { __asm volatile("nop"); }
static inline void __wfi(void) { __asm volatile("wfi"); }

#endif /* STM32G474_H */
