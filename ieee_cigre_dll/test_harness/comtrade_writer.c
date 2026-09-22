#include "comtrade_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

struct ComtradeWriter
{
    double *rawTimestamps;  /* [sampleCap] elapsed sim time per sample, seconds */
    double *rawValues;      /* [sampleCap * numChannels], row-major, caller's (pre-reorder) channel order */
    size_t sampleCap;       /* allocated capacity, in samples */

    ComtradeChannelDef *channels;  /* copy of the caller's array, original order */
    int *displayOrder;             /* displayOrder[col] = index into channels[] for .cfg/.dat column 'col' (all analog first, then all digital) */
    int numChannels;
    int numAnalog;
    int numDigital;

    char basePath[512];
    char stationName[128];
    char deviceId[64];
    double nominalFreqHz;
    double sampleRateHz;
    unsigned long sampleCount;
};

static void free_writer(ComtradeWriter *writer)
{
    if (!writer)
    {
        return;
    }
    free(writer->rawTimestamps);
    free(writer->rawValues);
    free(writer->channels);
    free(writer->displayOrder);
    free(writer);
}

ComtradeWriter *comtrade_writer_open(const char *basePath,
                                      const char *stationName,
                                      const char *deviceId,
                                      double nominalFreqHz,
                                      double sampleRateHz,
                                      const ComtradeChannelDef *channels,
                                      int numChannels)
{
    ComtradeWriter *writer;
    int i, col;

    if (!basePath || !channels || numChannels <= 0)
    {
        return NULL;
    }

    writer = (ComtradeWriter *)calloc(1, sizeof(ComtradeWriter));
    if (!writer)
    {
        return NULL;
    }

    writer->channels = (ComtradeChannelDef *)malloc(sizeof(ComtradeChannelDef) * (size_t)numChannels);
    writer->displayOrder = (int *)malloc(sizeof(int) * (size_t)numChannels);
    if (!writer->channels || !writer->displayOrder)
    {
        free_writer(writer);
        return NULL;
    }
    memcpy(writer->channels, channels, sizeof(ComtradeChannelDef) * (size_t)numChannels);
    writer->numChannels = numChannels;

    col = 0;
    for (i = 0; i < numChannels; i++)
    {
        if (channels[i].kind == COMTRADE_CH_ANALOG)
        {
            writer->displayOrder[col++] = i;
        }
    }
    writer->numAnalog = col;
    for (i = 0; i < numChannels; i++)
    {
        if (channels[i].kind == COMTRADE_CH_DIGITAL)
        {
            writer->displayOrder[col++] = i;
        }
    }
    writer->numDigital = numChannels - writer->numAnalog;

    snprintf(writer->basePath, sizeof(writer->basePath), "%s", basePath);
    snprintf(writer->stationName, sizeof(writer->stationName), "%s", stationName ? stationName : "");
    snprintf(writer->deviceId, sizeof(writer->deviceId), "%s", deviceId ? deviceId : "");
    writer->nominalFreqHz = nominalFreqHz;
    writer->sampleRateHz = sampleRateHz;
    writer->sampleCount = 0;

    return writer;
}

int comtrade_writer_sample(ComtradeWriter *writer, double timestampSeconds, const double *values)
{
    size_t idx;

    if (!writer || !values)
    {
        return -1;
    }

    if ((size_t)writer->sampleCount >= writer->sampleCap)
    {
        size_t newCap = writer->sampleCap ? writer->sampleCap * 2 : 4096;
        double *newTimestamps = (double *)realloc(writer->rawTimestamps, newCap * sizeof(double));
        double *newValues;

        if (!newTimestamps)
        {
            return -1;
        }
        writer->rawTimestamps = newTimestamps;

        newValues = (double *)realloc(writer->rawValues, newCap * (size_t)writer->numChannels * sizeof(double));
        if (!newValues)
        {
            return -1;
        }
        writer->rawValues = newValues;

        writer->sampleCap = newCap;
    }

    idx = (size_t)writer->sampleCount;
    writer->rawTimestamps[idx] = timestampSeconds;
    memcpy(&writer->rawValues[idx * (size_t)writer->numChannels], values, (size_t)writer->numChannels * sizeof(double));
    writer->sampleCount++;

    return 0;
}

