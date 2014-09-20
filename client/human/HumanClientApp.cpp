#include <GL/glew.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "HumanClientApp.h"

#include "HumanClientFSM.h"
#include "../../UI/CUIControls.h"
#include "../../UI/CUIStyle.h"
#include "../../UI/MapWnd.h"
#include "../../UI/IntroScreen.h"
#include "../../UI/GalaxySetupWnd.h"
#include "../../UI/MultiplayerLobbyWnd.h"
#include "../../UI/SaveFileDialog.h"
#include "../../UI/ServerConnectWnd.h"
#include "../../UI/Sound.h"
#include "../../network/Message.h"
#include "../../network/Networking.h"
#include "../../util/i18n.h"
#include "../../util/MultiplayerCommon.h"
#include "../../util/OptionsDB.h"
#include "../../util/Process.h"
#include "../../util/SaveGamePreviewUtils.h"
#include "../../util/Serialize.h"
#include "../../util/SitRepEntry.h"
#include "../../util/Directories.h"
#include "../../universe/Planet.h"
#include "../../universe/Species.h"
#include "../../Empire/Empire.h"
#include "../../combat/CombatLogManager.h"
#include "../parse/Parse.h"

#include <GG/BrowseInfoWnd.h>
#include <GG/Cursor.h>
#include <GG/utf8/checked.h>

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <sstream>

namespace fs = boost::filesystem;

#ifdef ENABLE_CRASH_BACKTRACE
# include <signal.h>
# include <execinfo.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>

void SigHandler(int sig) {
    void* backtrace_buffer[100];
    int num;
    int fd;

    signal(sig, SIG_DFL);
    fd = open("crash.txt",O_WRONLY|O_CREAT|O_APPEND|O_SYNC,0666);
    if (fd != -1) {
        write(fd, "--- New crash backtrace begins here ---\n", 24);
        num = backtrace(backtrace_buffer, 100);
        backtrace_symbols_fd(backtrace_buffer, num, fd);
        backtrace_symbols_fd(backtrace_buffer, num, 2);
        close(fd);
    }

    // Now we try to display a MessageBox; this might fail and also
    // corrupt the heap, but since we're dying anyway that's no big
    // deal

    ClientUI::MessageBox("The client has just crashed!\nFile a bug report and\nattach the file called 'crash.txt'\nif necessary", true);

    raise(sig);
}
#endif //ENABLE_CRASH_BACKTRACE

namespace {
    const unsigned int  SERVER_CONNECT_TIMEOUT = 4500; // in ms

    const bool          INSTRUMENT_MESSAGE_HANDLING = false;

    // command-line options
    void AddOptions(OptionsDB& db) {
        db.Add("autosave.single-player",        UserStringNop("OPTIONS_DB_AUTOSAVE_SINGLE_PLAYER"),     true,   Validator<bool>());
        db.Add("autosave.multiplayer",          UserStringNop("OPTIONS_DB_AUTOSAVE_MULTIPLAYER"),       true,   Validator<bool>());
        db.Add("autosave.turns",                UserStringNop("OPTIONS_DB_AUTOSAVE_TURNS"),             1,      RangedValidator<int>(1, 50));
        db.Add("autosave.limit",                UserStringNop("OPTIONS_DB_AUTOSAVE_LIMIT"),             10,     RangedValidator<int>(1, 100));
        db.Add("UI.swap-mouse-lr",              UserStringNop("OPTIONS_DB_UI_MOUSE_LR_SWAP"),           false);
        db.Add("UI.keypress-repeat-delay",      UserStringNop("OPTIONS_DB_KEYPRESS_REPEAT_DELAY"),      360,    RangedValidator<int>(0, 1000));
        db.Add("UI.keypress-repeat-interval",   UserStringNop("OPTIONS_DB_KEYPRESS_REPEAT_INTERVAL"),   20,     RangedValidator<int>(0, 1000));
        db.Add("UI.mouse-click-repeat-delay",   UserStringNop("OPTIONS_DB_MOUSE_REPEAT_DELAY"),         360,    RangedValidator<int>(0, 1000));
        db.Add("UI.mouse-click-repeat-interval",UserStringNop("OPTIONS_DB_MOUSE_REPEAT_INTERVAL"),      15,     RangedValidator<int>(0, 1000));

        db.Add("app-width",             UserStringNop("OPTIONS_DB_APP_WIDTH"),             1024,   RangedValidator<int>(800, 2560));
        db.Add("app-height",            UserStringNop("OPTIONS_DB_APP_HEIGHT"),            768,    RangedValidator<int>(600, 1600));
        db.Add("app-width-windowed",    UserStringNop("OPTIONS_DB_APP_WIDTH_WINDOWED"),    1024,   RangedValidator<int>(800, 2560));
        db.Add("app-height-windowed",   UserStringNop("OPTIONS_DB_APP_HEIGHT_WINDOWED"),   768,    RangedValidator<int>(600, 1600));
        db.Add("app-left-windowed",     UserStringNop("OPTIONS_DB_APP_LEFT_WINDOWED"),     SDL_WINDOWPOS_CENTERED,      RangedValidator<int>(-10240, 10240));
        db.Add("app-top-windowed",      UserStringNop("OPTIONS_DB_APP_TOP_WINDOWED"),      50,                          RangedValidator<int>(-10240, 10240));
    }
    bool temp_bool = RegisterOptions(&AddOptions);

