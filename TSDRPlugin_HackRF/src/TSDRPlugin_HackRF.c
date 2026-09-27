/*#-------------------------------------------------------------------------------
# Copyright (c) 2014 Martin Marinov.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html
#
# Contributors:
#     Martin Marinov - initial API and implementation
#------------------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "TSDRPlugin.h"
#include "TSDRCodes.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	#include <windows.h>
#else
	#include <time.h>
	#include <libhackrf/hackrf.h>
	/* The official header names the list struct hackrf_device_list_t; alias it so
	 * the shared code below can use the same name on every platform. */
	typedef hackrf_device_list_t hackrf_devlist_t;
#endif

#include <stdint.h>
#include <stdlib.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
/* The HackRF One API (libhackrf) is loaded dynamically from hackrf.dll so that the
 * plugin builds without the Great Scott Gadgets SDK and reports a clear error
 * when the DLL is missing at runtime. The structs and prototypes below mirror the
 * official libhackrf 2026.01.x header (hackrf.h) ABI for the subset of functions
 * that this plugin uses. Every hackrf_* call in this file is routed through the
 * API_hackrf_* function pointers via the #defines at the bottom of this block. */

typedef struct hackrf_device hackrf_device;

/* Mirror of libhackrf's hackrf_transfer (as delivered to the RX callback). */
typedef struct {
	hackrf_device * device;
	uint8_t * buffer;
	int buffer_length;
	int valid_length;
	void * rx_ctx;
	void * tx_ctx;
} hackrf_transfer;

typedef int (*hackrf_sample_block_cb_fn)(hackrf_transfer * transfer);

/* Mirror of libhackrf's hackrf_device_list. Only "devicecount" is used here,
 * but the layout must match so the pointer arithmetic is correct. */
typedef struct {
	char ** serial_numbers;
	int * usb_board_ids;
	int * usb_device_index;
	int devicecount;
	void ** usb_devices;
	int usb_devicecount;
} hackrf_devlist_t;

typedef int (*hackrf_init_t)(void);
typedef int (*hackrf_exit_t)(void);
typedef int (*hackrf_open_t)(hackrf_device ** device);
typedef int (*hackrf_close_t)(hackrf_device * device);
typedef hackrf_devlist_t * (*hackrf_device_list_t)(void);
typedef int (*hackrf_device_list_open_t)(hackrf_devlist_t * list, int idx, hackrf_device ** device);
typedef void (*hackrf_device_list_free_t)(hackrf_devlist_t * list);
typedef int (*hackrf_set_sample_rate_t)(hackrf_device * device, const double freq_hz);
typedef int (*hackrf_set_freq_t)(hackrf_device * device, const uint64_t freq_hz);
typedef uint32_t (*hackrf_compute_baseband_filter_bw_round_down_lt_t)(const uint32_t bandwidth_hz);
typedef int (*hackrf_set_baseband_filter_bandwidth_t)(hackrf_device * device, const uint32_t bandwidth_hz);
typedef int (*hackrf_set_lna_gain_t)(hackrf_device * device, uint32_t value);
typedef int (*hackrf_set_vga_gain_t)(hackrf_device * device, uint32_t value);
typedef int (*hackrf_set_amp_enable_t)(hackrf_device * device, const uint8_t value);
typedef int (*hackrf_start_rx_t)(hackrf_device * device, hackrf_sample_block_cb_fn callback, void * rx_ctx);
typedef int (*hackrf_stop_rx_t)(hackrf_device * device);
typedef int (*hackrf_is_streaming_t)(hackrf_device * device);

static HMODULE hackrf_handle = NULL;
static hackrf_init_t API_hackrf_init;
static hackrf_exit_t API_hackrf_exit;
static hackrf_open_t API_hackrf_open;
static hackrf_close_t API_hackrf_close;
static hackrf_device_list_t API_hackrf_device_list;
static hackrf_device_list_open_t API_hackrf_device_list_open;
static hackrf_device_list_free_t API_hackrf_device_list_free;
static hackrf_set_sample_rate_t API_hackrf_set_sample_rate;
static hackrf_set_freq_t API_hackrf_set_freq;
static hackrf_compute_baseband_filter_bw_round_down_lt_t API_hackrf_compute_baseband_filter_bw_round_down_lt;
static hackrf_set_baseband_filter_bandwidth_t API_hackrf_set_baseband_filter_bandwidth;
static hackrf_set_lna_gain_t API_hackrf_set_lna_gain;
static hackrf_set_vga_gain_t API_hackrf_set_vga_gain;
static hackrf_set_amp_enable_t API_hackrf_set_amp_enable;
static hackrf_start_rx_t API_hackrf_start_rx;
static hackrf_stop_rx_t API_hackrf_stop_rx;
static hackrf_is_streaming_t API_hackrf_is_streaming;

