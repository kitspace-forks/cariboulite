#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <lgpio.h>
#include "at86rf215.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "at86rf215_radio.h"
#include "at86rf215_regs.h"

//===================================================================
int at86rf215_write_buffer(at86rf215_st* dev, uint16_t addr, uint8_t *buffer, uint8_t size )
{
    // a maximal possible chunk size - 256 + 2(addr)
    uint8_t chunk_tx[258] = {0};
    uint8_t chunk_rx[258] = {0};
    chunk_tx[0] = ((addr >> 8) & 0x3F) | 0x80;
    chunk_tx[1] = addr & 0xFF;
    memcpy(chunk_tx + 2, buffer, size);

    return io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, chunk_tx, chunk_rx, size + 2, io_utils_spi_read_write);
}

//===================================================================
int at86rf215_read_buffer(at86rf215_st* dev, uint16_t addr, uint8_t *buffer, uint8_t size)
{
    // a maximal possible chunk size - 256 + 2(addr)
    uint8_t chunk_tx[258] = {0};
    uint8_t chunk_rx[258] = {0};
    chunk_tx[0] = (addr >> 8) & 0x3F;
    chunk_tx[1] = addr & 0xFF;

    int ret = io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, chunk_tx, chunk_rx, size + 2, io_utils_spi_read_write);

    if (ret == 0)
    {
        memcpy(buffer, chunk_rx + 2, size);
    }
    return ret;
}

//===================================================================
int at86rf215_write_byte(at86rf215_st* dev, uint16_t addr, uint8_t val )
{
    uint8_t chunk_tx[3] = {0};
    uint8_t chunk_rx[3] = {0};
    chunk_tx[0] = ((addr >> 8) & 0x3F) | 0x80;
    chunk_tx[1] = addr & 0xFF;
    chunk_tx[2] = val;
    return io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
            chunk_tx, chunk_rx, 3, io_utils_spi_read_write);
}

//===================================================================
int at86rf215_read_byte(at86rf215_st* dev, uint16_t addr)
{
    uint8_t chunk_tx[3] = {0};
    uint8_t chunk_rx[3] = {0};
    chunk_tx[0] = (addr >> 8) & 0x3F;
    chunk_tx[1] = addr & 0xFF;
    int ret = io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
                chunk_tx, chunk_rx, 3, io_utils_spi_read_write);
    if (ret < 0)
    {
        return ret;
    }
    return chunk_rx[2];
}

//===================================================================
int at86rf215_init(at86rf215_st* dev,
					io_utils_spi_st* io_spi)
{
	if (dev == NULL)
	{
		printf("ERROR @ at86rf215_init: dev is NULL\n");
		return -1;
	}

	dev->io_spi = io_spi;

    printf("INFO @ at86rf215_init: configuring reset and irq pins\n");
	// Configure GPIO pins
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);
	io_utils_setup_gpio(dev->irq_pin, io_utils_dir_input, io_utils_pull_up);

	// set to known state
	io_utils_write_gpio(dev->reset_pin, 1);

    printf("INFO @ at86rf215_init: Initializing io_utils_spi\n");
    io_utils_hard_spi_st hard_dev_modem = { .spi_dev_id = dev->spi_dev, .spi_dev_channel = dev->spi_channel, };
	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 5000000, 0, 0,
                        						io_utils_spi_chip_type_modem,
                                                &hard_dev_modem);

   // Setup the interrupts after clearing the register one time
    at86rf215_irq_st irq = {0};
    at86rf215_get_irqs(dev, &irq, 0);

    if (io_utils_setup_interrupt(dev->irq_pin, at86rf215_interrupt_handler, dev) < 0)
    {
        printf("Error @ at86rf215_init: interrupt registration for irq_pin (%d) failed\n", dev->irq_pin);
        io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);
        io_utils_setup_gpio(dev->irq_pin, io_utils_dir_input, io_utils_pull_up);
        io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);
        return -1;
    }

    dev->initialized = 1;

    return 0;
}