    /* Sets the value of options that need language-dependent default values.*/
    void SetStringtableDependentOptionDefaults() {
        if (GetOptionsDB().Get<std::string>("GameSetup.empire-name").empty())
            GetOptionsDB().Set("GameSetup.empire-name", UserString("DEFAULT_EMPIRE_NAME"));

        if (GetOptionsDB().Get<std::string>("GameSetup.player-name").empty())
            GetOptionsDB().Set("GameSetup.player-name", UserString("DEFAULT_PLAYER_NAME"));

        if (GetOptionsDB().Get<std::string>("multiplayersetup.player-name").empty())
            GetOptionsDB().Set("multiplayersetup.player-name", UserString("DEFAULT_PLAYER_NAME"));
    }

    static float stored_gl_version = -1.0f;  // to be replaced when gl version first checked

    float GetGLVersion() {
        if (stored_gl_version != -1.0f)
            return stored_gl_version;

        // get OpenGL version string and parse to get version number
        const GLubyte* gl_version = glGetString(GL_VERSION);
        std::string gl_version_string = boost::lexical_cast<std::string>(gl_version);

        float version_number = 0.0f;
        std::istringstream iss(gl_version_string);
        iss >> version_number;
        version_number += 0.05f;    // ensures proper rounding of 1.1 digit number

        stored_gl_version = version_number;

        return stored_gl_version;
    }

    void SetGLVersionDependentOptionDefaults() {
        // get OpenGL version string and parse to get version number
        float version_number = GetGLVersion();
        Logger().debugStream() << "OpenGL Version Number: " << DoubleToString(version_number, 2, false);    // combination of floating point precision and DoubleToString preferring to round down means the +0.05 is needed to round properly
        if (version_number < 1.5) {
            Logger().errorStream() << "OpenGL Version is less than 2.0 (official required) or 1.5 (usually works).  FreeOrion may crash when trying to start a game.";
        }

        // only execute default option setting once
        if (GetOptionsDB().Get<bool>("checked-gl-version"))
            return;
        GetOptionsDB().Set<bool>("checked-gl-version", true);

        // if GL version is too low, set various map rendering options to
        // disabled, so as to prevent crashes when running on systems that
        // don't support these GL features.  these options are added to the
        // DB in MapWnd.cpp's AddOptions and all default to true.
        if (version_number < 2.0) {
            GetOptionsDB().Set<bool>("UI.galaxy-gas-background",        false);
            GetOptionsDB().Set<bool>("UI.galaxy-starfields",            false);
            GetOptionsDB().Set<bool>("UI.optimized-system-rendering",   false);
            GetOptionsDB().Set<bool>("UI.system-fog-of-war",            false);
        }
    }
}

HumanClientApp::HumanClientApp(int width, int height, bool calculate_fps, const std::string& name, int x, int y, bool fullscreen, bool fake_mode_change) :
    ClientApp(),
    SDLGUI(width, height, calculate_fps, name, x, y, fullscreen, fake_mode_change),
    m_fsm(0),
    m_single_player_game(true),
    m_game_started(false),
    m_connected(false)
{
#ifdef ENABLE_CRASH_BACKTRACE
    signal(SIGSEGV, SigHandler);
#endif
    m_fsm = new HumanClientFSM(*this);

    const std::string HUMAN_CLIENT_LOG_FILENAME((GetUserDir() / "freeorion.log").string());

    InitLogger(HUMAN_CLIENT_LOG_FILENAME, "%d %p Client : %m%n");
    Logger().setPriority(PriorityValue(GetOptionsDB().Get<std::string>("log-level")));

    boost::shared_ptr<GG::StyleFactory> style(new CUIStyle());
    SetStyleFactory(style);

    SetMinDragTime(0);

    m_ui = boost::shared_ptr<ClientUI>(new ClientUI());

    if ((GetOptionsDB().Get<bool>("UI.sound.music-enabled")))
        Sound::GetSound().PlayMusic(GetOptionsDB().Get<std::string>("UI.sound.bg-music"), -1);

    Sound::GetSound().SetMusicVolume(GetOptionsDB().Get<int>("UI.sound.music-volume"));
    Sound::GetSound().SetUISoundsVolume(GetOptionsDB().Get<int>("UI.sound.volume"));

    EnableFPS();
    UpdateFPSLimit();
    GG::Connect(GetOptionsDB().OptionChangedSignal("show-fps"), &HumanClientApp::UpdateFPSLimit, this);

    boost::shared_ptr<GG::BrowseInfoWnd> default_browse_info_wnd(
        new GG::TextBoxBrowseInfoWnd(GG::X(400), ClientUI::GetFont(),
                                     GG::Clr(0, 0, 0, 200), ClientUI::WndOuterBorderColor(), ClientUI::TextColor(),
                                     GG::FORMAT_LEFT | GG::FORMAT_WORDBREAK, 1));
    GG::Wnd::SetDefaultBrowseInfoWnd(default_browse_info_wnd);

    boost::shared_ptr<GG::Texture> cursor_texture = m_ui->GetTexture(ClientUI::ArtDir() / "cursors" / "default_cursor.png");
    SetCursor(boost::shared_ptr<GG::TextureCursor>(new GG::TextureCursor(cursor_texture, GG::Pt(GG::X(6), GG::Y(3)))));
    RenderCursor(true);

    EnableKeyPressRepeat(GetOptionsDB().Get<int>("UI.keypress-repeat-delay"),
                         GetOptionsDB().Get<int>("UI.keypress-repeat-interval"));
    EnableMouseButtonDownRepeat(GetOptionsDB().Get<int>("UI.mouse-click-repeat-delay"),
                                GetOptionsDB().Get<int>("UI.mouse-click-repeat-interval"));

    GG::Connect(WindowResizedSignal,    &HumanClientApp::HandleWindowResize,    this);
    GG::Connect(FocusChangedSignal,     &HumanClientApp::HandleFocusChange,     this);
    GG::Connect(WindowMovedSignal,      &HumanClientApp::HandleWindowMove,      this);
    /* TODO: Wire these signals if theyare needed
    GG::Connect(WindowClosingSignal,    &HumanClientApp::HandleWindowClosing,   this);
    GG::Connect(WindowClosedSignal,     &HumanClientApp::HandleWindowClose,     this);
    */

    SetStringtableDependentOptionDefaults();
    SetGLVersionDependentOptionDefaults();

    this->SetMouseLRSwapped(GetOptionsDB().Get<bool>("UI.swap-mouse-lr"));

    std::map<std::string, std::map<int, int> > named_key_maps;
    parse::keymaps(GetResourceDir() / "keymaps.txt", named_key_maps);
    if (GetOptionsDB().Get<bool>("verbose-logging")) {
        Logger().debugStream() << "Keymaps:";
        for (std::map<std::string, std::map<int, int> >::const_iterator km_it = named_key_maps.begin();
             km_it != named_key_maps.end(); ++km_it)
        {
            Logger().debugStream() << "Keymap name = \"" << km_it->first << "\"";
            const std::map<int, int>& key_map = km_it->second;
            for (std::map<int, int>::const_iterator keys_it = key_map.begin(); keys_it != key_map.end(); ++keys_it)
                Logger().debugStream() << "    " << char(keys_it->first) << " : " << char(keys_it->second);
        }
    }
    std::map<std::string, std::map<int, int> >::const_iterator km_it = named_key_maps.find("TEST");
    if (km_it != named_key_maps.end()) {
        const std::map<int, int> int_key_map = km_it->second;
        std::map<GG::Key, GG::Key> key_map;
        for (std::map<int, int>::const_iterator key_int_it = int_key_map.begin();
             key_int_it != int_key_map.end(); ++key_int_it)
        { key_map[GG::Key(key_int_it->first)] = GG::Key(key_int_it->second); }
        this->SetKeyMap(key_map);
    }

    if (fake_mode_change && !FramebuffersAvailable()) {
        Logger().errorStream() << "Requested fake mode changes, but the framebuffer opengl extension is not available. Ignoring.";
    }

    m_fsm->initiate();
}