#define HACKRF_LOAD(fn) do { \
	API_hackrf_##fn = (hackrf_##fn##_t) GetProcAddress(hackrf_handle, "hackrf_" #fn); \
	if (API_hackrf_##fn == NULL) { \
		FreeLibrary(hackrf_handle); \
		hackrf_handle = NULL; \
		return 0; \
	} \
} while (0)

static int load_hackrf_api(void) {
	if (hackrf_handle != NULL) return 1;

	/* Standard search path first: the executable's folder, System32, PATH, the
	 * current directory (i.e. the folder TempestSDR is launched from), ... */
	hackrf_handle = LoadLibraryA("hackrf.dll");
	if (hackrf_handle == NULL) {
		/* Then look in the folder that holds this plugin DLL. */
		HMODULE self = GetModuleHandleA("TSDRPlugin_HackRF.dll");
		if (self != NULL) {
			char plugindir[MAX_PATH];
			const DWORD len = GetModuleFileNameA(self, plugindir, MAX_PATH);
			if (len > 0 && len < MAX_PATH) {
				char * slash = strrchr(plugindir, '\\');
				if (slash == NULL) slash = strrchr(plugindir, '/');
				if (slash != NULL) {
					*slash = '\0';
					strcat(plugindir, "\\hackrf.dll");
					hackrf_handle = LoadLibraryA(plugindir);
				}
			}
		}
	}
	if (hackrf_handle == NULL) {
		/* Finally try %TEMP%: the Java GUI extracts this plugin and its
		 * companion DLLs (hackrf.dll) into the system temp folder, so this
		 * works regardless of which folder TempestSDR was launched from. */
		char tempdir[MAX_PATH];
		const DWORD tlen = GetEnvironmentVariableA("TEMP", tempdir, MAX_PATH);
		if (tlen > 0 && tlen < MAX_PATH) {
			if (tempdir[tlen-1] != '\\') strcat(tempdir, "\\");
			strcat(tempdir, "hackrf.dll");
			hackrf_handle = LoadLibraryA(tempdir);
		}
	}
	if (hackrf_handle == NULL) return 0;

	HACKRF_LOAD(init);
	HACKRF_LOAD(exit);
	HACKRF_LOAD(open);
	HACKRF_LOAD(close);
	HACKRF_LOAD(device_list);
	HACKRF_LOAD(device_list_open);
	HACKRF_LOAD(device_list_free);
	HACKRF_LOAD(set_sample_rate);
	HACKRF_LOAD(set_freq);
	HACKRF_LOAD(compute_baseband_filter_bw_round_down_lt);
	HACKRF_LOAD(set_baseband_filter_bandwidth);
	HACKRF_LOAD(set_lna_gain);
	HACKRF_LOAD(set_vga_gain);
	HACKRF_LOAD(set_amp_enable);
	HACKRF_LOAD(start_rx);
	HACKRF_LOAD(stop_rx);
	HACKRF_LOAD(is_streaming);

	return 1;
}
#undef HACKRF_LOAD

/* Route the hackrf_* calls (otherwise statically linked on Linux/Mac) through the
 * runtime API on Windows. */
#define hackrf_init                 API_hackrf_init
#define hackrf_exit                 API_hackrf_exit
#define hackrf_open                 API_hackrf_open
#define hackrf_close                API_hackrf_close
#define hackrf_device_list          API_hackrf_device_list
#define hackrf_device_list_open     API_hackrf_device_list_open
#define hackrf_device_list_free     API_hackrf_device_list_free
#define hackrf_set_sample_rate      API_hackrf_set_sample_rate
#define hackrf_set_freq             API_hackrf_set_freq
#define hackrf_compute_baseband_filter_bw_round_down_lt API_hackrf_compute_baseband_filter_bw_round_down_lt
#define hackrf_set_baseband_filter_bandwidth API_hackrf_set_baseband_filter_bandwidth
#define hackrf_set_lna_gain         API_hackrf_set_lna_gain
#define hackrf_set_vga_gain         API_hackrf_set_vga_gain
#define hackrf_set_amp_enable       API_hackrf_set_amp_enable
#define hackrf_start_rx             API_hackrf_start_rx
#define hackrf_stop_rx              API_hackrf_stop_rx
#define hackrf_is_streaming         API_hackrf_is_streaming
#endif

#define SAMPLE_RATE (8000000)
#define HACKRF_SUCCESS (0)

volatile int working = 0;

