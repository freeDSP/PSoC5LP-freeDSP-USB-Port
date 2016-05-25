/***************************************************************************//**
* \file USBFS.c
* \version 3.0
*
* \brief
*  This file contains the global USBFS API functions.
*
* Note:
*  Many of the functions use an endpoint number. SRAM arrays are sized with 9
*  elements, so they are indexed directly by epNumber.  The SIE and ARB
*  registers are indexed by variations of epNumber - 1.
*
********************************************************************************
* \copyright
* Copyright 2008-2015, Cypress Semiconductor Corporation.  All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include "USBFS_pvt.h"
#include "USBFS_cydmac.h"
#include "USBFS_hid.h"


/***************************************
* Global data allocation
***************************************/

/** Indicates whether the USBFS has been initialized. The variable is
* initialized to 0 after device reset and set to 1 the first time USBFS_Start()
* is called. This allows the Component to restart without reinitialization after
* the first call to the USBFS_Start() routine.
* If re-initialization of the Component is required, the variable should be set
* to 0 before the USBFS_Start() routine is called. Alternatively, the USBFS can
* be reinitialized by calling both USBFS_Init() and USBFS_InitComponent()
* functions.
*/
uint8 USBFS_initVar = 0u;

#if (USBFS_EP_MANAGEMENT_DMA)
    #if (CY_PSOC4)
        static void USBFS_InitEpDma(void);

        /* DMA chanels assigend for endpoints. */
        const uint8 USBFS_DmaChan[USBFS_MAX_EP] =
        {
            0u,
            0u,
            (uint8) USBFS_ep2_dma_CHANNEL,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
        };        
    #else
        /* DMA chanels assigend for endpoints. */
        uint8 USBFS_DmaChan[USBFS_MAX_EP];
        
        /* DMA TDs require for PSoC 3/5LP operation. */
        uint8 USBFS_DmaTd[USBFS_MAX_EP];
    #endif /* (CY_PSOC4) */
#endif /* (USBFS_EP_MANAGEMENT_DMA) */

#if (USBFS_EP_MANAGEMENT_DMA_AUTO)
#if (CY_PSOC4)
    /* Number of DMA bursts. */
    uint8  USBFS_DmaEpBurstCnt   [USBFS_MAX_EP];
    
    /* Number of bytes to transfer in last DMA burst. */
    uint8  USBFS_DmaEpLastBurstEl[USBFS_MAX_EP];

    /* Storage for arrays above. */
    uint8  USBFS_DmaEpBurstCntBackup  [USBFS_MAX_EP];
    uint32 USBFS_DmaEpBufferAddrBackup[USBFS_MAX_EP];
     
    /* DMA trigger mux output for usb.dma_req[0-7]. */
    const uint8 USBFS_DmaReqOut[USBFS_MAX_EP] =
    {
        0u,
        0u,
        USBFS_ep2_dma__TR_OUTPUT,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
    };

    /* DMA trigger mux output for usb.dma_burstend[0-7]. */
    const uint8 USBFS_DmaBurstEndOut[USBFS_MAX_EP] =
    {
        0u,
        USBFS_BURSTEND_0_TR_OUTPUT,
        USBFS_BURSTEND_1_TR_OUTPUT,
        USBFS_BURSTEND_2_TR_OUTPUT,
        USBFS_BURSTEND_3_TR_OUTPUT,
        USBFS_BURSTEND_4_TR_OUTPUT,
        USBFS_BURSTEND_5_TR_OUTPUT,
        USBFS_BURSTEND_6_TR_OUTPUT,
        USBFS_BURSTEND_7_TR_OUTPUT
    };
    
#else
    #if (USBFS_EP_DMA_AUTO_OPT == 0u)
        static uint8 clearInDataRdyStatus = USBFS_ARB_EPX_CFG_DEFAULT;
        uint8 USBFS_DmaNextTd[USBFS_MAX_EP];
        const uint8 USBFS_epX_TD_TERMOUT_EN[USBFS_MAX_EP] =
        {
            0u,
            0u,
            USBFS_ep2__TD_TERMOUT_EN,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
        };

        volatile uint16 USBFS_inLength[USBFS_MAX_EP];
        const uint8 *USBFS_inDataPointer[USBFS_MAX_EP];
        volatile uint8 USBFS_inBufFull[USBFS_MAX_EP];
    #endif /* (USBFS_EP_DMA_AUTO_OPT == 0u) */
#endif /* (CY_PSOC4) */
#endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */


/*******************************************************************************
* Function Name: USBFS_Start
****************************************************************************//**
*
*   This function performs all required initialization for the USBFS component.
*   After this function call, the USB device initiates communication with the
*   host by pull-up D+ line. This is the preferred method to begin component
*   operation.
*
*   Note that global interrupts have to be enabled because interrupts are
*   required for USBFS component operation.
*
*   PSoC 4200L devices: when USBFS component configured to DMA with Automatic
*   Buffer Management, the DMA interrupt priority is changed to the highest
*   (priority 0) inside this function.
*
*   PSoC 3/PSoC 5LP devices: when USBFS component configured to DMA with
*   Automatic Buffer Management, the Arbiter interrupt priority is changed to
*   the highest (priority 0) inside this function.
*
*  \param device
*          Contains the device number of the desired device descriptor.
*          The device number can be found in the Device Descriptor Tab of
*          "Configure" dialog, under the settings of desired Device Descriptor,
*          in the "Device Number" field.
*  \param mode:
*   The operating voltage. This determines whether the voltage regulator
*   is enabled for 5V operation or if pass through mode is used for 3.3V
*   operation. Symbolic names and their associated values are given in the
*   following list.
*
*       *USBFS_3V_OPERATION* - Disable voltage regulator and pass-
*                                      through Vcc for pull-up
*
*       *USBFS_5V_OPERATION* - Enable voltage regulator and use
*                                      regulator for pull-up
*
*       *USBFS_DWR_POWER_OPERATION* - Enable or disable the voltage
*                                      regulator depending on the power supply
*                                      voltage configuration in the DWR tab.
*                                      For PSoC 3/5LP devices, the VDDD supply
*                                      voltage is considered and for PSoC 4A-L,
*                                      the VBUS supply voltage is considered.*
* \globalvars
*  \ref USBFS_initVar
*
* \sideeffect
*   This function will reset all communication states to default.
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_Start(uint8 device, uint8 mode) 
{
    if (0u == USBFS_initVar)
    {
        USBFS_Init();
        USBFS_initVar = 1u;
    }

    USBFS_InitComponent(device, mode);
}


/*******************************************************************************
* Function Name: USBFS_Init
****************************************************************************//**
*
* This function initializes or restores the component according to the
* customizer Configure dialog settings. It is not necessary to call
* USBFS_Init() because the USBFS_Start() routine calls
* this function and is the preferred method to begin component operation.
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_Init(void) 
{
#if (CY_PSOC4)
    /* Enable clock to USB IP. */
    USBFS_USB_CLK_EN_REG = USBFS_USB_CLK_CSR_CLK_EN;

    /* The internal regulator (CR1.REG_ENABLE) is enabled in
    * USBFS_InitComponent() if it is required.
    */

    /* Enable USBIO control on drive mode of D+ and D- pins. */
    USBFS_USBIO_CR1_REG &= ~ (uint32) USBFS_USBIO_CR1_IOMODE;

    /* Set number of LF CLK to detect UBS bus reset. */
    USBFS_BUS_RST_CNT_REG = USBFS_DEFUALT_BUS_RST_CNT;

    /* Select VBUS detection source and clear PHY isolate. The application level
    * must ensure that VBUS is valid. There is no need to wait 2us before VBUS is valid.
    */
    USBFS_POWER_CTRL_REG = USBFS_DEFAULT_POWER_CTRL_VBUS;

    /* Enable PHY detector and single-ended and differential receivers. */
    USBFS_POWER_CTRL_REG |= USBFS_DEFAULT_POWER_CTRL_PHY;

    /* Suspend clear sequence. */
    USBFS_POWER_CTRL_REG &= (uint32) ~USBFS_POWER_CTRL_SUSPEND;
    CyDelayUs(USBFS_WAIT_SUSPEND_DEL_DISABLE);
    USBFS_POWER_CTRL_REG &= (uint32) ~USBFS_POWER_CTRL_SUSPEND_DEL;

    /* Sets IMO lock options and clear all other bits. */
    USBFS_CR1_REG = USBFS_DEFUALT_CR1;

    /* Configure level (hi, lo, med) for each interrupt source. */
    USBFS_INTR_LVL_SEL_REG = USBFS_DEFAULT_INTR_LVL_SEL;

    /* Configure interrupt sources from: SOF, Bus Reset and EP0. */
    USBFS_INTR_SIE_MASK_REG = USBFS_DEFAULT_INTR_SIE_MASK;

#else
    uint8 enableInterrupts = CyEnterCriticalSection();

#if (USBFS_EP_MANAGEMENT_DMA)
    uint16 i;
#endif /* (USBFS_EP_MANAGEMENT_DMA) */

    /* Enable USB block. */
    USBFS_PM_ACT_CFG_REG |= USBFS_PM_ACT_EN_FSUSB;
    /* Enable USB block for Standby Power Mode. */
    USBFS_PM_STBY_CFG_REG |= USBFS_PM_STBY_EN_FSUSB;

    /* Enable core clock. */
    USBFS_USB_CLK_EN_REG = USBFS_USB_CLK_ENABLE;

    USBFS_CR1_REG = USBFS_CR1_ENABLE_LOCK;

    /* ENABLING USBIO PADS IN USB MODE FROM I/O MODE. */
    /* Ensure USB transmit enable is low (USB_USBIO_CR0.ten). - Manual Transmission - Disabled. */
    USBFS_USBIO_CR0_REG &= (uint8) ~USBFS_USBIO_CR0_TEN;
    CyDelayUs(USBFS_WAIT_REG_STABILITY_50NS);  /* ~50ns delay. */
    /* Disable USBIO by asserting PM.USB_CR0.fsusbio_pd_n(Inverted.
    *  high. These bits will be set low by the power manager out-of-reset.
    *  Also confirm USBIO pull-up is disabled.
    */
    USBFS_PM_USB_CR0_REG &= (uint8) ~(USBFS_PM_USB_CR0_PD_N |
                                                 USBFS_PM_USB_CR0_PD_PULLUP_N);

    /* Select IOMODE to USB mode. */
    USBFS_USBIO_CR1_REG &= (uint8) ~USBFS_USBIO_CR1_IOMODE;

    /* Enable USBIO reference by setting PM.USB_CR0.fsusbio_ref_en. */
    USBFS_PM_USB_CR0_REG |= USBFS_PM_USB_CR0_REF_EN;
    /* Reference is available for 1us after regulator is enabled. */
    CyDelayUs(USBFS_WAIT_REG_STABILITY_1US);
    /* OR 40us after power is restored. */
    CyDelayUs(USBFS_WAIT_VREF_RESTORE);
    /* Ensure single-ended disable bits are low (PRT15.INP_DIS[7:6])(input receiver enabled). */
    USBFS_DM_INP_DIS_REG &= (uint8) ~USBFS_DM_MASK;
    USBFS_DP_INP_DIS_REG &= (uint8) ~USBFS_DP_MASK;

    /* Enable USBIO. */
    USBFS_PM_USB_CR0_REG |= USBFS_PM_USB_CR0_PD_N;
    CyDelayUs(USBFS_WAIT_PD_PULLUP_N_ENABLE);
    /* Set USBIO pull-up enable. */
    USBFS_PM_USB_CR0_REG |= USBFS_PM_USB_CR0_PD_PULLUP_N;

    /* Reset Arbiter Write Address register for endpoint 1. */
    CY_SET_REG8(USBFS_ARB_RW1_WA_PTR,     0u);
    CY_SET_REG8(USBFS_ARB_RW1_WA_MSB_PTR, 0u);

#if (USBFS_EP_MANAGEMENT_DMA)
    /* Initialize transfer descriptor. This will be used to detect DMA state - initialized or not. */
    for (i = 0u; i < USBFS_MAX_EP; ++i)
    {
        USBFS_DmaTd[i] = DMA_INVALID_TD;

    #if (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u))
        USBFS_DmaNextTd[i] = DMA_INVALID_TD;
    #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u)) */
    }
#endif /* (USBFS_EP_MANAGEMENT_DMA) */

    CyExitCriticalSection(enableInterrupts);
#endif /* (CY_PSOC4) */

    /* Configure interrupts from USB block. */
#if (CY_PSOC4)
    /* Configure hi_int: set handler and priority. */
    CyIntSetPriority     (USBFS_INTR_HI_VECT_NUM,  USBFS_INTR_HI_PRIORITY);
    (void) CyIntSetVector(USBFS_INTR_HI_VECT_NUM, &USBFS_INTR_HI_ISR);

    /* Configure lo_int: set handler and priority. */
    CyIntSetPriority     (USBFS_INTR_LO_VECT_NUM,  USBFS_INTR_LO_PRIORITY);
    (void) CyIntSetVector(USBFS_INTR_LO_VECT_NUM, &USBFS_INTR_LO_ISR);

    /* Configure med_int: set handler and priority (routed through DSI). */
    CyIntSetPriority     (USBFS_INTR_MED_VECT_NUM,  USBFS_INTR_MED_PRIORITY);
    (void) CyIntSetVector(USBFS_INTR_MED_VECT_NUM, &USBFS_INTR_MED_ISR);