HumanClientApp::~HumanClientApp() {
    if (m_networking.Connected())
        m_networking.DisconnectFromServer();
    m_server_process.RequestTermination();
    delete m_fsm;
}

bool HumanClientApp::SinglePlayerGame() const
{ return m_single_player_game; }

bool HumanClientApp::CanSaveNow() const {
    // only host can save in multiplayer
    if (!SinglePlayerGame() && !Networking().PlayerIsHost(PlayerID()))
        return false;

    // can't save while AIs are playing their turns...
    for (std::map<int, PlayerInfo>::const_iterator player_it = this->m_player_info.begin();
         player_it != this->m_player_info.end(); ++player_it)
    {
        const PlayerInfo& info = player_it->second;
        if (info.client_type != Networking::CLIENT_TYPE_AI_PLAYER)
            continue;   // only care about AIs

        std::map<int, Message::PlayerStatus>::const_iterator
            status_it = this->m_player_status.find(player_it->first);

        if (status_it == this->m_player_status.end()) {
            return false;  // missing status for AI; can't assume it's ready
        }
        if (status_it->second != Message::WAITING) {
            return false;
        }
    }

    return true;
}

void HumanClientApp::SetSinglePlayerGame(bool sp/* = true*/)
{ m_single_player_game = sp; }

namespace {
    std::string ServerClientExe() {
#ifdef FREEORION_WIN32
        return PathString(GetBinDir() / "freeoriond.exe");
#else
        return (GetBinDir() / "freeoriond").string();
#endif
    }
}

#ifdef FREEORION_MACOSX
#include <stdlib.h>
#endif

void HumanClientApp::StartServer() {
    std::string SERVER_CLIENT_EXE = ServerClientExe();
    Logger().debugStream() << "HumanClientApp::StartServer: " << SERVER_CLIENT_EXE;

#ifdef FREEORION_MACOSX
    // On OSX set environment variable DYLD_LIBRARY_PATH to python framework folder
    // bundled with app, so the dynamic linker uses the bundled python library.
    // Otherwise the dynamic linker will look for a correct python lib in system
    // paths, and if it can't find it, throw an error and terminate!
    // Setting environment variable here, spawned child processes will inherit it.
    setenv("DYLD_LIBRARY_PATH", GetPythonHome().string().c_str(), 1);
#endif

    std::vector<std::string> args;
    args.push_back("\"" + SERVER_CLIENT_EXE + "\"");
    args.push_back("--resource-dir");
    args.push_back("\"" + GetOptionsDB().Get<std::string>("resource-dir") + "\"");
    args.push_back("--log-level");
    args.push_back(GetOptionsDB().Get<std::string>("log-level"));
    if (GetOptionsDB().Get<bool>("test-3d-combat"))
        args.push_back("--test-3d-combat");
    m_server_process = Process(SERVER_CLIENT_EXE, args);
}

void HumanClientApp::FreeServer() {
    m_server_process.Free();
    m_networking.SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking.SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);
}

