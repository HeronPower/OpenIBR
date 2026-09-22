/*
 * comtrade_writer.h
 *
 * Minimal binary COMTRADE (IEEE C37.111-1999-subset) writer. No
 * dependency on the DLL interface types or any controller struct -- it
 * only ever sees plain paths, doubles, and channel name/unit strings.
 *
 * Notes:
 *   - .dat is the IEEE C37.111 binary variant: each record is INT32U
 *     sample number, INT32U timestamp (us), one INT16 per analog channel,
 *     then packed INT16 words of digital channels (16/word, LSB first).
 *     Native little-endian byte order only.
 *   - Each analog channel's INT16 scale factor is computed automatically
 *     from its own observed peak over the run (peak -> +/-32767), so
 *     encoding is lossy (~4-5 significant digits).
 *   - The timestamp field wraps past ~4295s (~71.6 min) of recorded time.
 *   - Samples are buffered in memory and encoded/written once, in
 *     comtrade_writer_close() -- a crash mid-run loses the whole trace,
 *     not just the tail.
 *   - Digital bit-packing exists for API completeness but is unexercised;
 *     every channel this project records is COMTRADE_CH_ANALOG.
 *   - One constant sample rate for the whole record; correct for a
 *     fixed-step host, not a variable-step one.
 *   - .cfg start/trigger timestamps are always 01/01/1970,00:00:00 (sim
 *     t=0). Channel min/max bounds are the raw INT16 range.
 */

#ifndef COMTRADE_WRITER_H
#define COMTRADE_WRITER_H

typedef enum
{
    COMTRADE_CH_ANALOG,
    COMTRADE_CH_DIGITAL,
} ComtradeChannelKind;

typedef struct
{
    const char *name;   /* channel id string, e.g. "acMonitor_OUT.frequency" */
    const char *unit;   /* engineering unit; ignored for digital channels */
    const char *phase;  /* COMTRADE "ph" field, e.g. "A"/"B"/"C" -- many viewers
                          * (e.g. SynchroWave) use this for consistent per-phase
                          * trace coloring; NULL/"" leaves it blank. */
    ComtradeChannelKind kind;
} ComtradeChannelDef;

typedef struct ComtradeWriter ComtradeWriter; /* opaque */

/*
 * Remembers everything needed to write <basePath>.cfg and <basePath>.dat
 * once the run finishes -- neither file is touched yet (the total sample
 * count and each analog channel's scale factor aren't known until
 * comtrade_writer_close(); see the file-level comment above).
 *
 * 'channels' is copied by value (the array of structs, not the strings it
 * points to) -- name/unit pointers must stay valid for the writer's whole
 * lifetime, so pass string literals or entries from a static table (e.g.
 * the generated signal catalog), not stack/heap buffers that might be
 * freed before comtrade_writer_close().
 *
 * Returns NULL on failure (bad path, out of memory). Callers must treat
 * NULL as "disable logging for this run," never as a fatal error.
 *
 * Populate each channel's 'phase' field (see ComtradeChannelDef) for any
 * three-phase quantity -- viewers like SynchroWave use it for consistent
 * per-phase trace coloring. Without it, coloring falls back to channel
 * index/order, which silently reshuffles every time the channel count
 * changes (e.g. editing debug_signals_config.yaml).
 */
ComtradeWriter *comtrade_writer_open(const char *basePath,
                                      const char *stationName,
                                      const char *deviceId,
                                      double nominalFreqHz,
                                      double sampleRateHz,
                                      const ComtradeChannelDef *channels,
                                      int numChannels);

/*
 * Appends one sample's raw values to an in-memory buffer (not encoded or
 * written to disk until comtrade_writer_close(), once every channel's
 * scale factor is known). 'values' must have numChannels entries, in the
 * same order as the 'channels' array passed to comtrade_writer_open()
 * (the writer reorders internally so the .cfg/.dat put all analog
 * channels before all digital ones, per the COMTRADE layout convention --
 * callers don't need to pre-sort anything). 'timestampSeconds' is elapsed
 * sim time since the writer was opened. Returns 0 on success, nonzero if
 * writer/values is NULL or on allocation failure while growing the buffer.
 */
int comtrade_writer_sample(ComtradeWriter *writer, double timestampSeconds, const double *values);

/*
 * Computes each analog channel's scale factor, writes the finished
 * <basePath>.cfg (now that the total sample count is also known), encodes
 * every buffered sample into the binary <basePath>.dat body and writes it
 * in one fwrite(), then frees the writer. Safe to call with NULL.
 */
void comtrade_writer_close(ComtradeWriter *writer);

#endif /* COMTRADE_WRITER_H */
