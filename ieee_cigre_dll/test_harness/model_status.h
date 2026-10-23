/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENIBR_MODEL_STATUS_H
#define OPENIBR_MODEL_STATUS_H

#include <stdio.h>
#include "../IEEE_Cigre_DLLInterface.h"

static int model_status_ok(const char *name, int status,
                           const IEEE_Cigre_DLLInterface_Instance *instance)
{
    const char *message;

    switch (status)
    {
    case IEEE_Cigre_DLLInterface_Return_OK:
        return 1;
    case IEEE_Cigre_DLLInterface_Return_Message:
        message = instance ? instance->LastGeneralMessage : NULL;
        fprintf(stderr, "MESSAGE: %s: %s\n", name, message ? message : "(no message)");
        return 1;
    case IEEE_Cigre_DLLInterface_Return_Error:
        message = instance ? instance->LastErrorMessage : NULL;
        fprintf(stderr, "FAIL: %s: %s\n", name, message ? message : "(no message)");
        return 0;
    default:
        fprintf(stderr, "FAIL: %s returned unknown status %d\n", name, status);
        return 0;
    }
}

#endif
