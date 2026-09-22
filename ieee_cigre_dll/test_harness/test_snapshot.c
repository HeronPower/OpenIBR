/*
 * test_snapshot.c -- verifies dll_snapshot.c's "memory grab" mechanism
 * actually captures a complete, correct DLL state.
 *
 * Drives an idealized, stiff 60.0 Hz / 0.97 pu voltage source directly
 * (not ac_network.c's weak-grid model) under fixed dispatch, so any
 * settling transient in the recorded PLL frequency signal is purely the
 * DLL's own internal state warming up from zero.
 *
 * Run 1 ("raw"): a fresh DLL instance, stepped for 10.0s -- an integer
 * number of 60Hz cycles, so the voltage waveform's phase at the end
 * matches its phase at t=0. Expect a visible settling transient. A
 * snapshot is captured at the end.
 *
 * Run 2 ("restored"): a second, freshly-loaded copy of the same DLL file
 * (independent .data/.bss, same trick documented for multi-instance),
 * initialized normally, then immediately overwritten with run 1's
 * snapshot before any stepping, then run from t=0 under the identical
 * scenario. Expect it to be flat from step 0.
 *
 * Both runs are also recorded to their own binary COMTRADE pair
 * (snapshot_raw.cfg/.dat, snapshot_restored.cfg/.dat, next to the DLL) so
 * the traces can be inspected directly, not just the printed summary.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "../IEEE_Cigre_DLLInterface.h"
#include "converter_sim.h"
#include "dll_snapshot.h"
#include "comtrade_writer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DT               (1.0 / 80e3)
#define MODEL_SUBSTEPS   16
#define APP_STEP_SECONDS (DT * MODEL_SUBSTEPS)

#define SCENARIO_VLL  (480.0 * 0.97)
#define SCENARIO_FNOM 60.0

typedef const IEEE_Cigre_DLLInterface_Model_Info * (MODEL_CALL *ModelGetInfoFn)(void);
typedef int (MODEL_CALL *ModelVoidFn)(void);
typedef int (MODEL_CALL *ModelInstanceFn)(IEEE_Cigre_DLLInterface_Instance *);

typedef struct
{
    HMODULE hDll;
    ModelGetInfoFn GetInfo;
    ModelVoidFn PrintInfo;
    ModelInstanceFn FirstCall, CheckParameters, Initialize, Outputs, Iterate, Terminate;
    const IEEE_Cigre_DLLInterface_Model_Info *info;
} DllHandle;

static int load_dll(const char *path, DllHandle *h)
{
    memset(h, 0, sizeof(*h));
    h->hDll = LoadLibraryA(path);
    if (!h->hDll)
    {
        fprintf(stderr, "FAIL: could not load '%s' (error %lu)\n", path, GetLastError());
        return -1;
    }
    h->GetInfo         = (ModelGetInfoFn)GetProcAddress(h->hDll, "Model_GetInfo");
    h->FirstCall       = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_FirstCall");
    h->PrintInfo       = (ModelVoidFn)GetProcAddress(h->hDll, "Model_PrintInfo");
    h->CheckParameters = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_CheckParameters");
    h->Initialize      = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_Initialize");
    h->Outputs         = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_Outputs");
    h->Iterate         = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_Iterate");
    h->Terminate       = (ModelInstanceFn)GetProcAddress(h->hDll, "Model_Terminate");
    if (!h->GetInfo || !h->FirstCall || !h->PrintInfo || !h->CheckParameters ||
        !h->Initialize || !h->Outputs || !h->Iterate || !h->Terminate)
    {
        fprintf(stderr, "FAIL: GetProcAddress could not resolve one or more exports in '%s'\n", path);
        return -1;
    }
    h->info = h->GetInfo();
    return 0;
}

static int total_port_width(const IEEE_Cigre_DLLInterface_Signal *ports, int numPorts)
{
    int p, total = 0;
    for (p = 0; p < numPorts; p++)
    {
        total += ports[p].Width;
    }
    return total;
}

static int find_port_offset(const IEEE_Cigre_DLLInterface_Signal *ports, int numPorts, const char *name)
{
    int p, offset = 0;
    for (p = 0; p < numPorts; p++)
    {
        if (strcmp(ports[p].Name, name) == 0)
        {
            return offset;
        }
        offset += ports[p].Width;
    }
    return -1;
}

/* A-B-C for any channel name ending in "[0]"/"[1]"/"[2]" -- same
 * convention as test_harness.c, so viewers color three-phase traces
 * consistently. */
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

