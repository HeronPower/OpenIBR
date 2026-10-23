/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "IEEE_Cigre_DLLInterface.h"
@STATUS_INCLUDE@

static IEEE_Cigre_DLLInterface_Instance instance;
static int callback_status;
static unsigned int callback_calls;
static unsigned int unload_calls;
static int fixture_error;

static int status_callback(IEEE_Cigre_DLLInterface_Instance *received)
{
    if (received != &instance)
    {
        fixture_error = 1;
    }
    callback_calls++;
    return callback_status;
}

static int (*@ENTRY_POINT@_p)(IEEE_Cigre_DLLInterface_Instance *) = status_callback;

@UNLOAD_STUB@

static int dispatch(void)
{
@LOCALS@
@DISPATCH@
    return 0;
@FAIL_LABEL@
}

int main(int argc, char **argv)
{
    unsigned char before[sizeof(instance)];
    const char *poison = (const char *)(uintptr_t)1;
    char *end;
    long parsed;
    int result;
    int unchanged;

    if (argc != 3)
    {
        return 90;
    }
    errno = 0;
    parsed = strtol(argv[1], &end, 10);
    if (errno || *end || end == argv[1] || parsed < INT_MIN || parsed > INT_MAX)
    {
        return 90;
    }
    callback_status = (int)parsed;
    memset(&instance, 0, sizeof(instance));
    instance.Time = 17.0;
    instance.SimTool_EMT_RMS_Mode = 2;
    instance.LastErrorMessage = "error diagnostic";
    instance.LastGeneralMessage = "general diagnostic";

    if (strcmp(argv[2], "valid") == 0)
    {
        /* Both diagnostics are valid and deliberately distinct. */
    }
    else if (strcmp(argv[2], "ok_poison") == 0 && callback_status == 0)
    {
        instance.LastErrorMessage = poison;
        instance.LastGeneralMessage = poison;
    }
    else if (strcmp(argv[2], "message_poison_error") == 0 && callback_status == 1)
    {
        instance.LastErrorMessage = poison;
    }
    else if (strcmp(argv[2], "error_poison_general") == 0 && callback_status == 2)
    {
        instance.LastGeneralMessage = poison;
    }
    else if (strcmp(argv[2], "message_null") == 0 && callback_status == 1)
    {
        instance.LastGeneralMessage = NULL;
        instance.LastErrorMessage = poison;
    }
    else if (strcmp(argv[2], "error_null") == 0 && callback_status == 2)
    {
        instance.LastErrorMessage = NULL;
        instance.LastGeneralMessage = poison;
    }
    else
    {
        return 90;
    }

    memcpy(before, &instance, sizeof(instance));
    result = dispatch();
    unchanged = memcmp(before, &instance, sizeof(instance)) == 0;
    printf("RESULT continued=%d calls=%u unchanged=%d unloads=%u\n",
           result == 0, callback_calls, unchanged, unload_calls);
    if (!unchanged || callback_calls != 1 || fixture_error)
    {
        return 91;
    }
    return result;
}