void HumanClientApp::KillServer() {
    Logger().debugStream() << "HumanClientApp::KillServer()";
    m_server_process.Kill();
    m_networking.SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking.SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);
}

void HumanClientApp::NewSinglePlayerGame(bool quickstart) {
    if (!GetOptionsDB().Get<bool>("force-external-server")) {
        try {
            StartServer();
        } catch (const std::runtime_error& err) {
            Logger().errorStream() << "HumanClientApp::NewSinglePlayerGame : Couldn't start server.  Got error message: " << err.what();
            ClientUI::MessageBox(UserString("SERVER_WONT_START"), true);
            return;
        }
    }

    bool ended_with_ok = false;
    if (!quickstart) {
        GalaxySetupWnd galaxy_wnd;
        galaxy_wnd.Run();
        ended_with_ok = galaxy_wnd.EndedWithOk();
    }

    bool failed = false;
    if (quickstart || ended_with_ok) {
        unsigned int start_time = Ticks();
        while (!m_networking.ConnectToLocalHostServer(boost::posix_time::seconds(2))) {
            if (SERVER_CONNECT_TIMEOUT < Ticks() - start_time) {
                ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
                failed = true;
                break;
            }
        }

        if (!failed) {
            Logger().debugStream() << "HumanClientApp::NewSinglePlayerGame : Connected to server";

            SinglePlayerSetupData setup_data;
            setup_data.m_new_game = true;
            setup_data.m_filename.clear();  // not used for new game

            // get values stored in options from previous time game was run or
            // from just having run GalaxySetupWnd

            // GalaxySetupData
            setup_data.m_seed =             GetOptionsDB().Get<std::string>("GameSetup.seed");
            setup_data.m_size =             GetOptionsDB().Get<int>("GameSetup.stars");
            setup_data.m_shape =            GetOptionsDB().Get<Shape>("GameSetup.galaxy-shape");
            setup_data.m_age =              GetOptionsDB().Get<GalaxySetupOption>("GameSetup.galaxy-age");
            setup_data.m_starlane_freq =    GetOptionsDB().Get<GalaxySetupOption>("GameSetup.starlane-frequency");
            setup_data.m_planet_density =   GetOptionsDB().Get<GalaxySetupOption>("GameSetup.planet-density");
            setup_data.m_specials_freq =    GetOptionsDB().Get<GalaxySetupOption>("GameSetup.specials-frequency");
            setup_data.m_monster_freq =     GetOptionsDB().Get<GalaxySetupOption>("GameSetup.monster-frequency");
            setup_data.m_native_freq =      GetOptionsDB().Get<GalaxySetupOption>("GameSetup.native-frequency");
            setup_data.m_ai_aggr =          GetOptionsDB().Get<Aggression>("GameSetup.ai-aggression");

            // SinglePlayerSetupData contains a map of PlayerSetupData, for
            // the human and AI players.  Need to compile this information
            // from the specified human options and number of requested AIs

            // Human player setup data first

            PlayerSetupData human_player_setup_data;
            human_player_setup_data.m_player_name = GetOptionsDB().Get<std::string>("GameSetup.player-name");
            human_player_setup_data.m_empire_name = GetOptionsDB().Get<std::string>("GameSetup.empire-name");

            // DB stores index into array of available colours, so need to get that array to look up value of index.
            // if stored value is invalid, use a default colour
            const std::vector<GG::Clr>& empire_colours = EmpireColors();
            int colour_index = GetOptionsDB().Get<int>("GameSetup.empire-color");
            if (colour_index >= 0 && colour_index < static_cast<int>(empire_colours.size()))
                human_player_setup_data.m_empire_color = empire_colours[colour_index];
            else
                human_player_setup_data.m_empire_color = GG::CLR_GREEN;

            human_player_setup_data.m_starting_species_name = GetOptionsDB().Get<std::string>("GameSetup.starting-species");
            if (human_player_setup_data.m_starting_species_name == "1")
                human_player_setup_data.m_starting_species_name = "SP_HUMAN";   // kludge / bug workaround for bug with options storage and retreival.  Empty-string options are stored, but read in as "true" boolean, and converted to string equal to "1"

            if (!GetSpecies(human_player_setup_data.m_starting_species_name)) {
                const SpeciesManager& sm = GetSpeciesManager();
                if (sm.NumPlayableSpecies() < 1)
                    human_player_setup_data.m_starting_species_name.clear();
                else
                    human_player_setup_data.m_starting_species_name = sm.playable_begin()->first;
             }

            human_player_setup_data.m_save_game_empire_id = ALL_EMPIRES; // not used for new games
            human_player_setup_data.m_client_type = Networking::CLIENT_TYPE_HUMAN_PLAYER;

            // add to setup data players
            setup_data.m_players.push_back(human_player_setup_data);

            // AI player setup data.  One entry for each requested AI

            int num_AIs = GetOptionsDB().Get<int>("GameSetup.ai-players");
            for (int ai_i = 1; ai_i <= num_AIs; ++ai_i) {
                PlayerSetupData ai_setup_data;

                ai_setup_data.m_player_name = "AI_" + boost::lexical_cast<std::string>(ai_i);
                ai_setup_data.m_empire_name.clear();                // leave blank, to be set by server in Universe::GenerateEmpires
                ai_setup_data.m_empire_color = GG::CLR_ZERO;        // to be set by server
                ai_setup_data.m_starting_species_name.clear();      // leave blank, to be set by server
                ai_setup_data.m_save_game_empire_id = ALL_EMPIRES;  // not used for new games
                ai_setup_data.m_client_type = Networking::CLIENT_TYPE_AI_PLAYER;

                setup_data.m_players.push_back(ai_setup_data);
            }

            m_networking.SendMessage(HostSPGameMessage(setup_data));
            m_fsm->process_event(HostSPGameRequested());
        }
    } else {
        failed = true;
    }

    if (failed)
        KillServer();
    else
        m_connected = true;
}

