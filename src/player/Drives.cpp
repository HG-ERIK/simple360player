#include <xtl.h>
#include "Drives.h"
#include "Log.h"

namespace
{
    struct KString
    {
        USHORT Length;
        USHORT MaximumLength;
        const char* Buffer;
    };

    struct DriveInfo
    {
        const char*    link;
        const char*    device;
        const char*    root;
        const wchar_t* name;
    };

    const DriveInfo DRIVES[] =
    {
        { "\\??\\hdd:",  "\\Device\\Harddisk0\\Partition1", "hdd:\\",  L"Hard drive" },
        { "\\??\\usb0:", "\\Device\\Mass0",                 "usb0:\\", L"USB 1" },
        { "\\??\\usb1:", "\\Device\\Mass1",                 "usb1:\\", L"USB 2" },
        { "\\??\\usb2:", "\\Device\\Mass2",                 "usb2:\\", L"USB 3" },
        { "\\??\\dvd:",  "\\Device\\Cdrom0",                "dvd:\\",  L"Disc" },
    };

    KString Make( const char* s )
    {
        KString k;
        k.Length = (USHORT)strlen( s );
        k.MaximumLength = k.Length + 1;
        k.Buffer = s;
        return k;
    }
}

extern "C" LONG __stdcall ObCreateSymbolicLink( KString* link, KString* device );

void MountDrives()
{
    for( int i = 0; i < sizeof( DRIVES ) / sizeof( DRIVES[0] ); ++i )
    {
        KString link = Make( DRIVES[i].link ), device = Make( DRIVES[i].device );
        LONG status = ObCreateSymbolicLink( &link, &device );
        Log::Write( "Mount %s -> 0x%08X", DRIVES[i].root, status );
    }
}

std::vector<Drive> AvailableDrives()
{
    std::vector<Drive> out;
    for( int i = 0; i < sizeof( DRIVES ) / sizeof( DRIVES[0] ); ++i )
    {
        WIN32_FIND_DATA fd;
        std::string pattern = std::string( DRIVES[i].root ) + "*";
        HANDLE h = FindFirstFile( pattern.c_str(), &fd );
        if( h == INVALID_HANDLE_VALUE )
            continue;
        FindClose( h );
        Drive d;
        d.root = DRIVES[i].root;
        d.name = DRIVES[i].name;
        out.push_back( d );
    }
    return out;
}
