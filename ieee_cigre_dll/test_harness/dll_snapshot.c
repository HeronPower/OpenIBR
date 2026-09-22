#include "dll_snapshot.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    DWORD rva;   /* offset from the module base */
    DWORD size;  /* VirtualSize */
    unsigned char *data;
} SnapshotSection;

struct DllSnapshot
{
    SnapshotSection *sections;
    int count;
};

static int is_capturable(const IMAGE_SECTION_HEADER *sec)
{
    if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE))
    {
        return 0;
    }
    /* PE section names are zero-padded when shorter than 8 bytes, so
     * comparing 5 bytes (".tls" + the null) is exact. */
    if (memcmp(sec->Name, ".tls", 5) == 0)
    {
        return 0;
    }
    return 1;
}

DllSnapshot *dll_snapshot_capture(HMODULE hDll)
{
    unsigned char *base = (unsigned char *)hDll;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sections;
    DllSnapshot *snap;
    int i, n, count;

    if (!hDll)
    {
        return NULL;
    }

    dos = (IMAGE_DOS_HEADER *)base;
    nt  = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    sections = IMAGE_FIRST_SECTION(nt);
    n = nt->FileHeader.NumberOfSections;

    snap = (DllSnapshot *)calloc(1, sizeof(DllSnapshot));
    if (!snap)
    {
        return NULL;
    }

    snap->sections = (SnapshotSection *)calloc((size_t)n, sizeof(SnapshotSection));
    if (!snap->sections)
    {
        free(snap);
        return NULL;
    }

    count = 0;
    for (i = 0; i < n; i++)
    {
        if (!is_capturable(&sections[i]))
        {
            continue;
        }

        snap->sections[count].rva  = sections[i].VirtualAddress;
        snap->sections[count].size = sections[i].Misc.VirtualSize;
        snap->sections[count].data = (unsigned char *)malloc(sections[i].Misc.VirtualSize);
        if (!snap->sections[count].data)
        {
            dll_snapshot_free(snap);
            return NULL;
        }
        memcpy(snap->sections[count].data, base + sections[i].VirtualAddress, sections[i].Misc.VirtualSize);
        count++;
    }

    snap->count = count;
    return snap;
}

int dll_snapshot_restore(const DllSnapshot *snap, HMODULE hDll)
{
    unsigned char *base = (unsigned char *)hDll;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *sections;
    int i, j, n;

    if (!snap || !hDll)
    {
        return -1;
    }

    dos = (IMAGE_DOS_HEADER *)base;
    nt  = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    sections = IMAGE_FIRST_SECTION(nt);
    n = nt->FileHeader.NumberOfSections;

    /* Confirm hDll has a matching section at each captured RVA/size
     * before writing anything -- don't partially restore. */
    for (i = 0; i < snap->count; i++)
    {
        int found = 0;
        for (j = 0; j < n; j++)
        {
            if (sections[j].VirtualAddress == snap->sections[i].rva)
            {
                if (sections[j].Misc.VirtualSize != snap->sections[i].size)
                {
                    return -1;
                }
                found = 1;
                break;
            }
        }
        if (!found)
        {
            return -1;
        }
    }

    for (i = 0; i < snap->count; i++)
    {
        memcpy(base + snap->sections[i].rva, snap->sections[i].data, snap->sections[i].size);
    }

    return 0;
}

void dll_snapshot_free(DllSnapshot *snap)
{
    int i;

    if (!snap)
    {
        return;
    }
    for (i = 0; i < snap->count; i++)
    {
        free(snap->sections[i].data);
    }
    free(snap->sections);
    free(snap);
}