#else
    /* Set bus reset interrupt. */
    CyIntSetPriority(USBFS_BUS_RESET_VECT_NUM, USBFS_BUS_RESET_PRIOR);
    (void) CyIntSetVector(USBFS_BUS_RESET_VECT_NUM,   &USBFS_BUS_RESET_ISR);

    /* Set Control Endpoint Interrupt. */
    CyIntSetPriority(USBFS_EP_0_VECT_NUM, USBFS_EP_0_PRIOR);
    (void) CyIntSetVector(USBFS_EP_0_VECT_NUM,   &USBFS_EP_0_ISR);

    /* Set SOF interrupt. */
    #if (USBFS_SOF_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_SOF_VECT_NUM,  USBFS_SOF_PRIOR);
        (void) CyIntSetVector(USBFS_SOF_VECT_NUM, &USBFS_SOF_ISR);
    #endif /* (USBFS_SOF_ISR_ACTIVE) */

    /* Set Data Endpoint 1 Interrupt. */
    #if (USBFS_EP1_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_1_VECT_NUM,  USBFS_EP_1_PRIOR);
        (void) CyIntSetVector(USBFS_EP_1_VECT_NUM,  &USBFS_EP_1_ISR);
    #endif /* (USBFS_EP1_ISR_ACTIVE) */

    /* Set Data Endpoint 2 Interrupt. */
    #if (USBFS_EP2_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_2_VECT_NUM,  USBFS_EP_2_PRIOR);
        (void) CyIntSetVector(USBFS_EP_2_VECT_NUM, &USBFS_EP_2_ISR);
    #endif /* (USBFS_EP2_ISR_ACTIVE) */

    /* Set Data Endpoint 3 Interrupt. */
    #if (USBFS_EP3_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_3_VECT_NUM,  USBFS_EP_3_PRIOR);
        (void) CyIntSetVector(USBFS_EP_3_VECT_NUM, &USBFS_EP_3_ISR);
    #endif /* (USBFS_EP3_ISR_ACTIVE) */

    /* Set Data Endpoint 4 Interrupt. */
    #if (USBFS_EP4_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_4_VECT_NUM,  USBFS_EP_4_PRIOR);
        (void) CyIntSetVector(USBFS_EP_4_VECT_NUM, &USBFS_EP_4_ISR);
    #endif /* (USBFS_EP4_ISR_ACTIVE) */

    /* Set Data Endpoint 5 Interrupt. */
    #if (USBFS_EP5_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_5_VECT_NUM,  USBFS_EP_5_PRIOR);
        (void) CyIntSetVector(USBFS_EP_5_VECT_NUM, &USBFS_EP_5_ISR);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    /* Set Data Endpoint 6 Interrupt. */
    #if (USBFS_EP6_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_6_VECT_NUM,  USBFS_EP_6_PRIOR);
        (void) CyIntSetVector(USBFS_EP_6_VECT_NUM, &USBFS_EP_6_ISR);
    #endif /* (USBFS_EP6_ISR_ACTIVE) */

     /* Set Data Endpoint 7 Interrupt. */
    #if (USBFS_EP7_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_7_VECT_NUM,  USBFS_EP_7_PRIOR);
        (void) CyIntSetVector(USBFS_EP_7_VECT_NUM, &USBFS_EP_7_ISR);
    #endif /* (USBFS_EP7_ISR_ACTIVE) */

    /* Set Data Endpoint 8 Interrupt. */
    #if (USBFS_EP8_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_EP_8_VECT_NUM,  USBFS_EP_8_PRIOR);
        (void) CyIntSetVector(USBFS_EP_8_VECT_NUM, &USBFS_EP_8_ISR);
    #endif /* (USBFS_EP8_ISR_ACTIVE) */

    /* Set ARB Interrupt. */
    #if (USBFS_EP_MANAGEMENT_DMA && USBFS_ARB_ISR_ACTIVE)
        CyIntSetPriority     (USBFS_ARB_VECT_NUM,  USBFS_ARB_PRIOR);
        (void) CyIntSetVector(USBFS_ARB_VECT_NUM, &USBFS_ARB_ISR);
    #endif /* (USBFS_EP_MANAGEMENT_DMA && USBFS_ARB_ISR_ACTIVE) */
#endif /* (CY_PSOC4) */

    /* Common: Configure GPIO interrupt for wakeup. */
#if (USBFS_DP_ISR_ACTIVE)
    CyIntSetPriority     (USBFS_DP_INTC_VECT_NUM,  USBFS_DP_INTC_PRIORITY);
    (void) CyIntSetVector(USBFS_DP_INTC_VECT_NUM, &USBFS_DP_ISR);
#endif /* (USBFS_DP_ISR_ACTIVE) */

#if (USBFS_EP_MANAGEMENT_DMA && CY_PSOC4)
    /* Initialize DMA channels. */
    USBFS_InitEpDma();
#endif /* (USBFS_EP_MANAGEMENT_DMA && CY_PSOC4) */
}


/*******************************************************************************
* Function Name: USBFS_InitComponent
****************************************************************************//**
*
*   This function initializes the component�s global variables and initiates
*   communication with the host by pull-up D+ line.
*
* \param device:
*   Contains the device number of the desired device descriptor. The device
*   number can be found in the Device Descriptor Tab of "Configure" dialog,
*   under the settings of desired Device Descriptor, in the *Device Number*
*   field.
*  \param mode:
*   The operating voltage. This determines whether the voltage regulator
*   is enabled for 5V operation or if pass through mode is used for 3.3V
*   operation. Symbolic names and their associated values are given in the
*   following list.
*
*       *USBFS_3V_OPERATION* - Disable voltage regulator and pass-
*                                      through Vcc for pull-up
*
*       *USBFS_5V_OPERATION* - Enable voltage regulator and use
*                                      regulator for pull-up
*
*       *USBFS_DWR_POWER_OPERATION* - Enable or disable the voltage
*                                      regulator depending on the power supply
*                                      voltage configuration in the DWR tab.
*                                      For PSoC 3/5LP devices, the VDDD supply
*                                      voltage is considered and for PSoC 4A-L,
*                                      the VBUS supply voltage is considered.
*
* \globalvars
*   \ref USBFS_device
*   \ref USBFS_transferState
*   \ref USBFS_configuration
*   \ref USBFS_deviceStatus
*
*   \ref USBFS_deviceAddress - Contains the current device address. This
*       variable is initialized to zero in this API. The Host starts to communicate
*      to the device with address 0 and then sets it to a whatever value using a
*      SET_ADDRESS request.
*
*   \ref USBFS_lastPacketSize - Initialized to 0;
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_InitComponent(uint8 device, uint8 mode) 
{
    /* Initialize _hidProtocol variable to comply with
    *  HID 7.2.6 Set_Protocol Request:
    *  "When initialized, all devices default to report protocol."
    */
#if defined(USBFS_ENABLE_HID_CLASS)
    uint8 i;

    for (i = 0u; i < USBFS_MAX_INTERFACES_NUMBER; i++)
    {
        USBFS_hidProtocol[i] = USBFS_PROTOCOL_REPORT;
    }
#endif /* USBFS_ENABLE_HID_CLASS */

    /* Store device number to access descriptor. */
    USBFS_device = device;

    /* Reset component internal variables. */
    USBFS_transferState   = USBFS_TRANS_STATE_IDLE;
    USBFS_configurationChanged = 0u;
    USBFS_configuration   = 0u;
    USBFS_interfaceNumber = 0u;
    USBFS_deviceAddress   = 0u;
    USBFS_deviceStatus    = 0u;
    USBFS_lastPacketSize  = 0u;

    /* Enable component interrupts. */
#if (CY_PSOC4)
    CyIntEnable(USBFS_INTR_HI_VECT_NUM);
    CyIntEnable(USBFS_INTR_MED_VECT_NUM);
    CyIntEnable(USBFS_INTR_LO_VECT_NUM);
#else
    CyIntEnable(USBFS_BUS_RESET_VECT_NUM);
    CyIntEnable(USBFS_EP_0_VECT_NUM);

    #if (USBFS_SOF_ISR_ACTIVE)
        CyIntEnable(USBFS_SOF_VECT_NUM);
    #endif /* (USBFS_SOF_ISR_ACTIVE) */

    #if (USBFS_EP1_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_1_VECT_NUM);
    #endif /* (USBFS_EP1_ISR_ACTIVE) */

    #if (USBFS_EP2_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_2_VECT_NUM);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    #if (USBFS_EP3_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_3_VECT_NUM);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    #if (USBFS_EP4_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_4_VECT_NUM);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    #if (USBFS_EP5_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_5_VECT_NUM);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    #if (USBFS_EP6_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_6_VECT_NUM);
    #endif /* USBFS_EP6_ISR_REMOVE */

    #if (USBFS_EP7_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_7_VECT_NUM);
    #endif /* (USBFS_EP7_ISR_ACTIVE) */

    #if (USBFS_EP8_ISR_ACTIVE)
        CyIntEnable(USBFS_EP_8_VECT_NUM);
    #endif /* (USBFS_EP8_ISR_ACTIVE) */
#endif /* (CY_PSOC4) */

#if (USBFS_EP_MANAGEMENT_DMA && USBFS_ARB_ISR_ACTIVE)
    /* Enable ARB EP interrupt sources. */
    USBFS_ARB_INT_EN_REG = USBFS_DEFAULT_ARB_INT_EN;

    #if (CY_PSOC3 || CY_PSOC5)
        CyIntEnable(USBFS_ARB_VECT_NUM);
    #endif /* (CY_PSOC3 || CY_PSOC5) */
#endif   /* (USBFS_EP_MANAGEMENT_DMA && USBFS_ARB_ISR_ACTIVE) */

/* Arbiter configuration for DMA transfers. */
#if (USBFS_EP_MANAGEMENT_DMA)
    /* Configure Arbiter for Manual or Auto DMA operation and clear configuration completion. */
    USBFS_ARB_CFG_REG = USBFS_DEFAULT_ARB_CFG;

    #if (CY_PSOC4)
        /* Enable DMA operation. */
        CyDmaEnable();

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
            /* Change DMA priority to be highest. */
             CyIntSetPriority(CYDMA_INTR_NUMBER, USBFS_DMA_AUTO_INTR_PRIO);
        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    #endif /* (CY_PSOC4) */

    #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
        #if (CY_PSOC4)
            /* Enable DMA interrupt to handle DMA management. */
            CyIntEnable(CYDMA_INTR_NUMBER);
        #else
            #if (USBFS_EP_DMA_AUTO_OPT == 0u)
                /* Initialize interrupts which handle verification of successful DMA transaction. */
                USBFS_EP_DMA_Done_isr_StartEx(&USBFS_EP_DMA_DONE_ISR);
                USBFS_EP17_DMA_Done_SR_InterruptEnable();
                USBFS_EP8_DMA_Done_SR_InterruptEnable();
            #endif /* (USBFS_EP_DMA_AUTO_OPT == 0u) */
        #endif /* (CY_PSOC4) */
    #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
#endif /* (USBFS_EP_MANAGEMENT_DMA) */

    /* Enable USB regulator depends on operation voltage. IMO Locking is enabled in Init(). */
    switch(mode)
    {
    case USBFS_3V_OPERATION:
        /* Disable regulator for 3V operation. */
        USBFS_CR1_REG &= (uint8) ~USBFS_CR1_REG_ENABLE;
        break;

    case USBFS_5V_OPERATION:
        /* Enable regulator for 5V operation. */
        USBFS_CR1_REG |= (uint8)  USBFS_CR1_REG_ENABLE;
        break;

    default: /* Check DWR settings of USB power supply. */
    #if (USBFS_VDDD_MV < USBFS_3500MV)
        /* Disable regulator for 3V operation. */
        USBFS_CR1_REG &= (uint8) ~USBFS_CR1_REG_ENABLE;
    #else
        /* Enable regulator for 5V operation. */
        USBFS_CR1_REG |= (uint8)  USBFS_CR1_REG_ENABLE;
    #endif /* (USBFS_VDDD_MV < USBFS_3500MV) */
        break;
    }

#if (CY_PSOC4)
    /* Clear bus activity. */
    USBFS_CR1_REG &= (uint32) ~USBFS_CR1_BUS_ACTIVITY;

    /* Clear EP0 count register. */
    USBFS_EP0_CNT_REG = USBFS_CLEAR_REG;

    /* Set EP0.CR: ACK Setup, NAK IN/OUT. */
    USBFS_EP0_CR_REG = USBFS_MODE_NAK_IN_OUT;

    #if (USBFS_LPM_ACTIVE)
        if (NULL != USBFS_GetBOSPtr())
        {
            /* Enable LPM and acknowledge LPM packets for active device.
            * Reset NYET_EN and SUB_RESP bits in the LPM_CTRL register.
            */
            USBFS_LPM_CTRL_REG = (USBFS_LPM_CTRL_LPM_EN | \
                                             USBFS_LPM_CTRL_LPM_ACK_RESP);
        }
        else
        {
            /* Disable LPM for active device. */
            USBFS_LPM_CTRL_REG &= (uint32) ~USBFS_LPM_CTRL_LPM_EN;
        }
    #endif /* (USBFS_LPM_ACTIVE) */

    /* Enable device to responds to USB traffic with address 0. */
    USBFS_CR0_REG = USBFS_DEFUALT_CR0;

#else
    /* Set EP0.CR: ACK Setup, STALL IN/OUT. */
    USBFS_EP0_CR_REG = USBFS_MODE_STALL_IN_OUT;

    /* Enable device to respond to USB traffic with address 0. */
    USBFS_CR0_REG = USBFS_DEFUALT_CR0;
    CyDelayCycles(USBFS_WAIT_CR0_REG_STABILITY);
#endif /* (CY_PSOC4) */

    /* Enable D+ pull-up and keep USB control on IO. */
    USBFS_USBIO_CR1_REG = USBFS_USBIO_CR1_USBPUEN;
}


