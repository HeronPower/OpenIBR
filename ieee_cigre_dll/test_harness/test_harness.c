/*
 * test_harness.c -- standalone EMT host for GFM_Battery_OpenIBR.dll
 *
 * Loads the DLL dynamically, couples it to the host-side AC network
 * (ac_network.c) and converter circuit (converter_sim.c), and records
 * every declared output port -- including extras from
 * debug_signals_config.yaml -- to a binary COMTRADE .cfg/.dat pair.
 * Recording is driven entirely by Model_Info, not hardcoded to this
 * model. The DLL performs no file I/O; see
 * documentation/9_using_the_ieee_cigre_dll.md, section 2.1.
 *
 * No automated pass/fail on controller behavior -- a nonzero exit means
 * the DLL failed to load, initialize, or step. Open the COMTRADE to
 * judge the traces.
 *
 * Usage:
 *   test_harness.exe [path\to\GFM_Battery_OpenIBR.dll] [comtrade_output_base_name]
 * Both default paths are self-locating: the DLL defaults to
 * GFM_Battery_OpenIBR.dll next to this exe, and the COMTRADE base
 * defaults to "comtrade_log" next to the DLL -- so running with no
 * arguments works regardless of invocation directory.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../IEEE_Cigre_DLLInterface.h"
#include "../gfm_battery_app.h"
#include "comtrade_writer.h"
#include "ac_network.h"
#include "converter_sim.h"
#include "inverter_requests.h"

#define TEST_DURATION 30.0

/* The DLL is called at its own 5kHz control rate. The network/converter
 * circuit sims need a much finer step to resolve their fast dynamics, so
 * the main loop below runs at 5kHz on the outside and performs
 * MODEL_SUBSTEPS fine (80kHz) network/converter steps per iteration,
 * holding the DLL's v_bridge/pwm_enable output (zero-order hold) across
 * those fine steps. See documentation/9_using_the_ieee_cigre_dll.md,
 * "Two clocks, one nested inside the other, now on the host". */
#define DT               (1.0 / 80e3)          /* fine model (network/converter) step */
#define MODEL_SUBSTEPS   16                     /* fine model steps per DLL call */
#define APP_STEP_SECONDS (DT * MODEL_SUBSTEPS)  /* 200 us -- the DLL's own 5kHz rate */
#define NUM_STEPS        ((int)(TEST_DURATION / APP_STEP_SECONDS))

typedef const IEEE_Cigre_DLLInterface_Model_Info * (MODEL_CALL *ModelGetInfoFn)(void);
typedef int (MODEL_CALL *ModelVoidFn)(void);
typedef int (MODEL_CALL *ModelInstanceFn)(IEEE_Cigre_DLLInterface_Instance *);

/* Per CIGRE TB958, ExternalInputs/ExternalOutputs are void*, cast by the
 * model's own code to a model-specific struct with one field per declared
 * port in order -- not an array of pointers. A generic host (this
 * harness included) doesn't need to know that struct's field names, only
 * that it's a flat, tightly-packed buffer of `Width` doubles per port, in
 * port-declaration order -- computed here from Model_Info alone. */
static int total_port_width(const IEEE_Cigre_DLLInterface_Signal *ports, int numPorts)
{
    int p, total = 0;
    for (p = 0; p < numPorts; p++)
    {
        total += ports[p].Width;
    }
    return total;
}

/* Cumulative flat-buffer offset (in doubles) of port `portIndex`'s first
 * element. */
static int port_flat_offset(const IEEE_Cigre_DLLInterface_Signal *ports, int portIndex)
{
    int p, offset = 0;
    for (p = 0; p < portIndex; p++)
    {
        offset += ports[p].Width;
    }
    return offset;
}

/* A-B-C for any channel name ending in "[0]"/"[1]"/"[2]" -- every such
 * signal in this project is a 3-phase quantity. Matches by name, not
 * port width, since a debug_signals_config.yaml entry naming one array
 * element arrives as its own width-1 port. */