void HumanClientApp::MultiPlayerGame() {
    if (m_networking.Connected()) {
        Logger().errorStream() << "HumanClientApp::MultiPlayerGame aborting because already connected to a server";
        return;
    }

    ServerConnectWnd server_connect_wnd;
    server_connect_wnd.Run();

    std::string server_name = server_connect_wnd.Result().second;

    if (server_name.empty())
        return;

    if (server_name == "HOST GAME SELECTED") {
        if (!GetOptionsDB().Get<bool>("force-external-server")) {
            try {
                StartServer();
                FreeServer();
            } catch (const std::runtime_error& err) {
                Logger().errorStream() << "Couldn't start server.  Got error message: " << err.what();
                ClientUI::MessageBox(UserString("SERVER_WONT_START"), true);
                return;
            }
            server_name = "localhost";
        }
        server_name = GetOptionsDB().Get<std::string>("external-server-address");
    }

    unsigned int start_time = Ticks();
    while (!m_networking.ConnectToServer(server_name, boost::posix_time::seconds(2))) {
        if (SERVER_CONNECT_TIMEOUT < Ticks() - start_time) {
            ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
            if (server_connect_wnd.Result().second == "HOST GAME SELECTED")
                KillServer();
            return;
        }
    }

    if (server_connect_wnd.Result().second == "HOST GAME SELECTED") {
        m_networking.SendMessage(HostMPGameMessage(server_connect_wnd.Result().first));
        m_fsm->process_event(HostMPGameRequested());
    } else {
        m_networking.SendMessage(JoinGameMessage(server_connect_wnd.Result().first, Networking::CLIENT_TYPE_HUMAN_PLAYER));
        m_fsm->process_event(JoinMPGameRequested());
    }
    m_connected = true;
}

void HumanClientApp::StartMultiPlayerGameFromLobby()
{ m_fsm->process_event(StartMPGameClicked()); }

void HumanClientApp::CancelMultiplayerGameFromLobby()
{ m_fsm->process_event(CancelMPGameClicked()); }

void HumanClientApp::SaveGame(const std::string& filename) {
    Message response_msg;
    m_networking.SendSynchronousMessage(HostSaveGameMessage(PlayerID(), filename), response_msg);
    if (response_msg.Type() != Message::SAVE_GAME) {
        Logger().errorStream() << "HumanClientApp::SaveGame sent synchronous HostSaveGameMessage, but received back message of wrong type: " << response_msg.Type();
        throw std::runtime_error("HumanClientApp::SaveGame synchronous message received invalid response message type");
    }
    HandleSaveGameDataRequest();
}

void HumanClientApp::LoadSinglePlayerGame(std::string filename/* = ""*/) {
    Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame";

    if (!filename.empty()) {
#if defined(FREEORION_WIN32)
        boost::filesystem::path::string_type file_name_native;
        utf8::utf8to16(filename.begin(), filename.end(), std::back_inserter(file_name_native));
        boost::filesystem::path file_path(file_name_native);
#else
        boost::filesystem::path file_path(filename);
#endif
        if (!exists(file_path)) {
            std::string msg = "HumanClientApp::LoadSinglePlayerGame() given a nonexistent file \""
                            + filename + "\" to load; aborting.";
            Logger().fatalStream() << msg;
            std::cerr << msg << '\n';
            abort();
        }
    } else {
        try {
            SaveFileDialog sfd(SP_SAVE_FILE_EXTENSION, true);
            sfd.Run();
            if (!sfd.Result().empty())
                filename = sfd.Result();
        } catch (const std::exception& e) {
            ClientUI::MessageBox(e.what(), true);
        }
    }

    if (filename.empty()) {
        Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame has empty filename. Aborting load.";
        return;
    }

    // end any currently-playing game before loading new one
    if (m_game_started) {
        EndGame();
        // delay to make sure old game is fully cleaned up before attempting to start a new one
        boost::this_thread::sleep(boost::posix_time::seconds(3));
    } else {
        Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() not already in a game, so don't need to end it";
    }

    if (!GetOptionsDB().Get<bool>("force-external-server")) {
        Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() Starting server";
        StartServer();
        Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() Server started";
    } else {
        Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() assuming external server will be available";
    }

    Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() Connecting to server";
    unsigned int start_time = Ticks();
    while (!m_networking.ConnectToLocalHostServer()) {
        if (SERVER_CONNECT_TIMEOUT < Ticks() - start_time) {
            Logger().errorStream() << "HumanClientApp::LoadSinglePlayerGame() server connecting timed out";
            ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
            KillServer();
            return;
        }
    }

    Logger().debugStream() << "HumanClientApp::LoadSinglePlayerGame() Connected to server";

    m_connected = true;
    m_networking.SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking.SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);

    SinglePlayerSetupData setup_data;
    // leving GalaxySetupData information default / blank : not used when loading a game
    setup_data.m_new_game = false;
    setup_data.m_filename = filename;
    // leving setup_data.m_players empty : not specified when loading a game, as server will generate from save file


    m_networking.SendMessage(HostSPGameMessage(setup_data));
    m_fsm->process_event(HostSPGameRequested());
}

