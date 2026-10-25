/* Direct-link, read-only observation of the unchanged copied OpenIBR wrapper.
 * Each invocation owns one controller trajectory in a fresh process.
 * Open-loop inputs are API stimuli, not a simulated plant or electrical result. */
#include "ieee_cigre_dll/gfm_battery_dll.c"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <float.h>

enum { NI = 12, NO = 15, RECOVERY = 250 };
static const double dt = 200e-6;
static const char error_seed[] = "retained host error sentinel";
static const char general_seed[] = "retained host general sentinel";
static const char *selected[] = {
    "acMonitor_IN.v_ac[0]", "acMonitor_OUT.frequency",
    "acMonitor_OUT.Vpve.real", "acMonitor_OUT.Vpve.imag",
    "machineModel_IN.enable", "machineModel_OUT.i_ref[0]",
    "currentSource_IN.P_request", "currentSource_IN.Q_request",
    "currentSource_STATE.P_cmd", "currentSource_STATE.Q_cmd",
    "currentSource_OUT.i_ref[0]", "powerLimiter_IN.P_limit_upper",
    "powerLimiter_IN.P_limit_lower", "powerLimiter_STATE.pInst",
    "powerLimiter_STATE.pExcess", "powerLimiter_OUT.i_excess[0]",
    "currentLimiting_OUT.i_limited[0]", "acChecks_OUT.acOk",
    "acChecks_IN.freq", "acChecks_IN.Vph[0]", "acChecks_QUALIFY_STATE.timer",
    "circuitControls_IN.enable", "circuitControls_IN.v_dc",
    "circuitControls_IN.i_L[0]", "circuitControls_IN.v_ac[0]",
    "circuitControls_IN.i_ref[0]", "circuitControls_OUT.v_bridge[0]",
    "circuitControls_OUT.v_bridge[1]", "circuitControls_OUT.v_bridge[2]"
};
static int selected_index[sizeof(selected) / sizeof(selected[0])];