//===================================================================
int at86rf215_close(at86rf215_st* dev)
{
    if (dev == NULL)
	{
		printf("ERROR @ at86rf215_close: device pointer NULL\n");
		return -1;
	}

	if (!dev->initialized)
	{
		printf("Error @ at86rf215_close: device not initialized\n");
		return 0;
	}

	dev->initialized = 0;

	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->irq_pin, io_utils_dir_input, io_utils_pull_up);

	// Release the SPI device
    io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);

	printf("INFO @ at86rf215_close: device release completed\n");
    return 0;
}

//===================================================================
void at86rf215_reset(at86rf215_st* dev)
{
	io_utils_write_gpio(dev->reset_pin, 1);
    io_utils_usleep(300);
	io_utils_write_gpio(dev->reset_pin, 0);
    io_utils_usleep(300);
	io_utils_write_gpio(dev->reset_pin, 1);
}

//===================================================================
void at86rf215_chip_reset_with_spi(at86rf215_st* dev)
{
    uint8_t val = 0x7;
	at86rf215_write_byte(dev, REG_RF_RST, val);
}

//===================================================================
void at86rf215_get_versions(at86rf215_st* dev, uint8_t *pn, uint8_t *vn)
{
    if (pn) *pn = at86rf215_read_byte(dev, REG_RF_PN);
    if (vn) *vn = at86rf215_read_byte(dev, REG_RF_VN);
}

//===================================================================
int at86rf215_write_fifo(at86rf215_st* dev, uint8_t *buffer, uint8_t size )
{
    at86rf215_write_buffer(dev, 0, buffer, size);
}

//===================================================================
int at86rf215_read_fifo(at86rf215_st* dev, uint8_t *buffer, uint8_t size )
{
    return at86rf215_read_buffer(dev, 0, buffer, size);
}

//===================================================================
void at86rf215_set_clock_output(at86rf215_st* dev,
                                at86rf215_drive_current_en drv_level,
                                at86rf215_clock_out_freq_en clock_val)
{
    uint8_t val = ( (drv_level&0x03)<<3 ) | (clock_val&0x07);
	at86rf215_write_byte(dev, REG_RF_CLKO, val);
}

//===================================================================
void at86rf215_setup_rf_irq(at86rf215_st* dev,  uint8_t active_low,
                                                uint8_t show_masked_irq,
                                                at86rf215_drive_current_en drive)
{
    uint8_t val = 0;
    val |= (show_masked_irq&0x1)<<3;
    val |= (active_low&0x1)<<2;
    val |= (drive&0x3);
	at86rf215_write_byte(dev, REG_RF_CFG, val);
}

//===================================================================
static void at86rf215_print_radio_irq(at86rf215_radio_irq_st* irq)
{
    printf("    IQ_if_sync_fail: %d\n", irq->IQ_if_sync_fail);
    printf("    trx_error: %d\n", irq->trx_error);
    printf("    battery_low: %d\n", irq->battery_low);
    printf("    energy_detection_complete: %d\n", irq->energy_detection_complete);
    printf("    trx_ready: %d\n", irq->trx_ready);
    printf("    wake_up_por: %d\n", irq->wake_up_por);
}

//===================================================================
static void at86rf215_print_bb_irq(at86rf215_baseband_irq_st* irq)
{
    printf("    frame_buffer_level: %d\n", irq->frame_buffer_level);
    printf("    agc_release: %d\n", irq->agc_release);
    printf("    agc_hold: %d\n", irq->agc_hold);
    printf("    frame_tx_complete: %d\n", irq->frame_tx_complete);
    printf("    frame_rx_match_extended: %d\n", irq->frame_rx_match_extended);
    printf("    frame_rx_address_match: %d\n", irq->frame_rx_address_match);
    printf("    frame_rx_complete: %d\n", irq->frame_rx_complete);
    printf("    frame_rx_started: %d\n", irq->frame_rx_started);
}

