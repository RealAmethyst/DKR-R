"""Reuse the vehicle card preview hook for background-decoded character portraits."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Portrait anchor differs: '+old[:120])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def lobby_layout(text):
    # Move the existing content after all lobby/party repairs have composed.
    # Keep callbacks and permissions intact; only the drawing positions move.
    start=text.index('void DrawOlLobby(');end=text.index('// Knocking on the host',start)
    body=text[start:end]
    visibility='''    OlParagraph(visibility,14.F,kOlSoft,width);
    if(host)DrawOlDirectoryVisibility(width,frame.busy||!directory->connected||hosting!=2);
'''
    body=once(body,visibility,'')
    begin=body.index('    OlParagraph("ONLINE SESSION SAVE - supplied by "')
    finish=body.index('    PaddockGap(14.0F);\n    OlPanelLook look;',begin)
    summary=body[begin:finish]
    body=once(body,summary+'    PaddockGap(14.0F);\n','')
    body=once(body,'''        const float plate_width = narrow ? inner : kOlPlateWidth;
        const bool side = !narrow && inner - plate_width - 20.0F >= 260.0F;
        const float title_width = side ? inner - plate_width - 20.0F : inner;''',
        '''        const float aside_target = std::clamp(inner * 0.32F, kOlPlateWidth, 440.0F);
        const bool side = !narrow && inner - aside_target - 20.0F >= 360.0F;
        const float aside_width = side ? aside_target : inner;
        const float plate_width = std::min(inner, narrow ? inner : kOlPlateWidth);
        const float title_width = side ? inner - aside_width - 20.0F : inner;''')
    summary=''.join('        '+line[4:]+'\n' for line in summary.splitlines())
    summary=summary.replace(', width);',', title_width);')
    body=once(body,'        OlParagraph(line, 15.0F, kOlSoft, title_width);',
        '        OlParagraph(line, 15.0F, kOlSoft, title_width);\n        PaddockGap(16.0F);\n'+summary.rstrip())
    body=once(body,'''        const ImVec2 plate_at = side ? ImVec2{at.x + inner - plate_width, at.y} : ImVec2{at.x, at.y + title_height + 20.0F};
        DrawOlPlate(plate_at, plate_width, frame);
        const float height = side ? std::max(title_height, OlPlateHeight(host) + 4.0F)
                                  : title_height + 20.0F + OlPlateHeight(host) + 4.0F;''',
        '''        const ImVec2 aside_at = side ? ImVec2{at.x + inner - aside_width, at.y}
                                         : ImVec2{at.x, at.y + title_height + 20.0F};
        ImGui::SetCursorScreenPos(aside_at);
        ImGui::BeginGroup();
        DrawOlPlate({aside_at.x + aside_width - plate_width, aside_at.y}, plate_width, frame);
        ImGui::SetCursorScreenPos(aside_at);
        ImGui::Dummy({aside_width, OlPlateHeight(host) + 4.0F});
        PaddockGap(12.0F);
        OlParagraph(visibility,14.F,kOlSoft,aside_width);
        if(host)DrawOlDirectoryVisibility(aside_width,frame.busy||!directory->connected||hosting!=2);
        ImGui::EndGroup();
        const float aside_height = ImGui::GetItemRectSize().y;
        const float height = side ? std::max(title_height, aside_height)
                                  : title_height + 20.0F + aside_height;''')
    return text[:start]+body+text[end:]

def profile_layout(text):
    return once(text,'''    case OlSection::Profile:
        {
            const OlEnter enter(0);
            DrawOlProfileCard(width, frame, true);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(1);
            DrawOlPrivacy(width);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(2);
            DrawOlFriendCodes(width);
        }''', '''    case OlSection::Profile:
        DrawOlProfileSections(width, frame);''')

def profile_account_ui(text,out):
    text=once(text,'// .ol-card: a padded panel around content measured as it is drawn.',
        '#include "'+(out/'profile_panels.inl').as_posix()+'"\n\n// .ol-card: a padded panel around content measured as it is drawn.')
    text=once(text,'    PaddockBox box(width, {padding, padding});\n    content(box.Inner());\n    box.End([&](ImDrawList* draw, ImVec2 a, ImVec2 b) { PaintOlPanel(draw, a, b, look); });',
        '    auto* equal = g_equal_profile_row;\n    const bool capture = equal && std::exchange(equal->capture_next, false);\n'
        '    PaddockBox box(width, {padding, padding});\n    content(box.Inner());\n'
        '    box.End([&](ImDrawList* draw, ImVec2 a, ImVec2 b) {\n'
        '        if(capture && equal->count < equal->panels.size()) equal->panels[equal->count++] = {a,b,look,equal->section};\n'
        '        else PaintOlPanel(draw, a, b, look);\n    });')
    text=once(text,'void DrawOlDirectoryAccount(float);',
        'extern bool g_open_discord_profile;\nvoid DrawOlSelfAvatar(ImDrawList*, ImVec2, float, std::string_view);\n'
        'void DrawOlProfileAvatar(ImDrawList*, ImVec2, float, std::string_view, const std::string&, bool = false);\n'
        'void DrawOlPictureControls(float,bool);\nvoid SaveOlProfilePicture(bool = false);\n'
        'void DrawOlRememberSignIn(float,const OnlineFrame&);\nvoid DrawOlSignOut(float,const OnlineFrame&);\n'
        'void DrawOlPrivacy(float);\nvoid DrawOlFriendCodes(float);\n'
        'void DrawOlAccountSettings(float,const OnlineFrame&);\nvoid DrawOlDirectoryAccount(float);')
    begin=text.index('void DrawOlProfileCard(float width, const OnlineFrame& frame, bool profile_section) {')
    end=text.index('// ------------------------------------------------------------ open lobbies',begin)
    text=text[:begin]+'#include "'+(out/'profile_header.inl').as_posix()+'"\n\n'+text[end:]
    text=once(text,'void DrawOlModals(const OnlineFrame& frame) {',
        '#include "'+(out/'profile_ui.inl').as_posix()+'"\n\nvoid DrawOlModals(const OnlineFrame& frame) {')
    return text


def friends_layout(text,out):
    text=once(text,'    std::string name;          // for the avatar',
        '    std::string avatar_url;\n    std::string name;          // for the avatar')
    text=once(text,'void DrawOlFriends(float width, const OnlineFrame& frame) {',
        '#include "'+(out/'friends_cards.inl').as_posix()+'"\n\nvoid DrawOlFriends(float width, const OnlineFrame& frame) {')
    begin=text.index('void DrawOlFriends(');end=text.index('// ------------------------------------------------------------ overlays',begin)
    body=text[begin:end]
    body=once(body,'        row.key = "friend-" + racer.identity;',
        '        row.key = "friend-" + racer.identity;\n        row.avatar_url = racer.blocked?std::string{}:dkr::profile::bridge::peer_avatar(racer.identity);')
    body=once(body,'        row.actions.push_back({"Manage", OlTone::Plain, false, nullptr, [identity, racer] {',
        '''        if(racer.online && racer.hosting && !racer.blocked && !racer.lobby_code.empty() && !frame.in_lobby && !frame.active) {
            row.status="Hosting - "+std::to_string(racer.players)+"/"+std::to_string(racer.maximum_players)+" racers";
            row.actions.push_back({"Join",OlTone::Go,!frame.rom_ready || frame.busy,nullptr,[racer]{
                std::string error;g_online_page.join_attempted=true;g_online_page.join_error.clear();
                ConfigurePartyJoin();if(!dkr::runtime::netplay::session().join(racer.lobby_code,g_online_player_name,error))OlNotify(error);
            }});
        }
        row.actions.push_back({"Manage", OlTone::Plain, false, nullptr, [identity, racer] {''')
    add_start=body.index('        // Add a friend.')
    add_end=body.rindex('    });')
    add=body[body.index('        OlParagraph("Add a friend",',add_start):add_end]
    add=add.replace('OlButtonWidth("Add",','OlButtonWidth("Add Friend",').replace('OlButton("Add",','OlButton("Add Friend",')
    body=body[:add_start]+body[add_end:]
    body=once(body,'        // Toolbar.',add+'        PaddockGap(16.F);\n        // Toolbar.')
    groups_start=body.index('        // Groups.')
    groups_end=body.index('        if (racers.empty())',groups_start)
    body=body[:groups_start]+'''        static bool requests_open=false;
        const auto requests=incoming.size()+outgoing.size();
        const std::string title="Friend requests ("+std::to_string(requests)+")";
        if(OlButton(title.c_str(),OlTone::Plain,inner))requests_open=!requests_open;
        if(requests_open){
            for(const auto* rows:{&incoming,&outgoing})for(const auto& row:*rows){PaddockGap(8.F);DrawOlFriendRow(row,inner);}
        }
        PaddockGap(14.F);
        DrawOlFriendCards(friends,inner);
'''+body[groups_end:]
    text=text[:begin]+body+text[end:]
    text=once(text,'// ------------------------------------------------------------ lobby\n',
        '#include "'+(out/'lobby_friends_ui.inl').as_posix()+'"\n\n// ------------------------------------------------------------ lobby\n')
    grid_start=text.index('void DrawOlGrid(');grid_end=text.index('void DrawOlGate(',grid_start)
    grid=text[grid_start:grid_end]
    grid=once(grid,'        heights[static_cast<std::size_t>(slot)] = std::max(76.0F, body_height + 20.0F);',
        '        heights[static_cast<std::size_t>(slot)] = std::max(76.0F, body_height + 20.0F) +\n'
        '            (OlLobbyFriendAction(player.peer_id,static_cast<std::size_t>(slot)==view.local_slot)?48.F:0.F);')
    grid=once(grid,'            DrawOlAvatar(draw, {x, a.y + std::round((row_height - 38.0F) * 0.5F)}, 38.0F, player.display_name);',
        '            const float friend_footer=OlLobbyFriendAction(player.peer_id,you)?48.F:0.F;\n'
        '            const float content_height=row_height-friend_footer;\n'
        '            DrawOlProfileAvatar(draw, {x, a.y + std::round((content_height - 38.0F) * 0.5F)}, 38.0F, player.display_name,OlLobbyAvatar(player.peer_id,you));')
    grid=once(grid,'            const float top = a.y + std::round((row_height - body_height) * 0.5F);',
        '            const float top = a.y + std::round((content_height - body_height) * 0.5F);')
    grid=once(grid,'a.y + std::round((row_height - 40.0F) * 0.5F)', 'a.y + std::round((content_height - 40.0F) * 0.5F)')
    grid=once(grid,'            ImGui::PopID();\n        }\n        y += row_height + 12.0F;',
        '            if(friend_footer>0.F){ImGui::SetCursorScreenPos({a.x+66.F,b.y-48.F});DrawOlLobbyFriendAction(player.peer_id,cell-78.F);}\n'
        '            ImGui::PopID();\n        }\n        y += row_height + 12.0F;')
    return text[:grid_start]+grid+text[grid_end:]

def account_service(text,out):
    # Extend the retained service at its existing composition point. The history
    # policy is normal first-party source, shared with the ROM-free host tests.
    history=ROOT/'runtime-recomp/src/game/mod_store/download_history.hpp'
    text='#include "'+history.as_posix()+'"\n'+text
    begin=text.index('        Json rows = Json::array();',text.index('    void persist_downloads()'))
    end=text.index('\n    }',begin)
    text=text[:begin]+'''        Json saved;
        {
            std::lock_guard lock(mutex);
            saved = dkr::store::history::save(state.downloads);
        }
        write_json(root / "downloads.json", saved);'''+text[end:]
    text=once(text,'        const auto saved = read_json(p.root / "downloads.json");',
        '''        const auto saved = read_json(p.root / "downloads.json");
        const auto receipts = read_json(p.root / "installations.json");
        auto restored = dkr::store::history::restore<Download>(saved, receipts, [&](const std::string& path) {
            return dkr::store::history::member_present(p.root.parent_path(), path);
        });''')
    begin=text.index('            if (saved.contains("jobs")')
    end=text.index('\n        });',begin)
    text=text[:begin]+'            s.downloads = std::move(restored);'+text[end:]
    text=once(text,'            s.downloads = std::move(restored);\n        });',
        '''            s.downloads = std::move(restored);
        });
        if (saved.is_object() && saved.contains("jobs") && saved["jobs"].is_array() &&
            saved["jobs"].size() <= 64 && (!saved.contains("schema") || saved["schema"] == 1 || saved["schema"] == 2)) {
            try { p.persist_downloads(); } catch (const std::exception& error) { p.fail(error); }
        }''')
    text=once(text,'namespace dkr::store {',
        '#include "'+(out/'display_name_save.hpp').as_posix()+'"\n#include "'+(out/'profile_bridge.hpp').as_posix()+'"\n#include "'+(out/'picture_codec.hpp').as_posix()+'"\n#include "'+(out/'profile_retry.hpp').as_posix()+'"\n#define DKR_PROFILE_WITH_KEYRING 1\n#include "'+(out/'linux_device_store.hpp').as_posix()+'"\n\nnamespace dkr::store {')
    for operation in ('store','load','clear'):
        text=text.replace('dkr::online::keyring::'+operation+'(', 'dkr::profile::devices::'+operation+'(')
    text=text.replace('if(!dkr::online::keyring::available())s.remember = false;','// Linux supports owner-only device storage when a keyring is unavailable.')
    text=text.replace('if(!dkr::online::keyring::available())remember = false;','// The preference is available on Linux without a keyring too.')
    text=text.replace('The desktop keyring could not remember sign-in. You are signed in for this session only.',
        'Sign-in could not be saved on this device. You are signed in for this session only.')
    text=once(text,'        auto user = me.value("user", Json{});',
        '''        auto user = me.value("user", Json{});
        // Optional on older deployments. Profile metadata never blocks sign-in.
        try {
            auto profile = get("/v1/launcher/discord-profile", stop, true);
            if(profile.is_object() && text(profile,"accountId",80)==text(user,"id",80) &&
               !dkr::profile::display_name(text(profile,"displayName",200)).empty()) user["discordProfile"]=std::move(profile);
        } catch(...) {}''')
    text=once(text,'        // Optional on older deployments. Profile metadata never blocks sign-in.',
        '''        try {
            const auto public_key=dkr::profile::bridge::card().public_key;
            auto profile = get("/v1/launcher/profile?key="+public_key, stop, true);
            if(profile.is_object() && text(profile,"accountId",80)==text(user,"id",80)) user["launcherProfile"]=std::move(profile);
        } catch(...) {}
        // Optional on older deployments. Profile metadata never blocks sign-in.''')
    text=once(text,'    void load_image(const std::string& url, std::stop_token stop) {',
        '#include "'+(out/'profile_service.inl').as_posix()+'"\n\n    void load_image(const std::string& url, std::stop_token stop) {')
    text=once(text,'        "catalogue.update", "admin.role",     "account.profile",   "account.delete"};',
        '        "catalogue.update", "admin.role",     "account.profile",   "account.delete", "launcher.profile", "launcher.profile.preview"};')
    text=once(text,'        Json response;\n        try {\n            response = document(',
        '        if(name=="launcher.profile" || name=="launcher.profile.preview") { p.profile_action(name,input,epoch,credential,stop); return; }\n'
        '        Json response;\n        try {\n            response = document(')
    text=once(text,'        p.update([&](Snapshot& s) {\n            s.status = "Your change was saved.";',
        '\n'.join([
            '        if(name=="account.profile") {',
            '            const auto accepted=response.value("result",Json{});',
            '            if(text(accepted,"id",80)!=text(p.published.load()->user,"id",80) ||',
            '               !dkr::profile::complete_display_name(text(accepted,"displayName",200)))',
            '                throw Error("The account service returned an invalid display name. Refresh and try again.");',
            '        }',
            '        p.update([&](Snapshot& s) {',
            '            if(p.account!=epoch) return;',
            '            if(name=="account.profile") s.user["displayName"]=response["result"]["displayName"];',
            '            s.status = "Your change was saved.";']))
    text=once(text,'        if (name == "account.delete") {',
        '        if(name=="account.profile") { dkr::online::refresh_profile(); return; }\n        if (name == "account.delete") {')
    text=once(text,'            auto r = http({url, "GET", {}, {}, "application/json", {}, 0, MaxImage}, stop);',
        '''            std::string profile_credential;
            if(dkr::profile::custom_avatar_url(url)) {
                std::lock_guard lock(mutex);
                const auto profile=state.user.is_object()?state.user.value("launcherProfile",Json{}):Json{};
                if(text(profile,"avatarUrl",180)==url)profile_credential=token;
            }
            auto r = http({url, "GET", {}, profile_credential, "application/json", {}, 0, MaxImage}, stop);''')
    text=once(text,'prefix(url, std::string(Site) + "/dkr-r/assets/");',
        'prefix(url, std::string(Site) + "/dkr-r/assets/") || dkr::profile::avatar_url(url);')
    text=once(text,'    p.queue([&p](std::stop_token) { p.preferences(); });\n}\nvoid Service::refresh()',
        '''    p.queue([&p](std::stop_token) {
        p.preferences();
        if(!p.published.load()->remember) {
#ifdef _WIN32
            std::error_code error;std::filesystem::remove(p.root/"device.bin",error);
#else
            dkr::profile::devices::clear(p.root.generic_string());
#endif
        }
    });
}
void Service::refresh()''')
    return text

def compose(rows,out):
    if len(rows)!=3 or {Path(r['path']).name for r in rows}!={'runtime_ui.cpp','runtime_mod_library_ui.inl','mod_store.cpp'}:raise ValueError('Portrait source closure differs')
    files={p.name:p.read_bytes() for p in (AUTH/'native').glob('*')};swaps=[]
    files.update({p.name:p.read_bytes() for p in (ROOT/'runtime-recomp/src/game/online_profile').glob('*') if p.is_file()})
    for name in ('profile_bridge.hpp','profile_policy.hpp'):
        # One definition of the bridge registry across UI, account and social TUs.
        files[name]=('#include "'+(ROOT/'runtime-recomp/src/game/online_profile'/name).as_posix()+'"\n').encode()
    def stage_include(text,ending,change):
        paths=re.findall(r'^#include "([^"\n]*'+re.escape(ending)+r')"',text,flags=re.M)
        if len(paths)!=1:raise ValueError('Expected final include: '+ending)
        original=Path(paths[0]);raw=original.read_bytes();copy=change(raw.decode().replace('\r\n','\n'))
        def absolute(m):
            path=Path(m[1]);path=path if path.is_absolute() else original.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        copy=re.sub(r'^#include "([^"\n]+)"',absolute,copy,flags=re.M)
        files[ending]=copy.encode()
        swaps.append({'target':'DKRPortGame','path':str(original),'new':str(out/ending),'input_sha256':sha(raw),'output_sha256':sha(copy.encode()),'compiled':False})
        return once(text,'#include "'+paths[0]+'"','#include "'+(out/ending).as_posix()+'"')
    def directory_ui(text):
        text=once(text,'const bool secure_remember=dkr::online::keyring::available();','const bool secure_remember=true;')
        return once(text,'"Protected by this computer\'s credential store."',
            '"Uses the system credential store when available. Linux without a keyring saves a file readable only by your Linux user."')
    def account_ui(text):
        text=once(text,'const bool can_remember=dkr::online::keyring::available();','const bool can_remember=true;')
        text=once(text,'''        StoreField("profile-name", "Display name", profile.inner(), 60);
        ImGui::BeginDisabled(g_mod_store.fields["profile-name"].empty() ||
                             g_mod_store.fields["profile-name"] == StoreText(store.user, "displayName", 60));
        if (StoreButton("Save name", 0, true))
            StoreAction("account.profile", {{"displayName", g_mod_store.fields["profile-name"]}});
        ImGui::EndDisabled();''',
            '        DrawOlStoreProfileName(profile.inner(), StoreBusyAction(store));')
        return once(text,'"Your desktop keyring remembers this sign-in."','"Sign-in is remembered on this device. Linux uses its keyring or a file restricted to your Linux user."')
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        if source.name=='mod_store.cpp':
            text=account_service(text,out)
        elif source.suffix=='.cpp':
            text='#include "'+(out/'display_name_save.hpp').as_posix()+'"\n#include "'+(out/'profile_bridge.hpp').as_posix()+'"\n'+text
            text=once(text,'char g_online_profile_name[25] = "Racer";',
                'char g_online_profile_name[25] = "Racer";\nstd::string g_discord_choice_account, g_discord_avatar_url;\nbool g_profile_share_picture = true;\nvoid DrawOlDisplayName(float,bool);\nvoid DrawOlDisplayNameStatus(float);\nvoid DrawOlStoreProfileName(float,bool);\nbool SaveOlDisplayName(std::string_view);')
            text=once(text,'        output << "online_player_name="',
                '        output << "discord_choice_account=" << g_discord_choice_account << \'\\n\';\n'
                '        output << "discord_avatar_url=" << g_discord_avatar_url << \'\\n\';\n        output << "online_player_name="')
            text=once(text,'        output << "discord_avatar_url="',
                '        output << "profile_share_picture=" << (g_profile_share_picture?1:0) << \'\\n\';\n        output << "discord_avatar_url="')
            text=once(text,'        if (key == "online_player_name") {',
                '        if (key == "discord_choice_account") { if(value.size()<=80 && value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==value.npos)g_discord_choice_account=value; continue; }\n'
                '        if (key == "discord_avatar_url") { if(dkr::profile::avatar_url(value))g_discord_avatar_url=value; continue; }\n'
                '        if (key == "online_player_name") {')
            text=once(text,'        if (key == "discord_avatar_url") {',
                '        if (key == "profile_share_picture") { g_profile_share_picture=value!="0"; continue; }\n        if (key == "discord_avatar_url") {')
            if text.count('        DrawTextEntryKeyboard();')!=2:raise ValueError('Launcher/overlay keyboard surface changed')
            text=text.replace('        DrawTextEntryKeyboard();','        DrawDiscordProfilePrompt();\n        DrawTextEntryKeyboard();')
            text='#include "'+(out/'character_portraits.hpp').as_posix()+'"\n'+text
            online=re.findall(r'^#include "([^"\n]*runtime_online_ui\.inl)"',text,flags=re.M)
            if len(online)!=1:raise ValueError('Expected one final lobby UI include')
            online_source=Path(online[0]);online_source=online_source if online_source.is_absolute() else source.parent/online_source
            online_text=stage_include(online_source.read_text(encoding='utf-8'),'directory_ui.inl',directory_ui)
            online_text=stage_include(online_text,'lobby_controls.inl',lambda value:once(value,
                '            OlParagraph(m->name,20.F,kOlCream,width,true);PaddockGap(12.F);',
                '            OlParagraph(m->name,20.F,kOlCream,width,true);PaddockGap(12.F);\n'
                '            if(OlLobbyFriendAction(std::to_string(m->id),m->id==g_ol_party.link.local)){DrawOlLobbyFriendAction(std::to_string(m->id),width);PaddockGap(12.F);}') )
            files['runtime_online_ui.inl']=friends_layout(profile_account_ui(profile_layout(lobby_layout(online_text)),out),out).encode()
            swaps.append({'target':row['target'],'path':str(online_source),'new':str(out/'runtime_online_ui.inl'),
                'input_sha256':sha(online_source.read_bytes()),'output_sha256':sha(files['runtime_online_ui.inl']),'compiled':False})
            text=once(text,'#include "'+online[0]+'"','#include "'+(out/'runtime_online_ui.inl').as_posix()+'"')
            text=stage_include(text,'mod_store_ui.inl',lambda value:stage_include(value,'store_account_page.inl',account_ui))
            text=once(text,'#include "runtime_mod_library_ui.inl"','#include "'+(out/'runtime_mod_library_ui.inl').as_posix()+'"')
            text=once(text,'    ReleaseModStoreTextures();','    ReleaseDiscordProfileTexture();\n    ReleaseCharacterPortraits();\n    ReleaseModStoreTextures();')
            text=once(text,'    g_mod_store_renderer = renderer;','    g_mod_store_renderer = renderer;\n    g_character_portrait_renderer = renderer;')
        else:
            text=once(text,'// ------------------------------------------------------------ library data',
                '#include "'+(out/'character_portraits_ui.inl').as_posix()+'"\n\n// ------------------------------------------------------------ library data')
            text=once(text,'        view.info = "Custom racer";',
                '        view.info = "Custom racer";\n        view.preview_height=100;view.preview=[item](float w,float h){DrawCharacterPortrait(item,w,h);};')
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        files[source.name]=text.encode();swaps.append({**row,'new':str(out/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode()),'compiled':source.suffix=='.cpp'})
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Portrait outputs must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Character portraits staged through the existing vehicle card preview hook.')