static void need(bool condition, const char *what)
{
    if (!condition) { fprintf(stderr, "Harness integrity failure: %s\n", what); exit(2); }
}
static void string(const char *s)
{
    if (!s) { printf("null"); return; }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32) printf("\\u%04x", (unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static void number(double value, const void *address, size_t bytes)
{
    printf("{\"value\":\"%a\",\"classification\":\"%s\",\"bytes\":\"", value,
           isnan(value) ? "nan" : isinf(value) ? (signbit(value) ? "negative_infinity" : "positive_infinity") : "finite");
    const unsigned char *p = address;
    for (size_t i = 0; i < bytes; ++i) printf("%02x", (unsigned)p[i]);
    printf("\",\"byte_count\":%zu}", bytes);
}
static void array(const double *values, size_t count)
{
    putchar('[');
    for (size_t i = 0; i < count; ++i) {
        if (i) putchar(',');
        number(values[i], &values[i], sizeof(values[i]));
    }
    putchar(']');
}
static const void *pointer(const SignalCatalogEntry *e, size_t *size)
{
    switch (e->type) {
        case SIGNAL_CATALOG_TYPE_FLOAT: *size = sizeof(*e->ptr.f); return e->ptr.f;
        case SIGNAL_CATALOG_TYPE_INT: *size = sizeof(*e->ptr.i); return e->ptr.i;
        case SIGNAL_CATALOG_TYPE_UINT: *size = sizeof(*e->ptr.u); return e->ptr.u;
        case SIGNAL_CATALOG_TYPE_BOOL: *size = sizeof(*e->ptr.b); return e->ptr.b;
        default: need(false, "unsupported catalog type"); return NULL;
    }
}
static void field(const SignalCatalogEntry *e)
{
    size_t size; const void *p = pointer(e, &size);
    number(signal_catalog_read(e), p, size);
}
static void catalog(bool full)
{
    putchar('{');
    int n = full ? g_signalCatalogCount : (int)(sizeof(selected) / sizeof(selected[0]));
    for (int i = 0; i < n; ++i) {
        const SignalCatalogEntry *e = &g_signalCatalog[full ? i : selected_index[i]];
        if (i) putchar(',');
        string(e->name); putchar(':'); field(e);
    }
    putchar('}');
}
static void snapshot(const char *id, long step)
{
    printf("{\"event\":\"snapshot\",\"id\":"); string(id);
    printf(",\"after_call_index_zero_based\":%ld,\"fields\":", step);
    catalog(true); printf("}\n");
}
static void inputs_flat(const GFM_Battery_Inputs *in, double v[NI])
{
    for (int i = 0; i < 3; ++i) { v[i] = in->v_ac[i]; v[i + 3] = in->i_L[i]; }
    v[6] = in->v_dc; v[7] = in->P_request; v[8] = in->Q_request;
    v[9] = in->enable; v[10] = in->P_lim_high; v[11] = in->P_lim_low;
}
static void outputs_flat(const GFM_Battery_Outputs *out, double v[NO])
{
    for (int i = 0; i < 3; ++i) v[i] = out->v_bridge[i];
    v[3] = out->pwm_enable;
    for (int i = 0; i < NUM_EXTRA_OUTPUT_PORTS; ++i) v[i + 4] = out->extra[i];
}
static void stimulus(GFM_Battery_Inputs *in, unsigned long k, bool disabled)
{
    double t = k * dt, pi = acos(-1.0), peak = VLL_RATED * sqrt(2.0 / 3.0);
    for (int i = 0; i < 3; ++i) {
        double phase = i == 0 ? 0.0 : i == 1 ? -2.0 * pi / 3.0 : 2.0 * pi / 3.0;
        in->v_ac[i] = peak * sin(2.0 * pi * F_NOM_RATED * t + phase);
        in->i_L[i] = 0.0;
    }
    in->v_dc = 1000.0; in->P_request = 500000.0; in->Q_request = 0.0;
    in->enable = disabled ? 0.0 : 1.0;
    in->P_lim_high = 1000000.0; in->P_lim_low = -1000000.0;
}
static double perturbation(const char *text)
{
    if (!strcmp(text, "p_at")) return (double)(currentSource_GENERAL_CFG.Plimit * currentSource_GENERAL_CFG.SRated);
    if (!strcmp(text, "p_over")) return 2.0 * (double)(currentSource_GENERAL_CFG.Plimit * currentSource_GENERAL_CFG.SRated);
    if (!strcmp(text, "q_at")) return (double)(currentSource_GENERAL_CFG.Qlimit * currentSource_GENERAL_CFG.SRated);
    if (!strcmp(text, "q_over")) return 2.0 * (double)(currentSource_GENERAL_CFG.Qlimit * currentSource_GENERAL_CFG.SRated);
    errno = 0; char *end; double value = strtod(text, &end);
    need(!errno && end != text && !*end, "perturbation token"); return value;
}
static void replace(GFM_Battery_Inputs *in, int scalar, double value)
{
    if (scalar < 3) in->v_ac[scalar] = value;
    else if (scalar < 6) in->i_L[scalar - 3] = value;
    else switch (scalar) {
        case 6: in->v_dc = value; break; case 7: in->P_request = value; break;
        case 8: in->Q_request = value; break; case 9: in->enable = value; break;
        case 10: in->P_lim_high = value; break; case 11: in->P_lim_low = value; break;
        default: need(false, "input scalar index");
    }
}
int main(int argc, char **argv)
{
    need(argc == 7, "arguments: id history enabled|disabled scalar|-1 token output-prefix");
    char *end; unsigned long history = strtoul(argv[2], &end, 10);
    need(*argv[2] && !*end && (history == 0 || history == 12004), "frozen warm history");
    bool disabled = !strcmp(argv[3], "disabled");
    need(disabled || !strcmp(argv[3], "enabled"), "context");
    long scalar = strtol(argv[4], &end, 10);
    need(*argv[4] && !*end && scalar >= -1 && scalar < NI, "scalar");
    const IEEE_Cigre_DLLInterface_Model_Info *info = Model_GetInfo();
    need(info && info->NumInputPorts == 8 && info->NumOutputPorts == 13 &&
         g_signalCatalogCount == 231 && NUM_EXTRA_OUTPUT_PORTS == 11 &&
         sizeof(double) == 8 && sizeof(float) == 4 && info->FixedStepBaseSampleTime == dt, "metadata shape");
    for (size_t j = 0; j < sizeof(selected) / sizeof(selected[0]); ++j) {
        selected_index[j] = -1;
        for (int i = 0; i < g_signalCatalogCount; ++i)
            if (!strcmp(selected[j], g_signalCatalog[i].name)) selected_index[j] = i;
        need(selected_index[j] >= 0, "selected catalog name");
    }
    GFM_Battery_Parameters parameters = { H_DEFAULT, D_DEFAULT, KDROOP_DEFAULT };
    GFM_Battery_Inputs in = {0}; GFM_Battery_Outputs out = {0};
    IEEE_Cigre_DLLInterface_Instance host = {0};
    host.Parameters = &parameters; host.ExternalInputs = &in; host.ExternalOutputs = &out;
    host.LastErrorMessage = error_seed; host.LastGeneralMessage = general_seed;
    int first_code = Model_FirstCall(&host);
    need(first_code == 0, "valid first call");
    int initialize_code = Model_Initialize(&host);
    need(initialize_code == 0, "valid initialization");
    uint16_t endian = 1;
    printf("{\"event\":\"metadata\",\"case\":"); string(argv[1]);
    printf(",\"history_calls\":%lu,\"recovery_calls\":%d,\"scalar\":%ld,\"context\":", history, RECOVERY, scalar); string(argv[3]);
    printf(",\"token\":"); string(argv[5]);
    printf(",\"dt\":\"%a\",\"initialization_returns\":{\"Model_FirstCall\":%d,\"Model_Initialize\":%d},"
           "\"single_instance\":true,\"open_loop\":true,\"electrical_network\":false,\"byte_order\":",
           dt, first_code, initialize_code);
    string(*(unsigned char *)&endian ? "little" : "big");
    printf(",\"input_names\":[\"v_ac[0]\",\"v_ac[1]\",\"v_ac[2]\",\"i_L[0]\",\"i_L[1]\",\"i_L[2]\",\"v_dc\",\"P_request\",\"Q_request\",\"enable\",\"P_lim_high\",\"P_lim_low\"],\"output_names\":[\"v_bridge[0]\",\"v_bridge[1]\",\"v_bridge[2]\",\"pwm_enable\"");
    for (int i = 2; i < info->NumOutputPorts; ++i) { putchar(','); string(info->OutputPortsInfo[i].Name); }
    printf("],\"catalog_definitions\":{");
    for (int i = 0; i < g_signalCatalogCount; ++i) {
        const SignalCatalogEntry *e = &g_signalCatalog[i]; size_t size; pointer(e, &size);
        if (i) putchar(','); string(e->name); printf(":{\"type_id\":%d,\"byte_count\":%zu,\"unit\":", e->type, size); string(e->unit); putchar('}');
    }
    printf("},\"parameters\":"); double pv[3] = {parameters.H, parameters.D, parameters.kDroop}; array(pv, 3); printf("}\n");
    snapshot("after_initialization", -1);
    FILE *files[3]; const char *suffix[3] = {"inputs.bin", "outputs.bin", "returns.bin"};
    for (int i = 0; i < 3; ++i) {
        char path[4096]; int n = snprintf(path, sizeof(path), "%s.%s", argv[6], suffix[i]);
        need(n > 0 && (size_t)n < sizeof(path), "raw path length"); files[i] = fopen(path, "wb"); need(files[i] != NULL, "raw file open");
    }
    unsigned long total = history + 1 + RECOVERY, nonfinite = 0;
    for (unsigned long k = 0; k < total; ++k) {
        stimulus(&in, k, disabled); host.Time = k * dt;
        if (k == history) {
            snapshot("before_injection", (long)k - 1);
            if (scalar >= 0) replace(&in, (int)scalar, perturbation(argv[5]));
        }
        double input[NI], output[NO]; inputs_flat(&in, input);
        int code = Model_Outputs(&host); outputs_flat(&out, output);
        double after[NI]; inputs_flat(&in, after);
        need(!memcmp(input, after, sizeof(input)), "host input bytes changed");
        need(parameters.H == H_DEFAULT && parameters.D == D_DEFAULT && parameters.kDroop == KDROOP_DEFAULT,
             "host parameters changed");
        need(host.ExternalInputs == &in && host.ExternalOutputs == &out && host.Parameters == &parameters && host.Time == k * dt,
             "host identity/time changed");
        int32_t status = code;
        need(fwrite(input, sizeof(double), NI, files[0]) == NI && fwrite(output, sizeof(double), NO, files[1]) == NO &&
             fwrite(&status, sizeof(status), 1, files[2]) == 1, "complete raw write");
        for (int i = 0; i < NO; ++i) if (!isfinite(output[i])) ++nonfinite;
        if (k + 5 >= history) {
            printf("{\"event\":\"step\",\"index\":%lu,\"time\":\"%a\",\"phase\":\"%s\",\"return_code\":%d,\"error_seed_retained\":%s,\"general_seed_retained\":%s,\"error\":",
                   k, k * dt, k < history ? "prefix_tail" : k == history ? "injection" : "recovery", code,
                   host.LastErrorMessage == error_seed ? "true" : "false", host.LastGeneralMessage == general_seed ? "true" : "false");
            string(host.LastErrorMessage); printf(",\"general\":"); string(host.LastGeneralMessage);
            printf(",\"inputs\":"); array(input, NI); printf(",\"outputs\":"); array(output, NO);
            printf(",\"selected\":"); catalog(false); printf("}\n");
        }
        if (k == history) snapshot("after_injection", (long)k);
        if (k == history + 1) snapshot("after_first_recovery", (long)k);
        if (k == history + 5) snapshot("after_fifth_recovery", (long)k);
    }
    snapshot("after_final_recovery", (long)total - 1);
    for (int i = 0; i < 3; ++i) need(fclose(files[i]) == 0, "raw close");
    printf("{\"event\":\"result\",\"controller_calls\":%lu,\"nonfinite_output_observations\":%lu,\"harness_integrity_failures\":0,\"numeric_rejection_policy_asserted\":false}\n", total, nonfinite);
    return 0;
}
