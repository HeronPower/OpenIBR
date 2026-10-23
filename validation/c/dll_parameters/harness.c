/* Include the wrapper to use its private parameter type. */
#include "ieee_cigre_dll/gfm_battery_dll.c"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static unsigned checks, failures, cases;
static const char sentinel[] = "existing host error";

static void check(bool condition, const char *description)
{
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", description);
    }
}

static void set_parameter(GFM_Battery_Parameters *p, unsigned field, double value)
{
    if (field == 0) p->H = value;
    else if (field == 1) p->D = value;
    else p->kDroop = value;
}

static GFM_Battery_Parameters defaults(void)
{
    return (GFM_Battery_Parameters){ H_DEFAULT, D_DEFAULT, KDROOP_DEFAULT };
}

static void parameter_case(const char *name, GFM_Battery_Parameters p,
                           int expected_return, bool original_should_fail,
                           const char *previous_error)
{
    unsigned before_checks = checks, before_failures = failures;
    GFM_Battery_Parameters saved;
    GFM_Battery_Inputs inputs = {{1, 2, 3}, {4, 5, 6}, 7, 8, 9, 0, 10, -10};
    GFM_Battery_Outputs outputs = {0}, saved_outputs;
    GFM_Battery_Inputs saved_inputs;
    int ints[2] = {17, 23};
    float floats[2] = {1.5f, -2.5f};
    double doubles[2] = {3.5, -4.5};
    int saved_ints[2];
    float saved_floats[2];
    double saved_doubles[2];
    IEEE_Cigre_DLLInterface_Instance instance = {0}, expected;
    instance.Parameters = &p;
    instance.ExternalInputs = &inputs;
    instance.ExternalOutputs = &outputs;
    instance.Time = 12.5;
    instance.SimTool_EMT_RMS_Mode = 1;
    instance.LastErrorMessage = previous_error;
    instance.LastGeneralMessage = sentinel;
    instance.IntStates = ints;
    instance.FloatStates = floats;
    instance.DoubleStates = doubles;
    memcpy(&saved, &p, sizeof(p));
    memcpy(&saved_inputs, &inputs, sizeof(inputs));
    memcpy(&saved_outputs, &outputs, sizeof(outputs));
    memcpy(saved_ints, ints, sizeof(ints));
    memcpy(saved_floats, floats, sizeof(floats));
    memcpy(saved_doubles, doubles, sizeof(doubles));
    memcpy(&expected, &instance, sizeof(instance));

    int result = Model_CheckParameters(&instance);
    check(result == expected_return, "parameter return code");
    if (expected_return == IEEE_Cigre_DLLInterface_Return_Error) {
        check(instance.LastErrorMessage &&
              !strcmp(instance.LastErrorMessage, "Parameter out of range"),
              "rejection retains existing error text");
    } else {
        check(instance.LastErrorMessage == previous_error,
              "success preserves existing error pointer");
    }
    /* Compare every other host-instance byte; the error pointer may change. */
    expected.LastErrorMessage = instance.LastErrorMessage;
    check(!memcmp(&instance, &expected, sizeof(instance)), "unrelated instance fields unchanged");
    check(!memcmp(&p, &saved, sizeof(p)), "parameter bytes unchanged");
    check(!memcmp(&inputs, &saved_inputs, sizeof(inputs)), "host inputs unchanged");
    check(!memcmp(&outputs, &saved_outputs, sizeof(outputs)), "host outputs unchanged");
    check(!memcmp(ints, saved_ints, sizeof(ints)) &&
          !memcmp(floats, saved_floats, sizeof(floats)) &&
          !memcmp(doubles, saved_doubles, sizeof(doubles)), "host state buffers unchanged");
    printf("{\"event\":\"case\",\"name\":\"%s\",\"original_should_fail\":%s,"
           "\"input_hex\":[\"%a\",\"%a\",\"%a\"],\"expected_return\":%d,\"actual_return\":%d,"
           "\"error\":\"%s\",\"checks\":%u,\"failures\":%u}\n",
           name, original_should_fail ? "true" : "false", p.H, p.D, p.kDroop,
           expected_return, result, instance.LastErrorMessage ? instance.LastErrorMessage : "",
           checks - before_checks, failures - before_failures);
    cases++;
}

