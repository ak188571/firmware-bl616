/*
 * GPIO JTAG programmer for Gowin GW5A and GW2A
 *
 * (c) 2025.2, nand2mario
 *
 * This source code is licensed under the Apache 2.0 license found in the
 * LICENSE file in the root directory of this source tree. 
 *
 * Based in part on openFPGALoader by Gwenhael Goavec-Merou
 */

#include <string.h>
#include "programmer.h"
#ifdef TANG_CONSOLE138K
#include "ct2090.h"
#include "hardware/gpio_reg.h"
#include "hardware/uart_reg.h"
#endif
#include <FreeRTOS.h>
#include "task.h"
#include "utils.h"
#include "overlay.h"
#include "tc_transition_trace.h"
#ifdef TC_JTAG_SPI_HZ
#include "tc_jtag_spi.h"
#endif

#define JTAG_MAX_CHAIN 8

int chain_len;
uint32_t idcodes[JTAG_MAX_CHAIN];

#define JTAG_TMS_BUFFER_SIZE 128

typedef enum tapState_t {
    TEST_LOGIC_RESET = 0,
    RUN_TEST_IDLE = 1,
    SELECT_DR_SCAN = 2,
    CAPTURE_DR = 3,
    SHIFT_DR = 4,
    EXIT1_DR = 5,
    PAUSE_DR = 6,
    EXIT2_DR = 7,
    UPDATE_DR = 8,
    SELECT_IR_SCAN = 9,
    CAPTURE_IR = 10,
    SHIFT_IR = 11,
    EXIT1_IR = 12,
    PAUSE_IR = 13,
    EXIT2_IR = 14,
    UPDATE_IR = 15,
    UNKNOWN = 16,
} tapState_t;

static int _num_tms;                // in bits
static int _curr_tms, _curr_tdi;
static unsigned char _tms_buffer[JTAG_TMS_BUFFER_SIZE];
static tapState_t _state;

const int JTAG_RATE=5000000;						// 5Mhz jtag speed
const int JTAG_DELAY=32;                			// 320Mhz / 5Mhz / 2 = 32 cycles

// Tang Primer 25K
#define IDCODE_GW5A_25 0x0001281b
// Tang Mega/Console 60K
#define IDCODE_GW5AT_60 0x0001481b
// Tang Mega/Console/Mega Pro 138K
#define IDCODE_GWAST_138 0x0001081b
// Not a current Tang board
#define IDCODE_GW5AT_138 0x0001181b
// Tang Nano 20K
#define IDCODE_GW2A_18 0x0000081b

// ------------------------------------------------------------
// Physical I/O functions
// follow libgpiodJtagBitbang.cpp
#define get_mcycle() (0)
#define wait_mcycle(start, cycles) (0)
// static inline uint32_t get_mcycle(void) {
// 	uint32_t mcycle;
// 	asm volatile("csrr %0, mcycle" : "=r"(mcycle));
// 	return mcycle;
// }
// static inline uint32_t wait_mcycle(uint32_t start, uint32_t cycles) {
//     uint32_t end = start + cycles;
//     uint32_t now;
//     while ((now = get_mcycle()) < end);
//     return now;
// }

static int jtag_read_tdo() {
    return GPIO_PIN_JTAG_TDO_V();
}

static int jtag_writeTMS(const uint8_t *tms_buf, uint32_t len, bool flush_buffer, const uint8_t tdi) {
	if (len == 0) // nothing -> stop
		return len;

	// uint32_t start = get_mcycle();
	for (uint32_t i = 0; i < len; i++) {
		int tms = ((tms_buf[i >> 3] & (1 << (i & 7))) ? 1 : 0);

        if (tms) GPIO_PIN_JTAG_TMS_H(); else GPIO_PIN_JTAG_TMS_L(); // send TMS
        if (tdi) GPIO_PIN_JTAG_TDI_H(); else GPIO_PIN_JTAG_TDI_L();     // send TDI
        GPIO_PIN_JTAG_TCK_L();                                      // low  TCK
		// start = wait_mcycle(start, JTAG_DELAY);

        if (tms) GPIO_PIN_JTAG_TMS_H(); else GPIO_PIN_JTAG_TMS_L(); // send TMS
        if (tdi) GPIO_PIN_JTAG_TDI_H(); else GPIO_PIN_JTAG_TDI_L();     // send TDI
        GPIO_PIN_JTAG_TCK_H();                                      // high TCK

        _curr_tms = tms;
		// start = wait_mcycle(start, JTAG_DELAY);
	}
    // GPIO_PIN_JTAG_TCK_L();                                          // low  TCK
	_curr_tdi = tdi;

	return len;
}

uint64_t jtag_writetdi_time = 0;	// performance counter

//         TMS  TCK  TDO  TDI 
// Nano20K: 16   10   14   12
// Others:   0    1    2    3
// bit 25 is set, bit 26 is clear
#ifdef TANG_NANO20K
volatile uint32_t *reg_gpio_tms = (volatile uint32_t *)0x20000904;     // gpio16
volatile uint32_t *reg_gpio_tck = (volatile uint32_t *)0x200008ec;     // gpio10
volatile uint32_t *reg_gpio_tdo = (volatile uint32_t *)0x200008fc;	   // gpio14
volatile uint32_t *reg_gpio_tdi = (volatile uint32_t *)0x200008f4;     // gpio12
#else
volatile uint32_t *reg_gpio_tms = (volatile uint32_t *)0x200008c4;     // gpio0, bl616 reference 4.8.5
volatile uint32_t *reg_gpio_tck = (volatile uint32_t *)0x200008c8;     // gpio1
volatile uint32_t *reg_gpio_tdo = (volatile uint32_t *)0x200008cc;	   // gpio2
volatile uint32_t *reg_gpio_tdi = (volatile uint32_t *)0x200008d0;     // gpio3
#endif

// nand2mario: this is the hotspot for JTAG programming and optimized for performance
static int jtag_writeTDI(const uint8_t *tx, uint8_t *rx, int len, bool end) {
	bool tms = _curr_tms;
	bool tdi = _curr_tdi;
	const uint32_t mask_set = (1 << 25);
	const uint32_t mask_clear = (1 << 26);

	uint64_t time_start = bflb_mtimer_get_time_us();

	if (rx)
		memset(rx, 0, len / 8);

	// uint32_t start = get_mcycle();
	for (uint32_t i = 0; i < len; i++) {
		if (end && (i == len - 1)) {
			tms = 1;
			*reg_gpio_tms |= mask_set;
		}

		if (tx)
			tdi = (tx[i >> 3] & (1 << (i & 7)));

		*reg_gpio_tdi |= tdi ? mask_set : mask_clear;	// set TDI
		*reg_gpio_tck |= mask_clear;					// clock low
		*reg_gpio_tck |= mask_set;						// clock high

		if (rx) {
			if (jtag_read_tdo() > 0)
				rx[i >> 3] |= 1 << (i & 7);
		}
	}
	_curr_tms = tms;
	_curr_tdi = tdi;

	jtag_writetdi_time += bflb_mtimer_get_time_us() - time_start;
	return len;
}