/* Everything is in Hz (the GUI and the TSDR core talk in Hz). */
volatile uint64_t desiredfreq = 200000000;
volatile int desiredlna = 8;   /* HackRF One LNA: 0..40 dB, step 8 */
volatile int desiredvga = 20;  /* HackRF One VGA: 0..62 dB, step 2 */
volatile int desiredamp = 0;   /* RF amplifier: 0 or 1 */

static tsdrplugin_readasync_function tsdr_cb = NULL;
static void * tsdr_ctx = NULL;

static float * rxbuf = NULL;
static int rxbuf_items = 0;

static int errormsg_code;
static char * errormsg;
static int errormsg_size = 0;
#define RETURN_EXCEPTION(message, status) {announceexception(message, status); return status;}
#define RETURN_OK() {errormsg_code = TSDR_OK; return TSDR_OK;}

static inline void announceexception(const char * message, int status) {
	errormsg_code = status;
	if (status == TSDR_OK) return;

	const int length = strlen(message);
	if (errormsg_size == 0) {
			errormsg_size = length;
			errormsg = (char *) malloc(length+1);
		} else if (length > errormsg_size) {
			errormsg_size = length;
			errormsg = (char *) realloc((void*) errormsg, length+1);
		}
	strcpy(errormsg, message);
}

char TSDRPLUGIN_API __stdcall * tsdrplugin_getlasterrortext(void) {
	if (errormsg_code == TSDR_OK)
		return NULL;
	else
		return errormsg;
}

void TSDRPLUGIN_API __stdcall tsdrplugin_getName(char * name) {
	strcpy(name, "TSDR HackRF SDR Plugin");
}

uint32_t TSDRPLUGIN_API __stdcall tsdrplugin_setsamplerate(uint32_t rate) {
	return SAMPLE_RATE;
}

uint32_t TSDRPLUGIN_API __stdcall tsdrplugin_getsamplerate() {
	return SAMPLE_RATE;
}

int TSDRPLUGIN_API __stdcall tsdrplugin_setbasefreq(uint32_t freq) {
	desiredfreq = freq;
	RETURN_OK();
}

int TSDRPLUGIN_API __stdcall tsdrplugin_stop(void) {
	working = 0;
	RETURN_OK();
}

int TSDRPLUGIN_API __stdcall tsdrplugin_setgain(float gain) {
	if (gain < 0.0f) gain = 0.0f;
	if (gain > 1.0f) gain = 1.0f;
	desiredamp = 0;
	desiredlna = (gain >= 0.5f) ? 16 : 0;
	desiredvga = (int) (gain * 62.0f);      /* 0..62 */
	desiredvga = (desiredvga / 2) * 2;      /* valid values go in steps of 2 */
	RETURN_OK();
}

int TSDRPLUGIN_API __stdcall tsdrplugin_init(const char * params) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	if (!load_hackrf_api())
		RETURN_EXCEPTION(
			"The HackRF API (hackrf.dll / libhackrf, 64-bit) was not found. The GUI ships hackrf.dll inside the jar and extracts it automatically, so nothing needs to be copied. If you are running from class files (no jar), copy hackrf.dll (x64) next to the plugin, into the launch folder, or into System32. See TSDRPlugin_HackRF/README for details.",
			TSDR_INCOMPATIBLE_PLUGIN);
#endif
	RETURN_OK();
}

/* RX callback: called by libhackrf from the libusb event thread with interleaved
 * int8 I/Q samples. Convert them to interleaved float (-1..1) and push them to
 * the TSDR core, exactly like the other plugins do from their capture threads. */
static int hackrf_rx_callback(hackrf_transfer * transfer) {
	if (transfer == NULL || transfer->buffer == NULL || transfer->valid_length < 2) return 0;

	const int nsamples = transfer->valid_length / 2;
	if (nsamples > rxbuf_items) {
		float * nb = (float *) realloc(rxbuf, sizeof(float) * 2 * nsamples);
		if (nb == NULL) return 0;
		rxbuf = nb;
		rxbuf_items = nsamples;
	}

	const int8_t * src = (const int8_t *) transfer->buffer;
	for (int i = 0; i < nsamples; i++) {
		rxbuf[2*i]   = src[2*i]   / 128.0f;
		rxbuf[2*i+1] = src[2*i+1] / 128.0f;
	}

	if (tsdr_cb != NULL) tsdr_cb(rxbuf, nsamples, tsdr_ctx, 0);
	return 0;
}

static inline void tsdr_plugin_sleep_ms(int ms) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	Sleep(ms);
#else
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
#endif
}