/*******************************************************************************
* Function Name: USBFS_ReInitComponent
****************************************************************************//**
*
*  This function reinitialize the component configuration and is
*  intend to be called from the Reset interrupt.
*
* \globalvars
*   USBFS_device - Contains the device number of the desired Device
*        Descriptor. The device number can be found in the Device Descriptor tab
*       of the Configure dialog, under the settings of the desired Device Descriptor,
*       in the Device Number field.
*   USBFS_transferState - This variable is used by the communication
*       functions to handle the current transfer state. Initialized to
*       TRANS_STATE_IDLE in this API.
*   USBFS_configuration - Contains the current configuration number
*       set by the Host using a SET_CONFIGURATION request.
*       Initialized to zero in this API.
*   USBFS_deviceAddress - Contains the current device address. This
*       variable is initialized to zero in this API. The Host starts to communicate
*      to the device with address 0 and then sets it to a whatever value using
*      a SET_ADDRESS request.
*   USBFS_deviceStatus - Initialized to 0.
*       This is a two-bit variable which contains the power status in the first bit
*       (DEVICE_STATUS_BUS_POWERED or DEVICE_STATUS_SELF_POWERED) and the remote
*       wakeup status (DEVICE_STATUS_REMOTE_WAKEUP) in the second bit.
*   USBFS_lastPacketSize - Initialized to 0;
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_ReInitComponent(void) 
{
    /* Initialize _hidProtocol variable to comply with HID 7.2.6 Set_Protocol
    *  Request: "When initialized, all devices default to report protocol."
    */
#if defined(USBFS_ENABLE_HID_CLASS)
    uint8 i;

    for (i = 0u; i < USBFS_MAX_INTERFACES_NUMBER; i++)
    {
        USBFS_hidProtocol[i] = USBFS_PROTOCOL_REPORT;
    }
#endif /* USBFS_ENABLE_HID_CLASS */

    /* Reset component internal variables. */
    USBFS_transferState   = USBFS_TRANS_STATE_IDLE;
    USBFS_configurationChanged = 0u;
    USBFS_configuration   = 0u;
    USBFS_interfaceNumber = 0u;
    USBFS_deviceAddress   = 0u;
    USBFS_deviceStatus    = 0u;
    USBFS_lastPacketSize  = 0u;

#if (CY_PSOC4)
    /* Set EP0.CR: ACK Setup, NAK IN/OUT. */
    USBFS_EP0_CR_REG = USBFS_MODE_NAK_IN_OUT;
#else
    /* Set EP0.CR: ACK Setup, STALL IN/OUT. */
    USBFS_EP0_CR_REG = USBFS_MODE_STALL_IN_OUT;
#endif /* (CY_PSOC4) */

    /* Enable device to respond to USB traffic with address 0. */
    USBFS_CR0_REG = USBFS_DEFUALT_CR0;
}


/*******************************************************************************
* Function Name: USBFS_Stop
****************************************************************************//**
*
*  This function shuts down the USB function including to release
*  the D+ pull-up and disabling the SIE.
*
* \globalvars
*   \ref USBFS_configuration
*
*   USBFS_deviceAddress - Contains the current device address. This
*       variable is initialized to zero in this API. The Host starts to communicate
*      to the device with address 0 and then sets it to a whatever value using
*      a SET_ADDRESS request.
*
*   \ref USBFS_deviceStatus
*
*   \ref USBFS_configurationChanged
*
*   USBFS_intiVar -  This variable is set to zero
*
*******************************************************************************/
void USBFS_Stop(void) 
{
    uint8 enableInterrupts;

#if (USBFS_EP_MANAGEMENT_DMA)
    /* Stop all DMA channels. */
    USBFS_Stop_DMA(USBFS_MAX_EP);
#endif /* (USBFS_EP_MANAGEMENT_DMA) */

    enableInterrupts = CyEnterCriticalSection();

    /* Disable USB IP to respond to USB traffic. */
    USBFS_CR0_REG &= (uint8) ~USBFS_CR0_ENABLE;

    /* Disable D+ pull-up. */
    USBFS_USBIO_CR1_REG &= (uint8) ~ USBFS_USBIO_CR1_USBPUEN;

#if (CY_PSOC4)
    /* Disable USBFS block. */
    USBFS_POWER_CTRL_REG &= (uint32) ~USBFS_POWER_CTRL_ENABLE;
#else
    /* Clear power active and standby mode templates. */
    USBFS_PM_ACT_CFG_REG  &= (uint8) ~USBFS_PM_ACT_EN_FSUSB;
    USBFS_PM_STBY_CFG_REG &= (uint8) ~USBFS_PM_STBY_EN_FSUSB;
#endif /* (CY_PSOC4) */

    CyExitCriticalSection(enableInterrupts);

    /* Disable component interrupts. */
#if (CY_PSOC4)
    CyIntDisable(USBFS_INTR_HI_VECT_NUM);
    CyIntDisable(USBFS_INTR_LO_VECT_NUM);
    CyIntDisable(USBFS_INTR_MED_VECT_NUM);
#else

    CyIntDisable(USBFS_BUS_RESET_VECT_NUM);
    CyIntDisable(USBFS_EP_0_VECT_NUM);

    #if (USBFS_SOF_ISR_ACTIVE)
        CyIntDisable(USBFS_SOF_VECT_NUM);
    #endif /* (USBFS_SOF_ISR_ACTIVE) */

    #if (USBFS_EP1_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_1_VECT_NUM);
    #endif /* (USBFS_EP1_ISR_ACTIVE) */

    #if (USBFS_EP2_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_2_VECT_NUM);
    #endif /* (USBFS_EP2_ISR_ACTIVE) */

    #if (USBFS_EP3_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_3_VECT_NUM);
    #endif /* (USBFS_EP3_ISR_ACTIVE) */

    #if (USBFS_EP4_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_4_VECT_NUM);
    #endif /* (USBFS_EP4_ISR_ACTIVE) */

    #if (USBFS_EP5_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_5_VECT_NUM);
    #endif /* (USBFS_EP5_ISR_ACTIVE) */

    #if (USBFS_EP6_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_6_VECT_NUM);
    #endif /* USBFS_EP6_ISR_REMOVE */

    #if (USBFS_EP7_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_7_VECT_NUM);
    #endif /* (USBFS_EP7_ISR_ACTIVE) */

    #if (USBFS_EP8_ISR_ACTIVE)
        CyIntDisable(USBFS_EP_8_VECT_NUM);
    #endif /* (USBFS_EP8_ISR_ACTIVE) */
#endif /* (CY_PSOC4) */

    /* Reset component internal variables. */
    USBFS_configurationChanged = 0u;
    USBFS_configuration   = 0u;
    USBFS_interfaceNumber = 0u;
    USBFS_deviceAddress   = 0u;
    USBFS_deviceStatus    = 0u;

    /* It is mandatory for correct device startup. */
    USBFS_initVar = 0u;
}


/*******************************************************************************
* Function Name: USBFS_CheckActivity
****************************************************************************//**
*
*  This function returns the activity status of the bus. It clears the hardware
*  status to provide updated status on the next call of this function. It
*  provides a way to determine whether any USB bus activity occurred. The
*  application should use this function to determine if the USB suspend
*  conditions are met.
*
*
* \return
*  cystatus: Status of the bus since the last call of the function.
*  Return Value |   Description
*  -------------|---------------------------------------------------------------
*  1            |Bus activity was detected since the last call to this function
*  0            |Bus activity was not detected since the last call to this function
*
*
*******************************************************************************/
uint8 USBFS_CheckActivity(void) 
{
    uint8 cr1Reg = USBFS_CR1_REG;

    /* Clear bus activity. */
    USBFS_CR1_REG = (cr1Reg & (uint8) ~USBFS_CR1_BUS_ACTIVITY);

    /* Get bus activity. */
    return ((0u != (cr1Reg & USBFS_CR1_BUS_ACTIVITY)) ? (1u) : (0u));
}


/*******************************************************************************
* Function Name: USBFS_GetConfiguration
****************************************************************************//**
*
*  This function gets the current configuration of the USB device.
*
* \return
*  Returns the currently assigned configuration. Returns 0 if the device
*  is not configured
*
*******************************************************************************/
uint8 USBFS_GetConfiguration(void) 
{
    return (USBFS_configuration);
}


/*******************************************************************************
* Function Name: USBFS_IsConfigurationChanged
****************************************************************************//**
*
*  This function returns the clear-on-read configuration state.  It is useful
*  when the host sends double SET_CONFIGURATION request with the same 
*  configuration number or changes alternate settings of the interface. 
*  After configuration has been changed the OUT endpoints must be enabled and IN 
*  endpoint must be loaded with data to start communication with the host.
*
* \return
*  None-zero value when new configuration has been changed, otherwise zero is
*  returned.
*
* \globalvars
*
*  \ref USBFS_configurationChanged - This variable is set to 1 after
*   a SET_CONFIGURATION request and cleared in this function.
*
*******************************************************************************/
uint8 USBFS_IsConfigurationChanged(void) 
{
    uint8 res = 0u;

    if (USBFS_configurationChanged != 0u)
    {
        res = USBFS_configurationChanged;
        USBFS_configurationChanged = 0u;
    }

    return (res);
}


/*******************************************************************************
* Function Name: USBFS_GetInterfaceSetting
****************************************************************************//**
*
*  This function gets the current alternate setting for the specified interface.
*  It is useful to identify which alternate settings are active in the specified 
*  interface.
*
*  \param
*  interfaceNumber interface number
*
* \return
*  Returns the current alternate setting for the specified interface.
*
*******************************************************************************/
uint8  USBFS_GetInterfaceSetting(uint8 interfaceNumber)
                                                    
{
    return (USBFS_interfaceSetting[interfaceNumber]);
}


/*******************************************************************************
* Function Name: USBFS_GetEPState
****************************************************************************//**
*
*  This function returns the state of the requested endpoint.
*
* \param epNumber Data endpoint number
*
* \return
*  Returns the current state of the specified USBFS endpoint. Symbolic names and
*  their associated values are given in the following table. Use these constants
*  whenever you write code to change the state of the endpoints, such as ISR
*  code, to handle data sent or received.
*
*  Return Value           | Description
*  -----------------------|-----------------------------------------------------
*  USBFS_NO_EVENT_PENDING |The endpoint is awaiting SIE action
*  USBFS_EVENT_PENDING    |The endpoint is awaiting CPU action
*  USBFS_NO_EVENT_ALLOWED |The endpoint is locked from access
*  USBFS_IN_BUFFER_FULL   |The IN endpoint is loaded and the mode is set to ACK IN
*  USBFS_IN_BUFFER_EMPTY  |An IN transaction occurred and more data can be loaded
*  USBFS_OUT_BUFFER_EMPTY |The OUT endpoint is set to ACK OUT and is waiting for data
*  USBFS_OUT_BUFFER_FULL  |An OUT transaction has occurred and data can be read
*
*******************************************************************************/
uint8 USBFS_GetEPState(uint8 epNumber) 
{
    return (USBFS_EP[epNumber].apiEpState);
}


/*******************************************************************************
* Function Name: USBFS_GetEPCount
****************************************************************************//**
*
*  This function supports Data Endpoints only(EP1-EP8).
*  Returns the transfer count for the requested endpoint.  The value from
*  the count registers includes 2 counts for the two byte checksum of the
*  packet.  This function subtracts the two counts.
*
*  \param epNumber Data Endpoint Number.
*                   Valid values are between 1 and 8.
*
* \return
*  Returns the current byte count from the specified endpoint or 0 for an
*  invalid endpoint.
*
*******************************************************************************/
uint16 USBFS_GetEPCount(uint8 epNumber) 
{
    uint16 cntr = 0u;

    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
        /* Get 11-bits EP counter where epCnt0 - 3 bits MSB and epCnt1 - 8 bits LSB. */
        cntr  = ((uint16) USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt0) & USBFS_EPX_CNT0_MASK;
        cntr  = ((uint16) (cntr << 8u)) | ((uint16) USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt1);
        cntr -= USBFS_EPX_CNTX_CRC_COUNT;
    }

    return (cntr);
}


