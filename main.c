/*******************************************************************************
* File Name:   main.c
*
* Description: This is the main file of the code example. initialization of the
* peripherals, I2C slave callback function, different macros and variables are
* implemented in this file.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "mtb_hal.h"

/*******************************************************************************
* Macros
*******************************************************************************/
/* Period of PWM controlling the LED in microseconds */
#define LED_PERIOD              (255u)

/* I2C slave interrupt priority */
#define I2C_SLAVE_IRQ_PRIORITY  (7u)

/* Valid command packet size */
#define PACKET_SIZE             (0x03u)

/* Master write and read buffer size */
#define SL_RD_BUFFER_SIZE       (PACKET_SIZE)
#define SL_WR_BUFFER_SIZE       (PACKET_SIZE)

/* Start and end of packet markers */
#define PACKET_SOP              (0x01u)
#define PACKET_EOP              (0x17u)

/* Command valid status */
#define STS_CMD_DONE            (0x00u)
#define STS_CMD_FAIL            (0xFFu)

/* Packet positions */
#define PACKET_SOP_POS          (0x00u)
#define PACKET_STS_POS          (0x01u)
#define PACKET_LED_POS          (0x01u)
#define PACKET_EOP_POS          (0x02u)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* I2C read and write buffers - I2C master writes into i2c_write_buffer
 * and reads from i2c_read_buffer */
uint8_t i2c_read_buffer [SL_RD_BUFFER_SIZE] =
                                       {PACKET_SOP, STS_CMD_FAIL, PACKET_EOP};
uint8_t i2c_write_buffer[SL_WR_BUFFER_SIZE];

/* I2C context  */
cy_stc_scb_i2c_context_t callback_i2c_context;

/* Flag for I2C slave error events */
bool error_detected = false;

/* Debug UART variables */
static cy_stc_scb_uart_context_t    DEBUG_UART_context; /* UART context */
static mtb_hal_uart_t               DEBUG_UART_hal_obj; /* Debug UART HAL object */

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/* LED PWM initialization */
void led_pwm_init(void);
/* I2C slave initialization */
void i2c_slave_init(void);
/* set the PWM LED intensity */
void execute_command(void);
/* Process received commands slave interrupt */
void handle_i2c_slave_events(uint32_t event);
/* handler for general errors */
void handle_error(uint32_t status);
/* ISR for I2C slave */
void i2c_slave_interrupt(void);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  The main function performs the following actions:
*   1. Initialization of the debug UART and print initial message.
*   2. Set up I2C to be in I2C slave mode.
*   2. Registering of the I2C slave callback.
*   3. Initializing of PWM to control the LED.
*
*\param
*  None
*
*\return
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    handle_error(result);

    /* Configure retarget-io to use the debug UART port */
    result = (cy_rslt_t)Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config,
                                         &DEBUG_UART_context);

    /* UART init failed. Stop program execution */
    handle_error(result);

    /* Enable UART */
    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup the HAL UART */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config,
                                &DEBUG_UART_context, NULL);

    /* HAL UART init failed. Stop program execution */
    handle_error(result);

    /* Initialize redirecting of low level IO */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    /* retarget IO init failed. Stop program execution */
    handle_error(result);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("************************************************************\r\n");
    printf("PSOC Control C3M/P8: I2C slave using callback\r\n");
    printf("************************************************************\r\n\n");

    printf("Send the command (w 08 01 00 17 p) using \r\n"
           "Bridge Control Panel to turn on the LED.\r\n\n");

    printf("Use numbers between 00 and FF as the fourth\r\n"
           "character to vary the LED brightness. \r\n");

    /* Initialize the PWM object */
    led_pwm_init();

    /* Initialize the I2C slave */
    i2c_slave_init();

    /* Enable global interrupts */
    __enable_irq();

    for (;;)
    {
        /* If error is detected in I2C slave operation, handle the error */
        if (true == error_detected)
        {
            handle_error(1U);
        }
    }
}

/*******************************************************************************
* Function Name: i2c_slave_interrupt
********************************************************************************
* Summary:
* This is the I2C slave interrupt function.
*
*\param
*  None
*
*\return
*  None
*
*******************************************************************************/
inline void i2c_slave_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(CALLBACK_I2C_HW, &callback_i2c_context);
}

/*******************************************************************************
* Function Name: led_pwm_init
********************************************************************************
* Summary:
* This function initializes and starts the PWM to drive the LED.
*
*\param
*  None
*
*\return
*  None
*
*******************************************************************************/
void led_pwm_init(void)
{
    cy_rslt_t result;

    /* Initializing the Timer for LED */
    result = Cy_TCPWM_PWM_Init(LED_PWM_HW, LED_PWM_NUM, &LED_PWM_config);
    handle_error(result);

    /* Enable PWM peripheral */
    Cy_TCPWM_PWM_Enable(LED_PWM_HW, LED_PWM_NUM);

    /* Set PWM period */
    Cy_TCPWM_PWM_SetPeriod0(LED_PWM_HW, LED_PWM_NUM, LED_PERIOD);

    /* Initialize the PWM compare value with the PWM period to keep the LED OFF. */
    Cy_TCPWM_PWM_SetCompare0Val(LED_PWM_HW, LED_PWM_NUM, LED_PERIOD);

    /* Start the PWM */
    Cy_TCPWM_TriggerStart_Single(LED_PWM_HW, LED_PWM_NUM);
 }

