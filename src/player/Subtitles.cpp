#include <xtl.h>
#include <stdio.h>
#include <algorithm>
#include "Subtitles.h"

namespace
{
    bool IsUtf8( const std::string& s )
    {
        for( size_t i = 0; i < s.size(); )
        {
            unsigned char c = (unsigned char)s[i];
            int n = c < 0x80 ? 0 : ( c >> 5 ) == 6 ? 1 : ( c >> 4 ) == 14 ? 2 : ( c >> 3 ) == 30 ? 3 : -1;
            if( n < 0 || i + n >= s.size() + ( n == 0 ? 1 : 0 ) )
                return false;
            for( int k = 1; k <= n; ++k )
                if( ( (unsigned char)s[i + k] >> 6 ) != 2 )
                    return false;
            i += n + 1;
        }
        return true;
    }

    std::wstring FromUtf8( const std::string& s )
    {
        std::wstring out;
        for( size_t i = 0; i < s.size(); )
        {
            unsigned char c = (unsigned char)s[i];
            unsigned cp = c, n = 0;
            if( c >= 0xF0 )      { cp = c & 0x07; n = 3; }
            else if( c >= 0xE0 ) { cp = c & 0x0F; n = 2; }
            else if( c >= 0xC0 ) { cp = c & 0x1F; n = 1; }
            for( unsigned k = 1; k <= n && i + k < s.size(); ++k )
                cp = ( cp << 6 ) | ( (unsigned char)s[i + k] & 0x3F );
            i += n + 1;
            out += cp > 0xFFFF ? L'?' : (wchar_t)cp;
        }
        return out;
    }

    // Windows-1250 where it differs from Latin-1.
    wchar_t FromCp1250( unsigned char c )
    {
        static const struct { unsigned char from; wchar_t to; } map[] =
        {
            { 0x80, 0x20AC }, { 0x84, 0x201E }, { 0x85, 0x2026 }, { 0x8A, 0x0160 }, { 0x8C, 0x015A }, { 0x8D, 0x0164 },
            { 0x8E, 0x017D }, { 0x8F, 0x0179 }, { 0x91, 0x2018 }, { 0x92, 0x2019 }, { 0x93, 0x201C }, { 0x94, 0x201D },
            { 0x96, 0x2013 }, { 0x97, 0x2014 }, { 0x9A, 0x0161 }, { 0x9C, 0x015B }, { 0x9D, 0x0165 }, { 0x9E, 0x017E },
            { 0x9F, 0x017A }, { 0xA3, 0x0141 }, { 0xA5, 0x0104 }, { 0xAA, 0x015E }, { 0xAF, 0x017B }, { 0xB3, 0x0142 },
            { 0xB9, 0x0105 }, { 0xBA, 0x015F }, { 0xBF, 0x017C }, { 0xC0, 0x0154 }, { 0xC3, 0x0102 }, { 0xC5, 0x0139 },
            { 0xC6, 0x0106 }, { 0xC8, 0x010C }, { 0xCA, 0x0118 }, { 0xCC, 0x011A }, { 0xCF, 0x010E }, { 0xD0, 0x0110 },
            { 0xD1, 0x0143 }, { 0xD2, 0x0147 }, { 0xD5, 0x0150 }, { 0xD8, 0x0158 }, { 0xD9, 0x016E }, { 0xDB, 0x0170 },
            { 0xDE, 0x0162 }, { 0xE0, 0x0155 }, { 0xE3, 0x0103 }, { 0xE5, 0x013A }, { 0xE6, 0x0107 }, { 0xE8, 0x010D },
            { 0xEA, 0x0119 }, { 0xEC, 0x011B }, { 0xEF, 0x010F }, { 0xF0, 0x0111 }, { 0xF1, 0x0144 }, { 0xF2, 0x0148 },
            { 0xF5, 0x0151 }, { 0xF8, 0x0159 }, { 0xF9, 0x016F }, { 0xFB, 0x0171 }, { 0xFE, 0x0163 },
        };
        for( int i = 0; i < sizeof( map ) / sizeof( map[0] ); ++i )
            if( map[i].from == c )
                return map[i].to;
        return (wchar_t)c;
    }