/* Flattens Model_Info's output ports into one COMTRADE channel per scalar
 * value -- same approach as test_harness.c's build_flat_output_channels. */
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

/* <dllPath's directory>/<suffix> -- same directory-splitting logic as
 * default_dll_path() below, reused so COMTRADE output lands next to the
 * DLL regardless of invocation directory. */
static void sibling_path(const char *dllPath, const char *suffix, char *outBase, size_t outSize)
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
        snprintf(outBase + dirLen, outSize - dirLen, "/%s", suffix);
    }
    else
    {
        snprintf(outBase, outSize, "%s", suffix);
    }
}

/* Default DLL path: GFM_Battery_OpenIBR.dll next to this exe itself, not
 * a bare filename resolved against the current working directory.
 * LoadLibraryA happens to also search the exe's own directory, so a bare
 * filename works there -- but CopyFileA (needed below, for the "load a
 * second, uniquely-named copy" step) resolves relative paths against the
 * CWD, so a bare default silently breaks as soon as this exe is run from
 * anywhere other than its own directory. Same helper as test_harness.c. */
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
        size_t dirLen = (size_t)(cut - exePath + 1);
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

typedef struct
{
    int vAc, iL, vDc, pReq, qReq, enable, pLimHi, pLimLo, freq;
} PortOffsets;

/* Where a run's samples go: comtrade_writer_sample() gets the DLL's own
 * `outputs` plus the synthesized v_ac/i_ac appended, same layout
 * convention test_harness.c uses for its own harness-known channels. */
typedef struct
{
    ComtradeWriter *writer;
    double *flatValues;   /* [numDllChannels + 6] scratch buffer */
    int numDllChannels;
    int vAcBase, iAcBase;
} ComtradeSink;

/* Steps one DLL instance through the fixed 60Hz/0.97pu, zero-dispatch
 * scenario for numAppSteps 5kHz cycles (each with MODEL_SUBSTEPS fine
 * 80kHz network/converter steps), tracking the min/max of the named PLL
 * frequency signal over the first simulated second, and recording every
 * fine step to `sink` (if non-NULL). */