void comtrade_writer_close(ComtradeWriter *writer)
{
    char cfgPath[600];
    char datPath[600];
    FILE *cfg;
    FILE *dat;
    double *scale = NULL;
    int numDigitalWords;
    size_t recordSize;
    unsigned char *datBuf = NULL;
    size_t s;
    int col;

    if (!writer)
    {
        return;
    }

    /* One scale factor per analog channel, sized so that channel's own
     * observed peak magnitude maps to the full +/-32767 INT16 range --
     * computed here (not in comtrade_writer_open()) because it needs to
     * have seen every sample first. */
    if (writer->numAnalog > 0)
    {
        scale = (double *)malloc(sizeof(double) * (size_t)writer->numAnalog);
    }
    if (scale)
    {
        for (col = 0; col < writer->numAnalog; col++)
        {
            int srcIdx = writer->displayOrder[col];
            double maxAbs = 0.0;

            for (s = 0; s < writer->sampleCount; s++)
            {
                double v = fabs(writer->rawValues[s * (size_t)writer->numChannels + (size_t)srcIdx]);
                if (v > maxAbs)
                {
                    maxAbs = v;
                }
            }
            scale[col] = (maxAbs > 0.0) ? (maxAbs / 32767.0) : 1.0;
        }
    }

    snprintf(cfgPath, sizeof(cfgPath), "%s.cfg", writer->basePath);
    cfg = fopen(cfgPath, "w");
    if (cfg)
    {
        const char *timestamp = "01/01/1970,00:00:00.000000";
        int an, dn;

        fprintf(cfg, "%s,%s,1999\n", writer->stationName, writer->deviceId);
        fprintf(cfg, "%d,%dA,%dD\n", writer->numChannels, writer->numAnalog, writer->numDigital);

        an = 1;
        for (col = 0; col < writer->numAnalog; col++)
        {
            const ComtradeChannelDef *ch = &writer->channels[writer->displayOrder[col]];
            double a = scale ? scale[col] : 1.0;
            fprintf(cfg, "%d,%s,%s,,%s,%.10g,0,0,-32768,32767,1,1,P\n", an++, ch->name,
                    ch->phase ? ch->phase : "", ch->unit ? ch->unit : "", a);
        }
        dn = 1;
        for (col = writer->numAnalog; col < writer->numChannels; col++)
        {
            const ComtradeChannelDef *ch = &writer->channels[writer->displayOrder[col]];
            fprintf(cfg, "%d,%s,,,0\n", dn++, ch->name);
        }

        fprintf(cfg, "%.10g\n", writer->nominalFreqHz);
        fprintf(cfg, "1\n");
        fprintf(cfg, "%.10g,%lu\n", writer->sampleRateHz, writer->sampleCount);
        fprintf(cfg, "%s\n", timestamp);
        fprintf(cfg, "%s\n", timestamp);
        fprintf(cfg, "BINARY\n");

        fclose(cfg);
    }

    /* Binary .dat body: one fixed-size record per sample --
     *   INT32U sample number, INT32U timestamp (us, elapsed from t=0),
     *   INT16 per analog channel (raw = round(value / scale)),
     *   INT16 words of packed digital channels (16 channels/word, LSB
     *   first, channel 0 in word 0's bit 0).
     * Native (little-endian) byte order -- this project only ever builds
     * for x86/x64 Windows, so no portable byte-swapping is implemented.
     * Built as one buffer and written with a single fwrite(), same as the
     * in-memory-then-single-write approach used for .cfg's sample count. */
    numDigitalWords = (writer->numDigital + 15) / 16;
    recordSize = sizeof(uint32_t) * 2
               + sizeof(int16_t) * (size_t)writer->numAnalog
               + sizeof(uint16_t) * (size_t)numDigitalWords;

    if (writer->sampleCount > 0)
    {
        datBuf = (unsigned char *)malloc(recordSize * (size_t)writer->sampleCount);
    }

    if (datBuf)
    {
        int wordsToAlloc = numDigitalWords > 0 ? numDigitalWords : 1;
        uint16_t *digWords = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)wordsToAlloc);

        for (s = 0; s < writer->sampleCount; s++)
        {
            unsigned char *rec = datBuf + s * recordSize;
            uint32_t sampleNum = (uint32_t)(s + 1);
            uint32_t timestampUs = (uint32_t)(writer->rawTimestamps[s] * 1.0e6 + 0.5);
            size_t off = 0;
            int digCol;

            memcpy(rec + off, &sampleNum, sizeof(sampleNum));
            off += sizeof(sampleNum);
            memcpy(rec + off, &timestampUs, sizeof(timestampUs));
            off += sizeof(timestampUs);

            for (col = 0; col < writer->numAnalog; col++)
            {
                int srcIdx = writer->displayOrder[col];
                double raw = writer->rawValues[s * (size_t)writer->numChannels + (size_t)srcIdx] / scale[col];
                long q = lround(raw);
                int16_t code;

                if (q > 32767) { q = 32767; }
                if (q < -32768) { q = -32768; }
                code = (int16_t)q;

                memcpy(rec + off, &code, sizeof(code));
                off += sizeof(code);
            }

            if (digWords)
            {
                memset(digWords, 0, sizeof(uint16_t) * (size_t)wordsToAlloc);
                for (digCol = 0; digCol < writer->numDigital; digCol++)
                {
                    int srcIdx = writer->displayOrder[writer->numAnalog + digCol];
                    double v = writer->rawValues[s * (size_t)writer->numChannels + (size_t)srcIdx];
                    if (v != 0.0)
                    {
                        digWords[digCol / 16] = (uint16_t)(digWords[digCol / 16] | (uint16_t)(1u << (digCol % 16)));
                    }
                }
                if (numDigitalWords > 0)
                {
                    memcpy(rec + off, digWords, sizeof(uint16_t) * (size_t)numDigitalWords);
                }
            }
        }

        free(digWords);
    }

    snprintf(datPath, sizeof(datPath), "%s.dat", writer->basePath);
    dat = fopen(datPath, "wb");
    if (dat)
    {
        if (datBuf && writer->sampleCount > 0)
        {
            fwrite(datBuf, 1, recordSize * (size_t)writer->sampleCount, dat);
        }
        fclose(dat);
    }

    free(datBuf);
    free(scale);
    free_writer(writer);
}