/*******************************************************************************
* Function Name: i2c_slave_init
********************************************************************************
* Summary:
* This function initializes the I2C instance, configures the callback, and
* configures the ISR to communicate with the master.
*
*\param
*  None
*
*\return
*  None
*
*******************************************************************************/
void i2c_slave_init(void)
{
    /* initialization status variables */
    cy_en_scb_i2c_status_t init_status;
    cy_en_sysint_status_t sys_status;

    /* Interrupt configuration structure for I2C slave */
    cy_stc_sysint_t callback_I2C_SCB_IRQ_cfg =
    {
            /*.intrSrc =*/ CALLBACK_I2C_IRQ,
            /*.intrPriority =*/ I2C_SLAVE_IRQ_PRIORITY
    };

    /* Initialize I2C */
    init_status = Cy_SCB_I2C_Init(CALLBACK_I2C_HW,
                                  &CALLBACK_I2C_config, &callback_i2c_context);
    /* I2C init failed. Stop program execution */
    handle_error(init_status);

    /* Initialize system interrupt */
    sys_status = Cy_SysInt_Init(&callback_I2C_SCB_IRQ_cfg, &i2c_slave_interrupt);

    /* Sys interrupt init failed. Stop program execution */
    handle_error(sys_status);

    /* Configure read buffer */
    Cy_SCB_I2C_SlaveConfigReadBuf(CALLBACK_I2C_HW, i2c_read_buffer,
                                  SL_WR_BUFFER_SIZE, &callback_i2c_context);

    /* Configure write buffer */
    Cy_SCB_I2C_SlaveConfigWriteBuf(CALLBACK_I2C_HW, i2c_write_buffer,
                                   SL_RD_BUFFER_SIZE, &callback_i2c_context);

    /* Register I2C event callback */
    Cy_SCB_I2C_RegisterEventCallback(CALLBACK_I2C_HW,
                                     (cy_cb_scb_i2c_handle_events_t)handle_i2c_slave_events,
                                     &callback_i2c_context);

    /* Enable interrupt */
    NVIC_EnableIRQ((IRQn_Type)callback_I2C_SCB_IRQ_cfg.intrSrc);

    /* Enable I2C */
    Cy_SCB_I2C_Enable(CALLBACK_I2C_HW);
}

/*******************************************************************************
* Function Name: handle_i2c_slave_events
********************************************************************************
* Summary:
* The I2C slave callback function. Handles slave events write and read
* completion events. This function also validates the messages and update
* duty cycle of the PWM controlling the LED.
*
* \param callback_arg
*
* \param event
*   Event to be handled
*
* \return
*  None
*
*******************************************************************************/
void handle_i2c_slave_events(uint32_t event)
{
    /* Check write complete event */
    if (0UL != (CY_SCB_I2C_SLAVE_WR_CMPLT_EVENT & event))
    {
        /* Check for errors */
        if (0UL == (CY_SCB_I2C_SLAVE_ERR_EVENT & event))
        {
            /* Check packet length */
            if (PACKET_SIZE ==  Cy_SCB_I2C_SlaveGetWriteTransferCount(CALLBACK_I2C_HW,
                                                                      &callback_i2c_context))
            {
                /* Check start and end of packet markers */
                if ((i2c_write_buffer[PACKET_SOP_POS] == PACKET_SOP) &&
                    (i2c_write_buffer[PACKET_EOP_POS] == PACKET_EOP))
                {
                    /* Execute command and update reply status for received
                     * command */
                    execute_command();
                    i2c_read_buffer[PACKET_STS_POS] = STS_CMD_DONE;
                }
            }
        }
        else
        {
            error_detected = true;
        }

         /* Configure write buffer for the next write */
        Cy_SCB_I2C_SlaveConfigWriteBuf(CALLBACK_I2C_HW, i2c_write_buffer,
                                       SL_WR_BUFFER_SIZE, &callback_i2c_context);
     }

     /* Check read complete event */
     if (0UL != (CY_SCB_I2C_SLAVE_RD_CMPLT_EVENT & event))
     {
         /* Configure read buffer for the next read */
         i2c_read_buffer[PACKET_STS_POS] = STS_CMD_FAIL;
         Cy_SCB_I2C_SlaveConfigReadBuf(CALLBACK_I2C_HW, i2c_read_buffer,
                                       SL_RD_BUFFER_SIZE, &callback_i2c_context);
     }
}

/*******************************************************************************
* Function Name: execute_command
********************************************************************************
* Summary:
* Sets the compare value for the LED PWM received by slave.
*
* \param
*  None
*
* \return
*  None
*
*******************************************************************************/
void execute_command(void)
{
    /* Sets the pulse width to control the brightness of the LED. */
    Cy_TCPWM_PWM_SetCompare0Val(LED_PWM_HW, LED_PWM_NUM,
                                (i2c_write_buffer[PACKET_LED_POS]));
}

/*******************************************************************************
* Function Name: handle_error
********************************************************************************
* Summary:
* User defined error handling function.
* This function processes unrecoverable errors such as any
* initialization errors etc. In case of such error the system will
* enter into assert.
*
* Parameters:
*  uint32_t status - status indicates success or failure
*
* Return:
*  void
*
*******************************************************************************/
void handle_error(uint32_t status)
{
    if (status != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
}

/* [] END OF FILE */