static void null_cases(void)
{
    unsigned before = failures;
    check(Model_CheckParameters(NULL) == IEEE_Cigre_DLLInterface_Return_Error,
          "null instance rejected");
    printf("{\"event\":\"case\",\"name\":\"null_instance\",\"original_should_fail\":false,"
           "\"checks\":1,\"failures\":%u}\n", failures - before);
    cases++;
    IEEE_Cigre_DLLInterface_Instance instance = {0}, expected;
    instance.LastErrorMessage = sentinel;
    instance.LastGeneralMessage = sentinel;
    instance.Time = 12.5;
    memcpy(&expected, &instance, sizeof(instance));
    before = failures;
    check(Model_CheckParameters(&instance) == IEEE_Cigre_DLLInterface_Return_Error,
          "null parameters rejected");
    check(!memcmp(&instance, &expected, sizeof(instance)), "null rejection preserves host instance");
    printf("{\"event\":\"case\",\"name\":\"null_parameters\",\"original_should_fail\":false,"
           "\"checks\":2,\"failures\":%u}\n", failures - before);
    cases++;
}

static void metadata(void)
{
    const IEEE_Cigre_DLLInterface_Model_Info *info = Model_GetInfo();
    check(info != NULL && info == Model_GetInfo(), "stable metadata address");
    check(info->NumParameters == 3 && info->NumInputPorts == 8 && info->NumOutputPorts == 13,
          "parameter and port counts unchanged");
    check(info->FixedStepBaseSampleTime == 200e-6 && info->EMT_RMS_Mode == 1,
          "fixed step and EMT mode unchanged");
    printf("{\"event\":\"metadata\",\"name\":\"%s\",\"version\":\"%s\","
           "\"interface_version\":[%u,%u,%u,%u],\"step_hex\":\"%a\",\"mode\":%u,"
           "\"state_counts\":[%d,%d,%d],\"native_parameter_bytes\":%zu,"
           "\"native_parameter_offsets\":[%zu,%zu,%zu]}\n",
           info->ModelName, info->ModelVersion,
           (unsigned)info->DLLInterfaceVersion[0], (unsigned)info->DLLInterfaceVersion[1],
           (unsigned)info->DLLInterfaceVersion[2], (unsigned)info->DLLInterfaceVersion[3],
           info->FixedStepBaseSampleTime, (unsigned)info->EMT_RMS_Mode,
           info->NumIntStates, info->NumFloatStates, info->NumDoubleStates,
           sizeof(GFM_Battery_Parameters), offsetof(GFM_Battery_Parameters, H),
           offsetof(GFM_Battery_Parameters, D), offsetof(GFM_Battery_Parameters, kDroop));
    const char *names[] = {"H", "D", "kDroop"};
    const double lower[] = {0.1, 0, 0}, upper[] = {20, 10, 10};
    for (unsigned i = 0; i < 3; i++) {
        const IEEE_Cigre_DLLInterface_Parameter *p = &info->ParametersInfo[i];
        check(!strcmp(p->Name, names[i]) && p->MinValue.Real64_Val == lower[i] &&
              p->MaxValue.Real64_Val == upper[i], "named inclusive bounds unchanged");
        printf("{\"event\":\"parameter_metadata\",\"name\":\"%s\",\"group\":\"%s\","
               "\"description\":\"%s\",\"unit\":\"%s\",\"type\":%d,\"fixed\":%d,"
               "\"default_hex\":\"%a\",\"lower_hex\":\"%a\",\"upper_hex\":\"%a\"}\n",
               p->Name, p->GroupName, p->Description, p->Unit, p->DataType, p->FixedValue,
               p->DefaultValue.Real64_Val, p->MinValue.Real64_Val, p->MaxValue.Real64_Val);
    }
    for (unsigned direction = 0; direction < 2; direction++) {
        const IEEE_Cigre_DLLInterface_Signal *ports = direction ? info->OutputPortsInfo : info->InputPortsInfo;
        int count = direction ? info->NumOutputPorts : info->NumInputPorts;
        for (int i = 0; i < count; i++)
            printf("{\"event\":\"port_metadata\",\"direction\":\"%s\",\"name\":\"%s\","
                   "\"description\":\"%s\",\"unit\":\"%s\",\"type\":%d,\"width\":%d}\n",
                   direction ? "output" : "input", ports[i].Name, ports[i].Description,
                   ports[i].Unit, ports[i].DataType, ports[i].Width);
    }
}