//===================================================================
void at86rf215_get_irqs(at86rf215_st* dev, at86rf215_irq_st* irq, int verbose)
{
    at86rf215_read_buffer(dev, REG_RF09_IRQS, (uint8_t*)irq, 4);
    if (verbose)
    {
        printf("IRQ Status:\n");
        printf("  Radio09:\n"); at86rf215_print_radio_irq(&irq->radio09);
        printf("  Radio24:\n"); at86rf215_print_radio_irq(&irq->radio24);
        printf("  Baseband09:\n"); at86rf215_print_bb_irq(&irq->bb0);
        printf("  Baseband24:\n"); at86rf215_print_bb_irq(&irq->bb1);
    }
}

//===================================================================
void at86rf215_set_xo_trim(at86rf215_st* dev, uint8_t fast_start, float cap_trim)
{
    if (cap_trim < 0.0f) cap_trim = 0.0f;
    if (cap_trim > 4.5f) cap_trim = 4.5f;
    uint8_t trim_val = ((uint8_t)((cap_trim+0.1f)/0.3f))&0xF;
    trim_val |= (fast_start&0x1)<<4;
    at86rf215_write_byte(dev, REG_RF_XOC, trim_val);
}

//===================================================================
void at86rf215_get_iq_if_cfg(at86rf215_st* dev, at86rf215_iq_interface_config_st* cfg, int verbose)
{
    uint8_t data[3] = {0};
    at86rf215_read_buffer(dev, REG_RF_IQIFC0, data, 3);
    cfg->loopback_enable = (data[0]>>7)&0x01;
    cfg->synchronization_failed = (data[0]>>6)&0x01;
    cfg->drv_strength = (data[0]>>4)&0x03;
    uint8_t cmv = (data[0]>>2)&0x03;
    cfg->common_mode_voltage = ((data[0]>>1)&0x01)?at86rf215_iq_common_mode_v_ieee1596_1v2:cmv;
    cfg->tx_control_with_iq_if = (data[0]>>0)&0x01;
    cfg->in_failsafe_mode = (data[1]>>7)&0x01;
    uint8_t chip_mode = (data[1]>>4)&0x07;

    switch (chip_mode)
    {
        case 0x00:
            cfg->radio09_mode = at86rf215_baseband_mode;
            cfg->radio24_mode = at86rf215_baseband_mode;
            break;
        case 0x01:
            cfg->radio09_mode = at86rf215_iq_if_mode;
            cfg->radio24_mode = at86rf215_iq_if_mode;
            break;
        case 0x04:
            cfg->radio09_mode = at86rf215_iq_if_mode;
            cfg->radio24_mode = at86rf215_baseband_mode;
            break;
        case 0x05:
            cfg->radio09_mode = at86rf215_baseband_mode;
            cfg->radio24_mode = at86rf215_iq_if_mode;
            break;
        default:
            printf("Error @ at86rf215_get_iq_if_cfg: I/Q chipmode invalid: %d\n", chip_mode);
            break;
    }

    cfg->clock_skew = (data[1]>>0)&0x03;
    cfg->synchronized_incoming_iq = (data[2]>>7)&0x01;

    if (verbose)
    {
        printf("Current I/Q interface settings:\n");
        printf("   Loopback (RX => TX): %s\n", cfg->loopback_enable?"enabled":"disabled");
        printf("   Drive strength: %d mA\n", cfg->drv_strength+1);

        if (cfg->common_mode_voltage == at86rf215_iq_common_mode_v_ieee1596_1v2)
        {
            printf("   Common mode voltage: %.1f V\n", 1.2f);
        }
        else
        {
            printf("   Common mode voltage: %d mV\n", (cfg->common_mode_voltage+1) * 50 + 100);
        }

        printf("   I/Q interface for sub-GHz: %s\n", cfg->radio09_mode==at86rf215_iq_if_mode?"enabled":"diabled");
        printf("   I/Q interface for 2.4-GHz: %s\n", cfg->radio24_mode==at86rf215_iq_if_mode?"enabled":"diabled");
        printf("   I/Q Clock <=> Data skew: %.3f ns\n", cfg->clock_skew+1.906f);

        printf("   Status 'Sync Failure': %d\n", cfg->synchronization_failed);
        printf("   Status 'Failsafe mode': %d\n", cfg->in_failsafe_mode);
        printf("   Status 'Is synchronized to incoming I/Q': %d\n", cfg->synchronized_incoming_iq);
    }
}