#define GPIO_INT_MASK    (1<<22)
#define GPIO_FUNC_SWGPIO (0xB<<8)
#define GPIO_OUTPUT_EN   (1<<6)
#define GPIO_INPUT_EN    (1)
#define GPIO_SCHMITT_EN  (1<<1)
#define GPIO_DRV_3       (3<<2)
#define GPIO_MODE_VALUE  (0<<30)
#define GPIO_MODE_SETCLR (1<<30)
#define GPIO_MODE_DMA    (2<<30)
#define GPIO_MODE_DMA_SETCLR (3<<30)
uint32_t jtag_tms_cfg, jtag_tck_cfg, jtag_tdi_cfg;
volatile uint32_t *reg_gpio0_31 = (volatile uint32_t *)0x20000ae4;  // gpio_cfg136，Register Controlled GPIO Output Value

// set GPIO0 (TMS), GPIO1 (TCK) and GPIO3 (TDI) as direct output mode
void jtag_enter_gpio_out_mode() {
	jtag_tms_cfg = *reg_gpio_tms;
	jtag_tck_cfg = *reg_gpio_tck;
	jtag_tdi_cfg = *reg_gpio_tdi;
	*reg_gpio_tms = GPIO_INT_MASK | GPIO_FUNC_SWGPIO | GPIO_OUTPUT_EN | GPIO_SCHMITT_EN | GPIO_DRV_3;
	*reg_gpio_tck = GPIO_INT_MASK | GPIO_FUNC_SWGPIO | GPIO_OUTPUT_EN | GPIO_SCHMITT_EN | GPIO_DRV_3;
	*reg_gpio_tdi = GPIO_INT_MASK | GPIO_FUNC_SWGPIO | GPIO_OUTPUT_EN | GPIO_SCHMITT_EN | GPIO_DRV_3;
}

// restore GPIO1 and GPIO3 settings
void jtag_exit_gpio_out_mode() {
	*reg_gpio_tms = jtag_tms_cfg;
	*reg_gpio_tck = jtag_tck_cfg;
	*reg_gpio_tdi = jtag_tdi_cfg;
}

// nand2mario: this is faster than jtag_writeTDI()
// 1. avoid data conversion by writing *tx MSB first
// 2. use GPIO_CFG144 to set GPIO0-3 as output
#ifdef TANG_CONSOLE138K
// Keep direct bit calculation: the nibble-table version measured slower.
// Inlining with constant end removes per-byte final-bit decisions.
static inline __attribute__((always_inline)) void jtag_emit_byte(uint8_t byte, bool end) {

		// bit 7
		*reg_gpio0_31 = (byte & 0x80) >> 4;           // bit 3 (TDI) = data, bit 1 (TCK) = 0
		*reg_gpio0_31 = ((byte & 0x80) >> 4) | 2;     // bit 3 (TDI) = data, bit 1 (TCK) = 1
		// bit 6
		*reg_gpio0_31 = (byte & 0x40) >> 3;
		*reg_gpio0_31 = ((byte & 0x40) >> 3) | 2;
		// bit 5
		*reg_gpio0_31 = (byte & 0x20) >> 2;
		*reg_gpio0_31 = ((byte & 0x20) >> 2) | 2;
		// bit 4
		*reg_gpio0_31 = (byte & 0x10) >> 1;
		*reg_gpio0_31 = ((byte & 0x10) >> 1) | 2;
		// bit 3
		*reg_gpio0_31 = (byte & 0x8);
		*reg_gpio0_31 = (byte & 0x8) | 2;
		// bit 2
		*reg_gpio0_31 = (byte & 0x4) << 1;
		*reg_gpio0_31 = ((byte & 0x4) << 1) | 2;
		// bit 1
		*reg_gpio0_31 = (byte & 0x2) << 2;
		*reg_gpio0_31 = ((byte & 0x2) << 2) | 2;
		// bit 0
		*reg_gpio0_31 = ((byte & 0x1) << 3) | end;  	// TMS=1 if at the end
		*reg_gpio0_31 = ((byte & 0x1) << 3) | 2 | end;	// TMS=1 if at the end

}
#endif

void jtag_writeTDI_msb_first_gpio_out_mode(const uint8_t *tx, int bytes, bool end) {
#ifdef TANG_CONSOLE138K
    const int regular=bytes-((end && bytes>0)?1:0);
    for(int i=0;i<regular;++i)
        jtag_emit_byte(tx[i],false);
    if(end && bytes>0)
        jtag_emit_byte(tx[bytes-1],true);
#else

	for (int i = 0; i < bytes; i++) {
		uint8_t byte = tx[i];
#ifdef TANG_NANO20K
		// bit 7
		*reg_gpio0_31 = (byte & 0x80) << 5;                // bit 12 (TDI) = data, bit 10 (TCK) = 0
		*reg_gpio0_31 = ((byte & 0x80) << 5) | (1 << 10);  // bit 12 (TDI) = data, bit 10 (TCK) = 1
		// bit 6
		*reg_gpio0_31 = (byte & 0x40) << 6; 
		*reg_gpio0_31 = ((byte & 0x40) << 6) | (1 << 10); 
		// bit 5
		*reg_gpio0_31 = (byte & 0x20) << 7; 
		*reg_gpio0_31 = ((byte & 0x20) << 7) | (1 << 10); 
		// bit 4
		*reg_gpio0_31 = (byte & 0x10) << 8; 
		*reg_gpio0_31 = ((byte & 0x10) << 8) | (1 << 10); 
		// bit 3
		*reg_gpio0_31 = (byte & 0x8) << 9; 
		*reg_gpio0_31 = ((byte & 0x8) << 9) | (1 << 10); 
		// bit 2
		*reg_gpio0_31 = (byte & 0x4) << 10; 
		*reg_gpio0_31 = ((byte & 0x4) << 10) | (1 << 10); 
		// bit 1
		*reg_gpio0_31 = (byte & 0x2) << 11; 
		*reg_gpio0_31 = ((byte & 0x2) << 11) | (1 << 10); 
		// bit 0
		*reg_gpio0_31 = (byte & 0x1) << 12 | (i == bytes-1 && end ? (1 << 16) : 0); 	// bit 16: TMS
		*reg_gpio0_31 = ((byte & 0x1) << 12) | (1 << 10) | (i == bytes-1 && end ? (1 << 16) : 0); 
#else
		// bit 7
		*reg_gpio0_31 = (byte & 0x80) >> 4;           // bit 3 (TDI) = data, bit 1 (TCK) = 0
		*reg_gpio0_31 = ((byte & 0x80) >> 4) | 2;     // bit 3 (TDI) = data, bit 1 (TCK) = 1
		// bit 6
		*reg_gpio0_31 = (byte & 0x40) >> 3;     
		*reg_gpio0_31 = ((byte & 0x40) >> 3) | 2; 
		// bit 5
		*reg_gpio0_31 = (byte & 0x20) >> 2;     
		*reg_gpio0_31 = ((byte & 0x20) >> 2) | 2; 
		// bit 4
		*reg_gpio0_31 = (byte & 0x10) >> 1;     
		*reg_gpio0_31 = ((byte & 0x10) >> 1) | 2; 
		// bit 3
		*reg_gpio0_31 = (byte & 0x8);     
		*reg_gpio0_31 = (byte & 0x8) | 2; 
		// bit 2
		*reg_gpio0_31 = (byte & 0x4) << 1;     
		*reg_gpio0_31 = ((byte & 0x4) << 1) | 2; 
		// bit 1
		*reg_gpio0_31 = (byte & 0x2) << 2;     
		*reg_gpio0_31 = ((byte & 0x2) << 2) | 2; 
		// bit 0
		*reg_gpio0_31 = ((byte & 0x1) << 3) | (i == bytes-1 && end);  	// TMS=1 if at the end
		*reg_gpio0_31 = ((byte & 0x1) << 3) | 2 | (i == bytes-1 && end);	// TMS=1 if at the end
#endif
	}
#endif
	_curr_tms = end;
}