#if (USBFS_EP_MANAGEMENT_DMA)
#if (CY_PSOC4)
    /*******************************************************************************
    * Function Name: USBFS_InitEpDma
    ****************************************************************************//**
    *
    *  This function configures priority for all DMA channels utilized by the
    *  component. Also sets callbacks for DMA auto mode.
    *
    *******************************************************************************/
    static void USBFS_InitEpDma(void)
    {
    #if (USBFS_DMA1_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep1_dma_CHANNEL] = USBFS_ep1_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA1_ACTIVE) */

    #if (USBFS_DMA2_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep2_dma_CHANNEL] = USBFS_ep2_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA2_ACTIVE) */

    #if (USBFS_DMA3_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep3_dma_CHANNEL] = USBFS_ep3_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA3_ACTIVE) */

    #if (USBFS_DMA4_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep4_dma_CHANNEL] = USBFS_ep4_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA4_ACTIVE) */

    #if (USBFS_DMA5_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep5_dma_CHANNEL] = USBFS_ep5_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA5_ACTIVE) */

    #if (USBFS_DMA6_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep6_dma_CHANNEL] = USBFS_ep6_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA6_ACTIVE) */

    #if (USBFS_DMA7_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep7_dma_CHANNEL] = USBFS_ep7_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA7_ACTIVE) */

    #if (USBFS_DMA8_ACTIVE)
        CYDMA_CH_CTL_BASE.ctl[USBFS_ep8_dma_CHANNEL] = USBFS_ep8_dma_CHANNEL_CFG;
    #endif /* (USBFS_DMA8_ACTIVE) */

    #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
        /* Initialize DMA channel callbacks. */
        #if (USBFS_DMA1_ACTIVE)
            (void) USBFS_ep1_dma_SetInterruptCallback(&USBFS_EP1_DMA_DONE_ISR);
        #endif /* (USBFS_DMA1_ACTIVE) */

        #if (USBFS_DMA2_ACTIVE)
            (void) USBFS_ep2_dma_SetInterruptCallback(&USBFS_EP2_DMA_DONE_ISR);
        #endif /* (USBFS_DMA2_ACTIVE) */

        #if (USBFS_DMA3_ACTIVE)
            (void) USBFS_ep3_dma_SetInterruptCallback(&USBFS_EP3_DMA_DONE_ISR);
        #endif /* (USBFS_DMA3_ACTIVE) */

        #if (USBFS_DMA4_ACTIVE)
            (void) USBFS_ep4_dma_SetInterruptCallback(&USBFS_EP4_DMA_DONE_ISR);
        #endif /* (USBFS_DMA4_ACTIVE) */

        #if (USBFS_DMA5_ACTIVE)
            (void) USBFS_ep5_dma_SetInterruptCallback(&USBFS_EP5_DMA_DONE_ISR);
        #endif /* (USBFS_DMA5_ACTIVE) */

        #if (USBFS_DMA6_ACTIVE)
            (void) USBFS_ep6_dma_SetInterruptCallback(&USBFS_EP6_DMA_DONE_ISR);
        #endif /* (USBFS_DMA6_ACTIVE) */

        #if (USBFS_DMA7_ACTIVE)
            (void) USBFS_ep7_dma_SetInterruptCallback(&USBFS_EP7_DMA_DONE_ISR);
        #endif /* (USBFS_DMA7_ACTIVE) */

        #if (USBFS_DMA8_ACTIVE)
            (void) USBFS_ep8_dma_SetInterruptCallback(&USBFS_EP8_DMA_DONE_ISR);
        #endif /* (USBFS_DMA8_ACTIVE) */
    #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    }
#else


    /***************************************************************************
    * Function Name: USBFS_InitEP_DMA
    ************************************************************************//**
    *
    *  This function allocates and initializes a DMA channel to be used by the
    *  USBFS_LoadInEP() or USBFS_ReadOutEP() APIs for data
    *  transfer. It is available when the Endpoint Memory Management parameter
    *  is set to DMA.
    *
    *  This function is automatically called from the USBFS_LoadInEP() and USBFS_ReadOutEP() APIs.
    *
    *  \param epNumber Contains the data endpoint number.
    *            Valid values are between 1 and 8.
    *  \param *pData Pointer to a data array that is related to the EP transfers.
    *
    * \reentrant No.
    *
    ***************************************************************************/
    void USBFS_InitEP_DMA(uint8 epNumber, const uint8 *pData)
                                                                    
    {
        uint16 src;
        uint16 dst;

    #if (CY_PSOC3)
        src = HI16(CYDEV_SRAM_BASE);
        dst = HI16(CYDEV_PERIPH_BASE);
        pData = pData;
    #else
        if ((USBFS_EP[epNumber].addr & USBFS_DIR_IN) != 0u)
        {
            /* IN endpoint: source is memory buffer. */
            src = HI16(pData);
            dst = HI16(CYDEV_PERIPH_BASE);
        }
        else
        {
            /* OUT endpoint: source is USB IP memory buffer. */
            src = HI16(CYDEV_PERIPH_BASE);
            dst = HI16(pData);
        }
    #endif  /* (CY_PSOC3) */

        switch(epNumber)
        {
    #if (USBFS_DMA1_ACTIVE)
        case USBFS_EP1:
            USBFS_DmaChan[epNumber] = USBFS_ep1_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                    USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA1_ACTIVE) */

    #if (USBFS_DMA2_ACTIVE)
        case USBFS_EP2:
            USBFS_DmaChan[epNumber] = USBFS_ep2_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                    USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA2_ACTIVE) */

    #if (USBFS_DMA3_ACTIVE)
            case USBFS_EP3:
                USBFS_DmaChan[epNumber] = USBFS_ep3_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                        USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA3_ACTIVE) */

    #if (USBFS_DMA4_ACTIVE)
            case USBFS_EP4:
                USBFS_DmaChan[epNumber] = USBFS_ep4_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                        USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA4_ACTIVE) */

    #if (USBFS_DMA5_ACTIVE)
            case USBFS_EP5:
                USBFS_DmaChan[epNumber] = USBFS_ep5_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                        USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA5_ACTIVE) */

    #if (USBFS_DMA6_ACTIVE)
        case USBFS_EP6:
            USBFS_DmaChan[epNumber] = USBFS_ep6_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                    USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA6_ACTIVE) */

    #if (USBFS_DMA7_ACTIVE)
        case USBFS_EP7:
                USBFS_DmaChan[epNumber] = USBFS_ep7_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                        USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA7_ACTIVE) */

    #if (USBFS_DMA8_ACTIVE)
        case USBFS_EP8:
                USBFS_DmaChan[epNumber] = USBFS_ep8_DmaInitialize(USBFS_DMA_BYTES_PER_BURST,
                                                                                        USBFS_DMA_REQUEST_PER_BURST, src, dst);
            break;
    #endif /* (USBFS_DMA8_ACTIVE) */

        default:
            /* Do nothing for endpoints other than 1-8. */
            break;
        }

        if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
        {
            USBFS_DmaTd[epNumber] = CyDmaTdAllocate();

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u))
            USBFS_DmaNextTd[epNumber] = CyDmaTdAllocate();
        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u)) */
        }
    }
#endif /* (CY_PSOC4) */

    /***************************************************************************
    * Function Name: USBFS_Stop_DMA
    ************************************************************************//**
    *  
    *  This function stops DMA channel associated with endpoint. It is available 
    *  when the Endpoint Buffer Management parameter is set to DMA. Call this 
    *  function when endpoint direction is changed from IN to OUT or vice versa 
    *  to trigger DMA re-configuration when USBFS_LoadInEP() or 
    *  USBFS_ReadOutEP() functions are called the first time. 
    *  
    *  \param epNumber: The data endpoint number for which associated DMA 
    *  channel is stopped. The range of valid values is between 1 and 8. To stop 
    *  all DMAs associated with endpoints call this function with 
    *  USBFS_MAX_EP argument.
    *
    * \reentrant
    *  No.
    *
    ***************************************************************************/
    void USBFS_Stop_DMA(uint8 epNumber) 
    {
        uint8 i;

        i = (epNumber < USBFS_MAX_EP) ? epNumber : USBFS_EP1;

        do
        {
        #if (CY_PSOC4)
            if (0u != USBFS_DmaChan[i])
            {
                USBFS_CyDmaChDisable(USBFS_DmaChan[i]);
            }
        #else
            if(USBFS_DmaTd[i] != DMA_INVALID_TD)
            {
                (void) CyDmaChDisable(USBFS_DmaChan[i]);
                CyDmaTdFree(USBFS_DmaTd[i]);
                USBFS_DmaTd[i] = DMA_INVALID_TD;
            }

            #if (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u))
                if(USBFS_DmaNextTd[i] != DMA_INVALID_TD)
                {
                    CyDmaTdFree(USBFS_DmaNextTd[i]);
                    USBFS_DmaNextTd[i] = DMA_INVALID_TD;
                }
            #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u)) */

        #endif /* (CY_PSOC4) */
            i++;
        }
        while ((i < USBFS_MAX_EP) && (epNumber == USBFS_MAX_EP));
    }
#endif /* (USBFS_EP_MANAGEMENT_DMA) */


#if (CY_PSOC3 || CY_PSOC5)
#if (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u))
    /***************************************************************************
    * Function Name: USBFS_LoadNextInEP
    ************************************************************************//**
    *
    * Summary:
    *  This internal function is used for IN endpoint DMA reconfiguration in
    *  Auto DMA mode.
    *
    * Parameters:
    *  epNumber: Contains the data endpoint number.
    *  mode:   0 - Configure DMA to send the the rest of data.
    *          1 - Configure DMA to repeat 2 last bytes of the first burst.
    *
    ***************************************************************************/
    void USBFS_LoadNextInEP(uint8 epNumber, uint8 mode) 
    {
        reg16 *convert;

        if (mode == 0u)
        {
            /* Configure DMA to send rest of data. */
            /* CyDmaTdSetConfiguration API is optimized to change transfer length only and configure TD. */
            convert = (reg16 *) &CY_DMA_TDMEM_STRUCT_PTR[USBFS_DmaTd[epNumber]].TD0[0u];
            /* Set transfer length. */
            CY_SET_REG16(convert, USBFS_inLength[epNumber] - USBFS_DMA_BYTES_PER_BURST);
            /* CyDmaTdSetAddress API is optimized to change source address only. */
            convert = (reg16 *) &CY_DMA_TDMEM_STRUCT_PTR[USBFS_DmaTd[epNumber]].TD1[0u];
            CY_SET_REG16(convert, LO16((uint32)USBFS_inDataPointer[epNumber] +
                                            USBFS_DMA_BYTES_PER_BURST));
            USBFS_inBufFull[epNumber] = 1u;
        }
        else
        {
            /* Configure DMA to repeat 2 last bytes of the first burst. */
            /* CyDmaTdSetConfiguration API is optimized to change transfer length only and configure TD. */
            convert = (reg16 *) &CY_DMA_TDMEM_STRUCT_PTR[USBFS_DmaTd[epNumber]].TD0[0u];
            /* Set transfer length. */
            CY_SET_REG16(convert, USBFS_DMA_BYTES_REPEAT);
            /* CyDmaTdSetAddress API is optimized to change source address only. */
            convert = (reg16 *) &CY_DMA_TDMEM_STRUCT_PTR[USBFS_DmaTd[epNumber]].TD1[0u];
            CY_SET_REG16(convert,  LO16((uint32)USBFS_inDataPointer[epNumber] +
                                   (USBFS_DMA_BYTES_PER_BURST - USBFS_DMA_BYTES_REPEAT)));
        }

        /* CyDmaChSetInitialTd API is optimized to initialize TD. */
        CY_DMA_CH_STRUCT_PTR[USBFS_DmaChan[epNumber]].basic_status[1u] = USBFS_DmaTd[epNumber];
    }
#endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO && (USBFS_EP_DMA_AUTO_OPT == 0u)) */
#endif /* (CY_PSOC3 || CY_PSOC5) */


