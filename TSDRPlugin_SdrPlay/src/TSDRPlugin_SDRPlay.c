/*#-------------------------------------------------------------------------------
# Copyright (c) 2014 Martin Marinov.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://wworg/licenses/gpl.html
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
	#include "mir_sdr.h"
#else
	#include <mirsdrapi-rsp.h>
#endif

#include <stdint.h>
#include <stdlib.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
/* The legacy SDRplay RSP1/RSP2 "Mirics" API (mir_sdr_api.dll, v2.13) is loaded
 * dynamically so that the plugin builds without the SDRplay SDK and reports a
 * clear error when the API DLL is missing at runtime. The matching header is
 * vendored in src/mir_sdr.h and every mir_sdr_* call below is routed through
 * these function pointers. */
static HMODULE mir_sdr_handle = NULL;
static mir_sdr_Init_t                 API_mir_sdr_Init;
static mir_sdr_Uninit_t               API_mir_sdr_Uninit;
static mir_sdr_ReadPacket_t           API_mir_sdr_ReadPacket;
static mir_sdr_SetRf_t                API_mir_sdr_SetRf;
static mir_sdr_AgcControl_t           API_mir_sdr_AgcControl;
static mir_sdr_RSP_SetGr_t            API_mir_sdr_RSP_SetGr;
static mir_sdr_RSPII_AntennaControl_t API_mir_sdr_RSPII_AntennaControl;
static mir_sdr_GetDevices_t           API_mir_sdr_GetDevices;

#define MIR_SDR_LOAD(fn) do { \
	API_mir_sdr_##fn = (mir_sdr_##fn##_t) GetProcAddress(mir_sdr_handle, "mir_sdr_" #fn); \
	if (API_mir_sdr_##fn == NULL) { \
		FreeLibrary(mir_sdr_handle); \
		mir_sdr_handle = NULL; \
		return 0; \
	} \
} while (0)

static int load_mir_sdr_api(void) {
	if (mir_sdr_handle != NULL) return 1;

	/* Standard search path first: the executable's folder, the current directory,
	 * System32 (where the legacy SDRplay RSP driver installs the API), ... */
	mir_sdr_handle = LoadLibraryA("mir_sdr_api.dll");
	if (mir_sdr_handle == NULL) {
		/* Then look in the folder that holds this plugin DLL. */
		HMODULE self = GetModuleHandleA("TSDRPlugin_SDRPlay.dll");
		if (self != NULL) {
			char plugindir[MAX_PATH];
			const DWORD len = GetModuleFileNameA(self, plugindir, MAX_PATH);
			if (len > 0 && len < MAX_PATH) {
				char * slash = strrchr(plugindir, '\\');
				if (slash == NULL) slash = strrchr(plugindir, '/');
				if (slash != NULL) {
					*slash = '\0';
					strcat(plugindir, "\\mir_sdr_api.dll");
					mir_sdr_handle = LoadLibraryA(plugindir);
				}
			}
		}
	}
	if (mir_sdr_handle == NULL) return 0;

	MIR_SDR_LOAD(Init);
	MIR_SDR_LOAD(Uninit);
	MIR_SDR_LOAD(ReadPacket);
	MIR_SDR_LOAD(SetRf);
	MIR_SDR_LOAD(AgcControl);
	MIR_SDR_LOAD(RSP_SetGr);
	MIR_SDR_LOAD(RSPII_AntennaControl);
	MIR_SDR_LOAD(GetDevices);

	return 1;
}
#undef MIR_SDR_LOAD

/* Route the mir_sdr_* calls (otherwise statically linked) through the runtime API. */
#define mir_sdr_Init                 API_mir_sdr_Init
#define mir_sdr_Uninit               API_mir_sdr_Uninit
#define mir_sdr_ReadPacket           API_mir_sdr_ReadPacket
#define mir_sdr_SetRf                API_mir_sdr_SetRf
#define mir_sdr_AgcControl           API_mir_sdr_AgcControl
#define mir_sdr_RSP_SetGr            API_mir_sdr_RSP_SetGr
#define mir_sdr_RSPII_AntennaControl API_mir_sdr_RSPII_AntennaControl
#define mir_sdr_GetDevices           API_mir_sdr_GetDevices
#endif

#define SAMPLE_RATE (8000000)

volatile int working = 0;

volatile double desiredfreq = 200;
volatile int desiredgainred = 40;

#define SAMPLES_TO_PROCESS_AT_ONCE (200)

int errormsg_code;
char * errormsg;
int errormsg_size = 0;
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
	strcpy(name, "TSDR SDRplay SDR Plugin");
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
	desiredgainred = 20 + (int) ((1.0f - gain) * (59-20));
	if (desiredgainred < 20) desiredgainred = 20;
	else if (desiredgainred > 59) desiredgainred = 59;
	RETURN_OK();
}

int TSDRPLUGIN_API __stdcall tsdrplugin_init(const char * params) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	if (!load_mir_sdr_api())
		RETURN_EXCEPTION(
			"The SDRplay API (mir_sdr_api.dll, v2.13) was not found. Install the legacy SDRplay RSP1/RSP2 driver (it puts mir_sdr_api.dll into System32), or copy mir_sdr_api.dll (64-bit) into the folder you launch TempestSDR from, then try again. See TSDRPlugin_SdrPlay/README for details.",
			TSDR_INCOMPATIBLE_PLUGIN);
