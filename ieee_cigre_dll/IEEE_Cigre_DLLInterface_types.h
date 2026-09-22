/*
 * IEEE_Cigre_DLLInterface_types.h
 *
 * Scalar type identifiers for the IEEE/CIGRE real-code DLL interface
 * (CIGRE TB958, JWG CIGRE B4.82/IEEE). Values match the reference
 * implementation from RTE/TU Delft's InterOPERA PSCAD import tool
 * (github.com/rte-france/PSCAD-import-tool-for-IEEE-CIGRE-DLLs).
 */

#ifndef IEEE_CIGRE_DLLINTERFACE_TYPES_H
#define IEEE_CIGRE_DLLINTERFACE_TYPES_H

typedef enum
{
    IEEE_Cigre_DLLInterface_DataType_char_T   = 1,
    IEEE_Cigre_DLLInterface_DataType_int8_T   = 2,
    IEEE_Cigre_DLLInterface_DataType_uint8_T  = 3,
    IEEE_Cigre_DLLInterface_DataType_int16_T  = 4,
    IEEE_Cigre_DLLInterface_DataType_uint16_T = 5,
    IEEE_Cigre_DLLInterface_DataType_int32_T  = 6,
    IEEE_Cigre_DLLInterface_DataType_uint32_T = 7,
    IEEE_Cigre_DLLInterface_DataType_real32_T = 8,
    IEEE_Cigre_DLLInterface_DataType_real64_T = 9,
    IEEE_Cigre_DLLInterface_DataType_c_string_T = 10
} IEEE_Cigre_DLLInterface_DataType_Enum;

#endif /* IEEE_CIGRE_DLLINTERFACE_TYPES_H */