/*
static int jtag_writeTDI_slow(const uint8_t *tx, uint8_t *rx, int len, bool end) {
	int tms = _curr_tms;
	int tdi = _curr_tdi;

	uint64_t time_start = bflb_mtimer_get_time_us();

	if (rx)
		memset(rx, 0, len / 8);

	// uint32_t start = get_mcycle();
	for (uint32_t i = 0; i < len; i++) {
		if (end && (i == len - 1))
			tms = 1;

		if (tx)
			tdi = (tx[i >> 3] & (1 << (i & 7))) ? 1 : 0;

        if (tms) GPIO_PIN_JTAG_TMS_H(); else GPIO_PIN_JTAG_TMS_L();     // send TMS
        if (tdi) GPIO_PIN_JTAG_TDI_H(); else GPIO_PIN_JTAG_TDI_L();     // send TDI
        GPIO_PIN_JTAG_TCK_L();                                          // low  TCK 
		// start = wait_mcycle(start, JTAG_DELAY);

        if (tms) GPIO_PIN_JTAG_TMS_H(); else GPIO_PIN_JTAG_TMS_L();     // send TMS
        if (tdi) GPIO_PIN_JTAG_TDI_H(); else GPIO_PIN_JTAG_TDI_L();     // send TDI
        GPIO_PIN_JTAG_TCK_H();                                          // high TCK

        _curr_tms = tms;
        _curr_tdi = tdi;

		if (rx) {
			if (jtag_read_tdo() > 0)
				rx[i >> 3] |= 1 << (i & 7);
		}
		// start = wait_mcycle(start, JTAG_DELAY);
	}
    // GPIO_PIN_JTAG_TCK_L();                                          // low  TCK

	jtag_writetdi_time += bflb_mtimer_get_time_us() - time_start;
	return len;
}
*/

static int flushTMS(bool flush_buffer) {
	int ret = 0;
	if (_num_tms != 0) {
		ret = jtag_writeTMS(_tms_buffer, _num_tms, flush_buffer, _curr_tdi);

		/* reset buffer and number of bits */
		memset(_tms_buffer, 0, JTAG_TMS_BUFFER_SIZE);
		_num_tms = 0;
	} else if (flush_buffer) {
		// jtag_flush();
	}
	return ret;    
}

static int jtag_toggleClk_(uint8_t tms, uint8_t tdi, uint32_t clk_len)
{
	// uint32_t start = get_mcycle();	
    if(clk_len){
        if(tms)GPIO_PIN_JTAG_TMS_H();else GPIO_PIN_JTAG_TMS_L();
        if(tdi)GPIO_PIN_JTAG_TDI_H();else GPIO_PIN_JTAG_TDI_L();
    }
    for(uint32_t i=0;i<clk_len;++i){
        GPIO_PIN_JTAG_TCK_L();
        GPIO_PIN_JTAG_TCK_H();
    }

	_curr_tms = tms;
	_curr_tdi = tdi;
	
    return clk_len;
}

static void jtag_toggleClk(int nb)
{
	unsigned char c = (TEST_LOGIC_RESET == _state) ? 1 : 0;
	flushTMS(false);
	jtag_toggleClk_(c, 0, nb);
}

// isFull() and flush() is moot

// ------------------------------------------------------------
// JTAG functions, similar to jtag.cpp
static unsigned _dr_bits_before, _dr_bits_after;
static uint8_t _dr_bits[4];     // nand2mario: shiftDR() buffer. in our case, there's only one device.
                                // so shiftDr() is actually not needed.
static unsigned _ir_bits_before, _ir_bits_after;
static uint8_t _ir_bits[4];
                                
static void setTMS(unsigned char tms) {
	if (_num_tms+1 == JTAG_TMS_BUFFER_SIZE * 8)
		flushTMS(false);
	if (tms != 0)
		_tms_buffer[_num_tms>>3] |= (0x1) << (_num_tms & 0x7);
	_num_tms++;
}

static void go_test_logic_reset() {
    for (int i = 0; i < 6; i++)
        setTMS(0x01);
    flushTMS(false);
    _state = TEST_LOGIC_RESET;
}