//===================================================================
void at86rf215_setup_iq_if(at86rf215_st* dev, at86rf215_iq_interface_config_st* cfg)
{
    uint8_t data[2] = {0};
    data[0] |= (cfg->loopback_enable&0x01) << 7;
    data[0] |= (cfg->drv_strength&0x03) << 4;

    if (cfg->common_mode_voltage == at86rf215_iq_common_mode_v_ieee1596_1v2)
    {
        data[0] |= 1<<1;
    }
    else
    {
        data[0] |= (cfg->common_mode_voltage & 0x3) << 2;
    }
    data[0] |= (cfg->tx_control_with_iq_if & 0x01) << 0;

    if (cfg->radio09_mode == at86rf215_iq_if_mode && cfg->radio24_mode == at86rf215_iq_if_mode)
    {
        data[1] |= 0x01 << 4;
    }
    else if (cfg->radio09_mode == at86rf215_iq_if_mode && cfg->radio24_mode == at86rf215_baseband_mode)
    {
        data[1] |= 0x04 << 4;
    }
    else if (cfg->radio09_mode == at86rf215_baseband_mode && cfg->radio24_mode == at86rf215_iq_if_mode)
    {
        data[1] |= 0x05 << 4;
    }

    data[1] |= (cfg->clock_skew & 0x03) << 0;

    at86rf215_write_buffer(dev, REG_RF_IQIFC0, data, 2);
}

//===================================================================
int at86rf215_setup_channel ( at86rf215_st* dev, at86rf215_rf_channel_en ch, uint32_t freq_hz )
{
    if (dev->initialized == 0)
    {
        printf("Error @ at86rf215_setup_channel: device not initialized\n");
        return -1;
    }

    at86rf215_radio_channel_mode_en mode = 0;
    at86rf215_rf_channel_en req_ch = 0;

    if (at86rf215_radio_get_good_channel(freq_hz, &mode, &req_ch) < 0 || req_ch != ch)
    {
        printf("Error @ at86rf215_setup_channel: the requested channel or frequency not supported\n");
        return -1;
    }
    int center_freq_25khz_res = 0;
    int channel_number = 0;
    float actual_freq = at86rf215_radio_get_frequency(mode, 1, freq_hz, &center_freq_25khz_res, &channel_number);
    at86rf215_radio_setup_channel(dev, ch, 1, center_freq_25khz_res, channel_number, mode);
    return (int)actual_freq;
}

