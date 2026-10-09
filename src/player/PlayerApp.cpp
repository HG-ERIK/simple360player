// Simple 360 Player - plays video files from the console's drives.
#include <xtl.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include "App.h"
#include "Font.h"
#include "Draw.h"
#include "Log.h"
#include "Guard.h"
#include "FFPlayer.h"
#include "ffglue.h"
#include "CpuMeter.h"
#include "PlayerScreen.h"
#include "Drives.h"
#include "Subtitles.h"

namespace
{
    const DWORD COLOR_ACCENT = 0xff3fa9f5;
    const DWORD COLOR_TEXT   = 0xffeeeeee;
    const DWORD COLOR_DIM    = 0xff8a8a8a;
    const DWORD COLOR_ERROR  = 0xffff6060;

    const char* const SETTINGS_PATH = "game:\\settings.ini";
    const char* const VIDEO_EXTENSIONS[] = { ".mkv", ".mp4", ".m4v", ".mov", ".avi", ".ts", ".m2ts", ".mpg",
                                             ".mpeg", ".wmv", ".flv", ".webm", 0 };

    struct Entry
    {
        std::wstring name;
        std::string  path;
        bool         dir;
        __int64      size;
    };

    std::string Lower( std::string s )
    {
        for( size_t i = 0; i < s.size(); ++i )
            if( s[i] >= 'A' && s[i] <= 'Z' )
                s[i] = (char)( s[i] - 'A' + 'a' );
        return s;
    }

    bool EndsWith( const std::string& s, const char* tail )
    {
        size_t n = strlen( tail );
        return s.size() >= n && s.compare( s.size() - n, n, tail ) == 0;
    }

    bool IsVideo( const std::string& name )
    {
        std::string l = Lower( name );
        for( int i = 0; VIDEO_EXTENSIONS[i]; ++i )
            if( EndsWith( l, VIDEO_EXTENSIONS[i] ) )
                return true;
        return false;
    }

    std::wstring Widen( const std::string& s )
    {
        std::wstring w;
        for( size_t i = 0; i < s.size(); ++i )
            w += (wchar_t)(unsigned char)s[i];
        return w;
    }

    std::string BaseName( const std::string& path )
    {
        size_t slash = path.find_last_of( '\\' );
        return slash == std::string::npos ? path : path.substr( slash + 1 );
    }

    std::string Parent( const std::string& dir )
    {
        // "usb0:\Movies\" -> "usb0:\", "usb0:\" -> "" (the drive list)
        std::string d = dir.substr( 0, dir.size() - 1 );
        size_t slash = d.find_last_of( '\\' );
        return slash == std::string::npos ? "" : d.substr( 0, slash + 1 );
    }

    std::wstring SizeText( __int64 bytes )
    {
        wchar_t buf[32];
        if( bytes >= 1024LL * 1024 * 1024 )
            swprintf_s( buf, L"%.1f GB", bytes / ( 1024.0 * 1024 * 1024 ) );
        else
            swprintf_s( buf, L"%.0f MB", bytes / ( 1024.0 * 1024 ) );
        return buf;
    }