static void set_state(tapState_t newState) {
    const uint8_t tdi = 1;
	_curr_tdi = tdi;
	unsigned char tms = 0;
	while (newState != _state) {
		// display("_state : %16s(%02d) -> %s(%02d) ",
		// 	getStateName((tapState_t)_state),
		// 	_state,
		// 	getStateName((tapState_t)newState), newState);
		switch (_state) {
		case TEST_LOGIC_RESET:
			if (newState == TEST_LOGIC_RESET) {
				tms = 1;
			} else {
				tms = 0;
				_state = RUN_TEST_IDLE;
			}
			break;
		case RUN_TEST_IDLE:
			if (newState == RUN_TEST_IDLE) {
				tms = 0;
			} else {
				tms = 1;
				_state = SELECT_DR_SCAN;
			}
			break;
		case SELECT_DR_SCAN:
			switch (newState) {
			case CAPTURE_DR:
			case SHIFT_DR:
			case EXIT1_DR:
			case PAUSE_DR:
			case EXIT2_DR:
			case UPDATE_DR:
				tms = 0;
				_state = CAPTURE_DR;
				break;
			default:
				tms = 1;
				_state = SELECT_IR_SCAN;
			}
			break;
		case SELECT_IR_SCAN:
			switch (newState) {
			case CAPTURE_IR:
			case SHIFT_IR:
			case EXIT1_IR:
			case PAUSE_IR:
			case EXIT2_IR:
			case UPDATE_IR:
				tms = 0;
				_state = CAPTURE_IR;
				break;
			default:
				tms = 1;
				_state = TEST_LOGIC_RESET;
			}
			break;
			/* DR column */
		case CAPTURE_DR:
			if (newState == SHIFT_DR) {
				tms = 0;
				_state = SHIFT_DR;
			} else {
				tms = 1;
				_state = EXIT1_DR;
			}
			break;
		case SHIFT_DR:
			if (newState == SHIFT_DR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT1_DR;
			}
			break;
		case EXIT1_DR:
			switch (newState) {
			case PAUSE_DR:
			case EXIT2_DR:
			case SHIFT_DR:
			case EXIT1_DR:
				tms = 0;
				_state = PAUSE_DR;
				break;
			default:
				tms = 1;
				_state = UPDATE_DR;
			}
			break;
		case PAUSE_DR:
			if (newState == PAUSE_DR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT2_DR;
			}
			break;
		case EXIT2_DR:
			switch (newState) {
			case SHIFT_DR:
			case EXIT1_DR:
			case PAUSE_DR:
				tms = 0;
				_state = SHIFT_DR;
				break;
			default:
				tms = 1;
				_state = UPDATE_DR;
			}
			break;
		case UPDATE_DR:
		case UPDATE_IR:
			if (newState == RUN_TEST_IDLE) {
				tms = 0;
				_state = RUN_TEST_IDLE;
			} else {
				tms = 1;
				_state = SELECT_DR_SCAN;
			}
			break;
			/* IR column */
		case CAPTURE_IR:
			if (newState == SHIFT_IR) {
				tms = 0;
				_state = SHIFT_IR;
			} else {
				tms = 1;
				_state = EXIT1_IR;
			}
			break;
		case SHIFT_IR:
			if (newState == SHIFT_IR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT1_IR;
			}
			break;
		case EXIT1_IR:
			switch (newState) {
			case PAUSE_IR:
			case EXIT2_IR:
			case SHIFT_IR:
			case EXIT1_IR:
				tms = 0;
				_state = PAUSE_IR;
				break;
			default:
				tms = 1;
				_state = UPDATE_IR;
			}
			break;
		case PAUSE_IR:
			if (newState == PAUSE_IR) {
				tms = 0;
			} else {
				tms = 1;
				_state = EXIT2_IR;
			}
			break;
		case EXIT2_IR:
			switch (newState) {
			case SHIFT_IR:
			case EXIT1_IR:
			case PAUSE_IR:
				tms = 0;
				_state = SHIFT_IR;
				break;
			default:
				tms = 1;
				_state = UPDATE_IR;
			}
			break;
		case UNKNOWN:;
			// UNKNOWN should not be valid...
			// throw std::exception();
		}

		setTMS(tms);
		// display("%d %d %d %x\n", tms, _num_tms-1, _state,
		// 	_tms_buffer[(_num_tms-1) / 8]);
	}
	/* force write buffer */
	flushTMS(false);
}

static int read_write(const uint8_t *tdi, unsigned char *tdo, int len, char last) {
    flushTMS(false);
    jtag_writeTDI(tdi, tdo, len, last);
    if (last == 1)
        _state = (_state == SHIFT_DR) ? EXIT1_DR : EXIT1_IR;
    return 0;
}

static int shiftIR_end(const unsigned char *tdi, unsigned char *tdo, int irlen, tapState_t end_state)
{
    // display("%s: avant shiftIR\n", __func__);

	/* if not in SHIFT IR move to this state */
	if (_state != SHIFT_IR) {
		set_state(SHIFT_IR);
		if (_ir_bits_before)
			read_write(_ir_bits, NULL, _ir_bits_before, false);
	}

	// display("%s: envoi ircode\n", __func__);

	/* write tdi (and read tdo) to the selected device
	 * end (ie TMS high) is used only when current device
	 * is the last of the chain and a state change must
	 * be done
	 */
	read_write(tdi, tdo, irlen, _ir_bits_after == 0 && end_state != SHIFT_IR);

	/* it's asked to move out of SHIFT IR state */
	if (end_state != SHIFT_IR) {
		/* again if devices after fill '1' */
		if (_ir_bits_after > 0)
			read_write(_ir_bits, NULL, _ir_bits_after, true);
		/* move to the requested state */
		set_state(end_state);
	}

	return 0;
}
static int shiftIR(const uint8_t *tdi, unsigned char *tdo, int irlen) {
    shiftIR_end(tdi, tdo, irlen, RUN_TEST_IDLE);
	return 0;
}

static int shiftDR_end(const uint8_t *tdi, unsigned char *tdo, int drlen, tapState_t end_state)
{
	/* if current state not shift DR
	 * move to this state
	 */
	if (_state != SHIFT_DR) {
		set_state(SHIFT_DR);
		flushTMS(false);  // force transmit tms state

		if (_dr_bits_before) {
			printf("dr_bits_before: %d\n", _dr_bits_before);
			read_write(_dr_bits, NULL, _dr_bits_before, false);
		}
	}

	/* write tdi (and read tdo) to the selected device
	 * end (ie TMS high) is used only when current device
	 * is the last of the chain and a state change must
	 * be done
	 */
	read_write(tdi, tdo, drlen, _dr_bits_after == 0 && end_state != SHIFT_DR);

	/* if it's asked to move in FSM */
	if (end_state != SHIFT_DR) {
		/* if current device is not the last */
		if (_dr_bits_after) {
			printf("dr_bits_after: %d\n", _dr_bits_after);
			read_write(_dr_bits, NULL, _dr_bits_after, true);  // its the last force
								   // tms high with last bit
		}

		/* move to end_state */
		set_state(end_state);
	}
	return 0;
}

static int shiftDR(const uint8_t *tdi, unsigned char *tdo, int drlen) {
    shiftDR_end(tdi, tdo, drlen, RUN_TEST_IDLE);
	return 0;
}

// ------------------------------------------------------------
// Gowin specific: gowin.cpp
#define NOOP				0x02
#define ERASE_SRAM			0x05
#define XFER_DONE			0x09
#define READ_IDCODE			0x11
#define INIT_ADDR			0x12
#define READ_USERCODE		0x13
#define CONFIG_ENABLE		0x15
#define XFER_WRITE			0x17
#define CONFIG_DISABLE		0x3A
#define RELOAD				0x3C
#define STATUS_REGISTER		0x41

#define STATUS_CRC_ERROR			(1 << 0)
#define STATUS_BAD_COMMAND		(1 << 1)
#define STATUS_ID_VERIFY_FAILED	(1 << 2)
#define STATUS_TIMEOUT			(1 << 3)
#define STATUS_MEMORY_ERASE		(1 << 5)
#define STATUS_PREAMBLE			(1 << 6)
#define STATUS_SYSTEM_EDIT_MODE	(1 << 7)
#define STATUS_PRG_SPIFLASH_DIRECT (1 << 8)
#define STATUS_NON_JTAG_CNF_ACTIVE (1 << 10)
#define STATUS_BYPASS				(1 << 11)
#define STATUS_GOWIN_VLD			(1 << 12)
#define STATUS_DONE_FINAL			(1 << 13)
#define STATUS_SECURITY_FINAL		(1 << 14)
#define STATUS_READY				(1 << 15)
#define STATUS_POR				(1 << 16)
#define STATUS_FLASH_LOCK			(1 << 17)