/*******************************************************************************
* Function Name: USBFS_LoadInEP
****************************************************************************//**
*
*  This function performs different functionality depending on the Component�s
*  configured Endpoint Buffer Management. This parameter is defined in
*  the Descriptor Root in Component Configure window.
*
*  *Manual (Static/Dynamic Allocation):*
*  This function loads and enables the specified USB data endpoint for an IN
*  data transfer.
*
*  *DMA with Manual Buffer Management:*
*  Configures DMA for a data transfer from system RAM to endpoint buffer.
*  Generates request for a transfer.
*
*  *DMA with Automatic Buffer Management:*
*  Configures DMA. This is required only once, so it is done only when parameter
*  pData is not NULL. When the pData pointer is NULL, the function skips this
*  task. Sets Data ready status: This generates the first DMA transfer and
*  prepares data in endpoint buffer.
*
*  \param epNumber Contains the data endpoint number.
*            Valid values are between 1 and 8.
*  \param *pData A pointer to a data array from which the data for the endpoint space
*          is loaded.
*  \param length The number of bytes to transfer from the array and then send as
*          a result of an IN request. Valid values are between 0 and 512
*          (1023 for DMA with Automatic Buffer Management mode). The value 512
*          is applicable if only one endpoint is used.
*
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_LoadInEP(uint8 epNumber, const uint8 pData[], uint16 length)
                                                                        
{
    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
    #if (!USBFS_EP_MANAGEMENT_DMA_AUTO)
        /* Limit length to available buffer USB IP buffer size.*/
        if (length > (USBFS_EPX_DATA_BUF_MAX - USBFS_EP[epNumber].buffOffset))
        {
            length = USBFS_EPX_DATA_BUF_MAX - USBFS_EP[epNumber].buffOffset;
        }
    #endif /* (!USBFS_EP_MANAGEMENT_DMA_AUTO) */

        /* Set count and data toggle. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt0 = (uint8) HI8(length) | USBFS_EP[epNumber].epToggle;
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt1 = (uint8) LO8(length);

    #if (USBFS_EP_MANAGEMENT_MANUAL)
        if (NULL != pData)
        {
			/* Copy data using arbiter data register. */
            uint16 i;
            for (i = 0u; i < length; ++i)
            {
                USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr = pData[i];
            }
        }

        /* IN endpoint buffer is full - read to be read. */
        USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

        /* Arm IN endpoint. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;

    #else

        #if (CY_PSOC3 || CY_PSOC5LP)
            /* Initialize DMA if it was not initialized. */
            if (DMA_INVALID_TD == USBFS_DmaTd[epNumber])
            {
                USBFS_InitEP_DMA(epNumber, pData);
            }
        #endif /* (CY_PSOC3 || CY_PSOC5LP) */

        #if (USBFS_EP_MANAGEMENT_DMA_MANUAL)
            /* IN endpoint buffer will be fully loaded by DMA shortly. */
            USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

            if ((pData != NULL) && (length > 0u))
            {
            #if (CY_PSOC4)
                uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                /* Disable DMA channel: start configuration. */
                USBFS_CyDmaChDisable(channelNum);

                /* Configure source and destination. */
                USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) pData);
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);

                /* Configure DMA descriptor. */
                --length;
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG | length |
                                                        CYDMA_BYTE | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_INVALIDATE | CYDMA_PREEMPTABLE);

                /* Validate descriptor to execute on following DMA request. */
                USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);

                /* Enable DMA channel: configuration complete. */
                USBFS_CyDmaChEnable(channelNum);
            #else
                /* Configure DMA to transfer data. */
                (void) CyDmaChDisable(USBFS_DmaChan[epNumber]);
                (void) CyDmaTdSetConfiguration(USBFS_DmaTd[epNumber], length, CY_DMA_DISABLE_TD, TD_TERMIN_EN | TD_INC_SRC_ADR);
                (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32) pData), LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr));

                /* Enable DMA channel. */
                (void) CyDmaChSetInitialTd(USBFS_DmaChan[epNumber], USBFS_DmaTd[epNumber]);
                (void) CyDmaChEnable(USBFS_DmaChan[epNumber], 1u);
            #endif /* (CY_PSOC4) */

                /* Generate DMA request. */
                USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |=  (uint8)  USBFS_ARB_EPX_CFG_DMA_REQ;
                USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg &=  (uint8) ~USBFS_ARB_EPX_CFG_DMA_REQ;

                /* IN endpoint will be armed in ARB_ISR(source: IN_BUF_FULL) after first DMA transfer has been completed. */
            }
            else
            {
                /* When zero-length packet: arm IN endpoint directly. */
                USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;
            }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_MANUAL) */

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
            if (pData != NULL)
            {
            #if (CY_PSOC4)
                uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                /* Store address of buffer. */
                USBFS_DmaEpBufferAddrBackup[epNumber] = (uint32) pData;

                /* Disable DMA channel: start configuration. */
                USBFS_CyDmaChDisable(channelNum);

                /* Set destination address. */
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR1, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);

                /* Configure DMA descriptor. */
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG  |
                                                        CYDMA_BYTE | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_INVALIDATE | CYDMA_CHAIN);

                /* Configure DMA descriptor. */
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR1, USBFS_DMA_COMMON_CFG  |
                                                        CYDMA_BYTE | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_CHAIN);

                /* Enable interrupt from DMA channel. */
                USBFS_CyDmaSetInterruptMask(channelNum);

                /* Validate descriptor 1. It will not be invalided during operation. */
                USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR1);

                /* Enable DMA channel: configuration complete. */
                USBFS_CyDmaChEnable(channelNum);

            #else
                (void) CyDmaChDisable(USBFS_DmaChan[epNumber]);

                #if (USBFS_EP_DMA_AUTO_OPT == 0u)
                    USBFS_inLength[epNumber] = length;
                    USBFS_inDataPointer[epNumber] = pData;

                    /* Configure DMA to send data only for first burst */
                    (void) CyDmaTdSetConfiguration(USBFS_DmaTd[epNumber],
                        (length > USBFS_DMA_BYTES_PER_BURST) ? USBFS_DMA_BYTES_PER_BURST : length,
                        USBFS_DmaNextTd[epNumber], TD_TERMIN_EN | TD_INC_SRC_ADR);
                    (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32) pData),
                                                                               LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr));

                    /* The second TD will be executed only when the first one fails.
                    *  The intention of this TD is to generate NRQ interrupt
                    *  and repeat 2 last bytes of the first burst.
                    */
                    (void) CyDmaTdSetConfiguration(USBFS_DmaNextTd[epNumber], 1u,
                                                   USBFS_DmaNextTd[epNumber],
                                                   USBFS_epX_TD_TERMOUT_EN[epNumber]);

                    /* Configure DmaNextTd to clear Data Ready status. */
                    (void) CyDmaTdSetAddress(USBFS_DmaNextTd[epNumber], LO16((uint32) &clearInDataRdyStatus),
                                                                                   LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg));
                #else
                    /* Configure DMA to send all data. */
                    (void) CyDmaTdSetConfiguration(USBFS_DmaTd[epNumber], length,
                                                   USBFS_DmaTd[epNumber], TD_TERMIN_EN | TD_INC_SRC_ADR);
                    (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32) pData),
                                                                               LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr));
                #endif /* (USBFS_EP_DMA_AUTO_OPT == 0u) */

                /* Clear any potential pending DMA requests before starting DMA channel to transfer data. */
                (void) CyDmaClearPendingDrq(USBFS_DmaChan[epNumber]);
                /* Enable DMA. */
                (void) CyDmaChSetInitialTd(USBFS_DmaChan[epNumber], USBFS_DmaTd[epNumber]);
                (void) CyDmaChEnable(USBFS_DmaChan[epNumber], 1u);
            #endif /* (CY_PSOC4) */
            }
            else
            {
                /* IN endpoint buffer (32 bytes) will shortly be preloaded by DMA. */
                USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

                if (length > 0u)
                {
                #if (CY_PSOC4)
                    uint32 lengthDescr0, lengthDescr1;
                    uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                    /* Get number of full bursts. */
                    USBFS_DmaEpBurstCnt[epNumber] = (uint8) (length / USBFS_DMA_BYTES_PER_BURST);

                    /* Get number of elements in the last burst. */
                    USBFS_DmaEpLastBurstEl[epNumber] = (uint8) (length % USBFS_DMA_BYTES_PER_BURST);

                    /* Get total number of bursts. */
                    USBFS_DmaEpBurstCnt[epNumber] += (0u != USBFS_DmaEpLastBurstEl[epNumber]) ? 1u : 0u;

                    /* Adjust number of data elements transferred in last burst. */
                    USBFS_DmaEpLastBurstEl[epNumber] = (0u != USBFS_DmaEpLastBurstEl[epNumber]) ?
                                                                          (USBFS_DmaEpLastBurstEl[epNumber] - 1u) :
                                                                          (USBFS_DMA_BYTES_PER_BURST - 1u);

                    /* Get number of data elements to transfer for descriptor 0 and 1. */
                    lengthDescr0 = (1u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_BYTES_PER_BURST - 1u);
                    lengthDescr1 = (2u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_BYTES_PER_BURST - 1u);


                    /* Mark which descriptor is last one to execute. */
                    USBFS_DmaEpLastBurstEl[epNumber] |= (0u != (USBFS_DmaEpBurstCnt[epNumber] & 0x1u)) ?
                                                                            USBFS_DMA_DESCR0_MASK : USBFS_DMA_DESCR1_MASK;

                    /* Adjust burst counter taking to account: 2 valid descriptors and interrupt trigger after valid descriptor were executed. */
                    USBFS_DmaEpBurstCnt[epNumber] = USBFS_DMA_GET_BURST_CNT(USBFS_DmaEpBurstCnt[epNumber]);

                    /* Restore DMA settings for current transfer. */
                    USBFS_CyDmaChDisable(channelNum);

                    /* Restore destination address for input endpoint. */
                    USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) ((uint32) USBFS_DmaEpBufferAddrBackup[epNumber]));
                    USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR1, (void*) ((uint32) USBFS_DmaEpBufferAddrBackup[epNumber] +
                                                                                                                   USBFS_DMA_BYTES_PER_BURST));

                    /* Set number of elements to transfer. */
                    USBFS_CyDmaSetNumDataElements(channelNum, USBFS_DMA_DESCR0, lengthDescr0);
                    USBFS_CyDmaSetNumDataElements(channelNum, USBFS_DMA_DESCR1, lengthDescr1);

                    /* Validate descriptor 0 and command to start with it. */
                    USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);
                    USBFS_CyDmaSetDescriptor0Next(channelNum);

                    /* Enable DMA channel: configuration complete. */
                    USBFS_CyDmaChEnable(channelNum);

                #elif (USBFS_EP_DMA_AUTO_OPT == 0u)
                    USBFS_inLength[epNumber]  = length;
                    USBFS_inBufFull[epNumber] = 0u;

                    (void) CyDmaChDisable(USBFS_DmaChan[epNumber]);
                    /* Configure DMA to send data only for first burst. */
                    (void) CyDmaTdSetConfiguration(
                        USBFS_DmaTd[epNumber], (length > USBFS_DMA_BYTES_PER_BURST) ?
                        USBFS_DMA_BYTES_PER_BURST : length,
                        USBFS_DmaNextTd[epNumber], TD_TERMIN_EN | TD_INC_SRC_ADR );
                    (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32)  USBFS_inDataPointer[epNumber]),
                                                                               LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr));
                    /* Clear Any potential pending DMA requests before starting DMA channel to transfer data. */
                    (void) CyDmaClearPendingDrq(USBFS_DmaChan[epNumber]);
                    /* Enable DMA. */
                    (void) CyDmaChSetInitialTd(USBFS_DmaChan[epNumber], USBFS_DmaTd[epNumber]);
                    (void) CyDmaChEnable(USBFS_DmaChan[epNumber], 1u);
                #endif /* (CY_PSOC4) */

                #if !defined (USBFS_MANUAL_IN_EP_ARM)
                    /* Set IN data ready to generate DMA request to load data into endpoint buffer. */
                    USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |= USBFS_ARB_EPX_CFG_IN_DATA_RDY;
                #endif  /* (USBFS_MANUAL_IN_EP_ARM) */

                    /* IN endpoint will be armed in ARB_ISR(source: IN_BUF_FULL) after first DMA transfer has been completed. */
                }
                else
                {
                    /* When zero-length packet: arm IN endpoint directly. */
                    USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;
                }
            }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    #endif /* (USBFS_EP_MANAGEMENT_MANUAL) */
    }
}


/*******************************************************************************
* Function Name: USBFS_ReadOutEP
****************************************************************************//**
*
*   This function performs different functionality depending on the Component�s
*   configured Endpoint Buffer Management. This parameter is defined in the
*   Descriptor Root in Component Configure window.
*
*   *Manual (Static/Dynamic Allocation):*
*   This function moves the specified number of bytes from endpoint buffer to
*   system RAM. The number of bytes actually transferred from endpoint buffer to
*   system RAM is the lesser of the actual number of bytes sent by the host or
*   the number of bytes requested by the length parameter.
*
*   *DMA with Manual Buffer Management:*
*   Configure DMA to transfer data from endpoint buffer to system RAM. Generate
*   a DMA request. The firmware must wait until the DMA completes the data
*   transfer after calling the USBFS_ReadOutEP() API. For example,
*   by checking EPstate:
*
*   \snippet /USBFS_sut_02.cydsn/main.c checking EPstatey
*
*   The USBFS_EnableOutEP() has to be called to allow host to write data into
*   the endpoint buffer after DMA has completed transfer data from OUT endpoint
*   buffer to SRAM.
*
*   *DMA with Automatic Buffer Management:*
*   Configure DMA. This is required only once and automatically generates DMA
*   requests as data arrives
*
*  \param epNumber: Contains the data endpoint number.
*            Valid values are between 1 and 8.
*  \param pData: A pointer to a data array from which the data for the endpoint
*         space is loaded.
*  \param length: The number of bytes to transfer from the USB Out endpoint and
*          loads it into data array. Valid values are between 0 and 1023. The
*          function moves fewer than the requested number of bytes if the host
*          sends fewer bytes than requested.
*
* \return
*  Number of bytes received, 0 for an invalid endpoint.
*
* \reentrant
*  No.
*
*******************************************************************************/
uint16 USBFS_ReadOutEP(uint8 epNumber, uint8 pData[], uint16 length)
                                                                        