void HumanClientApp::RequestSavePreviews(const std::string& directory, PreviewInformation& previews){
    std::string  generic_directory = fs::path(directory).generic_string();
    if(!m_networking.Connected()){
        Logger().debugStream() << "HumanClientApp::RequestSavePreviews: No game running. Start a server for savegame queries.";
        StartServer();

        Logger().debugStream() << "HumanClientApp::RequestSavePreviews Connecting to server";
        unsigned int start_time = Ticks();
        while (!m_networking.ConnectToLocalHostServer()) {
            if (SERVER_CONNECT_TIMEOUT < Ticks() - start_time) {
                Logger().errorStream() << "HumanClientApp::LoadSinglePlayerGame() server connecting timed out";
                ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
                KillServer();
                return;
            }
        }
        m_connected = true;
    }
    Logger().debugStream() << "HumanClientApp::RequestSavePreviews Requesting previews for " << generic_directory;
    Message response;
    m_networking.SendSynchronousMessage(RequestSavePreviewsMessage(PlayerID(), generic_directory), response);
    if(response.Type() == Message::DISPATCH_SAVE_PREVIEWS){
        ExtractMessageData(response, previews);
        Logger().debugStream() << "HumanClientApp::RequestSavePreviews Got " << previews.previews.size() << " previews.";
    }else{
        Logger().errorStream() << "HumanClientApp::RequestSavePreviews: Wrong response type from server: " << EnumToString(response.Type());
    }
}


std::pair<int, int> HumanClientApp::GetWindowLeftTop() {
    int left(0), top(0);

    left = GetOptionsDB().Get<int>("app-left-windowed");
    top = GetOptionsDB().Get<int>("app-top-windowed");

    // clamp to edges to avoid weird bug with maximizing windows setting their
    // left and top to -9 which lead to weird issues when attmepting to recreate
    // the window at those positions next execution
    if (std::abs(left) < 10)
        left = 0;
    if (std::abs(top) < 10)
        top = 0;

    return std::make_pair(left, top);
}

std::pair<int, int> HumanClientApp::GetWindowWidthHeight() {
    int width(800), height(600);

    bool fullscreen = GetOptionsDB().Get<bool>("fullscreen");
    if (!fullscreen) {
        width = GetOptionsDB().Get<int>("app-width-windowed");
        height = GetOptionsDB().Get<int>("app-height-windowed");
        return std::make_pair(width, height);
    }

    bool reset_fullscreen = GetOptionsDB().Get<bool>("reset-fullscreen-size");
    if (!reset_fullscreen) {
        width = GetOptionsDB().Get<int>("app-width");
        height = GetOptionsDB().Get<int>("app-height");
        return std::make_pair(width, height);
    }

    GetOptionsDB().Set<bool>("reset-fullscreen-size", false);

    return std::make_pair(width, height);
}

void HumanClientApp::Reinitialize() {
    bool fullscreen = GetOptionsDB().Get<bool>("fullscreen");
    bool fake_mode_change = GetOptionsDB().Get<bool>("fake-mode-change");
    std::pair<int, int> size = GetWindowWidthHeight();

    SetVideoMode(GG::X(size.first), GG::Y(size.second), fullscreen, fake_mode_change);
    HandleWindowResize(GG::X(size.first), GG::Y(size.second));
}

float HumanClientApp::GLVersion() const
{ return GetGLVersion(); }

namespace {
    static bool enter_2d_mode_log_done(false);
}

void HumanClientApp::StartTurn() {
    ClientApp::StartTurn();
    m_fsm->process_event(TurnEnded());
}

void HumanClientApp::HandleSystemEvents() {
    SDLGUI::HandleSystemEvents();
    if (m_connected && !m_networking.Connected()) {
        m_connected = false;
        DisconnectedFromServer();
    } else if (m_networking.MessageAvailable()) {
        Message msg;
        m_networking.GetMessage(msg);
        HandleMessage(msg);
    }
}

void HumanClientApp::RenderBegin() {
    SDLGUI::RenderBegin();
    Sound::GetSound().DoFrame();
}

void HumanClientApp::HandleMessage(Message& msg) {
    if (INSTRUMENT_MESSAGE_HANDLING)
        std::cerr << "HumanClientApp::HandleMessage(" << msg.Type() << ")\n";

    switch (msg.Type()) {
    case Message::ERROR_MSG:            m_fsm->process_event(Error(msg));                   break;
    case Message::HOST_MP_GAME:         m_fsm->process_event(HostMPGame(msg));              break;
    case Message::HOST_SP_GAME:         m_fsm->process_event(HostSPGame(msg));              break;
    case Message::JOIN_GAME:            m_fsm->process_event(JoinGame(msg));                break;
    case Message::HOST_ID:              m_fsm->process_event(HostID(msg));                  break;
    case Message::LOBBY_UPDATE:         m_fsm->process_event(LobbyUpdate(msg));             break;
    case Message::LOBBY_CHAT:           m_fsm->process_event(LobbyChat(msg));               break;
    case Message::SAVE_GAME:            m_fsm->process_event(::SaveGame(msg));              break;
    case Message::GAME_START:           m_fsm->process_event(GameStart(msg));               break;
    case Message::TURN_UPDATE:          m_fsm->process_event(TurnUpdate(msg));              break;
    case Message::TURN_PARTIAL_UPDATE:  m_fsm->process_event(TurnPartialUpdate(msg));       break;
    case Message::TURN_PROGRESS:        m_fsm->process_event(TurnProgress(msg));            break;
    case Message::PLAYER_STATUS:        m_fsm->process_event(::PlayerStatus(msg));          break;
    case Message::PLAYER_CHAT:          m_fsm->process_event(PlayerChat(msg));              break;
    case Message::DIPLOMACY:            m_fsm->process_event(Diplomacy(msg));               break;
    case Message::DIPLOMATIC_STATUS:    m_fsm->process_event(DiplomaticStatusUpdate(msg));  break;
    case Message::VICTORY_DEFEAT :      m_fsm->process_event(VictoryDefeat(msg));           break;
    case Message::PLAYER_ELIMINATED:    m_fsm->process_event(PlayerEliminated(msg));        break;
    case Message::END_GAME:             m_fsm->process_event(::EndGame(msg));               break;
    default:
        Logger().errorStream() << "HumanClientApp::HandleMessage : Received an unknown message type \""
                               << msg.Type() << "\".";
    }
}