typedef enum prog_mode {
    NONE_MODE = 0,
    SPI_MODE = 1,
    FLASH_MODE = 1,
    MEM_MODE = 2,
    READ_MODE = 3,
} prog_mode;

// static prog_mode _mode = NONE_MODE;
// uint16_t _checksum;

// trust we are little-endian
#define htole32(x) (x)
#define le32toh(x) (x)

bool send_command(uint8_t cmd)
{
	shiftIR(&cmd, NULL, 8);
	jtag_toggleClk(6);
	return true;
}

static uint32_t readReg32(uint8_t cmd)
{
	uint32_t reg = 0, tmp = 0xffffffffU;
	send_command(cmd);
	shiftDR((uint8_t *)&tmp, (uint8_t *)&reg, 32);
	return le32toh(reg);
}

static uint32_t idCode()
{
	return readReg32(READ_IDCODE);
}

static uint32_t readStatusReg()
{
	return readReg32(STATUS_REGISTER);	// 0x41
}

static uint32_t readUserCode()
{
	return readReg32(READ_USERCODE);	// 0x13
}

bool pollFlag(uint32_t mask, uint32_t value)
{
	uint32_t status;
	int timeout = 0;
	do {
		status = readStatusReg();
		if (timeout == 100000000){
			printf("timeout\r\n");
			return false;
		}
		timeout++;
		// vTaskDelay(1);
	} while ((status & mask) != value);

	return true;
}

bool enableCfg()
{
	send_command(CONFIG_ENABLE);	// 0x15
	return pollFlag(STATUS_SYSTEM_EDIT_MODE, STATUS_SYSTEM_EDIT_MODE);
}

bool disableCfg()
{
	send_command(CONFIG_DISABLE);	// 0x3A
	send_command(NOOP);				// 0x02
	return pollFlag(STATUS_SYSTEM_EDIT_MODE, 0);
}

void sendClkUs(unsigned us)
{
	uint64_t clocks = 15000000;     // very rough estimate: 15Mhz
	clocks *= us;
	clocks /= 1000000;
    const uint64_t started=bflb_mtimer_get_time_us();
    jtag_toggleClk(clocks);
    while(bflb_mtimer_get_time_us()-started<us)
        jtag_toggleClk(1);
}



// ------------------------------------------------------------
// Public functions

bool is_gw2a;

int detectChain(int max_dev) {
    // char message[256];
    uint8_t rx_buff[4];
    const uint8_t tx_buff[4] = {0xff, 0xff, 0xff, 0xff};
    uint32_t tmp;

    // cleanup
    chain_len = 0;
    go_test_logic_reset();
	set_state(SHIFT_DR);

	for (int i = 0; i < max_dev; ++i) {
		read_write(tx_buff, rx_buff, 32, 0);
		tmp = 0;
		for (int ii = 0; ii < 4; ++ii)
			tmp |= (rx_buff[ii] << (8 * ii));

        if (tmp == 0) {
            return -1;              // TDO is stuck at 0
        }

		if (tmp == 0xffffffff) {    // end of chain
			break;
		}

        idcodes[chain_len++] = tmp;
	}
	set_state(TEST_LOGIC_RESET);
	flushTMS(true);

	if (chain_len > 0 && idcodes[0] == IDCODE_GW2A_18) {
		is_gw2a = true;
	}

	return chain_len;
}

bool eraseSRAM() {
	uint32_t id = idCode();
	uint32_t status = readStatusReg();
	overlay_cursor(2, 0);
	overlay_printf("ID=%08x, status=%08x", id, status);

	bool auto_boot_2nd_fail = (status & (1 << 4)) == (1 << 4);
	bool is_timeout = (status & (1 << 3)) == (1 << 3);
	bool bad_cmd = (status & STATUS_BAD_COMMAND) == STATUS_BAD_COMMAND;
	if (is_timeout || auto_boot_2nd_fail || bad_cmd) {
		overlay_status("Erase: send 0x3F\n");
		send_command(CONFIG_ENABLE);
		send_command(0x3F);
		send_command(CONFIG_DISABLE);
		send_command(NOOP);
		send_command(READ_IDCODE);
		send_command(NOOP);
		jtag_toggleClk(125 * 8);
	}

	if (!enableCfg()) {
		overlay_status("FAIL to enable configuration\r\n");
		return false;
	}
	send_command(ERASE_SRAM);	// 0x05
	send_command(NOOP);			// 0x02

	/* TN653 specifies to wait for 4ms with
	 * clock generated but
	 * status register bit MEMORY_ERASE goes low when ERASE_SRAM
	 * is send and goes high after erase
	 * this check seems enough
	 */
	if (idcodes[0] == 0x0001081b) // seems required for GW5AST...
		sendClkUs(10000);
	overlay_status("Erase: pollFlag...");
	if (pollFlag(STATUS_MEMORY_ERASE, STATUS_MEMORY_ERASE)) {
        overlay_status("Erase: OK");
        // success
    } else {
		overlay_status("FAIL\r\n");
		return false;
	}

	send_command(XFER_DONE);	// 0x09
	send_command(NOOP);			// 0x02
	overlay_status("Erase: disableCfg...");
	if (!disableCfg()) {		// 0x3A		<---- HANG here
		overlay_status("FAIL\r\n");
		return false;
	}
	overlay_status("Erase: disableCfg done...");
	send_command(NOOP);			// 0x02
    overlay_status("Erase: status=0x%08x\r\n", readStatusReg());

	return true;    
}

// Returns true if successful
bool writeSRAM_start() {
	overlay_status("Load SRAM\r\n");
	// uint32_t status = readStatusReg();
	// if (_verbose)
	// 	displayReadReg("before write sram", readStatusReg());
	// ProgressBar progress("Load SRAM", length, 50, _quiet);
	send_command(CONFIG_ENABLE); // config enable 0x15

	/* UG704 3.4.3 */
	send_command(INIT_ADDR); // address initialize 0x12
    // printf("Status after INIT_ADDR: 0x%08x\r\n", readStatusReg());

	/* 2.2.6.4 */
	send_command(XFER_WRITE); // transfer configuration data 0x17

	set_state(SHIFT_DR);

    // clear checksum
	// _checksum = 0;
    return true;
}

// call this repeatedly to send data in chunks
// NOTE: length is in BITS, and has to be multiple of 16, except the last block
bool writeSRAM_send(const uint8_t *data, uint32_t length, bool last) {
    /* 2.2.6.5 */
    shiftDR_end(data, NULL, length, last ? RUN_TEST_IDLE : SHIFT_DR);

    // update checksum by summing up 16-bit unsigned words
	// for (uint32_t i = 0; i < (length >> 3); i+=2)
	// 	_checksum += *(uint16_t *)(data + i);

    return true;
}

