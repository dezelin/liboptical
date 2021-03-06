/*
    debug.c - Debug functions implementation
    Copyright (C) 2006  Aleksandar Dezelin <dezelin@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "debug.h"
#include "errors.h"
#include "helpers.h"
#include "types.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>


#ifdef _DEBUG

/*
 * Module globals
 */

static char *_log_file = "optical.log";

/*
 * Debug functions
 */

RESULT optcl_debug_set_log_file(char filename[])
{
    _log_file = filename;
    return(SUCCESS);
}

RESULT optcl_debug_log_bytes(const char message[],
                             const uint8_t data[],
                             uint32_t size)
{
    uint32_t i;
    FILE *stream;
    errno_t error;

    assert(message != 0);
    if (message == 0)
        return E_INVALIDARG;

    if (_log_file == 0)
        stream = stderr;
    else {
#ifdef _WIN32
        error = fopen_s(&stream, _log_file, "a");
#else
        stream = fopen(_log_file, "a");
        error = errno;
#endif

        if (stream == 0)
            return MAKE_ERRORCODE(SEVERITY_ERROR, FACILITY_GENERAL, error);
    }

    fprintf(stream, "%s\r\n\n", message);

    if (data != 0 && size > 0) {
        for (i = 0; i < size; ++i) {
            fprintf(stream, "%x", data[i]);

            if (i < size - 1)
                fprintf(stream, "%s", ", ");
        }
    } else
        fprintf(stream, "%s", "null");

    fprintf(stream, "%s", "\r\n\n");
    fclose(stream);

    return SUCCESS;
}

#endif /* _DEBUG */