void HumanClientApp::HandleSaveGameDataRequest() {
    if (INSTRUMENT_MESSAGE_HANDLING)
        std::cerr << "HumanClientApp::HandleSaveGameDataRequest(" << Message::SAVE_GAME << ")\n";
    SaveGameUIData ui_data;
    m_ui->GetSaveGameUIData(ui_data);
    m_networking.SendMessage(ClientSaveDataMessage(PlayerID(), Orders(), ui_data));
}

void HumanClientApp::HandleWindowMove(GG::X w, GG::Y h) {
    //Logger().debugStream() << "HumanClientApp::HandleWindowMove(" << Value(w) << ", " << Value(h) << ")";

    GetOptionsDB().Set<int>("app-left-windowed", Value(w));
    GetOptionsDB().Set<int>("app-top-windowed", Value(h));
    GetOptionsDB().Commit();
}

void HumanClientApp::HandleWindowResize(GG::X w, GG::Y h) {
    //Logger().debugStream() << "HumanClientApp::HandleWindowResize(" << Value(w) << ", " << Value(h) << ")";
    if (ClientUI* ui = ClientUI::GetClientUI()) {
        if (MapWnd* map_wnd = ui->GetMapWnd())
            map_wnd->DoLayout();
        if (IntroScreen* intro_screen = ui->GetIntroScreen()) {
            intro_screen->Resize(GG::Pt(w, h));
            intro_screen->DoLayout();
        }
    }

    // store resize if window is not full-screen (so that fullscreen
    // resolution doesn't overwrite windowed resolution)
    if (!GetOptionsDB().Get<bool>("fullscreen")) {
        GetOptionsDB().Set<int>("app-width-windowed", Value(w));
        GetOptionsDB().Set<int>("app-height-windowed", Value(h));
    }

    GetOptionsDB().Commit();
}

void HumanClientApp::HandleWindowClosing()
{ Logger().debugStream() << "HumanClientApp::HandleWindowClosing()"; }

void HumanClientApp::HandleWindowClose() {
    Logger().debugStream() << "HumanClientApp::HandleWindowClose()";
    EndGame();
    Exit(0);
}

void HumanClientApp::HandleFocusChange() {
    Logger().debugStream() << "HumanClientApp::HandleFocusChange()";

    // TODO: Does SDL need some action here?

    CancelDragDrop();
    ClearEventState();
}

void HumanClientApp::StartGame() {
    m_game_started = true;
    Orders().Reset();
}

namespace {
    void RemoveOldestFiles(int files_limit, boost::filesystem::path& p) {
        using namespace boost::filesystem;
        try {
            if (!is_directory(p))
                return;
            if (files_limit < 0)
                return;

            std::multimap<std::time_t, path> files_by_write_time;

            for (directory_iterator dir_it(p); dir_it != directory_iterator(); ++dir_it) {
                const path& file_path = dir_it->path();
                if (!is_regular_file(file_path))
                    continue;
                if (file_path.extension() != SP_SAVE_FILE_EXTENSION &&
                    file_path.extension() != MP_SAVE_FILE_EXTENSION)
                { continue; }

                std::time_t t = last_write_time(file_path);
                files_by_write_time.insert(std::make_pair(t, file_path));
            }

            //Logger().debugStream() << "files by write time:";
            //for (std::multimap<std::time_t, path>::const_iterator it = files_by_write_time.begin();
            //     it != files_by_write_time.end(); ++it)
            //{ Logger().debugStream() << it->first << " : " << it->second.filename(); }

            int num_to_delete = files_by_write_time.size() - files_limit + 1;   // +1 because will add a new file after deleting, bringing number back up to limit
            if (num_to_delete <= 0)
                return; // don't need to delete anything.

            int num_deleted = 0;
            for (std::multimap<std::time_t, path>::const_iterator it = files_by_write_time.begin();
                 it != files_by_write_time.end(); ++it)
            {
                if (num_deleted >= num_to_delete)
                    break;
                remove(it->second);
                ++num_deleted;
            }
        } catch (...) {
            Logger().errorStream() << "Error removing oldest files";
        }
    }
}