bool writeSRAM_end() {

    // printf("Status after XFER_WRITE: 0x%08x\r\n", readStatusReg());
    // send checksum
    // send_command(0x0a);
    // uint32_t checksum = 0xF12D;     // _checksum;
	// shiftDR((uint8_t *)&checksum, NULL, 32);
	// send_command(0x08);

    // printf("Status after CHECKSUM: 0x%08x\r\n", readStatusReg());

	send_command(CONFIG_DISABLE); // config disable 0x3A
	send_command(NOOP); // noop 0x02

	uint32_t usercode = readUserCode();
	uint32_t status_reg = readStatusReg();
    overlay_status("Usercode=0x%08x, status=0x%08x\r\n", usercode, status_reg);

	if (status_reg & STATUS_DONE_FINAL) {
		// printf("DONE\r\n");
		return true;
	} else {
		// printf("FAIL\r\n");
		return false;
	}
}

void fpgaStatus() {
    overlay_status("Status=%08x, User=%08x", readStatusReg(), readUserCode());
}

#ifdef TANG_CONSOLE138K
// Comparison build: keep all 1,000,000 pulses, hoist the SDK GPIO work
// out of the repeated portion. The first pulse retains SDK pad validation.
static void jtag_reload_idle_clocks_fast() {
    const unsigned char tms=(TEST_LOGIC_RESET==_state) ? 1 : 0;
    flushTMS(false);
    if (tms) GPIO_PIN_JTAG_TMS_H(); else GPIO_PIN_JTAG_TMS_L();
    GPIO_PIN_JTAG_TDI_L();
    const uintptr_t bank=(GPIO_PIN_JTAG_TCK >> 5) << 2;
    const uintptr_t clear_addr=gpio_dev->reg_base + GLB_GPIO_CFG140_OFFSET + bank;
    const uintptr_t set_addr=gpio_dev->reg_base + GLB_GPIO_CFG138_OFFSET + bank;
    const uint32_t mask=uint32_t(1) << (GPIO_PIN_JTAG_TCK & 31);
    bflb_gpio_reset(gpio_dev,GPIO_PIN_JTAG_TCK);
    bflb_gpio_set(gpio_dev,GPIO_PIN_JTAG_TCK);
    for (uint32_t i=1; i<1000000; ++i) {
        putreg32(mask,clear_addr);
        putreg32(mask,set_addr);
    }
    _curr_tms=tms;
    _curr_tdi=0;
}
#endif

void fpgaReset(bool fast_idle=false
#ifdef TC_LAUNCH_TRACE
    , TcReloadTiming *timing=nullptr
#endif
) {
	send_command(RELOAD);
#ifdef TC_LAUNCH_TRACE
    if (timing) timing->mark(TcReloadBoundary::ReloadCommand,tc_launch_now_us());
#endif
	send_command(NOOP);    
#ifdef TC_LAUNCH_TRACE
    if (timing) timing->mark(TcReloadBoundary::NoopCommand,tc_launch_now_us());
#endif
    set_state(RUN_TEST_IDLE);
#ifdef TC_LAUNCH_TRACE
    if (timing) timing->mark(TcReloadBoundary::IdleState,tc_launch_now_us());
#endif
#ifdef TANG_CONSOLE138K
    if (fast_idle) jtag_reload_idle_clocks_fast();
    else
#endif
    jtag_toggleClk(1000000);
#ifdef TC_LAUNCH_TRACE
    if (timing) timing->mark(TcReloadBoundary::IdleClocks,tc_launch_now_us());
#endif
}

void enable_jtag_pins(void) {
    // JTAG pins
    bflb_gpio_init(gpio_dev, GPIO_PIN_JTAG_TMS, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_3);
    bflb_gpio_init(gpio_dev, GPIO_PIN_JTAG_TCK, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_3);
    bflb_gpio_init(gpio_dev, GPIO_PIN_JTAG_TDI, GPIO_OUTPUT | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_3);
    bflb_gpio_init(gpio_dev, GPIO_PIN_JTAG_TDO, GPIO_INPUT  | GPIO_FLOAT | GPIO_SMT_EN | GPIO_DRV_3);
}

void disable_jtag_pins(void) {
    bflb_gpio_deinit(gpio_dev, GPIO_PIN_JTAG_TMS);
    bflb_gpio_deinit(gpio_dev, GPIO_PIN_JTAG_TCK);
    bflb_gpio_deinit(gpio_dev, GPIO_PIN_JTAG_TDI);
    bflb_gpio_deinit(gpio_dev, GPIO_PIN_JTAG_TDO);
}

/////////////// top programming functions ///////////////