int TSDRPLUGIN_API __stdcall tsdrplugin_readasync(tsdrplugin_readasync_function cb, void *ctx) {
	tsdr_cb = cb;
	tsdr_ctx = ctx;
	working = 1;

	int err;
	int started = 0;
	hackrf_device * dev = NULL;

	err = hackrf_init();
	if (err != HACKRF_SUCCESS)
		RETURN_EXCEPTION(
			"Cannot initialize the HackRF library (hackrf_init failed). Check that hackrf.dll is working and the WinUSB driver is installed. See TSDRPlugin_HackRF/README for details.",
			TSDR_INCOMPATIBLE_PLUGIN);

	hackrf_devlist_t * devlist = hackrf_device_list();
	if (devlist == NULL || devlist->devicecount == 0) {
		if (devlist != NULL) hackrf_device_list_free(devlist);
		hackrf_exit();
		RETURN_EXCEPTION(
			"No HackRF devices found. Connect your HackRF One; with a PortaPack attached, reboot it into HackRF mode first. Make sure the WinUSB driver is installed (see TSDRPlugin_HackRF/README).",
			TSDR_CANNOT_OPEN_DEVICE);
	}

	err = hackrf_device_list_open(devlist, 0, &dev);
	hackrf_device_list_free(devlist);
	if (err != HACKRF_SUCCESS || dev == NULL) {
		hackrf_exit();
		RETURN_EXCEPTION("Cannot open the HackRF device. Unplug and reconnect it, then try again.", TSDR_CANNOT_OPEN_DEVICE);
	}

	/* Configure: fixed 8 MHz sample rate, matching the other TempestSDR plugins,
	 * with the sharpest baseband filter that fits into it. */
	err = hackrf_set_sample_rate(dev, (double) SAMPLE_RATE);
	if (err == HACKRF_SUCCESS)
		err = hackrf_set_baseband_filter_bandwidth(dev, hackrf_compute_baseband_filter_bw_round_down_lt(SAMPLE_RATE));
	if (err == HACKRF_SUCCESS)
		err = hackrf_set_amp_enable(dev, 0);
	if (err == HACKRF_SUCCESS)
		err = hackrf_set_lna_gain(dev, (uint32_t) desiredlna);
	if (err == HACKRF_SUCCESS)
		err = hackrf_set_vga_gain(dev, (uint32_t) desiredvga);
	if (err == HACKRF_SUCCESS)
		err = hackrf_set_freq(dev, desiredfreq);
	if (err != HACKRF_SUCCESS) {
		hackrf_close(dev);
		hackrf_exit();
		RETURN_EXCEPTION("Failed to configure the HackRF device. Unplug and reconnect it, then try again.", TSDR_CANNOT_OPEN_DEVICE);
	}

	err = hackrf_start_rx(dev, hackrf_rx_callback, tsdr_ctx);
	if (err != HACKRF_SUCCESS) {
		hackrf_close(dev);
		hackrf_exit();
		RETURN_EXCEPTION("Failed to start the HackRF stream. Unplug and reconnect it, then try again.", TSDR_CANNOT_OPEN_DEVICE);
	}
	started = 1;

	/* Slow pump loop: watch for frequency/gain changes from the GUI and for the
	 * device disappearing (USB unplug) while the callback keeps streaming. */
	int streaming_ok = 1;
	uint64_t freq = desiredfreq;
	int lna = desiredlna;
	int vga = desiredvga;
	int amp = desiredamp;

	while (working) {
		if (freq != desiredfreq) {
			if (hackrf_set_freq(dev, desiredfreq) == HACKRF_SUCCESS)
				freq = desiredfreq;
			else
				desiredfreq = freq; /* frequency out of range: keep the working one */
		}
		if (lna != desiredlna) { hackrf_set_lna_gain(dev, (uint32_t) desiredlna); lna = desiredlna; }
		if (vga != desiredvga) { hackrf_set_vga_gain(dev, (uint32_t) desiredvga); vga = desiredvga; }
		if (amp != desiredamp) { hackrf_set_amp_enable(dev, (uint8_t) desiredamp); amp = desiredamp; }

		if (!hackrf_is_streaming(dev)) { /* device disconnected or USB error */
			streaming_ok = 0;
			break;
		}

		tsdr_plugin_sleep_ms(50);
	}

	if (started) hackrf_stop_rx(dev);
	hackrf_close(dev);
	hackrf_exit();

	if (rxbuf != NULL) {
		free(rxbuf);
		rxbuf = NULL;
		rxbuf_items = 0;
	}

	RETURN_EXCEPTION("HackRF stopped responding.", streaming_ok ? TSDR_OK : TSDR_ERR_PLUGIN);
}

void TSDRPLUGIN_API __stdcall tsdrplugin_cleanup(void) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	if (hackrf_handle != NULL) {
		FreeLibrary(hackrf_handle);
		hackrf_handle = NULL;
	}
#endif
}