void HumanClientApp::Autosave() {
    // autosave only on appropriate turn numbers, and when enabled for current
    // game type (single vs. multiplayer)
    int autosave_turns = GetOptionsDB().Get<int>("autosave.turns");
    if (autosave_turns < 1)
        return;     // avoid divide by zero
    if (CurrentTurn() % autosave_turns != 0 && CurrentTurn() != 1)
        return;     // turns divisible by autosave_turns, and first turn, have autosaves done
    if (m_single_player_game && !GetOptionsDB().Get<bool>("autosave.single-player"))
        return;
    if (!m_single_player_game && !GetOptionsDB().Get<bool>("autosave.multiplayer"))
        return;

    const char* legal_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_-";

    // get empire name, filtered for filename acceptability
    int client_empire_id = EmpireID();
    const Empire* empire = Empires().Lookup(client_empire_id);
    std::string empire_name;
    if (empire)
        empire_name = empire->Name();
    else
        empire_name = UserString("OBSERVER");
    std::string::size_type first_good_empire_char = empire_name.find_first_of(legal_chars);
    if (first_good_empire_char == std::string::npos) {
        empire_name.clear();
    } else {
        std::string::size_type first_bad_empire_char = empire_name.find_first_not_of(legal_chars, first_good_empire_char);
        empire_name = empire_name.substr(first_good_empire_char, first_bad_empire_char - first_good_empire_char);
    }

    // get player name, also filtered
    std::string player_name;
    if (empire)
        player_name = empire->PlayerName();
    std::string::size_type first_good_player_char = player_name.find_first_of(legal_chars);
    if (first_good_player_char == std::string::npos) {
        player_name.clear();
    } else {
        std::string::size_type first_bad_player_char = player_name.find_first_not_of(legal_chars, first_good_player_char);
        player_name = player_name.substr(first_good_player_char, first_bad_player_char - first_good_player_char);
    }

    // select filename extension
    std::string extension;
    if (m_single_player_game)
        extension = SP_SAVE_FILE_EXTENSION;
    else
        extension = MP_SAVE_FILE_EXTENSION;

    // Add timestamp to autosave generated files
    std::string datetime_str = FilenameTimestamp();

    boost::filesystem::path autosave_dir_path(GetSaveDir() / "auto");

    std::string save_filename = boost::io::str(boost::format("FreeOrion_%s_%s_%04d_%s%s") % player_name % empire_name % CurrentTurn() % datetime_str % extension);
    boost::filesystem::path save_path(autosave_dir_path / save_filename);
    std::string path_string = PathString(save_path);

    try {
        // ensure autosave directory exists
        if (!exists(autosave_dir_path))
            boost::filesystem::create_directories(autosave_dir_path);
    } catch (const std::exception& e) {
        Logger().errorStream() << "Autosave unable to check / create autosave directory: " << e.what();
        std::cerr << "Autosave unable to check / create autosave directory: " << e.what() << std::endl;
    }

    // check for and remove excess oldest autosaves
    int max_autosaves = GetOptionsDB().Get<int>("autosave.limit");
    RemoveOldestFiles(max_autosaves, autosave_dir_path);

    // create new save
    Logger().debugStream() << "Autosaving to: " << path_string;
    try {
        SaveGame(path_string);
    } catch (const std::exception& e) {
        Logger().errorStream() << "Autosave failed: " << e.what();
        std::cerr << "Autosave failed: " << e.what() << std::endl;
    }
}

std::string HumanClientApp::SelectLoadFile(){
    SaveFileDialog sfd(true);
    sfd.Run();
    return sfd.Result();
}

std::string HumanClientApp::SelectSaveFile() {
    SaveFileDialog sfd(false);
    sfd.Run();
    return sfd.Result();
}

void HumanClientApp::EndGame(bool suppress_FSM_reset) {
    Logger().debugStream() << "HumanClientApp::EndGame";
    m_game_started = false;

    if (m_networking.Connected()) {
        Logger().debugStream() << "HumanClientApp::EndGame Sending server shutdown message.";
        m_networking.SendMessage(ShutdownServerMessage(m_networking.PlayerID()));
        boost::this_thread::sleep(boost::posix_time::seconds(1));
        m_networking.DisconnectFromServer();
        if (!m_networking.Connected())
            Logger().debugStream() << "HumanClientApp::EndGame Disconnected from server.";
        else
            Logger().errorStream() << "HumanClientApp::EndGame Unexpectedly still connected to server...?";
    }

    if (!m_server_process.Empty()) {
        Logger().debugStream() << "HumanClientApp::EndGame Terminated server process.";
        m_server_process.RequestTermination();
    }

    m_networking.SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking.SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);
    m_ui->GetMapWnd()->Sanitize();

    m_universe.Clear();
    m_empires.Clear();
    m_orders.Reset();
    GetCombatLogManager().Clear();

    if (!suppress_FSM_reset)
        m_fsm->process_event(ResetToIntroMenu());
}

void HumanClientApp::UpdateFPSLimit() {
    if (GetOptionsDB().Get<bool>("limit-fps")) {
        double fps = GetOptionsDB().Get<double>("max-fps");
        SetMaxFPS(fps);
        Logger().debugStream() << "Limited FPS to " << fps;
    } else {
        SetMaxFPS(0.0); // disable fps limit
        Logger().debugStream() << "Disabled FPS limit";
    }
}

void HumanClientApp::DisconnectedFromServer() {
    Logger().debugStream() << "HumanClientApp::DisconnectedFromServer";
    m_fsm->process_event(Disconnection());
}

HumanClientApp* HumanClientApp::GetApp()
{ return dynamic_cast<HumanClientApp*>(GG::GUI::GetGUI()); }

void HumanClientApp::Initialize() {

}