    void FfmpegLog( const char* line )
    {
        std::string s( line );
        while( !s.empty() && ( s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r' ) )
            s.erase( s.size() - 1 );
        if( !s.empty() )
            Log::Write( "ffmpeg: %s", s.c_str() );
    }
}

class PlayerApp : public App, public PlayerHost
{
public:
    PlayerApp() : m_sel( 0 ), m_first( 0 ), m_repeatTick( 0 ), m_heldDir( 0 ), m_playing( false ), m_audioIndex( -1 ),
                  m_subChoice( SUB_OFF ), m_brightness( 0 ), m_stats( false ), m_frameStart( 0 ), m_renderMs( 0 ),
                  m_toastTick( 0 ) {}

private:
    virtual HRESULT Initialize();
    virtual HRESULT Update();
    virtual HRESULT Render();

    void OpenDir( const std::string& dir );
    void UpdateBrowser( Pad* pad );
    void RenderBrowser();
    void Play( const Entry& e, double start );
    void Toast( const std::wstring& text );
    void LoadSettings();
    void SaveSettings();
    void EndFrame();

    // PlayerHost
    virtual void         Seek( double seconds ) { m_player.SeekAbsolute( seconds ); }
    virtual void         Leave() { m_player.Stop(); }
    virtual void         CancelStart() { m_player.Stop(); }
    virtual std::wstring Title() const { return m_title; }
    virtual std::wstring StartMessage() const { return L"Opening..."; }
    virtual std::wstring Tag() const { return L""; }
    virtual bool         ShowStats() const { return m_stats; }
    virtual std::wstring StatsMode() const { return L"4 decode threads   local file"; }
    virtual int          Brightness() const { return m_brightness; }
    virtual void         DrawOverlay( float bottom );
    virtual void         BuildMenu( int page, MenuPage& out );
    virtual MenuMove     Choose( int page, int sel );
    virtual MenuMove     Back( int page );
    virtual void         SliderStep( int page, int step );
    virtual void         AspectChanged() { SaveSettings(); }

    enum { PAGE_MAIN, PAGE_AUDIO, PAGE_SUBS, PAGE_BRIGHTNESS, PAGE_ASPECT };
    enum { ROW_AUDIO, ROW_SUBS, ROW_BRIGHTNESS, ROW_ASPECT, ROW_STATS };
    enum { SUB_OFF = -2, SUB_FILE = -1 };      // else an embedded track's container index
    std::vector<FgTrack> TracksOf( int type );
    std::wstring TrackLabel( const FgTrack& t ) const;
    void         ApplySubtitles();

    Font                 m_font, m_titleFont;
    FFPlayer             m_player;
    PlayerScreen         m_screen;
    CpuMeter             m_cpu;

    std::string          m_dir;             // "" = drive list
    std::vector<Entry>   m_entries;
    int                  m_sel, m_first;
    DWORD                m_repeatTick;
    int                  m_heldDir;

    bool                 m_playing;
    Entry                m_current;
    std::wstring         m_title;
    int                  m_audioIndex;      // container index, -1 = first
    int                  m_subChoice;
    std::vector<SubtitleCue> m_fileSubs;
    std::string          m_fileSubsName;

    int                  m_brightness;
    bool                 m_stats;
    DWORD                m_frameStart;
    float                m_renderMs;
    std::wstring         m_toast;
    DWORD                m_toastTick;
};

static void RunGuarded( PlayerApp& app )
{
    __try
    {
        app.Run();
    }
    __except( Guard_Filter( GetExceptionCode(), GetExceptionInformation(), "main" ) )
    {
    }
}

VOID __cdecl main()
{
    Log::Open( "game:\\playerlog.txt" );
    Log::Write( "Simple 360 Player starting" );
    PlayerApp app;
    app.m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    Guard_Start();
    RunGuarded( app );
}

HRESULT PlayerApp::Initialize()
{
    fg_set_stereo( 0 );     // 5.1 AC3/DTS stay 5.1; the console mixes down if needed
    Draw::Startup( m_pd3dDevice, m_d3dpp.BackBufferWidth, m_d3dpp.BackBufferHeight );
    Font::Startup( m_pd3dDevice );
    if( FAILED( m_font.Create( "game:\\Media\\Fonts\\Text_16.fnt" ) ) ||
        FAILED( m_titleFont.Create( "game:\\Media\\Fonts\\Text_22.fnt" ) ) )
        return E_FAIL;
    m_font.SetWindow( 0, 0, 1280, 720 );
    m_titleFont.SetWindow( 0, 0, 1280, 720 );

    MountDrives();
    fg_init( FfmpegLog );
    fg_set_decode_threads( 4 );
    m_player.Startup( m_pd3dDevice );
    m_screen.Init( &m_player, &m_font, &m_titleFont, &m_cpu, this, COLOR_ACCENT );
    LoadSettings();
    m_player.SetBrightness( m_brightness * 0.02f );
    OpenDir( m_dir );
    if( m_entries.empty() && !m_dir.empty() )
        OpenDir( "" );          // the remembered folder is gone (USB unplugged)
    return S_OK;
}

void PlayerApp::LoadSettings()
{
    FILE* f = fopen( SETTINGS_PATH, "rb" );
    if( !f )
        return;
    char line[600];
    while( fgets( line, sizeof( line ), f ) )
    {
        std::string s( line );
        while( !s.empty() && ( s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r' ) )
            s.erase( s.size() - 1 );
        size_t eq = s.find( '=' );
        if( eq == std::string::npos )
            continue;
        std::string k = s.substr( 0, eq ), v = s.substr( eq + 1 );
        if( k == "brightness" )   m_brightness = max( -5, min( 10, atoi( v.c_str() ) ) );
        else if( k == "stats" )   m_stats = v == "1";
        else if( k == "folder" )  m_dir = v;
        else if( k == "aspect" )  m_player.SetAspect( atoi( v.c_str() ) );
    }
    fclose( f );
}

void PlayerApp::SaveSettings()
{
    FILE* f = fopen( SETTINGS_PATH, "wb" );
    if( !f )
        return;
    fprintf( f, "brightness=%d\r\nstats=%d\r\naspect=%d\r\nfolder=%s\r\n", m_brightness, m_stats ? 1 : 0,
             m_player.Aspect(), m_dir.c_str() );
    fclose( f );
}

void PlayerApp::Toast( const std::wstring& text )
{
    m_toast = text;
    m_toastTick = GetTickCount();
}

//--------------------------------------------------------------------------------------
// Browser
//--------------------------------------------------------------------------------------
void PlayerApp::OpenDir( const std::string& dir )
{
    m_entries.clear();
    m_sel = m_first = 0;
    m_dir = dir;
    if( dir.empty() )
    {
        std::vector<Drive> drives = AvailableDrives();
        for( size_t i = 0; i < drives.size(); ++i )
        {
            Entry e;
            e.name = drives[i].name;
            e.path = drives[i].root;
            e.dir = true;
            e.size = 0;
            m_entries.push_back( e );
        }
        return;
    }

    std::vector<Entry> dirs, files;
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile( ( dir + "*" ).c_str(), &fd );
    if( h != INVALID_HANDLE_VALUE )
    {
        do
        {
            std::string name = fd.cFileName;
            Entry e;
            e.name = Widen( name );
            e.dir = ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
            e.path = dir + name + ( e.dir ? "\\" : "" );
            e.size = ( (__int64)fd.nFileSizeHigh << 32 ) | fd.nFileSizeLow;
            if( e.dir )
                dirs.push_back( e );
            else if( IsVideo( name ) )
                files.push_back( e );
        } while( FindNextFile( h, &fd ) );
        FindClose( h );
    }
    struct ByName
    {
        bool operator()( const Entry& a, const Entry& b ) const { return _wcsicmp( a.name.c_str(), b.name.c_str() ) < 0; }
    };
    std::sort( dirs.begin(), dirs.end(), ByName() );
    std::sort( files.begin(), files.end(), ByName() );
    m_entries = dirs;
    m_entries.insert( m_entries.end(), files.begin(), files.end() );
}

void PlayerApp::UpdateBrowser( Pad* pad )
{
    const WORD dirs = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN;
    WORD pressed = pad->wPressedButtons;
    int held = ( pad->wButtons & XINPUT_GAMEPAD_DPAD_UP ) || pad->fY1 > 0.6f ? -1 :
               ( pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN ) || pad->fY1 < -0.6f ? 1 : 0;
    DWORD now = GetTickCount();
    int move = 0;
    if( held != m_heldDir )
    {
        m_heldDir = held;
        move = held;
        m_repeatTick = now + 350;
    }
    else if( held && now >= m_repeatTick )
    {
        move = held;
        m_repeatTick = now + 70;
    }
    (void)dirs;
    int count = (int)m_entries.size();
    if( pressed & XINPUT_GAMEPAD_LEFT_SHOULDER )  move = -10;
    if( pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER ) move = 10;
    if( move && count )
        m_sel = max( 0, min( count - 1, m_sel + move ) );

    if( ( pressed & XINPUT_GAMEPAD_A ) && m_sel < count )
    {
        Entry e = m_entries[m_sel];
        if( e.dir )
        {
            OpenDir( e.path );
            SaveSettings();
        }
        else
            Play( e, 0 );
    }
    else if( pressed & XINPUT_GAMEPAD_B )
    {
        if( !m_dir.empty() )
        {
            std::string from = m_dir;
            OpenDir( Parent( m_dir ) );
            for( size_t i = 0; i < m_entries.size(); ++i )
                if( m_entries[i].path == from )
                    m_sel = (int)i;
            SaveSettings();
        }
    }
    else if( pressed & XINPUT_GAMEPAD_Y )
    {
        int sel = m_sel;
        OpenDir( m_dir );
        m_sel = min( sel, max( 0, (int)m_entries.size() - 1 ) );
    }
    else if( pressed & XINPUT_GAMEPAD_BACK )
    {
        Log::Write( "Exit to dashboard" );
        XLaunchNewImage( XLAUNCH_KEYWORD_DEFAULT_APP, 0 );
    }
}

void PlayerApp::RenderBrowser()
{
    FillGradient( 0xff1c1f24, 0xff0a0b0d );
    const float left = 110, right = 1170, top = 150, rowH = 48;
    const int rows = 10;

    m_titleFont.Begin();
    m_titleFont.SetScaleFactors( 1.3f, 1.3f );
    m_titleFont.DrawText( left, 52, COLOR_ACCENT, L"SIMPLE 360 PLAYER" );
    m_titleFont.SetScaleFactors( 1.0f, 1.0f );
    m_titleFont.End();
    std::wstring where = m_dir.empty() ? L"Choose a drive" : Widen( m_dir );
    m_font.Begin();
    m_font.DrawText( left, 104, COLOR_DIM, where.c_str(), FONT_TRUNCATED, right - left );
    m_font.End();

    int count = (int)m_entries.size();
    if( m_sel < m_first )
        m_first = m_sel;
    if( m_sel >= m_first + rows )
        m_first = m_sel - rows + 1;
    if( count == 0 )
    {
        m_titleFont.Begin();
        m_titleFont.DrawText( left, top + 20, COLOR_DIM, m_dir.empty() ? L"No drives found" : L"No videos or folders here" );
        m_titleFont.End();
    }
    for( int i = m_first; i < count && i < m_first + rows; ++i )
    {
        const Entry& e = m_entries[i];
        float y = top + ( i - m_first ) * rowH;
        bool sel = i == m_sel;
        if( sel )
        {
            Draw::Rect( left - 16, y - 8, right, y + rowH - 12, 0xFF2A2F36 );
            Draw::Rect( left - 16, y - 8, left - 11, y + rowH - 12, COLOR_ACCENT );
        }
        // A small folder tab or a play triangle in front of the name.
        if( e.dir )
        {
            Draw::Rect( left, y + 8, left + 22, y + 26, sel ? COLOR_ACCENT : 0xFF6E7681 );
            Draw::Rect( left, y + 5, left + 10, y + 9, sel ? COLOR_ACCENT : 0xFF6E7681 );
        }
        else
        {
            for( int k = 0; k < 9; ++k )
                Draw::Rect( left + 4 + k * 2, y + 6 + k, left + 6 + k * 2, y + 28 - k, sel ? COLOR_ACCENT : 0xFF6E7681 );
        }
        std::wstring size = e.dir ? L"" : SizeText( e.size );
        m_titleFont.Begin();
        m_titleFont.DrawText( left + 40, y, sel ? COLOR_TEXT : 0xFFBBBBBB, e.name.c_str(), FONT_TRUNCATED, right - left - 200 );
        m_titleFont.End();
        if( !size.empty() )
        {
            m_font.Begin();
            m_font.DrawText( right - 20, y + 4, COLOR_DIM, size.c_str(), FONT_RIGHT );
            m_font.End();
        }
    }
    if( count > rows )
    {
        float trackTop = top - 8, trackH = rows * rowH;
        float h = trackH * rows / count, y = trackTop + ( trackH - h ) * m_first / ( count - rows );
        Draw::Rect( right + 12, trackTop, right + 16, trackTop + trackH, 0x30FFFFFF );
        Draw::Rect( right + 12, y, right + 16, y + h, COLOR_ACCENT );
    }

    m_font.Begin();
    m_font.DrawText( left, 640, COLOR_DIM, m_dir.empty() ? L"A  Open      Y  Refresh      BACK  Exit" :
                                                          L"A  Open / play      B  Up      LB/RB  Page      Y  Refresh      BACK  Exit" );
    m_font.End();

    if( !m_toast.empty() && GetTickCount() - m_toastTick < 5000 )
    {
        float w = m_font.GetTextWidth( m_toast.c_str() ) + 40;
        Draw::Rect( 640 - w / 2, 560, 640 + w / 2, 600, 0xE0101010 );
        m_font.Begin();
        m_font.DrawText( 640, 568, COLOR_ERROR, m_toast.c_str(), FONT_CENTER_X );
        m_font.End();
    }
}

//--------------------------------------------------------------------------------------
// Playback
//--------------------------------------------------------------------------------------
void PlayerApp::Play( const Entry& e, double start )
{
    m_current = e;
    m_title = e.name;
    size_t dot = m_title.find_last_of( L'.' );
    if( dot != std::wstring::npos )
        m_title.erase( dot );
    m_audioIndex = -1;
    fg_set_audio_track( -1 );

    // An .srt next to the video (movie.srt, movie.en.srt, ...) is shown by default.
    m_fileSubs.clear();
    m_fileSubsName.clear();
    std::string folder = e.path.substr( 0, e.path.find_last_of( '\\' ) + 1 );
    std::string base = Lower( BaseName( e.path ) );
    base = base.substr( 0, base.find_last_of( '.' ) );
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile( ( folder + "*" ).c_str(), &fd );
    if( h != INVALID_HANDLE_VALUE )
    {
        do
        {
            std::string name = Lower( fd.cFileName );
            if( EndsWith( name, ".srt" ) && name.compare( 0, base.size(), base ) == 0 &&
                LoadSrt( folder + fd.cFileName, m_fileSubs ) )
            {
                m_fileSubsName = fd.cFileName;
                break;
            }
        } while( FindNextFile( h, &fd ) );
        FindClose( h );
    }
    m_subChoice = m_fileSubs.empty() ? SUB_OFF : SUB_FILE;
    Log::Write( "Play %s (%s)", e.path.c_str(), m_fileSubs.empty() ? "no subtitle file" : m_fileSubsName.c_str() );

    std::vector<std::string> urls( 1, e.path );
    std::vector<std::wstring> labels( 1, L"file" );
    m_player.Open( urls, labels, "", start );
    ApplySubtitles();
    m_screen.Reset();
    m_playing = true;
}

void PlayerApp::ApplySubtitles()
{
    if( m_subChoice == SUB_FILE )
        m_player.SetExternalSubtitles( m_fileSubs );
    else
    {
        m_player.SetExternalSubtitles( std::vector<SubtitleCue>() );
        m_player.SetSubtitleTrack( m_subChoice >= 0 ? m_subChoice : -1 );
    }
}

void PlayerApp::DrawOverlay( float bottom )
{
    std::wstring text = CleanSubtitle( m_player.SubtitleAt( m_player.Position() ) );
    if( text.empty() )
        return;

    // Wrap each line to the screen width, then stack the lines up from the bottom.
    const float maxW = 1000, lineH = 40;
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while( pos <= text.size() )
    {
        size_t nl = text.find( L'\n', pos );
        std::wstring para = text.substr( pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos );
        pos = nl == std::wstring::npos ? text.size() + 1 : nl + 1;
        std::wstring line;
        size_t w = 0;
        while( w <= para.size() )
        {
            size_t sp = para.find( L' ', w );
            std::wstring word = para.substr( w, sp == std::wstring::npos ? std::wstring::npos : sp - w );
            w = sp == std::wstring::npos ? para.size() + 1 : sp + 1;
            std::wstring candidate = line.empty() ? word : line + L" " + word;
            if( !line.empty() && m_titleFont.GetTextWidth( candidate.c_str() ) > maxW )
            {
                lines.push_back( line );
                line = word;
            }
            else
                line = candidate;
        }
        lines.push_back( line );
    }
    float y = bottom - lines.size() * lineH;
    D3DRECT window;
    m_titleFont.GetWindow( window );
    m_titleFont.SetWindow( 0, 0, 1280, 720 );
    m_titleFont.Begin();
    for( size_t i = 0; i < lines.size(); ++i, y += lineH )
    {
        static const float dx[4] = { -2, 2, 0, 0 }, dy[4] = { 0, 0, -2, 2 };
        for( int k = 0; k < 4; ++k )
            m_titleFont.DrawText( 640 + dx[k], y + dy[k], 0xFF000000, lines[i].c_str(), FONT_CENTER_X );
        m_titleFont.DrawText( 640, y, 0xFFFFFFFF, lines[i].c_str(), FONT_CENTER_X );
    }
    m_titleFont.End();
    m_titleFont.SetWindow( window );
}

std::vector<FgTrack> PlayerApp::TracksOf( int type )
{
    std::vector<FgTrack> all = m_player.Tracks(), out;
    for( size_t i = 0; i < all.size(); ++i )
        if( all[i].type == type && ( type != 3 || all[i].text ) )
            out.push_back( all[i] );
    return out;
}

std::wstring PlayerApp::TrackLabel( const FgTrack& t ) const
{
    // Track titles usually say everything ("English DD 5.1"); otherwise language and codec.
    if( t.title[0] )
        return Widen( t.title );
    std::wstring codec = Widen( t.codec );
    for( size_t i = 0; i < codec.size(); ++i )
        codec[i] = towupper( codec[i] );
    std::wstring lang = Widen( t.language );
    if( lang.empty() )
        return codec.empty() ? L"Track" : codec;
    return codec.empty() ? lang : lang + L"  (" + codec + L")";
}

void PlayerApp::BuildMenu( int page, MenuPage& out )
{
    MenuEntry e;
    switch( page )
    {
    case PAGE_MAIN:
        {
            out.title = L"Options";
            e.opens = true;
            std::vector<FgTrack> audio = TracksOf( 2 );
            e.label = L"Audio";
            e.value = L"Default";
            for( size_t i = 0; i < audio.size(); ++i )
                if( audio[i].index == m_audioIndex || ( m_audioIndex < 0 && i == 0 ) )
                    e.value = TrackLabel( audio[i] );
            out.entries.push_back( e );
            e.label = L"Subtitles";
            e.value = m_subChoice == SUB_OFF ? L"Off" : m_subChoice == SUB_FILE ? Widen( m_fileSubsName ) : L"On";
            std::vector<FgTrack> subs = TracksOf( 3 );
            for( size_t i = 0; i < subs.size(); ++i )
                if( subs[i].index == m_subChoice )
                    e.value = TrackLabel( subs[i] );
            out.entries.push_back( e );
            wchar_t buf[16];
            swprintf_s( buf, L"%+d", m_brightness );
            e.label = L"Brightness";
            e.value = buf;
            out.entries.push_back( e );
            e.label = L"Aspect ratio";
            e.value = FFPlayer::AspectName( m_player.Aspect() );
            out.entries.push_back( e );
            e.opens = false;
            e.label = L"Stats for nerds";
            e.value = m_stats ? L"On" : L"Off";
            out.entries.push_back( e );
        }
        break;

    case PAGE_AUDIO:
        {
            out.title = L"Audio";
            std::vector<FgTrack> audio = TracksOf( 2 );
            for( size_t i = 0; i < audio.size(); ++i )
            {
                e.label = TrackLabel( audio[i] );
                e.checked = audio[i].index == m_audioIndex || ( m_audioIndex < 0 && i == 0 );
                out.entries.push_back( e );
            }
            if( out.entries.empty() )
            {
                e.label = L"No audio tracks";
                out.entries.push_back( e );
            }
        }
        break;

    case PAGE_SUBS:
        {
            out.title = L"Subtitles";
            e.label = L"Off";
            e.checked = m_subChoice == SUB_OFF;
            out.entries.push_back( e );
            if( !m_fileSubs.empty() )
            {
                e.label = Widen( m_fileSubsName );
                e.checked = m_subChoice == SUB_FILE;
                out.entries.push_back( e );
            }
            std::vector<FgTrack> subs = TracksOf( 3 );
            for( size_t i = 0; i < subs.size(); ++i )
            {
                e.label = TrackLabel( subs[i] );
                e.checked = subs[i].index == m_subChoice;
                out.entries.push_back( e );
            }
        }
        break;

    case PAGE_BRIGHTNESS:
        out.title = L"Brightness";
        out.slider = true;
        out.value = m_brightness;
        out.minValue = -5;
        out.maxValue = 10;
        break;

    case PAGE_ASPECT:
        out.title = L"Aspect ratio";
        for( int i = 0; i < FFPlayer::ASPECT_COUNT; ++i )
        {
            e.label = FFPlayer::AspectName( i );
            e.checked = i == m_player.Aspect();
            out.entries.push_back( e );
        }
        break;
    }
}

MenuMove PlayerApp::Choose( int page, int sel )
{
    switch( page )
    {
    case PAGE_MAIN:
        if( sel == ROW_AUDIO || sel == ROW_SUBS )
        {
            int sub = sel == ROW_AUDIO ? PAGE_AUDIO : PAGE_SUBS;
            MenuPage list;
            BuildMenu( sub, list );
            int start = 0;
            for( size_t i = 0; i < list.entries.size(); ++i )
                if( list.entries[i].checked )
                    start = (int)i;
            return MenuMove::Open( sub, start );
        }
        if( sel == ROW_BRIGHTNESS )
            return MenuMove::Open( PAGE_BRIGHTNESS, 0 );
        if( sel == ROW_ASPECT )
            return MenuMove::Open( PAGE_ASPECT, m_player.Aspect() );
        if( sel == ROW_STATS )
        {
            m_stats = !m_stats;
            SaveSettings();
        }
        return MenuMove::Stay();

    case PAGE_AUDIO:
        {
            std::vector<FgTrack> audio = TracksOf( 2 );
            if( sel < (int)audio.size() && audio[sel].index != m_audioIndex && !( m_audioIndex < 0 && sel == 0 ) )
            {
                // The audio decoder is chosen when the file opens: reopen at the same spot.
                double pos = m_player.Position();
                m_audioIndex = audio[sel].index;
                fg_set_audio_track( m_audioIndex );
                std::vector<std::string> urls( 1, m_current.path );
                std::vector<std::wstring> labels( 1, L"file" );
                m_player.Open( urls, labels, "", pos );
                ApplySubtitles();
                Log::Write( "Audio track %d", m_audioIndex );
            }
            return MenuMove::Close();
        }

    case PAGE_SUBS:
        {
            int choice = SUB_OFF;
            int n = sel;
            if( n > 0 && !m_fileSubs.empty() )
            {
                if( n == 1 )
                    choice = SUB_FILE;
                --n;
            }
            std::vector<FgTrack> subs = TracksOf( 3 );
            if( n > 0 && n - 1 < (int)subs.size() )
                choice = subs[n - 1].index;
            if( choice != m_subChoice )
            {
                m_subChoice = choice;
                ApplySubtitles();
                if( choice >= 0 )
                    m_player.SeekAbsolute( m_player.Position() );   // re-read the lines around now
            }
            return MenuMove::Close();
        }

    case PAGE_BRIGHTNESS:
        return MenuMove::Open( PAGE_MAIN, ROW_BRIGHTNESS );

    case PAGE_ASPECT:
        m_player.SetAspect( sel );
        SaveSettings();
        return MenuMove::Close();
    }
    return MenuMove::Stay();
}

MenuMove PlayerApp::Back( int page )
{
    switch( page )
    {
    case PAGE_AUDIO:      return MenuMove::Open( PAGE_MAIN, ROW_AUDIO );
    case PAGE_SUBS:       return MenuMove::Open( PAGE_MAIN, ROW_SUBS );
    case PAGE_BRIGHTNESS: return MenuMove::Open( PAGE_MAIN, ROW_BRIGHTNESS );
    case PAGE_ASPECT:     return MenuMove::Open( PAGE_MAIN, ROW_ASPECT );
    }
    return MenuMove::Close();
}

void PlayerApp::SliderStep( int page, int step )
{
    if( page != PAGE_BRIGHTNESS )
        return;
    m_brightness = max( -5, min( 10, m_brightness + step ) );
    m_player.SetBrightness( m_brightness * 0.02f );
    SaveSettings();
}

//--------------------------------------------------------------------------------------
// Frame loop
//--------------------------------------------------------------------------------------
HRESULT PlayerApp::Update()
{
    Guard_Beat();
    Pad* pad = ReadPads();

    // Testing (game:\player.ini): autoplay=<path> plays a file at start; keys= presses one
    // key every 1.5 s (U D L R A B X Y, H = hold Right for 3 s, anything else waits).
    static bool s_testLoaded = false;
    static std::string s_keys;
    static size_t s_keyPos = 0;
    static DWORD s_keyTick = 0, s_holdUntil = 0;
    if( !s_testLoaded )
    {
        s_testLoaded = true;
        FILE* f = fopen( "game:\\player.ini", "rb" );
        if( f )
        {
            char line[600];
            while( fgets( line, sizeof( line ), f ) )
            {
                std::string s( line );
                while( !s.empty() && ( s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r' ) )
                    s.erase( s.size() - 1 );
                if( s.compare( 0, 9, "autoplay=" ) == 0 )
                {
                    Entry e;
                    e.path = s.substr( 9 );
                    e.name = Widen( BaseName( e.path ) );
                    e.dir = false;
                    e.size = 0;
                    Play( e, 0 );
                }
                else if( s.compare( 0, 5, "keys=" ) == 0 )
                    s_keys = s.substr( 5 );
            }
            fclose( f );
        }
        s_keyTick = GetTickCount();
    }
    if( s_keyPos < s_keys.size() && GetTickCount() - s_keyTick > 1500 )
    {
        s_keyTick = GetTickCount();
        char k = s_keys[s_keyPos++];
        WORD key = k == 'U' ? XINPUT_GAMEPAD_DPAD_UP : k == 'D' ? XINPUT_GAMEPAD_DPAD_DOWN :
                   k == 'L' ? XINPUT_GAMEPAD_DPAD_LEFT : k == 'R' ? XINPUT_GAMEPAD_DPAD_RIGHT :
                   k == 'A' ? XINPUT_GAMEPAD_A : k == 'B' ? XINPUT_GAMEPAD_B : k == 'X' ? XINPUT_GAMEPAD_X :
                   k == 'Y' ? XINPUT_GAMEPAD_Y : k == 'H' ? XINPUT_GAMEPAD_DPAD_RIGHT : 0;
        if( key )
        {
            Log::Write( "Test key %c", k );
            pad->wPressedButtons |= key;
        }
        if( k == 'H' )
            s_holdUntil = GetTickCount() + 3000;
    }
    if( GetTickCount() < s_holdUntil )
        pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if( m_playing )
    {
        if( m_player.IsActive() )
        {
            m_player.Update();
            m_screen.Update( pad );
            return S_OK;
        }
        m_playing = false;
        if( m_player.Failed() )
        {
            Log::Write( "Playback failed: %s", m_player.Error().c_str() );
            Toast( L"Can't play this file: " + Widen( m_player.Error() ) );
        }
        if( m_cpu.Running() )
            m_cpu.Stop();
    }
    UpdateBrowser( pad );
    return S_OK;
}

HRESULT PlayerApp::Render()
{
    m_frameStart = GetTickCount();
    D3DRECT safe = SafeArea();
    if( m_playing && m_player.IsActive() )
    {
        m_pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0 );
        D3DRECT full = { 0, 0, 1280, 720 };
        m_player.RenderFrame( full );
        m_font.SetWindow( safe );
        m_titleFont.SetWindow( safe );
        m_screen.Render( safe, m_renderMs );
        m_font.SetWindow( 0, 0, 1280, 720 );
        m_titleFont.SetWindow( 0, 0, 1280, 720 );
    }
    else
        RenderBrowser();
    EndFrame();
    return S_OK;
}

void PlayerApp::EndFrame()
{
    m_renderMs = m_renderMs * 0.9f + ( GetTickCount() - m_frameStart ) * 0.1f;
    static DWORD s_lastPresent = 0;
    DWORD elapsed = GetTickCount() - s_lastPresent;
    if( elapsed < 13 )
        Sleep( 13 - elapsed );
    m_pd3dDevice->Present( NULL, NULL, NULL, NULL );
    s_lastPresent = GetTickCount();
}
