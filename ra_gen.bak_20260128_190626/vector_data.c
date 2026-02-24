/* generated vector source file - do not edit */
        #include "bsp_api.h"
        /* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
        #if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = ceu_isr, /* CEU CEUI (CEU interrupt) */
            [1] = sci_b_uart_rxi_isr, /* SCI8 RXI (Receive data full) */
            [2] = sci_b_uart_txi_isr, /* SCI8 TXI (Transmit data empty) */
            [3] = sci_b_uart_tei_isr, /* SCI8 TEI (Transmit end) */
            [4] = sci_b_uart_eri_isr, /* SCI8 ERI (Receive error) */
            [5] = rm_ethosu_isr, /* NPU IRQ (NPU IRQ) */
            [6] = spi_b_rxi_isr, /* SPI0 RXI (Receive buffer full) */
            [7] = spi_b_txi_isr, /* SPI0 TXI (Transmit buffer empty) */
            [8] = spi_b_tei_isr, /* SPI0 TEI (Transmission complete event) */
            [9] = spi_b_eri_isr, /* SPI0 ERI (Error) */
            [10] = sdhimmc_accs_isr, /* SDHIMMC1 ACCS (Card access) */
            [11] = sdhimmc_card_isr, /* SDHIMMC1 CARD (Card detect) */
            [12] = dmac_int_isr, /* DMAC1 INT (DMAC1 transfer end) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_CEU_CEUI,GROUP0), /* CEU CEUI (CEU interrupt) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_SCI8_RXI,GROUP1), /* SCI8 RXI (Receive data full) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TXI,GROUP2), /* SCI8 TXI (Transmit data empty) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_SCI8_TEI,GROUP3), /* SCI8 TEI (Transmit end) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_SCI8_ERI,GROUP4), /* SCI8 ERI (Receive error) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_NPU_IRQ,GROUP5), /* NPU IRQ (NPU IRQ) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SPI0_RXI,GROUP6), /* SPI0 RXI (Receive buffer full) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SPI0_TXI,GROUP7), /* SPI0 TXI (Transmit buffer empty) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_SPI0_TEI,GROUP0), /* SPI0 TEI (Transmission complete event) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_SPI0_ERI,GROUP1), /* SPI0 ERI (Error) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_SDHIMMC1_ACCS,GROUP4), /* SDHIMMC1 ACCS (Card access) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_SDHIMMC1_CARD,GROUP5), /* SDHIMMC1 CARD (Card detect) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_DMAC1_INT,GROUP6), /* DMAC1 INT (DMAC1 transfer end) */
        };
        #endif
        #endif