//===================================================================
void at86rf215_setup_iq_radio_transmit (at86rf215_st* dev, at86rf215_rf_channel_en radio)
{
    /*
    It is assumed, that the radio has been reset before and is in State TRXOFF. All interrupts in register RFn_IRQS should be enabled (RFn_IRQM=0x3f).
    */

    // 1. Set TRXOFF mode
    // 2. Enable all interrupts in 09,_24_IRQS
    // 3. Enable I/Q radio mode - setting IQIFC1.CHPM=1 at AT86RF215
    // 4. Configure the Transmitter Frontend:
    //      Set the transmitter analog frontend sub-registers TXCUTC.LPFCUT and TXCUTC.PARAMP
    //      Set the transmitter digital frontend sub-registers TXDFE.SR and TXDFE.RCUT
    // 5. Configure the channel parameters, see section "Channel Configuration" on page 62 and transmit power
    // 6. Optional: Perform ED measurement, see section "Energy Measurement" on page 56. The following steps are recommended:
    //      Configure the measurement period, see register RFn_EDD.
    //      Switch to State RX.
    //      Start and finish a measurement:
    //          For single and continuous ED modes a measurement starts if the mode is written to sub-register EDC.EDM.
    //          The completion of the measurement is indicated by the interrupt IRQS.EDC. The resulting ED value can be read from register RFn_EDV.
    //      For the automatic mode, a measurement starts by setting bit AGCC.FRZC=1. After the completion of the measurement period, the ED value can be read from register RFn_EDV.
    // 7. Switch to State TXPREP; interrupt IRQS.TRXRDY is issued.
    // 8. To start the actual transmission, there are two possibilities, depending on the setting of sub-register IQIFC0.EEC:
    //      IQIFC0.EEC=0 => Enable the radio transmitter by writing command TX to the register RFn_CMD via SPI.
    //      IQIFC0.EEC=1 => The transmitter is activated automatically with the TX start signal embedded in I_DATA[0],
    // 9. To finish a transmission depends on the setting of bit IQIFC0.EEC:
    //      IQIFC0.EEC=0 => To leave the State TX, write command TXPREP to the register RFn_CMD. Reaching State TXPREP is indicated by the interrupt IRQS.TRXRDY.
    //      IQIFC0.EEC=1 => If the bit I_DATA[0] is set to 0 (see Figure 6-4 on page 47) the ramp down process of the PA is started automatically. After ramp down the transmitter switches back to State TXPREP.
}

//===================================================================
void at86rf215_setup_iq_radio_receive (at86rf215_st* dev, at86rf215_rf_channel_en radio)
{
    /*
    It is assumed, that the radio has been reset before and is in State TRXOFF. All interrupts in register RFn_IRQS should be enabled (RFn_IRQM=0x3f).
    */

    // 1. Set TRXOFF mode
    // 2. Enable all interrupts in 09,_24_IRQS
    // 3. Enable I/Q radio mode - setting IQIFC1.CHPM=1 at AT86RF215
    // 4. Configure the Receiving Frontend:
    //      Set the receiver analog frontend sub-registers RXBWC.BW and RXBWC.IFS,
    //      Set the receiver digital frontend sub-registers RXDFE.SR and RXDFE.RCUT
    //      Set the AGC registers RFn_AGCC and RFn_AGCS
    // 5. Configure the channel parameters, see section "Channel Configuration" on page 62 and transmit power
    // 6. Switch to State TXPREP; interrupt IRQS.TRXRDY is issued. TXD and TXCLK are activated as shown in Figure 4-12 on page 26.
    // 7. Prepare the external baseband for reception of I/Q samples
    // 8. Enable the radio receiver by writing command RX to the register RFn_CMD.
    // 9. To prevent the AGC from switching its gain during reception, it is recommended to set AGCC.FRZC=1 after reception of the preamble,
    //    the AGC has to be released after finishing reception by setting AGCC.FRZC=0.
}

//===================================================================
void at86rf215_setup_iq_radio_continues_tx (at86rf215_st* dev, at86rf215_rf_channel_en ch)
{
    // This is useful for application / production tests as well as certification tests.
    // Using this mode, the transceiver acts as a continuous transmitter
    // 1. Prior to transmission, the AT86RF215 must be in state TXPREP, see section "State Machine" on page 33.
    // 2. The continuous transmission is enabled if the sub-register PC.CTX is set to 1
    // 3. A frame transmission, started by CMD.CMD=TX with enabled continuous transmit mode (PC.CTX),
    //    transmits synchronization header (SHR), PHY header (PHR) and repeatedly PHY payload (PSDU).
    // 4. The current PHY settings are used
    // 5. the length of the PHY payload is configured by the concatenation of the registers BBCn_TXFLH and BBCn_TXFLL.
    //    If the sub-register PC.TXAFCS is set to 1, the last PHY payload octets are replaced by the calculated FCS
    // 6. The transmission proceeds as long as the sub-register PC.CTX remains 1. If the sub-register PC.CTX is set
    //    to 0, the transmission stops once the current PSDU transmission is completed
}