static void run_pass(DllHandle *h, IEEE_Cigre_DLLInterface_Instance *instance,
                      double *inputs, double *outputs, const PortOffsets *off,
                      int numAppSteps, ComtradeSink *sink,
                      double *minFreq1s, double *maxFreq1s)
{
    double v_bridge[3] = { 0.0, 0.0, 0.0 };
    bool   pwm_enable = false;
    double v_dc = 0.0;
    double i_ac[3] = { 0.0, 0.0, 0.0 };
    int appStep, sub;
    int oneSecondSteps = (int)(1.0 / APP_STEP_SECONDS);

    *minFreq1s = 1e300;
    *maxFreq1s = -1e300;

    for (appStep = 0; appStep < numAppSteps; appStep++)
    {
        double tApp = appStep * APP_STEP_SECONDS;

        for (sub = 0; sub < MODEL_SUBSTEPS; sub++)
        {
            double t = tApp + sub * DT;
            double wt = 2.0 * M_PI * SCENARIO_FNOM * t;
            double v_pk = SCENARIO_VLL * sqrt(2.0 / 3.0);
            double v_ac[3];

            v_ac[0] = v_pk * sin(wt);
            v_ac[1] = v_pk * sin(wt - 2.0 * M_PI / 3.0);
            v_ac[2] = v_pk * sin(wt + 2.0 * M_PI / 3.0);

            converter_sim_step(v_bridge, v_ac, pwm_enable, DT, i_ac, &v_dc);

            if (sub == 0)
            {
                instance->Time = tApp;
                inputs[off->vAc + 0] = v_ac[0];
                inputs[off->vAc + 1] = v_ac[1];
                inputs[off->vAc + 2] = v_ac[2];
                inputs[off->iL + 0]  = i_ac[0];
                inputs[off->iL + 1]  = i_ac[1];
                inputs[off->iL + 2]  = i_ac[2];
                inputs[off->vDc]     = v_dc;
                inputs[off->pReq]    = 0.0;
                inputs[off->qReq]    = 0.0;
                inputs[off->enable]  = 1.0;
                inputs[off->pLimHi]  = 1.0e6;
                inputs[off->pLimLo]  = -1.0e6;

                h->Outputs(instance);

                /* v_bridge is output port 0, pwm_enable is port 1 --
                 * always flat offsets 0-2 and 3. */
                v_bridge[0] = outputs[0];
                v_bridge[1] = outputs[1];
                v_bridge[2] = outputs[2];
                pwm_enable  = (outputs[3] != 0.0);

                if (appStep < oneSecondSteps)
                {
                    double f = outputs[off->freq];
                    if (f < *minFreq1s) *minFreq1s = f;
                    if (f > *maxFreq1s) *maxFreq1s = f;
                }
            }

            if (sink && sink->writer)
            {
                int k;
                memcpy(sink->flatValues, outputs, (size_t)sink->numDllChannels * sizeof(double));
                for (k = 0; k < 3; k++)
                {
                    sink->flatValues[sink->vAcBase + k] = v_ac[k];
                    sink->flatValues[sink->iAcBase + k] = i_ac[k];
                }
                comtrade_writer_sample(sink->writer, t, sink->flatValues);
            }
        }
    }
}