static const char *phase_letter(const char *name)
{
    static const char *letters[3] = { "A", "B", "C" };
    size_t len = strlen(name);
    if (len >= 3 && name[len - 3] == '[' && name[len - 2] >= '0' && name[len - 2] <= '2' && name[len - 1] == ']')
    {
        return letters[name[len - 2] - '0'];
    }
    return "";
}

/* Flattens Model_Info's output ports (which may have Width > 1, e.g.
 * v_bridge[3]) into one COMTRADE channel per scalar value. Simplification:
 * every channel is treated as analog -- the reconstructed IEEE/CIGRE
 * interface has no per-port digital/analog distinction (everything is
 * declared real64_T), so a boolean-like signal such as pwm_enable just
 * shows up as a 0.0/1.0 analog trace rather than a true digital channel.
 * Extra ports from debug_signals_config.yaml are included automatically.
 * Returns the total channel count; fills *outChannels and *outNameStorage
 * (caller frees both with free()). */
static int build_flat_output_channels(const IEEE_Cigre_DLLInterface_Signal *ports, int numPorts,
                                       ComtradeChannelDef **outChannels, char (**outNameStorage)[64])
{
    int p, e, total = 0, idx;
    ComtradeChannelDef *channels;
    char (*names)[64];

    for (p = 0; p < numPorts; p++)
    {
        total += ports[p].Width;
    }

    channels = (ComtradeChannelDef *)calloc((size_t)total, sizeof(ComtradeChannelDef));
    names    = (char (*)[64])calloc((size_t)total, sizeof(*names));

    idx = 0;
    for (p = 0; p < numPorts; p++)
    {
        for (e = 0; e < ports[p].Width; e++)
        {
            if (ports[p].Width == 1)
            {
                snprintf(names[idx], sizeof(names[idx]), "%s", ports[p].Name);
            }
            else
            {
                snprintf(names[idx], sizeof(names[idx]), "%s[%d]", ports[p].Name, e);
            }
            channels[idx].name = names[idx];
            channels[idx].unit = ports[p].Unit;
            channels[idx].phase = phase_letter(names[idx]);
            channels[idx].kind = COMTRADE_CH_ANALOG;
            idx++;
        }
    }

    *outChannels = channels;
    *outNameStorage = names;
    return total;
}

/* Default DLL path: GFM_Battery_OpenIBR.dll next to this exe itself, not a
 * hardcoded "build\GFM_Battery_OpenIBR.dll" relative to the current working
 * directory. A cwd-relative default only works when invoked from one
 * specific directory (ieee_cigre_dll/) -- running the exe from inside
 * ieee_cigre_dll/build/ itself would otherwise look for the nonexistent
 * build/build/...dll. */
static void default_dll_path(char *outPath, size_t outSize)
{
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    const char *lastSlash;
    const char *lastBackslash;
    const char *cut;

    if (len == 0 || len >= sizeof(exePath))
    {
        snprintf(outPath, outSize, "GFM_Battery_OpenIBR.dll");
        return;
    }

    lastSlash = strrchr(exePath, '/');
    lastBackslash = strrchr(exePath, '\\');
    cut = (lastBackslash && (!lastSlash || lastBackslash > lastSlash)) ? lastBackslash : lastSlash;

    if (cut)
    {
        size_t dirLen = (size_t)(cut - exePath + 1); /* keep the trailing slash */
        if (dirLen > outSize - 1)
        {
            dirLen = outSize - 1;
        }
        memcpy(outPath, exePath, dirLen);
        snprintf(outPath + dirLen, outSize - dirLen, "GFM_Battery_OpenIBR.dll");
    }
    else
    {
        snprintf(outPath, outSize, "GFM_Battery_OpenIBR.dll");
    }
}