{
    if ((pData != NULL) && (epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
    #if (!USBFS_EP_MANAGEMENT_DMA_AUTO)
        /* Adjust requested length to available data. */
        length = (length > USBFS_GetEPCount(epNumber)) ? USBFS_GetEPCount(epNumber) : length;
    #endif /* (!USBFS_EP_MANAGEMENT_DMA_AUTO) */

    #if (USBFS_EP_MANAGEMENT_MANUAL)
        {
            /* Copy data using arbiter data register. */
            uint16 i;
            for (i = 0u; i < length; ++i)
            {
                pData[i] = (uint8) USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr;
            }
        }

        /* Arm OUT endpoint after data has been copied from endpoint buffer. */
        USBFS_EnableOutEP(epNumber);
    #else

        #if (CY_PSOC3 || CY_PSOC5LP)
            /* Initialize DMA if it was not initialized. */
            if (DMA_INVALID_TD == USBFS_DmaTd[epNumber])
            {
                USBFS_InitEP_DMA(epNumber, pData);
            }
        #endif /* (CY_PSOC3 || CY_PSOC5LP) */

        #if (USBFS_EP_MANAGEMENT_DMA_MANUAL)
        #if (CY_PSOC4)
        {
            uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

            /* Disable DMA channel: start configuration. */
            USBFS_CyDmaChDisable(channelNum);

            /* Configure source and destination. */
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);
            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) pData);

            /* Configure DMA descriptor. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG | (uint16)(length - 1u) |
                                                    CYDMA_BYTE | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_INVALIDATE | CYDMA_PREEMPTABLE);

            /* Validate descriptor to execute on following DMA request. */
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);

            /* Enable DMA channel: configuration complete. */
            USBFS_CyDmaChEnable(channelNum);
        }
        #else
            /* Configure DMA to transfer data. */
            (void) CyDmaChDisable(USBFS_DmaChan[epNumber]);
            (void) CyDmaTdSetConfiguration(USBFS_DmaTd[epNumber], length, CY_DMA_DISABLE_TD, TD_TERMIN_EN | TD_INC_DST_ADR);
            (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr), LO16((uint32)pData));

            /* Enable DMA channel. */
            (void) CyDmaChSetInitialTd(USBFS_DmaChan[epNumber], USBFS_DmaTd[epNumber]);
            (void) CyDmaChEnable(USBFS_DmaChan[epNumber], 1u);
        #endif /* (CY_PSOC4) */

            /* Generate DMA request. */
            USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |=  (uint8)  USBFS_ARB_EPX_CFG_DMA_REQ;
            USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg &=  (uint8) ~USBFS_ARB_EPX_CFG_DMA_REQ;

            /* OUT endpoint has to be armed again by user when DMA transfers have been completed.
            * NO_EVENT_PENDING: notifies that data has been copied from endpoint buffer.
            */

        #endif /* (USBFS_EP_MANAGEMENT_DMA_MANUAL) */

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
        #if (CY_PSOC4)
        {
            uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];
            uint32 lengthDescr0, lengthDescr1;

            /* Get number of full bursts. */
            USBFS_DmaEpBurstCnt[epNumber] = (uint8) (length / USBFS_DMA_BYTES_PER_BURST);

            /* Get number of elements in the last burst. */
            USBFS_DmaEpLastBurstEl[epNumber] = (uint8) (length % USBFS_DMA_BYTES_PER_BURST);

            /* Get total number of bursts. */
            USBFS_DmaEpBurstCnt[epNumber] += (0u != USBFS_DmaEpLastBurstEl[epNumber]) ? 1u : 0u;

            /* Adjust number of the data elements transfered in last burst. */
            USBFS_DmaEpLastBurstEl[epNumber] = (0u != USBFS_DmaEpLastBurstEl[epNumber]) ?
                                                                  (USBFS_DmaEpLastBurstEl[epNumber] - 1u) :
                                                                  (USBFS_DMA_BYTES_PER_BURST - 1u);

            /* Get number of data elements to transfer for descriptor 0 and 1. */
            lengthDescr0 = (1u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_BYTES_PER_BURST - 1u);
            lengthDescr1 = (2u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_BYTES_PER_BURST - 1u);

            /* Mark if revert number of data elements in descriptor after transfer completion. */
            USBFS_DmaEpLastBurstEl[epNumber] |= (USBFS_DmaEpBurstCnt[epNumber] > 2u) ? USBFS_DMA_DESCR_REVERT : 0u;

            /* Mark last descriptor to be executed. */
            USBFS_DmaEpLastBurstEl[epNumber] |= (0u != (USBFS_DmaEpBurstCnt[epNumber] & 0x1u)) ?
                                                                    USBFS_DMA_DESCR0_MASK : USBFS_DMA_DESCR1_MASK;

            /* Adjust burst counter taking to account: 2 valid descriptors and interrupt trigger after valid descriptor were executed. */
            USBFS_DmaEpBurstCnt[epNumber] = USBFS_DMA_GET_BURST_CNT(USBFS_DmaEpBurstCnt[epNumber]);

            /* Store address of buffer and burst counter for endpoint. */
            USBFS_DmaEpBufferAddrBackup[epNumber] = (uint32) pData;
            USBFS_DmaEpBurstCntBackup[epNumber]   = USBFS_DmaEpBurstCnt[epNumber];

            /* Disable DMA channel: start configuration. */
            USBFS_CyDmaChDisable(channelNum);

            /* Set destination address. */
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR1, (void*) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr);

            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) ((uint32) pData));
            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR1, (void*) ((uint32) pData + USBFS_DMA_BYTES_PER_BURST));

            /* Configure DMA descriptor. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG  | lengthDescr0 |
                                                    CYDMA_BYTE | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_INVALIDATE | CYDMA_CHAIN);

            /* Configure DMA descriptor. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR1, USBFS_DMA_COMMON_CFG  | lengthDescr1 |
                                                    CYDMA_BYTE | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_CHAIN);

            /* Enable interrupt from DMA channel. */
            USBFS_CyDmaSetInterruptMask(channelNum);

            /* Validate DMA descriptor 0 and 1. */
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR1);

            /* Enable DMA channel: configuration complete. */
            USBFS_CyDmaChEnable(channelNum);
        }
        #else
            (void) CyDmaChDisable(USBFS_DmaChan[epNumber]);
            (void) CyDmaTdSetConfiguration(USBFS_DmaTd[epNumber], length,  USBFS_DmaTd[epNumber], TD_TERMIN_EN | TD_INC_DST_ADR);
            (void) CyDmaTdSetAddress(USBFS_DmaTd[epNumber], LO16((uint32) &USBFS_ARB_EP_BASE.arbEp[epNumber].rwDr), LO16((uint32) pData));

            /* Clear Any potential pending DMA requests before starting DMA channel to transfer data. */
            (void) CyDmaClearPendingDrq(USBFS_DmaChan[epNumber]);

            /* Enable DMA channel. */
            (void) CyDmaChSetInitialTd(USBFS_DmaChan[epNumber], USBFS_DmaTd[epNumber]);
            (void) CyDmaChEnable(USBFS_DmaChan[epNumber], 1u);
        #endif /* (CY_PSOC4) */

            /* OUT endpoint has to be armed again by user when DMA transfers have been completed.
            * NO_EVENT_PENDING: notifies that data has been copied from endpoint buffer.
            */

        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    #endif /* (USBFS_EP_MANAGEMENT_MANUAL) */
    }
    else
    {
        length = 0u;
    }

    return (length);
}


#if (USBFS_16BITS_EP_ACCESS_ENABLE)
/*******************************************************************************
* Function Name: USBFS_LoadInEP16
****************************************************************************//**
*
*  This function performs different functionality depending on the Component�s
*  configured Endpoint Buffer Management. This parameter is defined in
*  the Descriptor Root in Component Configure window.
*
*  *Manual (Static/Dynamic Allocation):*
*  This function loads and enables the specified USB data endpoint for an IN
*  data transfer.
*
*  *DMA with Manual Buffer Management:*
*  Configures DMA for a data transfer from system RAM to endpoint buffer.
*  Generates request for a transfer.
*
*  *DMA with Automatic Buffer Management:*
*  Configures DMA. This is required only once, so it is done only when parameter
*  pData is not NULL. When the pData pointer is NULL, the function skips this
*  task. Sets Data ready status: This generates the first DMA transfer and
*  prepares data in endpoint buffer.
*
*  \param epNumber Contains the data endpoint number.
*        Valid values are between 1 and 8.
*  \param *pData A pointer to a data array from which the data for the endpoint
*        space is loaded. It shall be ensured that this pointer address is even
*        to ensure the 16-bit transfer is aligned to even address. Else, a hard
*        fault condition can occur.
*  \param length The number of bytes to transfer from the array and then send as
*        a result of an IN request. Valid values are between 0 and 512 (1023 for
*        DMA with Automatic Buffer Management mode). The value 512 is applicable
*        if only one endpoint is used.
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_LoadInEP16(uint8 epNumber, const uint8 pData[], uint16 length)
{
    /* Check array alignment on half-word boundary. */
    CYASSERT(0u == (((uint32) pData) & 0x01u));

    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
    #if (!USBFS_EP_MANAGEMENT_DMA_AUTO)
        /* Limit length to available buffer USB IP buffer size. */
        if (length > (USBFS_EPX_DATA_BUF_MAX - USBFS_EP[epNumber].buffOffset))
        {
            length = USBFS_EPX_DATA_BUF_MAX - USBFS_EP[epNumber].buffOffset;
        }
    #endif /* (!USBFS_EP_MANAGEMENT_DMA_AUTO) */

        /* Set count and data toggle. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt0 = (uint32) HI8(length) | USBFS_EP[epNumber].epToggle;
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCnt1 = (uint32) LO8(length);

        /* Adjust requested length: 2 bytes are handled at one data register access. */
        length += (length & 0x01u);

    #if (USBFS_EP_MANAGEMENT_MANUAL)
        if (NULL != pData)
        {
            /* Convert uint8 array to uint16. */
            const uint16 *dataBuf = (uint16 *) pData;

            /* Copy data using 16-bits arbiter data register. */
            uint16 i;
            for (i = 0u; i < (length >> 1u); ++i)
            {
                USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16 = dataBuf[i];
            }
        }

        /* IN endpoint buffer is full - read to be read. */
        USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

        /* Arm IN endpoint. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;

    #else

        #if (USBFS_EP_MANAGEMENT_DMA_MANUAL)
            /* IN endpoint buffer will be fully loaded by DMA shortly. */
            USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

            if ((pData != NULL) && (length > 0u))
            {
                uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                /* Disable DMA channel: start configuration. */
                USBFS_CyDmaChDisable(channelNum);

                /* Configure source and destination. */
                USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) pData);
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);

                /* Configure DMA descriptor. */
                length = (length >> 1u) - 1u;
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG | length |
                                                        CYDMA_HALFWORD | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_INVALIDATE | CYDMA_PREEMPTABLE);

                /* Validate descriptor to execute on following DMA request. */
                USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);

                /* Enable DMA channel: configuration complete. */
                USBFS_CyDmaChEnable(channelNum);

                /* Generate DMA request. */
                USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |=  (uint32)  USBFS_ARB_EPX_CFG_DMA_REQ;
                USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg &=  (uint32) ~USBFS_ARB_EPX_CFG_DMA_REQ;

                /* IN endpoint will be armed in ARB_ISR(source: IN_BUF_FULL) after first DMA transfer has been completed. */
            }
            else
            {
                /* When zero-length packet: arm IN endpoint directly. */
                USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;
            }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_MANUAL) */

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
            if (pData != NULL)
            {
                uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                /* Store address of buffer. */
                USBFS_DmaEpBufferAddrBackup[epNumber] = (uint32) pData;

                /* Disable DMA channel: start configuration. */
                USBFS_CyDmaChDisable(channelNum);

                /* Set destination address. */
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);
                USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR1, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);

                /* Configure DMA descriptor. */
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG  |
                                                        CYDMA_HALFWORD | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_INVALIDATE | CYDMA_CHAIN);

                /* Configure DMA descriptor. */
                USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR1, USBFS_DMA_COMMON_CFG  |
                                                        CYDMA_HALFWORD | CYDMA_ELEMENT_WORD | CYDMA_INC_SRC_ADDR | CYDMA_CHAIN);

                /* Enable interrupt from DMA channel. */
                USBFS_CyDmaSetInterruptMask(channelNum);

                /* Validate descriptor 1. It will not be invalided during operation. */
                USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR1);

                /* Enable DMA channel: configuration complete. */
                USBFS_CyDmaChEnable(channelNum);
            }
            else
            {
                /* IN endpoint buffer (32 bytes) will shortly be preloaded by DMA. */
                USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

                if (length > 0u)
                {
                    uint32 lengthDescr0, lengthDescr1;
                    uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

                    /* Get number of full bursts. */
                    USBFS_DmaEpBurstCnt[epNumber] = (uint8) (length / USBFS_DMA_BYTES_PER_BURST);

                    /* Get number of elements in the last burst. */
                    USBFS_DmaEpLastBurstEl[epNumber] = (uint8) (length % USBFS_DMA_BYTES_PER_BURST);

                    /* Get total number of bursts. */
                    USBFS_DmaEpBurstCnt[epNumber] += (0u != USBFS_DmaEpLastBurstEl[epNumber]) ? 1u : 0u;

                    /* Adjust number of data elements transferred in last burst. */
                    USBFS_DmaEpLastBurstEl[epNumber] = (0u != USBFS_DmaEpLastBurstEl[epNumber]) ?
                                                                          ((USBFS_DmaEpLastBurstEl[epNumber] >> 1u) - 1u) :
                                                                           (USBFS_DMA_HALFWORDS_PER_BURST - 1u);

                    /* Get number of data elements to transfer for descriptor 0 and 1. */
                    lengthDescr0 = (1u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_HALFWORDS_PER_BURST - 1u);
                    lengthDescr1 = (2u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_HALFWORDS_PER_BURST - 1u);

                    /* Mark which descriptor is last one to execute. */
                    USBFS_DmaEpLastBurstEl[epNumber] |= (0u != (USBFS_DmaEpBurstCnt[epNumber] & 0x1u)) ?
                                                                            USBFS_DMA_DESCR0_MASK : USBFS_DMA_DESCR1_MASK;

                    /* Adjust burst counter taking to account: 2 valid descriptors and interrupt trigger after valid descriptor were executed. */
                    USBFS_DmaEpBurstCnt[epNumber] = USBFS_DMA_GET_BURST_CNT(USBFS_DmaEpBurstCnt[epNumber]);

                    /* Restore DMA settings for current transfer. */
                    USBFS_CyDmaChDisable(channelNum);

                    /* Restore destination address for input endpoint. */
                    USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) ((uint32) USBFS_DmaEpBufferAddrBackup[epNumber]));
                    USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR1, (void*) ((uint32) USBFS_DmaEpBufferAddrBackup[epNumber] +
                                                                                                                   USBFS_DMA_BYTES_PER_BURST));

                    /* Set number of elements to transfer. */
                    USBFS_CyDmaSetNumDataElements(channelNum, USBFS_DMA_DESCR0, lengthDescr0);
                    USBFS_CyDmaSetNumDataElements(channelNum, USBFS_DMA_DESCR1, lengthDescr1);

                    /* Validate descriptor 0 and command to start with it. */
                    USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);
                    USBFS_CyDmaSetDescriptor0Next(channelNum);

                    /* Enable DMA channel: configuration complete. */
                    USBFS_CyDmaChEnable(channelNum);

                #if !defined (USBFS_MANUAL_IN_EP_ARM)
                    /* Set IN data ready to generate DMA request to load data into endpoint buffer. */
                    USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |= USBFS_ARB_EPX_CFG_IN_DATA_RDY;
                #endif  /* (USBFS_MANUAL_IN_EP_ARM) */

                    /* IN endpoint will be armed in ARB_ISR(source: IN_BUF_FULL) after first DMA transfer has been completed. */
                }
                else
                {
                    /* When zero-length packet: arm IN endpoint directly. */
                    USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;
                }
            }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    #endif /* (USBFS_EP_MANAGEMENT_MANUAL) */
    }
}


