/*
 * gfm_battery_dll.c
 *
 * IEEE/CIGRE real-code DLL wrapper around the OpenIBR grid-forming battery
 * controller (gfm_battery_app.c).
 *
 * SINGLE-INSTANCE LIMITATION: app_task_init/app_task_5kHz operate on
 * file-scope globals inside the C library, correct for embedded firmware
 * (one instance per MCU) but unsafe for two DLL instances in the same
 * process -- see documentation/9_using_the_ieee_cigre_dll.md,
 * "Multi-Instance and Snapshot Support".
 */

#define GFM_BATTERY_DLL_EXPORTS
#include "IEEE_Cigre_DLLInterface.h"

#include "gfm_battery_app.h"
#include "dll_config_generated.h"

#include "logging/signal_catalog.h"
#include "logging/signal_catalog_autogen.h"
#include "logging/extra_outputs_autogen.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Signal interface                                                    */
/* ------------------------------------------------------------------ */

enum { NUM_INPUT_PORTS = 8, NUM_OUTPUT_PORTS = 2, NUM_PARAMETERS = 3 };

static IEEE_Cigre_DLLInterface_Signal s_inputPorts[NUM_INPUT_PORTS] =
{
    { "v_ac",        "3-phase AC terminal voltage",                    "V",   IEEE_Cigre_DLLInterface_DataType_real64_T, 3 },
    { "i_L",         "Measured converter output (inductor) current",   "A",   IEEE_Cigre_DLLInterface_DataType_real64_T, 3 },
    { "v_dc",        "DC bus voltage",                                 "V",   IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    { "P_request",   "Active power dispatch request",                  "W",   IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    { "Q_request",   "Reactive power dispatch request",                "VAr", IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    { "enable",      "Controller enable gate",                         "",    IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    { "P_lim_high",  "Upper active power limit",                       "W",   IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    { "P_lim_low",   "Lower active power limit",                       "W",   IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
};

/* The standard v_bridge/pwm_enable outputs, plus a fixed, compile-time set
 * of internal troubleshooting signals declared in
 * ieee_cigre_dll/debug_signals_config.yaml (see
 * logging/generate_extra_outputs.py). Any host can record these like any
 * other declared output port using its own native tools -- the DLL
 * itself performs no file I/O. */
static IEEE_Cigre_DLLInterface_Signal s_outputPorts[NUM_OUTPUT_PORTS + NUM_EXTRA_OUTPUT_PORTS] =
{
    { "v_bridge",    "Commanded 3-phase bridge voltage", "V", IEEE_Cigre_DLLInterface_DataType_real64_T, 3 },
    { "pwm_enable",  "PWM/bridge enable gate",           "",  IEEE_Cigre_DLLInterface_DataType_real64_T, 1 },
    EXTRA_OUTPUT_PORT_ROWS
};

static const int s_extraOutputCatalogIndex[NUM_EXTRA_OUTPUT_PORTS] = EXTRA_OUTPUT_CATALOG_INDICES;

/* Exposed control parameters -- host-configurable, default values from
 * dll_config.yaml (see dll_config_generated.h). Min/max are sanity bounds;
 * see Model_CheckParameters. Nameplate ratings and circuitControls tuning
 * are compiled-in only -- see gfm_battery_app.h and
 * documentation/9_using_the_ieee_cigre_dll.md, "Configuration tiers". */
static IEEE_Cigre_DLLInterface_Parameter s_parameters[NUM_PARAMETERS] =
{
    { "H",      "Control", "Inertia constant",           "sec", IEEE_Cigre_DLLInterface_DataType_real64_T, 0,
      { .Real64_Val = H_DEFAULT },      { .Real64_Val = 0.1 }, { .Real64_Val = 20.0 } },
    { "D",      "Control", "Damping gain",                "pu",  IEEE_Cigre_DLLInterface_DataType_real64_T, 0,
      { .Real64_Val = D_DEFAULT },      { .Real64_Val = 0.0 }, { .Real64_Val = 10.0 } },
    { "kDroop", "Control", "Active power droop gain",     "pu",  IEEE_Cigre_DLLInterface_DataType_real64_T, 0,
      { .Real64_Val = KDROOP_DEFAULT }, { .Real64_Val = 0.0 }, { .Real64_Val = 10.0 } },
};

/* Single-rate: called at the app's own 5kHz control rate. The host owns
 * the converter circuit sim and supplies i_L/v_dc as inputs each call --
 * see documentation/9_using_the_ieee_cigre_dll.md, "Two clocks, one
 * nested inside the other, now on the host". */
#define GFM_BATTERY_STEP_SECONDS 200e-6  /* 5 kHz -- the app's real rate */

/* Per CIGRE TB958 Appendix D/E: ExternalInputs/ExternalOutputs are void*,
 * cast by the model's own code to a model-specific struct with one field
 * per declared port, in declared order -- not an array of pointers. */
typedef struct
{
    double v_ac[3];
    double i_L[3];
    double v_dc;
    double P_request;
    double Q_request;
    double enable;
    double P_lim_high;
    double P_lim_low;
} GFM_Battery_Inputs;

typedef struct
{
    double v_bridge[3];
    double pwm_enable;
    double extra[NUM_EXTRA_OUTPUT_PORTS];
} GFM_Battery_Outputs;

typedef struct
{
    double H;
    double D;
    double kDroop;
} GFM_Battery_Parameters;

/* ------------------------------------------------------------------ */
/* Model_Info -- static, compile-time, returned by address             */
/* ------------------------------------------------------------------ */

static const IEEE_Cigre_DLLInterface_Model_Info s_modelInfo =
{
    .DLLInterfaceVersion  = { 1, 1, 0, 0 },
    .ModelName            = "GFM_Battery_OpenIBR",
    .ModelVersion         = "0.1.0-single-instance",
    .ModelDescription     = "OpenIBR grid-forming battery inverter controller",
    .GeneralInformation   =
        "Single-instance IEEE/CIGRE real-code DLL. Do not load more than one "
        "instance of this DLL in a simulation -- see gfm_battery_dll.c.",
    .ModelCreated         = "",
    .ModelCreator         = "OpenIBR",
    .ModelLastModifiedDate = "",
    .ModelLastModifiedBy   = "",
    .ModelModifiedComment  = "",
    .ModelModifiedHistory  = "",
    .FixedStepBaseSampleTime = GFM_BATTERY_STEP_SECONDS,
    .EMT_RMS_Mode         = 1, /* EMT, per TB958: EMT=1, RMS=2, EMT&RMS=3, otherwise=0 */

    .NumInputPorts  = NUM_INPUT_PORTS,
    .InputPortsInfo = s_inputPorts,

    .NumOutputPorts  = NUM_OUTPUT_PORTS + NUM_EXTRA_OUTPUT_PORTS,
    .OutputPortsInfo = s_outputPorts,

    .NumParameters  = NUM_PARAMETERS,
    .ParametersInfo = s_parameters,

    .NumIntStates    = 0,
    .NumFloatStates  = 0,
    .NumDoubleStates = 0,  /* would be nonzero under a multi-instance design; see
                            * documentation/9_using_the_ieee_cigre_dll.md,
                            * "Multi-Instance and Snapshot Support" */
};

/* ------------------------------------------------------------------ */
/* Model_GetInfo                                                       */
/* ------------------------------------------------------------------ */

const IEEE_Cigre_DLLInterface_Model_Info * MODEL_CALL Model_GetInfo(void)
{
    return &s_modelInfo;
}

/* ------------------------------------------------------------------ */
/* Model_FirstCall                                                      */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_FirstCall(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    /* No malloc, license check, or DLL-load needs -- nothing to do. */
    (void)Instance;
    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_PrintInfo                                                     */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_PrintInfo(void)
{
    printf("GFM_Battery_OpenIBR: OpenIBR grid-forming battery inverter controller\n");
    printf("  Single-instance build -- see gfm_battery_dll.c\n");
    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_CheckParameters                                               */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_CheckParameters(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    GFM_Battery_Parameters *p;
    int i;
    const double *values;

    if (!Instance || !Instance->Parameters)
    {
        return IEEE_Cigre_DLLInterface_Return_Error;
    }

    p = (GFM_Battery_Parameters *)Instance->Parameters;
    values = (const double *)p;

    for (i = 0; i < NUM_PARAMETERS; i++)
    {
        if (values[i] < s_parameters[i].MinValue.Real64_Val ||
            values[i] > s_parameters[i].MaxValue.Real64_Val)
        {
            Instance->LastErrorMessage = "Parameter out of range";
            return IEEE_Cigre_DLLInterface_Return_Error;
        }
    }

    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_Initialize                                                    */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_Initialize(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    GFM_Battery_Parameters *p;

    if (!Instance || !Instance->Parameters)
    {
        return IEEE_Cigre_DLLInterface_Return_Error;
    }

    p = (GFM_Battery_Parameters *)Instance->Parameters;
    app_task_init(p->H, p->D, p->kDroop);
    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_Outputs -- the per-timestep entry point                       */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_Outputs(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    GFM_Battery_Inputs  *in;
    GFM_Battery_Outputs *out;
    double v_bridge[3];
    bool   pwm_enable;
    int k;

    if (!Instance || !Instance->ExternalInputs || !Instance->ExternalOutputs)
    {
        return IEEE_Cigre_DLLInterface_Return_Error;
    }

    in  = (GFM_Battery_Inputs *)Instance->ExternalInputs;
    out = (GFM_Battery_Outputs *)Instance->ExternalOutputs;

    app_task_5kHz(v_bridge, &pwm_enable, in->v_ac, in->i_L, in->v_dc,
                  in->P_request, in->Q_request, in->enable != 0.0,
                  in->P_lim_high, in->P_lim_low);

    out->v_bridge[0] = v_bridge[0];
    out->v_bridge[1] = v_bridge[1];
    out->v_bridge[2] = v_bridge[2];
    out->pwm_enable  = pwm_enable ? 1.0 : 0.0;

    for (k = 0; k < NUM_EXTRA_OUTPUT_PORTS; k++)
    {
        out->extra[k] = signal_catalog_read(&g_signalCatalog[s_extraOutputCatalogIndex[k]]);
    }

    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_Iterate                                                       */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_Iterate(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    /* Per TB958: RMS-only (approximates fast control action between
     * Model_Outputs calls when the RMS solver's step is coarser than this
     * model's own rate), "reserved for future use." Not called by this
     * project's EMT-only test_harness.c. */
    (void)Instance;
    return IEEE_Cigre_DLLInterface_Return_OK;
}

/* ------------------------------------------------------------------ */
/* Model_Terminate                                                      */
/* ------------------------------------------------------------------ */

int MODEL_CALL Model_Terminate(IEEE_Cigre_DLLInterface_Instance *Instance)
{
    /* No controller-state heap allocation in the single-instance build.
     * See documentation/9_using_the_ieee_cigre_dll.md,
     * "Multi-Instance and Snapshot Support". */
    (void)Instance;
    return IEEE_Cigre_DLLInterface_Return_OK;
}