static bool program_image(const char *fname
#ifdef TANG_CONSOLE138K
                          , const Ct2090::Snapshot& expected
#endif
) {
    TcTransitionSection measured_program(TcTransitionStep::Program);
#if defined(TC_LAUNCH_TRACE) || !defined(TC_JTAG_SPI_HZ)
    uint64_t writetdi_time_start;
    uint64_t time_total = bflb_mtimer_get_time_us();
    uint64_t time_jtag = 0, time_flash = 0;
#endif
    UINT bytes = 0, total = 0;

    // FRESULT res_sd = f_mount(&fs_usb, "usb:", 1);
    // if (res_sd != FR_OK) {
    //     overlay_printf("mount fail, res:%d\r\n", res_sd);
    //     return false;
    // }
    int len = tc_transition_call(TcTransitionStep::FilePrepare,[&](){return get_file_size(fname);});
    if (len <= 0) return false;
    overlay_status("Writing %u bytes...", len);

    FRESULT res_sd = tc_transition_call(TcTransitionStep::FilePrepare,[&](){return f_open(&fcore, fname, FA_READ);});
    if (res_sd != FR_OK) {
        overlay_printf("open fail, res:%d\r\n", res_sd);
        return false;
    }
    bool res = false;
#ifdef TC_JTAG_SPI_HZ
    bool spi_attempted=false;
    bool stream_ok=true;
#endif

	enable_jtag_pins();

    chain_len = detectChain(JTAG_MAX_CHAIN);
    if (chain_len <= 0 || (    idcodes[0] != IDCODE_GW5AT_60
                            && idcodes[0] != IDCODE_GWAST_138
                            && idcodes[0] != IDCODE_GW5A_25
                            && idcodes[0] != IDCODE_GW2A_18)) {
        overlay_printf("No known board detected, IDCODE=%08x\n", idcodes[0]);
        goto load_core_close;
    }

#ifdef TANG_CONSOLE138K
#ifdef TC_JTAG_SPI_HZ
    // Preflight SPI/DMA without changing the JTAG pins, before the existing
    // identity recheck and both SRAM erases. Never silently fall back to GPIO.
    spi_attempted=true;
    if(!tc_jtag_spi_prepare())goto load_core_close;
#endif
    // Recheck after opening the file and reacquiring JTAG, before the first erase.
    if (chain_len != 1) goto load_core_close;
    {
        const auto id=idCode();
        if (id != expected.id) goto load_core_close;
        const auto user=readUserCode();
        if (user != expected.user) goto load_core_close;
        const auto status=readStatusReg();
        if (status != expected.status) goto load_core_close;
#ifdef TC_LAUNCH_TRACE
        if (tc_transition_active && tc_transition_active->boot.samples) {
            auto &boot=tc_transition_active->boot;
            boot.pre_id=id;
            boot.pre_user=user;
            boot.pre_status=status;
            boot.preerase_us=tc_launch_now_us()-boot.start_us;
        }
#endif
    }
#endif
    if (!tc_transition_call(TcTransitionStep::EraseFirst,[](){return eraseSRAM();})) {
        overlay_printf("Failed to erase SRAM\n");
        goto load_core_close;
    }

    // 138K needs erasing twice
    overlay_status("Erasing again...");
    if (!tc_transition_call(TcTransitionStep::EraseSecond,[](){return eraseSRAM();})) {
        overlay_printf("Failed to erase SRAM 2nd time\n");
        goto load_core_close;
    }    

    if (!tc_transition_call(TcTransitionStep::WriteBegin,[](){return writeSRAM_start();})) {
        overlay_printf("Failed to start write SRAM\n");
        goto load_core_close;
    }

#if defined(TC_LAUNCH_TRACE) || !defined(TC_JTAG_SPI_HZ)
    extern uint64_t jtag_writetdi_time;
    writetdi_time_start = jtag_writetdi_time;
#endif
    taskENTER_CRITICAL();
    {
    TcTransitionSection measured_stream(TcTransitionStep::Stream);
#ifndef TC_JTAG_SPI_HZ
    jtag_enter_gpio_out_mode();
#endif
    while (total < static_cast<UINT>(len)) {
        const UINT left = static_cast<UINT>(len) - total;
        const UINT want = left < BLOCK_SIZE ? left : BLOCK_SIZE;
        if (tc_transition_call(TcTransitionStep::SdRead,[&](){return f_read(&fcore, fbuf, want, &bytes);}) != FR_OK || bytes != want) break;
#ifdef TC_JTAG_SPI_HZ
        const bool last=total+bytes==static_cast<UINT>(len);
        stream_ok=tc_transition_call(TcTransitionStep::JtagShift,[&](){return tc_jtag_spi_send(fbuf,bytes,last);});
        if(!stream_ok)break;
        total+=bytes;
        _curr_tdi=fbuf[bytes-1]&1;
        _curr_tms=last;
        if(last)_state=EXIT1_DR; // Final TMS=1 was clocked by the GPIO tail.
#else
        total += bytes;
        tc_transition_call(TcTransitionStep::JtagShift,[&](){jtag_writeTDI_msb_first_gpio_out_mode(fbuf, bytes, total == static_cast<UINT>(len));});
#endif
    }
#ifdef TC_JTAG_SPI_HZ
    if(!tc_jtag_spi_close())stream_ok=false;
#else
    jtag_exit_gpio_out_mode();
#endif
    }
    taskEXIT_CRITICAL();
#ifdef TC_JTAG_SPI_HZ
    if(!stream_ok)goto load_core_close;
#endif
    if (total != static_cast<UINT>(len) || !tc_transition_call(TcTransitionStep::WriteEnd,[](){return writeSRAM_end();})) {
        overlay_status("Failed to program SRAM");
        goto load_core_close;
    }
#if defined(TC_LAUNCH_TRACE) || !defined(TC_JTAG_SPI_HZ)
    time_total = bflb_mtimer_get_time_us() - time_total;
    DEBUG("Time: total=%lld us, jtag=%lld us, flash=%lld us, writetdi=%lld us", time_total, time_jtag, 
        time_flash, jtag_writetdi_time - writetdi_time_start);
#endif

    // printf("Status after program sram: %x\n", readStatusReg());
    res = true;

load_core_close:
#ifdef TC_JTAG_SPI_HZ
    if(spi_attempted){
        if(!tc_jtag_spi_close())res=false;
#ifdef TC_LAUNCH_TRACE
        if(tc_transition_active){
            auto &record=*tc_transition_active;
            ++record.jtag_spi_attempts;
            record.jtag_spi=tc_jtag_spi_stats();
            if(!record.jtag_spi_first_error)
                record.jtag_spi_first_error=static_cast<unsigned>(record.jtag_spi.error);
        }
#endif
    }
#endif
    f_close(&fcore);

	disable_jtag_pins();
	
    return res;
}