static int init_instance(DllHandle *h, IEEE_Cigre_DLLInterface_Instance *instance,
                          double **params, double **inputs, double **outputs)
{
    int i;

    memset(instance, 0, sizeof(*instance));
    if (h->FirstCall(instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_FirstCall\n");
        return -1;
    }

    *params  = (double *)calloc((size_t)h->info->NumParameters, sizeof(double));
    for (i = 0; i < h->info->NumParameters; i++)
    {
        (*params)[i] = h->info->ParametersInfo[i].DefaultValue.Real64_Val;
    }
    *inputs  = (double *)calloc((size_t)total_port_width(h->info->InputPortsInfo, h->info->NumInputPorts), sizeof(double));
    *outputs = (double *)calloc((size_t)total_port_width(h->info->OutputPortsInfo, h->info->NumOutputPorts), sizeof(double));

    instance->Parameters      = *params;
    instance->ExternalInputs  = *inputs;
    instance->ExternalOutputs = *outputs;
    instance->SimTool_EMT_RMS_Mode = 1;

    if (h->CheckParameters(instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_CheckParameters\n");
        return -1;
    }
    if (h->Initialize(instance) != 0)
    {
        fprintf(stderr, "FAIL: Model_Initialize\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char dllPathDefault[MAX_PATH];
    const char *dllPath;
    char copyPath[MAX_PATH + 32]; /* +32: room for the ".snaptest.dll" suffix below */
    DllHandle h1, h2;
    IEEE_Cigre_DLLInterface_Instance instance1, instance2;
    double *params1 = NULL, *inputs1 = NULL, *outputs1 = NULL;
    double *params2 = NULL, *inputs2 = NULL, *outputs2 = NULL;
    PortOffsets off;
    double minFreq1, maxFreq1, minFreq2, maxFreq2, dev1, dev2;
    DllSnapshot *snap;
    int ok = 1;

    ComtradeChannelDef *comtradeChannels;
    char (*comtradeNames)[64];
    int numDllChannels, numChannels, vAcBase, iAcBase;
    ComtradeSink sinkRaw, sinkRestored;
    char rawBase[MAX_PATH + 32], restoredBase[MAX_PATH + 32];

    if (argc > 1)
    {
        dllPath = argv[1];
    }
    else
    {
        default_dll_path(dllPathDefault, sizeof(dllPathDefault));
        dllPath = dllPathDefault;
    }

    if (load_dll(dllPath, &h1) != 0)
    {
        return 1;
    }

    off.vAc    = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "v_ac");
    off.iL     = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "i_L");
    off.vDc    = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "v_dc");
    off.pReq   = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "P_request");
    off.qReq   = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "Q_request");
    off.enable = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "enable");
    off.pLimHi = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "P_lim_high");
    off.pLimLo = find_port_offset(h1.info->InputPortsInfo, h1.info->NumInputPorts, "P_lim_low");
    off.freq   = find_port_offset(h1.info->OutputPortsInfo, h1.info->NumOutputPorts, "acMonitor_OUT.frequency");

    if (off.vAc < 0 || off.iL < 0 || off.vDc < 0 || off.pReq < 0 || off.qReq < 0 ||
        off.enable < 0 || off.pLimHi < 0 || off.pLimLo < 0 || off.freq < 0)
    {
        fprintf(stderr, "FAIL: expected port not found -- is acMonitor_OUT.frequency in debug_signals_config.yaml?\n");
        return 1;
    }

    /* Channel list is the same for both runs (same DLL, same Model_Info
     * shape) -- built once, shared by both writers. */
    numDllChannels = build_flat_output_channels(h1.info->OutputPortsInfo, h1.info->NumOutputPorts,
                                                 &comtradeChannels, &comtradeNames);
    vAcBase = numDllChannels;
    iAcBase = numDllChannels + 3;
    numChannels = numDllChannels + 6;
    comtradeChannels = (ComtradeChannelDef *)realloc(comtradeChannels, (size_t)numChannels * sizeof(ComtradeChannelDef));
    comtradeNames    = (char (*)[64])realloc(comtradeNames, (size_t)numChannels * sizeof(*comtradeNames));
    { int c; for (c = 0; c < numChannels; c++) { comtradeChannels[c].name = comtradeNames[c]; } }
    { int k; for (k = 0; k < 3; k++)
    {
        snprintf(comtradeNames[vAcBase + k], sizeof(comtradeNames[vAcBase + k]), "v_ac[%d]", k);
        comtradeChannels[vAcBase + k].unit = "V";
        comtradeChannels[vAcBase + k].phase = phase_letter(comtradeNames[vAcBase + k]);
        comtradeChannels[vAcBase + k].kind = COMTRADE_CH_ANALOG;

        snprintf(comtradeNames[iAcBase + k], sizeof(comtradeNames[iAcBase + k]), "i_ac[%d]", k);
        comtradeChannels[iAcBase + k].unit = "Amps";
        comtradeChannels[iAcBase + k].phase = phase_letter(comtradeNames[iAcBase + k]);
        comtradeChannels[iAcBase + k].kind = COMTRADE_CH_ANALOG;
    } }

    sinkRaw.numDllChannels = sinkRestored.numDllChannels = numDllChannels;
    sinkRaw.vAcBase        = sinkRestored.vAcBase        = vAcBase;
    sinkRaw.iAcBase        = sinkRestored.iAcBase        = iAcBase;
    sinkRaw.flatValues      = (double *)calloc((size_t)numChannels, sizeof(double));
    sinkRestored.flatValues = (double *)calloc((size_t)numChannels, sizeof(double));

    sibling_path(dllPath, "snapshot_raw", rawBase, sizeof(rawBase));
    sibling_path(dllPath, "snapshot_restored", restoredBase, sizeof(restoredBase));
    sinkRaw.writer      = comtrade_writer_open(rawBase, h1.info->ModelName, "test_snapshot",
                                                SCENARIO_FNOM, 1.0 / DT, comtradeChannels, numChannels);
    sinkRestored.writer = comtrade_writer_open(restoredBase, h1.info->ModelName, "test_snapshot",
                                                SCENARIO_FNOM, 1.0 / DT, comtradeChannels, numChannels);
    if (sinkRaw.writer && sinkRestored.writer)
    {
        printf("Recording to %s.cfg/.dat and %s.cfg/.dat\n", rawBase, restoredBase);
    }
    else
    {
        fprintf(stderr, "WARNING: could not open COMTRADE output, continuing without recording\n");
    }

    /* Run 1: raw, 10.0s from a cold start. */
    if (init_instance(&h1, &instance1, &params1, &inputs1, &outputs1) != 0)
    {
        return 1;
    }
    run_pass(&h1, &instance1, inputs1, outputs1, &off, (int)(10.0 / APP_STEP_SECONDS), &sinkRaw, &minFreq1, &maxFreq1);

    snap = dll_snapshot_capture(h1.hDll);
    if (!snap)
    {
        fprintf(stderr, "FAIL: dll_snapshot_capture\n");
        return 1;
    }
    h1.Terminate(&instance1);

    /* Run 2: a second, independently-loaded copy of the DLL, restored
     * from run 1's snapshot before stepping at all. */
    snprintf(copyPath, sizeof(copyPath), "%s.snaptest.dll", dllPath);
    if (!CopyFileA(dllPath, copyPath, FALSE))
    {
        fprintf(stderr, "FAIL: CopyFileA to '%s' (error %lu)\n", copyPath, GetLastError());
        return 1;
    }
    if (load_dll(copyPath, &h2) != 0)
    {
        return 1;
    }
    if (init_instance(&h2, &instance2, &params2, &inputs2, &outputs2) != 0)
    {
        return 1;
    }
    if (dll_snapshot_restore(snap, h2.hDll) != 0)
    {
        fprintf(stderr, "FAIL: dll_snapshot_restore (section layout mismatch)\n");
        return 1;
    }
    run_pass(&h2, &instance2, inputs2, outputs2, &off, (int)(2.0 / APP_STEP_SECONDS), &sinkRestored, &minFreq2, &maxFreq2);
    h2.Terminate(&instance2);

    dev1 = fmax(fabs(minFreq1 - SCENARIO_FNOM), fabs(maxFreq1 - SCENARIO_FNOM));
    dev2 = fmax(fabs(minFreq2 - SCENARIO_FNOM), fabs(maxFreq2 - SCENARIO_FNOM));

    printf("Run 1 (raw, cold start):        first-second frequency %.6f - %.6f Hz (deviation %.6f)\n",
           minFreq1, maxFreq1, dev1);
    printf("Run 2 (restored from snapshot): first-second frequency %.6f - %.6f Hz (deviation %.6f)\n",
           minFreq2, maxFreq2, dev2);

    if (dev2 < 0.01 && dev1 > dev2)
    {
        printf("PASS: run 2 stayed flat, run 1 showed a real settling transient.\n");
    }
    else
    {
        printf("FAIL: expected run 2 to be flat (<0.01 Hz) and run 1 to show a larger transient.\n");
        ok = 0;
    }

    comtrade_writer_close(sinkRaw.writer);
    comtrade_writer_close(sinkRestored.writer);
    free(comtradeChannels);
    free(comtradeNames);
    free(sinkRaw.flatValues);
    free(sinkRestored.flatValues);
    dll_snapshot_free(snap);
    free(params1); free(inputs1); free(outputs1);
    free(params2); free(inputs2); free(outputs2);
    DeleteFileA(copyPath);

    /* We intentionally do not call FreeLibrary() or exit normally here.
     * Restoring h1's raw bytes into h2 also overwrote h2's own CRT
     * shutdown bookkeeping (for example, its atexit/cleanup-handler
     * tracking) with h1's data. That bookkeeping is valid only for h1's
     * lifetime, not h2's. If h2 is unloaded normally after h1 is gone,
     * Windows can crash while tearing it down.
     *
     * TerminateProcess() skips the normal DLL unload path entirely,
     * which is acceptable for this one-shot verification program. This is
     * a limitation of the memory-copy snapshot method, not a bug in the
     * test itself. See documentation/9_using_the_ieee_cigre_dll.md,
     * section 4.2. */
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), (UINT)(ok ? 0 : 1));
    return ok ? 0 : 1; /* unreachable */
}