#endif
	RETURN_OK();
}

int TSDRPLUGIN_API __stdcall tsdrplugin_readasync(tsdrplugin_readasync_function cb, void *ctx) {
	working = 1;

	int err, sps, grc, rfc, fsc, i, id;
	unsigned int fs;
	unsigned int frame = 0;
	int inited = 0 ;

	double freq = desiredfreq;
	double gainred = desiredgainred;

	mir_sdr_DeviceT devices[1];
	unsigned int num_devices = 0;
	// Seems the API requires enlisting devices first, otherwise throws libusb error on Mac
	mir_sdr_GetDevices(devices, &num_devices, 1);
	if (num_devices == 0) {
		RETURN_EXCEPTION("No devices found", TSDR_CANNOT_OPEN_DEVICE);
	}

	mir_sdr_RSPII_AntennaControl(mir_sdr_RSPII_ANTENNA_A);
	mir_sdr_AgcControl(1, -30, 0, 0, 0, 0, 1);
	err = mir_sdr_Init(gainred, 8, freq / 1000000.0, mir_sdr_BW_8_000, mir_sdr_IF_Zero, &sps);
	if (err != mir_sdr_Success) {
		mir_sdr_Uninit();
		RETURN_EXCEPTION(
			"Can't access the SDRplay RSP. Make sure it is properly plugged in, drivers are installed and the mir_sdr_api.dll is in the executable's folder and try again. Please, refer to the TSDRPlugin_SDRplay readme file for more information.",
			 TSDR_CANNOT_OPEN_DEVICE);
	} else
		inited = 1;

	short * xi = (short *)malloc(sps * SAMPLES_TO_PROCESS_AT_ONCE * sizeof(short));
	short * xq = (short *)malloc(sps * SAMPLES_TO_PROCESS_AT_ONCE * sizeof(short));

	int outbufsize =  2 * sps * SAMPLES_TO_PROCESS_AT_ONCE;
	float * outbuf = (float *) malloc(sizeof(float) * outbufsize);

	while (working) {
		unsigned int dropped = 0;

		for (id = 0; id < SAMPLES_TO_PROCESS_AT_ONCE; id++) {
			err = mir_sdr_ReadPacket(&xi[id*sps], &xq[id*sps], &fs, &grc, &rfc, &fsc);
			if (fs > frame)
				dropped += fs - frame;
//			else if (fs < frame) {
//				// wrapped around
//				dropped += 2147483647 - frame;
//				dropped += fs;
//			}
			frame = fs + sps;
			if (err != 0) break;
		}

		// if desired frequency is different and valid
		if (err == 0 && freq != desiredfreq && !(desiredfreq < 60e6 || (desiredfreq > 245e6 && desiredfreq < 420e6) || desiredfreq > 1000e6)) {

			err = mir_sdr_SetRf(desiredfreq, 1, 0);
			if (err == 0)
				freq = desiredfreq; // if OK, frequency has been changed
			else if (err == mir_sdr_RfUpdateError) {
				// if an error occurs, try to reset the device
				if (mir_sdr_Uninit() == 0) inited = 0;
				gainred = desiredgainred;
				if ((err = mir_sdr_Init(gainred, 8, desiredfreq / 1000000.0, mir_sdr_BW_8_000, mir_sdr_IF_Zero, &sps))==0) inited = 1;
				if (err == mir_sdr_OutOfRange) {
					// if the desired frequency is outside operational range, go back to the working frequency
					if (mir_sdr_Uninit() == 0) inited = 0;
					if (mir_sdr_Init(gainred, 8, freq / 1000000.0, mir_sdr_BW_8_000, mir_sdr_IF_Zero, &sps) == 0) inited = 1;
					desiredfreq = freq;
					err = 0;
				} else if (err == 0)
					freq = desiredfreq;
			} else if (err == mir_sdr_OutOfRange) {
				mir_sdr_SetRf(freq, 1, 0);
				desiredfreq = freq;
				err = 0;
			}
		}

		if (err == 0 && gainred != desiredgainred) {
			gainred = desiredgainred;
			mir_sdr_RSP_SetGr(gainred, 3, 1, 0);
		}

		if (err == 0)
		for (i = 0; i < outbufsize; i++) {
			const short val = (i & 1) ? (xq[i >> 1]) : (xi[i >> 1]);
			outbuf[i] = val / 32767.0;
		}

		if (err != 0)
			{ working = 0; break; }
		else
			cb(outbuf, outbufsize, ctx, dropped);
	}



	free(outbuf);
	free(xi);
	free(xq);

	if (inited && mir_sdr_Uninit() != 0) RETURN_EXCEPTION("Cannot properly close the SDRplay RSP. If you experience any further issues, please disconnect it and reconnect it again.", TSDR_CANNOT_OPEN_DEVICE);

	RETURN_EXCEPTION("SDRplay RSP dongle stopped responding.", (err == 0) ? TSDR_OK : TSDR_ERR_PLUGIN);
}

void TSDRPLUGIN_API __stdcall tsdrplugin_cleanup(void) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
	if (mir_sdr_handle != NULL) {
		FreeLibrary(mir_sdr_handle);
		mir_sdr_handle = NULL;
	}
#endif
}
