/*
 * IEEE_Cigre_DLLInterface.h
 *
 * Interface defined by CIGRE Technical Brochure 958 (JWG CIGRE B4.82/IEEE,
 * Feb 2025), "Guidelines for Use of Real-Code in EMT Models for HVDC,
 * FACTS and Inverter Based Generators in Power Systems Analysis". Lets a
 * single compiled DLL be loaded, unmodified, by any conforming EMT/RMS
 * simulator (PSCAD, EMTP, RTDS, PowerFactory, PSS/E EMT). Not yet a
 * formally ratified standard.
 *
 * Verified directly against TB958 (Appendix D/E). Before integrating with
 * a specific simulator, still check that simulator's own conventions on
 * top of this interface -- e.g. what its DLL Import tool expects in
 * LastErrorMessage/LastGeneralMessage.
 */

#ifndef IEEE_CIGRE_DLLINTERFACE_H
#define IEEE_CIGRE_DLLINTERFACE_H

#include <stdint.h>
#include "IEEE_Cigre_DLLInterface_types.h"

#if defined(_WIN32) || defined(_WIN64)
    #define MODEL_CALL __cdecl
    #ifdef GFM_BATTERY_DLL_EXPORTS
        #define MODEL_API __declspec(dllexport)
    #else
        #define MODEL_API __declspec(dllimport)
    #endif
#else
    #define MODEL_CALL
    #define MODEL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Verified against CIGRE TB958, Appendix D.                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    const char *Name;
    const char *Description;
    const char *Unit;
    int         DataType;   /* IEEE_Cigre_DLLInterface_DataType_Enum */
    int         Width;      /* number of scalar elements in this port */
} IEEE_Cigre_DLLInterface_Signal;

typedef union
{
    char           Char_Val;
    const char    *Char_Ptr;
    int8_t         Int8_Val;
    uint8_t        Uint8_Val;
    int16_t        Int16_Val;
    uint16_t       Uint16_Val;
    int32_t        Int32_Val;
    uint32_t       Uint32_Val;
    float          Real32_Val;
    double         Real64_Val;
} IEEE_Cigre_DLLInterface_DefaultValue;

typedef union
{
    char           Char_Val;
    int8_t         Int8_Val;
    uint8_t        Uint8_Val;
    int16_t        Int16_Val;
    uint16_t       Uint16_Val;
    int32_t        Int32_Val;
    uint32_t       Uint32_Val;
    float          Real32_Val;
    double         Real64_Val;
} IEEE_Cigre_DLLInterface_MinMaxValue;

typedef struct
{
    const char *Name;
    const char *GroupName;
    const char *Description;
    const char *Unit;
    int         DataType;    /* IEEE_Cigre_DLLInterface_DataType_Enum */
    int         FixedValue;  /* nonzero: user may not edit at run time */
    IEEE_Cigre_DLLInterface_DefaultValue DefaultValue;
    IEEE_Cigre_DLLInterface_MinMaxValue  MinValue;
    IEEE_Cigre_DLLInterface_MinMaxValue  MaxValue;
} IEEE_Cigre_DLLInterface_Parameter;

typedef struct
{
    uint8_t     DLLInterfaceVersion[4];
    const char *ModelName;
    const char *ModelVersion;
    const char *ModelDescription;
    const char *GeneralInformation;
    const char *ModelCreated;
    const char *ModelCreator;
    const char *ModelLastModifiedDate;
    const char *ModelLastModifiedBy;
    const char *ModelModifiedComment;
    const char *ModelModifiedHistory;
    double      FixedStepBaseSampleTime;
    uint8_t     EMT_RMS_Mode;   /* EMT = 1, RMS = 2, EMT & RMS = 3, otherwise: 0 */

    int NumInputPorts;
    IEEE_Cigre_DLLInterface_Signal *InputPortsInfo;

    int NumOutputPorts;
    IEEE_Cigre_DLLInterface_Signal *OutputPortsInfo;

    int NumParameters;
    IEEE_Cigre_DLLInterface_Parameter *ParametersInfo;

    int NumIntStates;
    int NumFloatStates;
    int NumDoubleStates;
} IEEE_Cigre_DLLInterface_Model_Info;

/* Return value of every Instance-taking function below. */
typedef enum
{
    IEEE_Cigre_DLLInterface_Return_OK      = 0, /* fine, no message */
    IEEE_Cigre_DLLInterface_Return_Message = 1, /* message generated, simulation continues */
    IEEE_Cigre_DLLInterface_Return_Error   = 2  /* error generated, simulation interrupted */
} IEEE_Cigre_DLLInterface_Return_Value;

typedef struct
{
    /* Cast by the model's own code to a model-specific struct, one field
     * per declared port/parameter in InputPortsInfo/ParametersInfo order
     * (see gfm_battery_dll.c's GFM_Battery_Inputs/Outputs) -- owned and
     * allocated by the simulation tool, not the DLL. */
    void *ExternalInputs;
    void *ExternalOutputs;
    void *Parameters;

    double  Time;                    /* current simulation time, seconds */
    uint8_t SimTool_EMT_RMS_Mode;    /* EMT = 1, RMS = 2 */

    const char *LastErrorMessage;    /* set by the DLL when returning _Error */
    const char *LastGeneralMessage;  /* set by the DLL when returning _Message */

    /* Per-instance state storage owned by the simulation tool, sized by
     * Model_Info.NumIntStates / NumFloatStates / NumDoubleStates. */
    int    *IntStates;
    float  *FloatStates;
    double *DoubleStates;
} IEEE_Cigre_DLLInterface_Instance;

/* ------------------------------------------------------------------ */
/* The eight exported entry points, in the order a conforming host     */
/* calls them (TB958 Figure 2).                                        */
/* ------------------------------------------------------------------ */

MODEL_API const IEEE_Cigre_DLLInterface_Model_Info * MODEL_CALL Model_GetInfo(void);
MODEL_API int MODEL_CALL Model_FirstCall(IEEE_Cigre_DLLInterface_Instance *Instance);
MODEL_API int MODEL_CALL Model_PrintInfo(void);
MODEL_API int MODEL_CALL Model_CheckParameters(IEEE_Cigre_DLLInterface_Instance *Instance);
MODEL_API int MODEL_CALL Model_Initialize(IEEE_Cigre_DLLInterface_Instance *Instance);
MODEL_API int MODEL_CALL Model_Outputs(IEEE_Cigre_DLLInterface_Instance *Instance);
MODEL_API int MODEL_CALL Model_Iterate(IEEE_Cigre_DLLInterface_Instance *Instance);
MODEL_API int MODEL_CALL Model_Terminate(IEEE_Cigre_DLLInterface_Instance *Instance);

#ifdef __cplusplus
}
#endif

#endif /* IEEE_CIGRE_DLLINTERFACE_H */