    // Converts to UTF-8 so all text goes through one path.
    std::string Cp1250ToUtf8( const std::string& s )
    {
        std::string out;
        for( size_t i = 0; i < s.size(); ++i )
        {
            unsigned cp = (unsigned char)s[i] < 0x80 ? (unsigned char)s[i] : FromCp1250( (unsigned char)s[i] );
            if( cp < 0x80 )
                out += (char)cp;
            else if( cp < 0x800 )
            {
                out += (char)( 0xC0 | ( cp >> 6 ) );
                out += (char)( 0x80 | ( cp & 0x3F ) );
            }
            else
            {
                out += (char)( 0xE0 | ( cp >> 12 ) );
                out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
                out += (char)( 0x80 | ( cp & 0x3F ) );
            }
        }
        return out;
    }

    bool ParseTime( const char* s, double& t )
    {
        int h, m, sec, ms;
        if( sscanf_s( s, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &ms ) != 4 )
            return false;
        t = h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
        return true;
    }
}

std::wstring CleanSubtitle( const std::string& raw )
{
    std::wstring s = FromUtf8( raw ), out;
    for( size_t i = 0; i < s.size(); ++i )
    {
        wchar_t c = s[i];
        if( c == L'{' )                                     // ASS override, e.g. {\i1}
        {
            size_t end = s.find( L'}', i );
            if( end != std::wstring::npos ) { i = end; continue; }
        }
        if( c == L'<' )                                     // SRT tags: <i>, <b>, <font ...>
        {
            size_t end = s.find( L'>', i );
            if( end != std::wstring::npos && end - i < 40 ) { i = end; continue; }
        }
        if( c == L'\\' && i + 1 < s.size() && ( s[i + 1] == L'N' || s[i + 1] == L'n' ) )
        {
            out += L'\n';
            ++i;
            continue;
        }
        if( c == L'\\' && i + 1 < s.size() && s[i + 1] == L'h' )
        {
            out += L' ';
            ++i;
            continue;
        }
        if( c == L'\r' )
            continue;
        out += c;
    }
    while( !out.empty() && ( out[out.size() - 1] == L'\n' || out[out.size() - 1] == L' ' ) )
        out.erase( out.size() - 1 );
    while( !out.empty() && ( out[0] == L'\n' || out[0] == L' ' ) )
        out.erase( 0, 1 );
    return out;
}

bool LoadSrt( const std::string& path, std::vector<SubtitleCue>& out )
{
    FILE* f = fopen( path.c_str(), "rb" );
    if( !f )
        return false;
    std::string data;
    char buf[4096];
    size_t n;
    while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 && data.size() < 4 * 1024 * 1024 )
        data.append( buf, n );
    fclose( f );
    if( data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF )
        data.erase( 0, 3 );
    if( !IsUtf8( data ) )
        data = Cp1250ToUtf8( data );

    out.clear();
    size_t pos = 0;
    while( pos < data.size() )
    {
        size_t eol = data.find( '\n', pos );
        std::string line = data.substr( pos, eol == std::string::npos ? std::string::npos : eol - pos );
        pos = eol == std::string::npos ? data.size() : eol + 1;
        size_t arrow = line.find( "-->" );
        double a, b;
        if( arrow == std::string::npos || !ParseTime( line.c_str(), a ) || !ParseTime( line.c_str() + arrow + 3 + strspn( line.c_str() + arrow + 3, " " ), b ) )
            continue;
        // Text lines until a blank line.
        std::string text;
        while( pos < data.size() )
        {
            eol = data.find( '\n', pos );
            std::string t = data.substr( pos, eol == std::string::npos ? std::string::npos : eol - pos );
            pos = eol == std::string::npos ? data.size() : eol + 1;
            if( !t.empty() && t[t.size() - 1] == '\r' )
                t.erase( t.size() - 1 );
            if( t.empty() )
                break;
            if( !text.empty() )
                text += "\n";
            text += t;
        }
        SubtitleCue cue;
        cue.start = a;
        cue.end = b;
        cue.text = text;
        out.push_back( cue );
    }
    struct ByStart
    {
        bool operator()( const SubtitleCue& a, const SubtitleCue& b ) const { return a.start < b.start; }
    };
    std::stable_sort( out.begin(), out.end(), ByStart() );
    return !out.empty();
}