int main(void)
{
    metadata();
    parameter_case("defaults", defaults(), 0, false, NULL);
    const char *fields[] = {"H", "D", "kDroop"};
    const double lower[] = {0.1, 0, 0}, upper[] = {20, 10, 10};
    const char *labels[] = {"lower_outside", "lower_exact", "lower_inside", "middle",
                           "upper_inside", "upper_exact", "upper_outside", "nan", "negative_nan",
                           "positive_inf", "negative_inf", "max_finite", "negative_max_finite",
                           "min_positive", "min_negative", "zero", "negative_zero"};
    for (unsigned field = 0; field < 3; field++) {
        const double values[] = {
            nextafter(lower[field], -INFINITY), lower[field], nextafter(lower[field], INFINITY),
            (lower[field] + upper[field]) / 2, nextafter(upper[field], -INFINITY), upper[field],
            nextafter(upper[field], INFINITY), NAN, -NAN, INFINITY, -INFINITY, DBL_MAX, -DBL_MAX,
            DBL_MIN, -DBL_MIN, 0.0, -0.0
        };
        const bool accepted[] = {false, true, true, true, true, true, false, false, false,
                                 false, false, false, false, field != 0, false, field != 0, field != 0};
        for (unsigned index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
            GFM_Battery_Parameters p = defaults();
            set_parameter(&p, field, values[index]);
            char name[80];
            snprintf(name, sizeof(name), "%s_%s", fields[field], labels[index]);
            parameter_case(name, p, accepted[index] ? 0 : 2, index == 7 || index == 8, sentinel);
        }
    }
    for (unsigned mask = 3; mask <= 7; mask++) {
        if (mask == 4) continue; /* Single-field NaNs are already covered. */
        GFM_Battery_Parameters p = defaults();
        for (unsigned field = 0; field < 3; field++)
            if (mask & (1u << field)) set_parameter(&p, field, NAN);
        char name[80];
        snprintf(name, sizeof(name), "nan_mask_%u", mask);
        parameter_case(name, p, 2, true, sentinel);
    }
    /* A NaN plus another already-invalid field still retains rejection. */
    for (unsigned nan_field = 0; nan_field < 3; nan_field++) {
        for (unsigned invalid_field = 0; invalid_field < 3; invalid_field++) {
            if (nan_field == invalid_field) continue;
            GFM_Battery_Parameters p = defaults();
            set_parameter(&p, nan_field, NAN);
            set_parameter(&p, invalid_field, nextafter(upper[invalid_field], INFINITY));
            char name[80];
            snprintf(name, sizeof(name), "nan_%s_already_invalid_%s", fields[nan_field], fields[invalid_field]);
            parameter_case(name, p, 2, false, sentinel);
        }
    }
    null_cases();
    /* Preserve the checker's existing successful-call stale-message semantics. */
    GFM_Battery_Parameters p = defaults();
    IEEE_Cigre_DLLInterface_Instance instance = {0};
    instance.Parameters = &p;
    p.H = 0;
    int rejected = Model_CheckParameters(&instance);
    const char *error = instance.LastErrorMessage;
    unsigned before = failures;
    check(rejected == 2 && error && !strcmp(error, "Parameter out of range"), "known invalid rejection");
    p = defaults();
    check(Model_CheckParameters(&instance) == 0 && instance.LastErrorMessage == error,
          "valid recheck preserves previous error pointer");
    printf("{\"event\":\"case\",\"name\":\"reject_then_valid\",\"original_should_fail\":false,"
           "\"checks\":2,\"failures\":%u}\n", failures - before);
    cases++;
    printf("{\"event\":\"result\",\"cases\":%u,\"checks\":%u,\"failures\":%u}\n",
           cases, checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
