#ifndef PARAMETER_DEFINITIONS_H
#define PARAMETER_DEFINITIONS_H

    // Ratings
    #define VLL_RATED (480.0f)
    #define P_RATED (1e6f)
    #define Q_RATED (0.5e6f)
    #define S_RATED (sqrtf(P_RATED*P_RATED + Q_RATED*Q_RATED))
	#define F_NOM	(60.0f)

    #define I_RATED (S_RATED/(VLL_RATED*SQRT_3))
	#define Z_BASE	((VLL_RATED*VLL_RATED)/S_RATED)
    #define L_BASE  (Z_BASE / (2.0f * PI * F_NOM))
    #define C_BASE  (1.0f / (2.0f * PI * F_NOM * Z_BASE))

    // Sensor parameters
    #define VDC_ADC_RANGE  (1.5e3f)         // maps to 3.0V
    #define VAC_ADC_RANGE  (750.0f)         // maps to 1.5V (biased around 1.5V)
    #define IAC_ADC_RANGE  (4.5e3f)       // maps to 1.5V (biased around 1.5V)

#endif // PARAMETER_DEFINITIONS_H 