void at86rf215_setup_iq_radio_dac_value_override (at86rf215_st* dev,
                                                    at86rf215_rf_channel_en ch,
                                                    uint32_t freq_hz,
                                                    uint8_t tx_power)
{
    // The AT86RF215 comprises a DAC (digital to analog converter) value overwrite functionality.
    // Each transceiver contains two transmitter DACs in order to transmit IQ signals. This feature is useful to
    // transmit an LO carrier which is necessary for certain certifications.
    // If the sub-register TXDACI.ENTXDACID is set to 1, the digital input value of the in-phase signal DAC is
    // overwritten with the value of the sub-register TXDACI.TXDACID. The same with TXDACQ.ENTXDACQD and TXDACQ.TXDACQD.

    // Both TXDACI.TXDACID and TXDACQ.TXDACQD contain 7-bit binary values in the range from 0x00 to 0x7E (dec. 126).
    // 0x3F (63) is zero, 0x00 is the minimal signal, and 0x7E is the maximal signal.
    // If the transceiver is in state TX and both DAC values are overwritten, the transceiver transmits an LO
    // carrier of the frequency selected by the "Frequency Synthesizer (PLL)" on page 62.
    // To start a continuous transmission of a LO carrier, the transmitter is started as described in
    // (at86rf215_setup_iq_radio_continues_tx) Frame Based Continuous Transmission.
    // Alternatively, the transmitter can be started using chip mode 1 if sub-register IQIFC1.CHPM is set to 0x01.
    // (this is the case of I/Q over LVDS) In this case the transmitter is started by command TX.

    at86rf215_radio_state_cmd_en state = at86rf215_radio_get_state(dev, ch);
    if (state != at86rf215_radio_state_cmd_trx_off)
    {
        at86rf215_radio_set_state(dev, ch, at86rf215_radio_state_cmd_trx_off);
    }
    at86rf215_radio_set_state(dev, ch, at86rf215_radio_state_cmd_tx_prep);

    at86rf215_radio_tx_ctrl_st tx_config =
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_32usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,
        .tx_power = tx_power,
        .analog_bw = at86rf215_radio_tx_cut_off_80khz,
        .digital_bw = at86rf215_radio_rx_f_cut_half_fs,
        .fs = at86rf215_radio_rx_sample_rate_4000khz,
        .direct_modulation = 0,
    };
    at86rf215_radio_setup_tx_ctrl(dev, ch, &tx_config);

    at86rf215_radio_external_ctrl_st aux_cfg =
    {
        .ext_lna_bypass_available = 0,
        .agc_backoff = 0,
        .analog_voltage_external = 0,
        .analog_voltage_enable_in_off = 0,
        .int_power_amplifier_voltage = 2,
        .fe_pad_configuration = 1,
    };

    at86rf215_radio_setup_external_settings(dev, ch, &aux_cfg);

    at86rf215_iq_interface_config_st iq_if_config =
    {
        .loopback_enable = 0,
        .drv_strength = at86rf215_iq_drive_current_2ma,
        .common_mode_voltage = at86rf215_iq_common_mode_v_ieee1596_1v2,
        .tx_control_with_iq_if = false,
        .radio09_mode = at86rf215_iq_if_mode,
        .radio24_mode = at86rf215_iq_if_mode,
        .clock_skew = at86rf215_iq_clock_data_skew_4_906ns,
    };
    at86rf215_setup_iq_if(dev, &iq_if_config);
    at86rf215_radio_set_tx_dac_input_iq(dev, ch, 1, 0x7E, 1, 0x3F);
    at86rf215_setup_channel (dev, ch, freq_hz);
    at86rf215_radio_set_state(dev, ch, at86rf215_radio_state_cmd_tx);
}