/* Default COMTRADE output base: alongside the DLL being tested, not
 * wherever test_harness.exe happens to be invoked from. Without this, a
 * bare relative name like "comtrade_log" lands in the current working
 * directory, which silently changes depending on how the harness is run
 * (e.g. from ieee_cigre_dll/ vs. ieee_cigre_dll/build/) even though the
 * DLL path stays the same. */
static void default_comtrade_base(const char *dllPath, char *outBase, size_t outSize)
{
    const char *lastSlash = strrchr(dllPath, '/');
    const char *lastBackslash = strrchr(dllPath, '\\');
    const char *cut = (lastBackslash && (!lastSlash || lastBackslash > lastSlash)) ? lastBackslash : lastSlash;

    if (cut)
    {
        size_t dirLen = (size_t)(cut - dllPath);
        if (dirLen > outSize - 1)
        {
            dirLen = outSize - 1;
        }
        memcpy(outBase, dllPath, dirLen);
        snprintf(outBase + dirLen, outSize - dirLen, "/comtrade_log");
    }
    else
    {
        snprintf(outBase, outSize, "comtrade_log");
    }
}

int main(int argc, char **argv)
{
    char dllPathDefault[MAX_PATH];
    const char *dllPath;
    char comtradeBaseDefault[MAX_PATH];
    const char *comtradeBase;
    HMODULE hDll;
    ModelGetInfoFn Model_GetInfo_p;
    ModelVoidFn Model_PrintInfo_p;
    ModelInstanceFn Model_FirstCall_p, Model_CheckParameters_p, Model_Initialize_p,
                    Model_Outputs_p, Model_Iterate_p, Model_Terminate_p;

    const IEEE_Cigre_DLLInterface_Model_Info *modelInfo;
    IEEE_Cigre_DLLInterface_Instance instance;
    double *parameters;
    double *inputs, *outputs;
    int numInputValues, numOutputValues;

    ComtradeChannelDef *comtradeChannels;
    char (*comtradeNames)[64];
    double *comtradeFlatValues;
    int comtradeNumDllChannels, comtradeNumChannels;
    ComtradeWriter *comtradeWriter;
    int vAcChannelBase, iAcChannelBase;

    /* v_bridge/pwm_enable are held across fine model substeps, refreshed
     * only once per DLL call (zero-order hold), and used immediately
     * (same fine step) by converter_sim_step() below. */
    double i_ac[3] = { 0.0, 0.0, 0.0 };
    double v_ac[3];
    double v_bridge[3] = { 0.0, 0.0, 0.0 };
    bool   pwm_enable = false;
    double v_dc = 0.0;
    int step;

    if (argc > 1)
    {
        dllPath = argv[1];
    }
    else
    {
        default_dll_path(dllPathDefault, sizeof(dllPathDefault));
        dllPath = dllPathDefault;
    }

    if (argc > 2)
    {
        comtradeBase = argv[2];
    }
    else
    {
        default_comtrade_base(dllPath, comtradeBaseDefault, sizeof(comtradeBaseDefault));
        comtradeBase = comtradeBaseDefault;
    }

    hDll = LoadLibraryA(dllPath);
    if (!hDll)
    {
        fprintf(stderr, "FAIL: could not load '%s' (error %lu)\n", dllPath, GetLastError());
        return 1;
    }

    Model_GetInfo_p        = (ModelGetInfoFn)GetProcAddress(hDll, "Model_GetInfo");
    Model_FirstCall_p       = (ModelInstanceFn)GetProcAddress(hDll, "Model_FirstCall");
    Model_PrintInfo_p       = (ModelVoidFn)GetProcAddress(hDll, "Model_PrintInfo");
    Model_CheckParameters_p = (ModelInstanceFn)GetProcAddress(hDll, "Model_CheckParameters");
    Model_Initialize_p     = (ModelInstanceFn)GetProcAddress(hDll, "Model_Initialize");
    Model_Outputs_p        = (ModelInstanceFn)GetProcAddress(hDll, "Model_Outputs");
    Model_Iterate_p         = (ModelInstanceFn)GetProcAddress(hDll, "Model_Iterate");
    Model_Terminate_p      = (ModelInstanceFn)GetProcAddress(hDll, "Model_Terminate");

    if (!Model_GetInfo_p || !Model_FirstCall_p || !Model_PrintInfo_p ||
        !Model_CheckParameters_p || !Model_Initialize_p || !Model_Outputs_p ||
        !Model_Iterate_p || !Model_Terminate_p)
    {
        fprintf(stderr, "FAIL: GetProcAddress could not resolve one or more of the 8 exports\n");
        FreeLibrary(hDll);
        return 1;
    }

    /* Call sequence per CIGRE TB958 Figure 2: GetInfo -> FirstCall ->
     * PrintInfo -> CheckParameters -> Initialize -> [Outputs each step] ->
     * ... -> Terminate. Model_Iterate is RMS-only and never called here;
     * resolving it above just confirms the export exists. */
    modelInfo = Model_GetInfo_p();
    if (!modelInfo)
    {
        fprintf(stderr, "FAIL: Model_GetInfo returned NULL\n");
        FreeLibrary(hDll);
        return 1;
    }

    printf("Loaded %s (%s)\n", modelInfo->ModelName, modelInfo->ModelVersion);
    printf("  %d input ports, %d output ports, %d parameters, step=%.6g s\n",
           modelInfo->NumInputPorts, modelInfo->NumOutputPorts, modelInfo->NumParameters,
           modelInfo->FixedStepBaseSampleTime);
    printf("Inverter ratings: VllRated=%.1f V, SRated=%.3g VA, FNom=%.1f Hz, IRated=%.1f A, ZBase=%.4f Ohm\n",
           VLL_RATED, S_RATED, F_NOM_RATED, I_RATED, Z_BASE);
    printf("Network ratings:  VLL_NOM=%.1f V, F_NOM=%.1f Hz, NETWORK_S_BASE=%.3g VA\n",
           VLL_NOM, F_NOM, NETWORK_S_BASE);

    memset(&instance, 0, sizeof(instance));
    if (Model_FirstCall_p(&instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_FirstCall returned nonzero\n");
        FreeLibrary(hDll);
        return 1;
    }
    Model_PrintInfo_p();

    parameters = (double *)calloc((size_t)modelInfo->NumParameters, sizeof(double));
    { int i; for (i = 0; i < modelInfo->NumParameters; i++) { parameters[i] = modelInfo->ParametersInfo[i].DefaultValue.Real64_Val; } }

    numInputValues  = total_port_width(modelInfo->InputPortsInfo, modelInfo->NumInputPorts);
    numOutputValues = total_port_width(modelInfo->OutputPortsInfo, modelInfo->NumOutputPorts);
    inputs  = (double *)calloc((size_t)numInputValues, sizeof(double));
    outputs = (double *)calloc((size_t)numOutputValues, sizeof(double));

    comtradeNumDllChannels = build_flat_output_channels(modelInfo->OutputPortsInfo, modelInfo->NumOutputPorts,
                                                          &comtradeChannels, &comtradeNames);

    /* v_ac[3]/i_ac[3] are DLL *inputs*, not output ports, so the generic
     * Model_Info-driven recording above never sees them -- append them as
     * 6 extra, harness-known channels alongside whatever the DLL outputs
     * (v_bridge, pwm_enable, extras). */
    vAcChannelBase = comtradeNumDllChannels;
    iAcChannelBase = comtradeNumDllChannels + 3;
    comtradeNumChannels = comtradeNumDllChannels + 6;

    comtradeChannels = (ComtradeChannelDef *)realloc(comtradeChannels, (size_t)comtradeNumChannels * sizeof(ComtradeChannelDef));
    comtradeNames    = (char (*)[64])realloc(comtradeNames, (size_t)comtradeNumChannels * sizeof(*comtradeNames));

    /* realloc may have moved comtradeNames, so every channel's name
     * pointer (including the ones build_flat_output_channels() already
     * set) must be re-pointed at the (possibly relocated) buffer. */
    { int c; for (c = 0; c < comtradeNumChannels; c++) { comtradeChannels[c].name = comtradeNames[c]; } }

    { int k; for (k = 0; k < 3; k++)
    {
        snprintf(comtradeNames[vAcChannelBase + k], sizeof(comtradeNames[vAcChannelBase + k]), "v_ac[%d]", k);
        comtradeChannels[vAcChannelBase + k].unit = "V";
        comtradeChannels[vAcChannelBase + k].phase = phase_letter(comtradeNames[vAcChannelBase + k]);
        comtradeChannels[vAcChannelBase + k].kind = COMTRADE_CH_ANALOG;

        snprintf(comtradeNames[iAcChannelBase + k], sizeof(comtradeNames[iAcChannelBase + k]), "i_ac[%d]", k);
        /* "Amps", not "A" -- that would be identical to phase A's "ph"
         * field on the same .cfg line and can trip up a viewer's parsing. */
        comtradeChannels[iAcChannelBase + k].unit = "Amps";
        comtradeChannels[iAcChannelBase + k].phase = phase_letter(comtradeNames[iAcChannelBase + k]);
        comtradeChannels[iAcChannelBase + k].kind = COMTRADE_CH_ANALOG;
    } }

    /* Recorded at this harness's own fine (80kHz) step rate, not the
     * DLL's declared 5kHz FixedStepBaseSampleTime -- the DLL's own ports
     * just hold their last value across the fine steps in between calls. */
    comtradeFlatValues = (double *)calloc((size_t)comtradeNumChannels, sizeof(double));
    comtradeWriter = comtrade_writer_open(comtradeBase, modelInfo->ModelName, "test_harness",
                                           F_NOM_RATED, 1.0 / DT,
                                           comtradeChannels, comtradeNumChannels);
    if (comtradeWriter)
    {
        printf("Recording %d DLL output channel(s) + 3 v_ac + 3 i_ac channel(s) to %s.cfg and %s.dat\n",
               comtradeNumDllChannels, comtradeBase, comtradeBase);
    }
    else
    {
        fprintf(stderr, "WARNING: could not open COMTRADE output '%s', continuing without recording\n", comtradeBase);
    }

    instance.Parameters      = parameters;
    instance.ExternalInputs  = inputs;
    instance.ExternalOutputs = outputs;
    instance.SimTool_EMT_RMS_Mode = 1; /* EMT, per TB958 */

    if (Model_CheckParameters_p(&instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_CheckParameters rejected default parameters: %s\n",
                instance.LastErrorMessage ? instance.LastErrorMessage : "(no message)");
        goto cleanup_fail;
    }

    if (Model_Initialize_p(&instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_Initialize returned nonzero\n");
        goto cleanup_fail;
    }

    /* Flat offsets into `inputs`, one per declared input port -- computed
     * once from Model_Info's port order, not hardcoded literals. */
    {
        int offVAc        = port_flat_offset(modelInfo->InputPortsInfo, 0);
        int offIL          = port_flat_offset(modelInfo->InputPortsInfo, 1);
        int offVDc         = port_flat_offset(modelInfo->InputPortsInfo, 2);
        int offPRequest   = port_flat_offset(modelInfo->InputPortsInfo, 3);
        int offQRequest   = port_flat_offset(modelInfo->InputPortsInfo, 4);
        int offEnable     = port_flat_offset(modelInfo->InputPortsInfo, 5);
        int offPLimHigh   = port_flat_offset(modelInfo->InputPortsInfo, 6);
        int offPLimLow    = port_flat_offset(modelInfo->InputPortsInfo, 7);

        /* Outer loop: one iteration per DLL call, at its own 5kHz rate. */
        for (step = 0; step < NUM_STEPS; step++)
        {
            double tApp = step * APP_STEP_SECONDS;
            int sub;

            /* Inner loop: MODEL_SUBSTEPS fine (80kHz) network/converter
             * steps per DLL call. */
            for (sub = 0; sub < MODEL_SUBSTEPS; sub++)
            {
                double t = tApp + sub * DT;

                /* i_ac still holds last fine step's value here -- consumed
                 * as the current injected into the grid. */
                ac_network_step(t, DT, i_ac, v_ac);

                /* v_bridge/pwm_enable hold whatever the DLL last produced
                 * (zero-order hold across the fine steps the app doesn't
                 * run) and are used immediately, this same fine step. */
                converter_sim_step(v_bridge, v_ac, pwm_enable, DT, i_ac, &v_dc);

                if (sub == 0)
                {
                    InverterRequests req = inverter_requests(tApp);

                    instance.Time = tApp;

                    inputs[offVAc + 0] = v_ac[0];
                    inputs[offVAc + 1] = v_ac[1];
                    inputs[offVAc + 2] = v_ac[2];
                    inputs[offIL + 0]  = i_ac[0];
                    inputs[offIL + 1]  = i_ac[1];
                    inputs[offIL + 2]  = i_ac[2];
                    inputs[offVDc]     = v_dc;

                    inputs[offPRequest] = req.P_request;
                    inputs[offQRequest] = req.Q_request;
                    inputs[offEnable]   = req.enable;
                    inputs[offPLimHigh] = req.P_lim_high;
                    inputs[offPLimLow]  = req.P_lim_low;

                    /* Run the DLL once per app cycle (v_ac/i_L/v_dc/dispatch
                     * in, v_bridge/pwm_enable out), using this cycle's first
                     * model substep's measurements. */
                    if (Model_Outputs_p(&instance) != 0)
                    {
                        fprintf(stderr, "FAIL: Model_Outputs returned nonzero at step %d\n", step);
                        goto cleanup_fail;
                    }

                    /* v_bridge is output port 0, pwm_enable is port 1 --
                     * always flat offsets 0-2 and 3. Held for the rest of
                     * this cycle's fine steps. */
                    v_bridge[0] = outputs[0];
                    v_bridge[1] = outputs[1];
                    v_bridge[2] = outputs[2];
                    pwm_enable  = (outputs[3] != 0.0);
                }

                if (comtradeWriter)
                {
                    int k;
                    /* `outputs` holds the DLL's last-produced values whether
                     * or not it was actually called this fine step -- exactly
                     * what was in effect at this instant. */
                    memcpy(comtradeFlatValues, outputs, (size_t)comtradeNumDllChannels * sizeof(double));
                    for (k = 0; k < 3; k++)
                    {
                        comtradeFlatValues[vAcChannelBase + k] = v_ac[k];
                        comtradeFlatValues[iAcChannelBase + k] = i_ac[k];
                    }
                    comtrade_writer_sample(comtradeWriter, t, comtradeFlatValues);
                }
            }

            if (step % 5000 == 0) /* ~once per second of sim time */
            {
                printf("t=%.1f / %.1f s\n", tApp, NUM_STEPS * APP_STEP_SECONDS);
            }
        }
    }

    Model_Terminate_p(&instance);

    printf("Wrote %s.cfg / %s.dat (%d steps, %.1f s)\n",
           comtradeBase, comtradeBase, NUM_STEPS * MODEL_SUBSTEPS, NUM_STEPS * APP_STEP_SECONDS);

    comtrade_writer_close(comtradeWriter);
    free(comtradeChannels);
    free(comtradeNames);
    free(comtradeFlatValues);
    free(inputs);
    free(outputs);
    free(parameters);
    FreeLibrary(hDll);
    return 0;

cleanup_fail:
    comtrade_writer_close(comtradeWriter);
    free(comtradeChannels);
    free(comtradeNames);
    free(comtradeFlatValues);
    free(inputs);
    free(outputs);
    free(parameters);
    FreeLibrary(hDll);
    return 1;
}