/*******************************************************************************
* Function Name: USBFS_ReadOutEP16
****************************************************************************//**
*
*   This function performs different functionality depending on the Component�s
*   configured Endpoint Buffer Management. This parameter is defined in the
*   Descriptor Root in Component Configure window.
*
*   *Manual (Static/Dynamic Allocation):*
*   This function moves the specified number of bytes from endpoint buffer to
*   system RAM. The number of bytes actually transferred from endpoint buffer to
*   system RAM is the lesser of the actual number of bytes sent by the host or
*   the number of bytes requested by the length parameter.
*
*   *DMA with Manual Buffer Management:*
*   Configure DMA to transfer data from endpoint buffer to system RAM. Generate
*   a DMA request. The firmware must wait until the DMA completes the data
*   transfer after calling the USBFS_ReadOutEP() API. For example,
*   by checking EPstate:
*
*   \snippet /USBFS_sut_02.cydsn/main.c checking EPstatey
*
*   The USBFS_EnableOutEP() has to be called to allow host to write data into
*   the endpoint buffer after DMA has completed transfer data from OUT endpoint
*   buffer to SRAM.
*
*   *DMA with Automatic Buffer Management:*
*   Configure DMA. This is required only once and automatically generates DMA
*   requests as data arrives
*
*  \param epNumber: Contains the data endpoint number.
*         Valid values are between 1 and 8.
*  \param pData: A pointer to a data array into which the data for the endpoint
*         space is copied. It shall be ensured that this pointer address is
*         even to ensure the 16-bit transfer is aligned to even address. Else,
*         a hard fault condition can occur.
*  \param length: The number of bytes to transfer from the USB Out endpoint and
*         loads it into data array. Valid values are between 0 and 1023. The
*         function moves fewer than the requested number of bytes if the host
*         sends fewer bytes than requested.
*
* \return
*  Number of bytes received, 0 for an invalid endpoint.
*
* \reentrant
*  No.
*
*******************************************************************************/
uint16 USBFS_ReadOutEP16(uint8 epNumber, uint8 pData[], uint16 length)
{
    uint32 adjLength;

    /* Check array alignment on half-word boundary */
    CYASSERT(0u == (((uint32) pData) & 0x01u));

    if ((pData != NULL) && (epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
    #if (!USBFS_EP_MANAGEMENT_DMA_AUTO)
        /* Adjust requested length to available data. */
        length = (length > USBFS_GetEPCount(epNumber)) ? USBFS_GetEPCount(epNumber) : length;
    #endif /* (!USBFS_EP_MANAGEMENT_DMA_AUTO) */

    /* Adjust requested length: 2 bytes are handled at one data register access. */
    adjLength =  length + ((uint32)length & 1u);

    #if (USBFS_EP_MANAGEMENT_MANUAL)
        {
            /* Convert uint8 array to uint16. */
            uint16 *dataBuf = (uint16 *) pData;

            /* Copy data using 16-bits arbiter data register. */
            uint16 i;
            for (i = 0u; i < (adjLength >> 1u); ++i)
            {
                dataBuf[i] = (uint16) USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16;
            }
        }

        /* Arm OUT endpoint after data has been read from endpoint buffer. */
        USBFS_EnableOutEP(epNumber);
    #else

        #if (USBFS_EP_MANAGEMENT_DMA_MANUAL)
        {
            uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

            /* Disable DMA channel: start configuration. */
            USBFS_CyDmaChDisable(channelNum);

            /* Configure source and destination. */
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);
            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) pData);

            /* Configure DMA descriptor. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG | (uint16)((adjLength >> 1u) - 1u) |
                                                    CYDMA_HALFWORD | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_INVALIDATE | CYDMA_PREEMPTABLE);

            /* Validate descriptor to execute on following DMA request. */
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);

            /* Enable DMA channel: configuration complete. */
            USBFS_CyDmaChEnable(channelNum);

            /* Generate DMA request. */
            USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg |=  (uint32)  USBFS_ARB_EPX_CFG_DMA_REQ;
            USBFS_ARB_EP_BASE.arbEp[epNumber].epCfg &=  (uint32) ~USBFS_ARB_EPX_CFG_DMA_REQ;

            /* OUT endpoint has to be armed again by user when DMA transfers have been completed.
            * NO_EVENT_PENDING: notifies that data has been copied from endpoint buffer.
            */
        }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_MANUAL) */

        #if (USBFS_EP_MANAGEMENT_DMA_AUTO)
        {
            uint32 lengthDescr0, lengthDescr1;
            uint32 channelNum = (uint32) USBFS_DmaChan[epNumber];

            /* Get number of full bursts. */
            USBFS_DmaEpBurstCnt[epNumber] = (uint8) (adjLength / USBFS_DMA_BYTES_PER_BURST);

            /* Get number of elements in last burst. */
            USBFS_DmaEpLastBurstEl[epNumber] = (uint8) (adjLength % USBFS_DMA_BYTES_PER_BURST);

            /* Get total number of bursts. */
            USBFS_DmaEpBurstCnt[epNumber] += (0u != USBFS_DmaEpLastBurstEl[epNumber]) ? 1u : 0u;

            /* Adjust number of data elements transferred in last burst. */
            USBFS_DmaEpLastBurstEl[epNumber] = (0u != USBFS_DmaEpLastBurstEl[epNumber]) ?
                                                                  ((USBFS_DmaEpLastBurstEl[epNumber] >> 1u) - 1u) :
                                                                   (USBFS_DMA_HALFWORDS_PER_BURST - 1u);

            /* Get number of data elements to transfer for descriptor 0 and 1. */
            lengthDescr0 = (1u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_HALFWORDS_PER_BURST - 1u);
            lengthDescr1 = (2u == USBFS_DmaEpBurstCnt[epNumber]) ? USBFS_DmaEpLastBurstEl[epNumber] : (USBFS_DMA_HALFWORDS_PER_BURST - 1u);

            /* Mark last descriptor to be executed. */
            USBFS_DmaEpLastBurstEl[epNumber] |= (0u != (USBFS_DmaEpBurstCnt[epNumber] & 0x1u)) ?
                                                                    USBFS_DMA_DESCR0_MASK : USBFS_DMA_DESCR1_MASK;

            /* Mark if revert number of data elements in descriptor after transfer completion. */
            USBFS_DmaEpLastBurstEl[epNumber] |= (USBFS_DmaEpBurstCnt[epNumber] > 2u) ? USBFS_DMA_DESCR_REVERT : 0u;

            /* Mark that 16-bits access to data register is performed. */
            USBFS_DmaEpLastBurstEl[epNumber] |= USBFS_DMA_DESCR_16BITS;

            /* Adjust burst counter taking to account: 2 valid descriptors and interrupt trigger after valid descriptor were executed. */
            USBFS_DmaEpBurstCnt[epNumber] = USBFS_DMA_GET_BURST_CNT(USBFS_DmaEpBurstCnt[epNumber]);

            /* Store address of buffer and burst counter for endpoint. */
            USBFS_DmaEpBufferAddrBackup[epNumber] = (uint32) pData;
            USBFS_DmaEpBurstCntBackup[epNumber]   = USBFS_DmaEpBurstCnt[epNumber];

            /* Disable DMA channel: start configuration. */
            USBFS_CyDmaChDisable(channelNum);

            /* Set destination address. */
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR0, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);
            USBFS_CyDmaSetSrcAddress(channelNum, USBFS_DMA_DESCR1, (void*) &USBFS_ARB_EP16_BASE.arbEp[epNumber].rwDr16);

            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR0, (void*) ((uint32) pData));
            USBFS_CyDmaSetDstAddress(channelNum, USBFS_DMA_DESCR1, (void*) ((uint32) pData + USBFS_DMA_BYTES_PER_BURST));

            /* Configure DMA descriptor 0. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR0, USBFS_DMA_COMMON_CFG  | lengthDescr0 |
                                                    CYDMA_HALFWORD | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_INVALIDATE | CYDMA_CHAIN);

            /* Configure DMA descriptor 1. */
            USBFS_CyDmaSetConfiguration(channelNum, USBFS_DMA_DESCR1, USBFS_DMA_COMMON_CFG  | lengthDescr1 |
                                                    CYDMA_HALFWORD | CYDMA_WORD_ELEMENT | CYDMA_INC_DST_ADDR | CYDMA_CHAIN);

            /* Enable interrupt from DMA channel. */
            USBFS_CyDmaSetInterruptMask(channelNum);

            /* Validate DMA descriptor 0 and 1. */
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR0);
            USBFS_CyDmaValidateDescriptor(channelNum, USBFS_DMA_DESCR1);

            /* Enable DMA channel: configuration complete. */
            USBFS_CyDmaChEnable(channelNum);

            /* OUT endpoint has to be armed again by user when DMA transfers have been completed.
            * NO_EVENT_PENDING: notifies that data has been copied from endpoint buffer.
            */
        }
        #endif /* (USBFS_EP_MANAGEMENT_DMA_AUTO) */
    #endif /* (USBFS_EP_MANAGEMENT_MANUAL) */
    }
    else
    {
        length = 0u;
    }

    return (length);
}
#endif /* (USBFS_16BITS_EP_ACCESS_ENABLE) */


/*******************************************************************************
* Function Name: USBFS_EnableOutEP
****************************************************************************//**
*
*  This function enables the specified endpoint for OUT transfers. Do not call
*  this function for IN endpoints.
*
*  \param epNumber: Contains the data endpoint number. Valid values are between
*  1 and 8.
*
* \globalvars
*
*  \ref USBFS_EP[epNumber].apiEpState - set to NO_EVENT_PENDING
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_EnableOutEP(uint8 epNumber) 
{
    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
        USBFS_EP[epNumber].apiEpState = USBFS_NO_EVENT_PENDING;

        /* Enable OUT endpoint to be written by Host. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_EP[epNumber].epMode;
        
    }
}


/*******************************************************************************
* Function Name: USBFS_DisableOutEP
****************************************************************************//**
*
*  This function disables the specified USBFS OUT endpoint. Do not call this
*  function for IN endpoints.
*
*  \param epNumber: Contains the data endpoint number.
*            Valid values are between 1 and 8.
*
*******************************************************************************/
void USBFS_DisableOutEP(uint8 epNumber) 
{
    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
        /* Set NAK response for OUT endpoint. */
        USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 = USBFS_MODE_NAK_OUT;
    }
}


/*******************************************************************************
* Function Name: USBFS_Force
****************************************************************************//**
*
*  This function forces a USB J, K, or SE0 state on the D+/D� lines. It provides
*  the necessary mechanism for a USB device application to perform a USB Remote
*  Wakeup. For more information, see the USB 2.0 Specification for details on
*  Suspend and Resume.
*
*  \param state A byte indicating which of the four bus states to enable.
*        Symbolic names  and their associated values are listed here:
*    State                      |Description
*    ---------------------------|----------------------------------------------
*    USBFS_FORCE_J   | Force a J State onto the D+/D� lines
*    USBFS_FORCE_K   | Force a K State onto the D+/D� lines
*    USBFS_FORCE_SE0 | Force a Single Ended 0 onto the D+/D� lines
*    USBFS_FORCE_NONE| Return bus to SIE control
*
*
*******************************************************************************/
void USBFS_Force(uint8 bState) 
{
    /* This registers is used only for manual control of SIE (no masking is
    * needed before write into it).
    */
    USBFS_USBIO_CR0_REG = bState;
}


/*******************************************************************************
* Function Name: USBFS_GetEPAckState
****************************************************************************//**
*
*  This function determines whether an ACK transaction occurred on this endpoint
*  by reading the ACK bit in the control register of the endpoint. It does not
*  clear the ACK bit.
*
*  \param epNumber Contains the data endpoint number.
*            Valid values are between 1 and 8.
*
*  \return
*  If an ACKed transaction occurred, this function returns a non-zero value.
*  Otherwise, it returns zero.
*
*******************************************************************************/
uint8 USBFS_GetEPAckState(uint8 epNumber) 
{
    uint8 cr = 0u;

    if ((epNumber > USBFS_EP0) && (epNumber < USBFS_MAX_EP))
    {
        cr = USBFS_SIE_EP_BASE.sieEp[epNumber].epCr0 & USBFS_MODE_ACKD;
    }

    return ((uint8) cr);
}


/*******************************************************************************
* Function Name: USBFS_SetPowerStatus
****************************************************************************//**
*
*  This function sets the current power status. The device replies to USB
*  GET_STATUS requests based on this value. This allows the device to properly
*  report its status for USB Chapter 9 compliance. Devices can change their
*  power source from self powered to bus powered at any time and report their
*  current power source as part of the device status. You should call this
*  function any time your device changes from self powered to bus powered or
*  vice versa, and set the status appropriately.
*
*  \param powerStatus: Contains the desired power status, one for self powered
*        or zero for bus powered. Symbolic names and their associated values are
*        given here:
*  Power Status                                |Description
*  --------------------------------------------|---------------------------
*  USBFS_DEVICE_STATUS_BUS_POWERED  | Set the device to bus powered
*  USBFS_DEVICE_STATUS_SELF_POWERED | Set the device to self powered
*
* \globalvars
*
*  \ref USBFS_deviceStatus - set power status
*
* \reentrant
*  No.
*
*******************************************************************************/
void USBFS_SetPowerStatus(uint8 powerStatus) 
{
    if (powerStatus != USBFS_DEVICE_STATUS_BUS_POWERED)
    {
        USBFS_deviceStatus |= (uint8)  USBFS_DEVICE_STATUS_SELF_POWERED;
    }
    else
    {
        USBFS_deviceStatus &= (uint8) ~USBFS_DEVICE_STATUS_SELF_POWERED;
    }
}


#if (USBFS_VBUS_MONITORING_ENABLE)
    /***************************************************************************
    * Function Name: USBFS_VBusPresent
    ************************************************************************//**
    *
    *  Determines VBUS presence for self-powered devices. This function is
    *  available when the VBUS Monitoring option is enabled in the Advanced tab.
    *
    * \return
    *  The return value can be the following:
    *  Return Value | Description
    *  -------------|-----------------
    *  1            | VBUS is present
    *  0            | VBUS is absent
    *
    *
    ***************************************************************************/
    uint8 USBFS_VBusPresent(void) 
    {
        return ((0u != (USBFS_VBUS_STATUS_REG & USBFS_VBUS_VALID)) ? (uint8) 1u : (uint8) 0u);
    }
#endif /* (USBFS_VBUS_MONITORING_ENABLE) */


/*******************************************************************************
* Function Name: USBFS_RWUEnabled
****************************************************************************//**
*
*  This function returns the current remote wakeup status.
*  If the device supports remote wakeup, the application should use this
*  function to determine if remote wakeup was enabled by the host. When the
*  device is suspended and it determines the conditions to initiate a remote
*  wakeup are met, the application should use the USBFS_Force() function to
*  force the appropriate J and K states onto the USB bus, signaling a remote
*  wakeup.
*
*
* \return
*  Returns non-zero value if remote wakeup is enabled and zero otherwise.
*
* \globalvars
*  USBFS_deviceStatus - checked to determine remote status
*
*******************************************************************************/
uint8 USBFS_RWUEnabled(void) 
{
    uint8 result = USBFS_FALSE;

    if (0u != (USBFS_deviceStatus & USBFS_DEVICE_STATUS_REMOTE_WAKEUP))
    {
        result = USBFS_TRUE;
    }

    return (result);
}