#ifdef TANG_CONSOLE138K
namespace {
struct RecoveryPort {
    const char* path;
    uint32_t uart_pin_config;
    bool short_wait;
    // SET/CLEAR mode uses the SET command to drive high. Sense the pad through
    // the input buffer, while keeping the output enabled and in SET/CLEAR mode.
    static constexpr uint32_t NativeDrive=GPIO_INT_MASK | GPIO_FUNC_SWGPIO | GPIO_OUTPUT_EN |
        GPIO_SCHMITT_EN | GPIO_MODE_SETCLR | GLB_REG_GPIO_0_SET;
    static constexpr uint32_t NativeMask=GLB_REG_GPIO_0_FUNC_SEL_MASK | GLB_REG_GPIO_0_MODE_MASK |
        GPIO_OUTPUT_EN | GPIO_INPUT_EN | GLB_REG_GPIO_0_I;
    static constexpr uint32_t NativeExpected=GPIO_FUNC_SWGPIO | GPIO_MODE_SETCLR |
        GPIO_OUTPUT_EN | GPIO_INPUT_EN | GLB_REG_GPIO_0_I;
#ifdef TC_LAUNCH_TRACE
    TcShortBootTrace *short_trace=nullptr;
#endif
    bool uart_idle() const {
        return bflb_uart_txempty(uart1_dev) &&
            !(getreg32(uart1_dev->reg_base + UART_STATUS_OFFSET) & UART_STS_UTX_BUS_BUSY);
    }
    bool snapshot(Ct2090::Snapshot& out) {
        TcTransitionSection measured(TcTransitionStep::Snapshot);
        enable_jtag_pins();
        if (detectChain(JTAG_MAX_CHAIN) != 1) return false;
        go_test_logic_reset();
        out.id = idCode();
        if (out.id != Ct2090::Device) return false;
        out.status = readStatusReg();
        out.user = readUserCode();
        return true;
    }
    bool reload() {
        TcTransitionSection measured(TcTransitionStep::Reload);
#ifdef TC_LAUNCH_TRACE
        TcReloadTiming *timing=nullptr;
        auto *observation=tc_transition_active ? &tc_transition_active->boot : nullptr;
        if (observation) {
            *observation={};
            observation->start_us=tc_launch_now_us();
            timing=tc_transition_active->reload_timings.begin(short_wait,observation->start_us);
        }
        if (short_trace) short_trace->reload_started=true;
#endif
        go_test_logic_reset();
#ifdef TC_LAUNCH_TRACE
        if (timing) timing->mark(TcReloadBoundary::TapReset,tc_launch_now_us());
        fpgaReset(short_wait,timing);
#else
        fpgaReset(short_wait);
#endif
        const uint64_t wait_start=bflb_mtimer_get_time_us();
#ifdef TC_LAUNCH_TRACE
        if (timing) timing->mark(TcReloadBoundary::WaitStart,wait_start);
        if (observation) observation->idle_end_us=tc_launch_now_us()-observation->start_us;
#endif
        // Clear the legacy logo once the known boot image is identified.
        // A short wait continues polling to verify stability after the logo is cleared.
        TcTransitionSection measured_wait(TcTransitionStep::BootWait);
        bool cleared = false;
        bool logo_timeout=false;
        unsigned ready_count=0;
        bool accepted=false;
        for (unsigned step = 0; step < 50; ++step) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (short_wait && bflb_mtimer_get_time_us()-wait_start>5000000) break;
#ifdef TC_LAUNCH_TRACE
            if (cleared && !short_wait && !observation) continue;
#else
            if (cleared && !short_wait) continue;
#endif
            Ct2090::Snapshot boot{};
            const bool snapshot_ok=snapshot(boot);
            const bool matched=snapshot_ok && boot.id==Ct2090::Device &&
                boot.user==Ct2090::Boot && boot.status==Ct2090::BootStatus;
#ifdef TC_LAUNCH_TRACE
            if (observation) observation->sample(matched,tc_launch_now_us());
#endif
            if (matched && !cleared) {
            auto& select = *reinterpret_cast<volatile uint32_t*>(
                gpio_dev->reg_base + GLB_GPIO_CFG0_OFFSET + 28 * sizeof(uint32_t));
            const uint32_t native = select;
            select = uart_pin_config & ~uint32_t(GLB_REG_GPIO_0_INT_CLR);
            overlay_clear_logo();
            // The last stop bit must leave GPIO28 before native selection.
            const auto started = bflb_mtimer_get_time_us();
            while (!uart_idle()) {
                if (bflb_mtimer_get_time_us() - started >= 20000) {
                    logo_timeout=true;
#ifdef TC_LAUNCH_TRACE
                    if (observation) observation->uart_timeout=true;
#endif
                    break;
                }
            }
            select = short_wait ? NativeDrive | GPIO_INPUT_EN : native;
            bflb_mtimer_delay_us(10);
#ifdef TC_LAUNCH_TRACE
            if (observation && !observation->uart_timeout)
                observation->uart_idle_us=tc_launch_now_us()-observation->start_us;
#endif
            cleared = true;
            }
            if (short_wait) {
                // Read every check independently so the trace identifies simultaneous failures.
                const bool fifo_empty=bflb_uart_txempty(uart1_dev);
                const bool bus_idle=!(getreg32(uart1_dev->reg_base + UART_STATUS_OFFSET) & UART_STS_UTX_BUS_BUSY);
                const auto gpio28=*reinterpret_cast<volatile uint32_t*>(
                    gpio_dev->reg_base + GLB_GPIO_CFG0_OFFSET + 28 * sizeof(uint32_t));
                const bool native=(gpio28 & NativeMask)==NativeExpected;
                const bool ready=matched && !logo_timeout && fifo_empty && bus_idle && native;
                ready_count=ready ? ready_count+1 : 0;
                const auto elapsed=bflb_mtimer_get_time_us()-wait_start;
#ifdef TC_LAUNCH_TRACE
                if (short_trace && short_trace->probe_count<TcShortBootTrace::MaxProbes) {
                    auto &probe=short_trace->probes[short_trace->probe_count++];
                    probe.gpio28=gpio28;
                    probe.elapsed_us=static_cast<uint32_t>(elapsed);
                    probe.mark(TcBootProbe::Snapshot,snapshot_ok);
                    probe.mark(TcBootProbe::Id,boot.id==Ct2090::Device);
                    probe.mark(TcBootProbe::User,snapshot_ok && boot.user==Ct2090::Boot);
                    probe.mark(TcBootProbe::Status,snapshot_ok && boot.status==Ct2090::BootStatus);
                    probe.mark(TcBootProbe::FifoEmpty,fifo_empty);
                    probe.mark(TcBootProbe::BusIdle,bus_idle);
                    probe.mark(TcBootProbe::LogoNoTimeout,!logo_timeout);
                    probe.mark(TcBootProbe::LogoAttempted,cleared);
                    probe.mark(TcBootProbe::Native,native);
                    probe.mark(TcBootProbe::MinimumWait,elapsed>=1000000);
                    probe.mark(TcBootProbe::WithinDeadline,elapsed<=5000000);
                    probe.mark(TcBootProbe::Stable,ready_count>=3);
                }
#endif
                if (elapsed>5000000) break;
                if (elapsed>=1000000 && ready_count>=3) {
                    accepted=true;
                    break;
                }
            }
        }
#ifdef TC_LAUNCH_TRACE
        if (observation) observation->end_us=tc_launch_now_us()-observation->start_us;
        if (timing) {
            timing->wait_end_us=observation->end_us;
            timing->wait_complete=true;
            timing->wait_ok=!short_wait || accepted;
        }
        if (short_trace) {
            short_trace->wait_accepted=accepted;
            short_trace->wait_end_us=static_cast<uint32_t>(bflb_mtimer_get_time_us()-wait_start);
        }
#endif
        return !short_wait || accepted;
    }
    bool program(const Ct2090::Snapshot& expected) {
#ifdef TC_LAUNCH_TRACE
        if (short_trace) short_trace->program_entered=true;
#endif
        return program_image(path, expected);
    }
};
}
#endif

bool fpga_program(const char* fname, bool try_short_boot_wait) {
#ifdef TANG_CONSOLE138K
    static bool monitor_confirmed = false;
    // GPIO28 is also UART TX. Drive native selection in one register write
    // and restore its original function only after the JTAG pins are released.
    auto& select = *reinterpret_cast<volatile uint32_t*>(
        gpio_dev->reg_base + GLB_GPIO_CFG0_OFFSET + 28 * sizeof(uint32_t));
    const uint32_t saved = select;
    const char* base = strrchr(fname, '/');
    const bool monitor = strcmp(base ? base+1 : fname, "monitor.bin") == 0;
    const bool short_wait=monitor && try_short_boot_wait;
#ifdef TC_JTAG_SPI_HZ
    // Observe the native-select pad during every JTAG DMA stream, including launch.
    select = RecoveryPort::NativeDrive | GPIO_INPUT_EN;
#else
    select = RecoveryPort::NativeDrive | (short_wait ? GPIO_INPUT_EN : 0u);
#endif
    bflb_mtimer_delay_us(10);
    RecoveryPort port{fname, saved, short_wait};
#ifdef TC_LAUNCH_TRACE
    if (short_wait && tc_transition_active) {
        port.short_trace=&tc_transition_active->short_boot;
        auto &attempt=*port.short_trace;
        attempt.attempted=true;
        attempt.gpio_before_select=saved;
        attempt.gpio_after_select=select;
        attempt.gpio_mask=RecoveryPort::NativeMask;
        attempt.gpio_expected=RecoveryPort::NativeExpected;
    }
#endif
    const bool ok = Ct2090::configure(port, monitor, monitor_confirmed);
#ifdef TC_LAUNCH_TRACE
    if (port.short_trace) port.short_trace->configured=ok;
#endif
    disable_jtag_pins();
    select = saved & ~uint32_t(GLB_REG_GPIO_0_INT_CLR);
    return ok;
#else
    return program_image(fname);
#endif
}
