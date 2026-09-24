/*
#-------------------------------------------------------------------------------
# Copyright (c) 2014 Martin Marinov.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the GNU Public License v3.0
# which accompanies this distribution, and is available at
# http://www.gnu.org/licenses/gpl.html
# 
# Contributors:
#     Martin Marinov - initial API and implementation
#-------------------------------------------------------------------------------
*/
#ifndef ERRORS_H_
#define ERRORS_H_

#include "TSDRCodes.h"
#include <cstring>
#include <cstdlib>

extern "C" {

static int errormsg_code = TSDR_OK;
static char *errormsg = NULL;
static size_t errormsg_size = 0;
#define RETURN_EXCEPTION(message, status) {announceexception(message, status); return status;}
#define RETURN_OK() {errormsg_code = TSDR_OK; return TSDR_OK;}

static inline void announceexception(const char * message, int status) {

	errormsg_code = status;
	if (status == TSDR_OK) return;

	if (message == NULL) message = "Unknown error";
	const size_t length = std::strlen(message);
	if (errormsg == NULL || length > errormsg_size) {
		char *new_errormsg = static_cast<char *>(std::realloc(errormsg, length + 1));
		if (new_errormsg == NULL) return;
		errormsg = new_errormsg;
		errormsg_size = length;
	}

	std::memcpy(errormsg, message, length + 1);
}

}

EXTERNC TSDRPLUGIN_API char * __stdcall tsdrplugin_getlasterrortext(void) {
	if (errormsg_code == TSDR_OK)
		return NULL;
	else
		return errormsg;
}

#endif