/*******************************************************************************
* Function Name: USBFS_GetDeviceAddress
****************************************************************************//**
*
*  This function returns the currently assigned address for the USB device.
*
* \return
*  Returns the currently assigned address.
*  Returns 0 if the device has not yet been assigned an address.
*
*******************************************************************************/
uint8 USBFS_GetDeviceAddress(void) 
{
    return (uint8)(USBFS_CR0_REG & USBFS_CR0_DEVICE_ADDRESS_MASK);
}


/*******************************************************************************
* Function Name: USBFS_EnableSofInt
****************************************************************************//**
*
*  This function enables interrupt generation when a Start-of-Frame (SOF)
*  packet is received from the host.
*
*******************************************************************************/
void USBFS_EnableSofInt(void) 
{
#if (CY_PSOC4)
    /* Enable SOF interrupt interrupt source. */
    USBFS_INTR_SIE_MASK_REG |= (uint32) USBFS_INTR_SIE_SOF_INTR;
#else
    /* Enable SOF interrupt if it is present. */
    #if (USBFS_SOF_ISR_ACTIVE)
        CyIntEnable(USBFS_SOF_VECT_NUM);
    #endif /* (USBFS_SOF_ISR_ACTIVE) */
#endif /* (CY_PSOC4) */
}


/*******************************************************************************
* Function Name: USBFS_DisableSofInt
****************************************************************************//**
*
*  This function disables interrupt generation when a Start-of-Frame (SOF)
*  packet is received from the host.
*
*******************************************************************************/
void USBFS_DisableSofInt(void) 
{
#if (CY_PSOC4)
    /* Disable SOF interrupt interrupt source. */
    USBFS_INTR_SIE_MASK_REG &= (uint32) ~USBFS_INTR_SIE_SOF_INTR;
#else
    /* Disable SOF interrupt if it is present. */
    #if (USBFS_SOF_ISR_ACTIVE)
        CyIntDisable(USBFS_SOF_VECT_NUM);
    #endif /* (USBFS_SOF_ISR_ACTIVE) */
#endif /* (CY_PSOC4) */
}


#if (USBFS_BATT_CHARG_DET_ENABLE)
    /***************************************************************************
    * Function Name: USBFS_DetectPortType
    ************************************************************************//**
    *
    *   This function implements the USB  Battery Charger Detection (BCD)
    *   algorithm to determine the type of USB host downstream port. This API
    *   is available only for PSoC 4 devices, and should be called when the VBUS
    *   voltage transition (OFF to ON) is detected on the bus. If the USB device
    *   functionality is enabled, this API first calls USBFS_Stop() API
    *   internally to disable the USB device functionality, and then proceeds to
    *   implement the BCD algorithm to detect the USB host port type.
    *   The USBFS_Start() API should be called after this API if the USB
    *   communication needs to be initiated with the host.
    *   *Note* This API is generated only if the �Enable Battery Charging 
    *   Detection� option is enabled in the �Advanced� tab of the component GUI.
    *   *Note* API implements the steps 2-4 of the BCD algorithm which are 
    *   - Data Contact Detect
    *   - Primary Detection 
    *   - Secondary Detection 
    * 
    *   The first step of BCD algorithm, namely, VBUS detection shall be handled 
    *   at the application firmware level.
    *
    * \return
    *   The return value can be the following:
    *   Return Value                      |Description
    *   ----------------------------------|-------------------------------------
    *   USBFS_BCD_PORT_SDP     | Standard downstream port detected
    *   USBFS_BCD_PORT_CDP     | Charging downstream port detected
    *   USBFS_BCD_PORT_DCP     | Dedicated charging port detected
    *   USBFS_BCD_PORT_UNKNOWN | Unable to detect charging port type (proprietary charger type)
    *   USBFS_BCD_PORT_ERR     | Error condition in detection process
    *
    *
    * \sideeffects
    *
    *  USB device functionality is disabled by this API if not already disabled.
    *
    ***************************************************************************/
    uint8 USBFS_Bcd_DetectPortType(void)
    {
        uint32 bkPwrCtrl;
        uint32 cr1RegVal;
        uint32 secondaryDetection = 0u;
        uint8 result = USBFS_BCD_PORT_UNKNOWN;

        /*Check USB Started and Stop it*/
        if(0u != USBFS_initVar)
        {
            USBFS_Stop();
        }
        /*Initialize USBFS IP for Charger detection*/

        /*Enable clock to USB IP. */
        USBFS_USB_CLK_EN_REG = USBFS_USB_CLK_CSR_CLK_EN;

        /* Enable USBIO control on drive mode of D+ and D- pins. */
        USBFS_USBIO_CR1_REG &= ~ (uint32) USBFS_USBIO_CR1_IOMODE;

        /* Select VBUS detection source and clear PHY isolate. The application
        *  level must ensure that VBUS is valid. There is no need to wait 2us
        *  before VBUS is valid.
        */
        bkPwrCtrl = USBFS_POWER_CTRL_REG;
        USBFS_POWER_CTRL_REG = USBFS_DEFAULT_POWER_CTRL_VBUS\
                            & (~USBFS_POWER_CTRL_ENABLE_VBUS_PULLDOWN)\
                            & (~USBFS_POWER_CTRL_ENABLE_DM_PULLDOWN);


        /* Enable PHY detector and single-ended and differential receivers.
         * Enable charger detection.  */
        USBFS_POWER_CTRL_REG |= USBFS_DEFAULT_POWER_CTRL_PHY\
                                         | USBFS_POWER_CTRL_ENABLE_CHGDET;

        /* Suspend clear sequence. */
        USBFS_POWER_CTRL_REG &= (uint32) ~USBFS_POWER_CTRL_SUSPEND;
        CyDelayUs(USBFS_WAIT_SUSPEND_DEL_DISABLE);
        USBFS_POWER_CTRL_REG &= (uint32) ~USBFS_POWER_CTRL_SUSPEND_DEL;

        /* Data connection detection
        * Realization with delay as Hard IP does not support DCD 300 ms.
        */
        #if defined (USBFS_NO_DCD)
        CyDelay(USBFS_BCD_TIMEOUT);
        #else
        /* DCD implementation:*/

        {
            uint16 timeout = USBFS_BCD_TIMEOUT;
            uint8 connectionApproved = 0u;
            uint8 connected = 0u;

            /*   BCD spec 1.2: Turns on Idp_src and D- pull-down resistor */
            USBFS_POWER_CTRL_REG |= USBFS_POWER_CTRL_ENABLE_DM_PULLDOWN;
            USBFS_CHGDET_CTRL_REG |= USBFS_CHGDET_CTRL_DCD_SRC_EN;

            /* BCD spec 1.2: Waits for D+ to be low for a time of Tdcd_dbnc*/
            while ((0u != timeout) && (0u == connectionApproved))
            {
                if (0u == (USBFS_USBIO_CR1_REG & USBFS_USBIO_CR1_DP0))
                {
                    connected++;
                }
                else
                {
                    connected = 0u;
                }
                connectionApproved = (USBFS_BCD_TDCD_DBNC < connected) ? 1u:0u;
                CyDelay(1u);
                timeout--;
            }

            /*   BCD spec 1.2: Turns off Idp_src. */
            USBFS_CHGDET_CTRL_REG &= ~USBFS_CHGDET_CTRL_DCD_SRC_EN;
        }
        #endif /*(USBFS_NO_DCD)*/

        /* Primary detection: enable VDP_SRC on D+ and IDM_SINK on D-. */
        USBFS_CHGDET_CTRL_REG = USBFS_CHGDET_CTRL_PRIMARY;
        CyDelay(USBFS_BCD_PRIMARY_WAIT);
        cr1RegVal = USBFS_USBIO_CR1_REG;

        /* Check is it SDP or DCP/CDP, read comparator 2 output. */
        if (0u == (USBFS_CHGDET_CTRL_REG & USBFS_CHGDET_CTRL_COMP_OUT))
        {
            /* Check status of D- line. */
            if (0u == (cr1RegVal & USBFS_USBIO_CR1_DM0))
            {
                result = USBFS_BCD_PORT_SDP;
            }
            else
            {
                /* ERROR: such combination is impossible. Abort charger
                 * detection.
                */
                result = USBFS_BCD_PORT_ERR;
            }
        }
        else
        {
                /* Need Secondary detection. Charging port: DCP or proprietary*/
                secondaryDetection = 1u;
        }

        /* Secondary detection: Set CHGDET_CTRL register to enable VDM_SRC on D- and IDP_SINK on D+. */

        if (0u != secondaryDetection)
        {
            USBFS_CHGDET_CTRL_REG = USBFS_CHGDET_CTRL_DEFAULT \
                                                | USBFS_CHGDET_CTRL_SECONDARY;
            CyDelay(USBFS_BCD_SECONDARY_WAIT);
            cr1RegVal = USBFS_USBIO_CR1_REG;

            /* Check is it SDP or DCP/CDP, read comparator 1 output. */
            if (0u == (USBFS_CHGDET_CTRL_REG & USBFS_CHGDET_CTRL_COMP_OUT))
            {
                /* Check status of D+ line. */
                if (0u == (cr1RegVal & USBFS_USBIO_CR1_DP0))
                {
                    result = USBFS_BCD_PORT_CDP;
                }
                else
                {
                    /* ERROR: such combination is impossible. Abort charger
                     * detection.
                    */
                    result = USBFS_BCD_PORT_ERR;
                }
            }
            else
            {
                /* Check status of D+ line. */
                if (0u == (cr1RegVal & USBFS_USBIO_CR1_DP0))
                {
                    result = USBFS_BCD_PORT_DCP;
                }
                else
                {
                    /* It is may be proprietary charger. Proprietary charge is
                     * not supported byHardware IP block.
                    */
                    result = USBFS_BCD_PORT_UNKNOWN;
                }
            }
        }

        /* Restore CHGDET_CTRL. */
        USBFS_CHGDET_CTRL_REG = 0u;

        /*Revert registers back*/
        USBFS_POWER_CTRL_REG = bkPwrCtrl;
        USBFS_USBIO_CR1_REG |= (uint32) USBFS_USBIO_CR1_IOMODE;
        USBFS_USB_CLK_EN_REG = ~USBFS_USB_CLK_CSR_CLK_EN;

        return (result);
    }
#endif  /* (USBFS_BATT_CHARG_DET_ENABLE) */


#if (USBFS_LPM_ACTIVE)
    /***************************************************************************
    * Function Name: USBFS_Lpm_GetBeslValue
    ************************************************************************//**
    *
    *  This function returns the Best Effort Service Latency (BESL) value
    *  sent by the host as part of the LPM token transaction.
    *
    * \return
    *  4-bit BESL value received in the LPM token packet from the host
    *
    *
    ***************************************************************************/
    uint32 USBFS_Lpm_GetBeslValue(void)
    {
        return (uint32) (USBFS_LPM_STAT_REG & USBFS_LPM_STAT_LPM_BESL_MASK);
    }


    /***************************************************************************
    * Function Name: USBFS_Lpm_RemoteWakeUpAllowed
    ************************************************************************//**
    *
    *  This function returns the remote wakeup permission set for the device by
    *  the host as part of the LPM token transaction.
    *
    * \return
    *   0 - remote wakeup not allowed, 1 - remote wakeup allowed
    *
    *
    ***************************************************************************/
    uint32 USBFS_Lpm_RemoteWakeUpAllowed(void)
    {
        return (uint32) (USBFS_LPM_STAT_REG & USBFS_LPM_STAT_LPM_REMOTE_WAKE);
    }


    /***************************************************************************
    * Function Name: USBFS_Lpm_SetResponse
    ************************************************************************//**
    *
    *  This function configures the response in the handshake packet the device
    *  has to send when an LPM token packet is received.
    *
    * \param response
    *   type of response to return for an LPM token packet
    *   Allowed response values:
    *       - USBFS_LPM_REQ_ACK - next LPM request will be
    *                                           responded with ACK
    *       - USBFS_LPM_REQ_NACK - next LPM request will be
    *                                           responded with NACK
    *       - USBFS_LPM_REQ_NYET - next LPM request will be
    *                                           responded with NYET
    *
    ***************************************************************************/
    void USBFS_Lpm_SetResponse(uint32 response)
    {
        uint32 lpmCtrl = USBFS_LPM_CTRL_REG & (uint32) ~USBFS_LPM_CTRL_ACK_NYET_MASK;

        USBFS_LPM_CTRL_REG = lpmCtrl | ((uint32) response & USBFS_LPM_CTRL_ACK_NYET_MASK);
    }


    /***************************************************************************
    * Function Name: USBFS_Lpm_GetResponse
    ************************************************************************//**
    *
    *  This function returns the currently configured response value that the
    *  device will send as part of the handshake packet when an LPM token
    *  packet is received.
    *
    * \return
    *   type of handshake response that will be returned by the device
    *   for an LPM token packet
    *   Possible response values:
    *       - USBFS_LPM_REQ_ACK - next LPM request will be responded
    *                                        with ACK
    *       - USBFS_LPM_REQ_NACK - next LPM request will be responded
    *                                        with NACK
    *       - USBFS_LPM_REQ_NYET - next LPM request will be responded
    *                                        with NYET
    *
    ***************************************************************************/
    uint32 USBFS_Lpm_GetResponse(void)
    {

        return  ((uint32) USBFS_LPM_CTRL_REG & (uint32)USBFS_LPM_CTRL_ACK_NYET_MASK);
    }


#endif /* (USBFS_LPM_ACTIVE) */


/* [] END OF FILE */
