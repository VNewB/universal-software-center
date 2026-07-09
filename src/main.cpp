/*
 * VNLF App Store - GTK3 C++ Application
 * Kho ứng dụng Linux tiếng Việt
 */

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <fontconfig/fontconfig.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <iostream>
#include <unistd.h>
#include <vte/vte.h>
#include <vte/vteterminal.h>

// ─── Font loader ──────────────────────────────────────────────────────────────
static std::string g_fonts_dir;
static void load_app_fonts() {
    if (g_fonts_dir.empty()) return;
    FcInit();
    FcConfig *cfg = FcConfigGetCurrent();
    if (!FcConfigAppFontAddDir(cfg, reinterpret_cast<const FcChar8*>(g_fonts_dir.c_str()))) {
        GDir *dir = g_dir_open(g_fonts_dir.c_str(), 0, nullptr);
        if (dir) {
            const gchar *fn;
            while ((fn = g_dir_read_name(dir)) != nullptr) {
                std::string s(fn);
                if (s.size()>4 && (s.substr(s.size()-4)==".ttf"||s.substr(s.size()-4)==".otf")) {
                    std::string p = g_fonts_dir+"/"+s;
                    FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(p.c_str()));
                }
            }
            g_dir_close(dir);
        }
    }
    FcConfigBuildFonts(cfg);
}

// ─── Package Manager Detection ────────────────────────────────────────────────
struct PkgManager {
    std::string id, display, check_cmd;
    bool needs_sudo, available;
};
static std::vector<PkgManager> g_pkg_managers;

static bool cmd_exists(const std::string& cmd) {
    return system(("command -v "+cmd+" >/dev/null 2>&1").c_str()) == 0;
}

static void detect_package_managers() {
    g_pkg_managers = {
        {"apt",      "APT (Debian/Ubuntu)",  "apt",      true,  false},
        {"apt-get",  "apt-get",              "apt-get",  true,  false},
        {"dnf",      "DNF (Fedora/RHEL)",    "dnf",      true,  false},
        {"yum",      "YUM (CentOS/RHEL)",    "yum",      true,  false},
        {"pacman",   "Pacman (Arch)",         "pacman",   true,  false},
        {"yay",      "Yay (AUR)",             "yay",      false, false},
        {"paru",     "Paru (AUR)",            "paru",     false, false},
        {"zypper",   "Zypper (openSUSE)",     "zypper",   true,  false},
        {"flatpak",  "Flatpak",              "flatpak",  false, false},
        {"snap",     "Snap",                 "snap",     true,  false},
        {"brew",     "Homebrew",             "brew",     false, false},
        {"nix-env",  "Nix",                  "nix-env",  false, false},
        {"pip",      "pip (Python)",          "pip",      false, false},
        {"pip3",     "pip3 (Python3)",        "pip3",     false, false},
        {"cargo",    "Cargo (Rust)",          "cargo",    false, false},
        {"npm",      "npm (Node.js)",         "npm",      false, false},
        {"appimage", "AppImage",             "chmod",    false, false},
    };
    for (auto& pm : g_pkg_managers) pm.available = cmd_exists(pm.check_cmd);
    for (auto& pm : g_pkg_managers) if (pm.id=="appimage") pm.available = true;
    std::cout << "[PM] Available: ";
    for (auto& pm : g_pkg_managers) if (pm.available) std::cout << pm.id << " ";
    std::cout << "\n";
}

static bool is_pm_available(const std::string& id) {
    for (auto& pm : g_pkg_managers) if (pm.id==id && pm.available) return true;
    return false;
}
static bool pm_needs_sudo(const std::string& id) {
    for (auto& pm : g_pkg_managers) if (pm.id==id) return pm.needs_sudo;
    return true;
}
static std::string pm_display(const std::string& id) {
    for (auto& pm : g_pkg_managers) if (pm.id==id) return pm.display;
    return id;
}
// ─── JSON helpers ─────────────────────────────────────────────────────────────
static std::string json_get_string(const std::string& json, const std::string& key) {
    std::string search = "\""+key+"\"";
    size_t pos = json.find(search);
    if (pos==std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos==std::string::npos) return "";
    size_t vpos = json.find_first_not_of(" \t\r\n", pos+1);
    if (vpos!=std::string::npos && json[vpos]!='"') {
        size_t epos = json.find_first_of(",}\n", vpos);
        std::string raw = epos==std::string::npos ? "" : json.substr(vpos, epos-vpos);
        // trim whitespace
        while (!raw.empty() && (raw.back()==' '||raw.back()=='\r'||raw.back()=='\n')) raw.pop_back();
        return raw;
    }
    pos = json.find('"', pos);
    if (pos==std::string::npos) return "";
    size_t start = pos+1;
    size_t end = json.find('"', start);
    while (end!=std::string::npos && json[end-1]=='\\') end = json.find('"', end+1);
    if (end==std::string::npos) return "";
    std::string val = json.substr(start, end-start);
    std::string result;
    for (size_t i=0; i<val.size(); i++) {
        if (val[i]=='\\' && i+1<val.size()) {
            switch(val[i+1]){case 'n':result+='\n';i++;break;case 't':result+='\t';i++;break;
                             case '"':result+='"';i++;break;case '\\':result+='\\';i++;break;default:result+=val[i];}
        } else result+=val[i];
    }
    return result;
}

static bool json_get_bool(const std::string& json, const std::string& key, bool def=false) {
    std::string v = json_get_string(json, key);
    if (v=="true") return true; if (v=="false") return false; return def;
}

static std::vector<std::string> json_get_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    size_t pos = json.find("\""+key+"\"");
    if (pos==std::string::npos) return result;
    pos = json.find('[', pos);
    if (pos==std::string::npos) return result;
    size_t end = json.find(']', pos);
    if (end==std::string::npos) return result;
    std::string arr = json.substr(pos+1, end-pos-1);
    size_t p = 0;
    while (p<arr.size()) {
        size_t s = arr.find('"', p); if (s==std::string::npos) break;
        size_t e = arr.find('"', s+1); if (e==std::string::npos) break;
        result.push_back(arr.substr(s+1, e-s-1)); p = e+1;
    }
    return result;
}

static std::vector<std::string> json_get_object_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    size_t pos = json.find("\""+key+"\"");
    if (pos==std::string::npos) return result;
    pos = json.find('[', pos);
    if (pos==std::string::npos) return result;
    int depth=0; size_t obj_start=std::string::npos;
    for (size_t i=pos; i<json.size(); i++) {
        if (json[i]=='{') { if(!depth) obj_start=i; depth++; }
        else if (json[i]=='}') { depth--; if(!depth&&obj_start!=std::string::npos){result.push_back(json.substr(obj_start,i-obj_start+1));obj_start=std::string::npos;} }
        else if (json[i]==']' && !depth) break;
    }
    return result;
}

// ─── Data structures ──────────────────────────────────────────────────────────
struct InstallMethod {
    std::string pm, package, script;
    bool needs_sudo, sudo_override, unlocked;
};

struct AppInfo {
    std::string id, name, summary, description, category, icon;
    std::string version, author, license, website, script;
    std::vector<std::string> tags;
    std::vector<InstallMethod> install_methods;
    bool installed;
    std::string installed_via;
};

// ─── Install Log ──────────────────────────────────────────────────────────────
static std::string g_log_path;
struct InstalledEntry { std::string app_id, pm, package, version; };
static std::vector<InstalledEntry> g_install_log;

static std::string esc_json(const std::string& s) {
    std::string r;
    for (char c:s){if(c=='"')r+="\\\"";else if(c=='\\')r+="\\\\";else if(c=='\n')r+="\\n";else r+=c;}
    return r;
}

static void save_install_log() {
    std::string dir = g_log_path.substr(0, g_log_path.rfind('/'));
    g_mkdir_with_parents(dir.c_str(), 0755);
    std::ofstream f(g_log_path);
    if (!f.is_open()) return;
    f << "{\n  \"installed\": [\n";
    for (size_t i=0; i<g_install_log.size(); i++) {
        auto& e = g_install_log[i];
        f << "    {\"app_id\":\"" << esc_json(e.app_id) << "\",\"pm\":\"" << esc_json(e.pm)
          << "\",\"package\":\"" << esc_json(e.package) << "\",\"version\":\"" << esc_json(e.version) << "\"}";
        if (i+1<g_install_log.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

static void load_install_log() {
    g_install_log.clear();
    std::ifstream f(g_log_path);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    for (auto& obj : json_get_object_array(content, "installed")) {
        InstalledEntry e;
        e.app_id=json_get_string(obj,"app_id"); e.pm=json_get_string(obj,"pm");
        e.package=json_get_string(obj,"package"); e.version=json_get_string(obj,"version");
        if (!e.app_id.empty()) g_install_log.push_back(e);
    }
    std::cout << "[Log] " << g_install_log.size() << " entries\n";
}

static bool is_in_log(const std::string& app_id) {
    for (auto& e:g_install_log) if(e.app_id==app_id) return true; return false;
}
static InstalledEntry* find_log_entry(const std::string& app_id) {
    for (auto& e:g_install_log) if(e.app_id==app_id) return &e; return nullptr;
}
static void add_to_log(const InstalledEntry& entry) {
    for (auto& e:g_install_log) if(e.app_id==entry.app_id){e=entry;save_install_log();return;}
    g_install_log.push_back(entry); save_install_log();
}
static void remove_from_log(const std::string& app_id) {
    g_install_log.erase(std::remove_if(g_install_log.begin(),g_install_log.end(),
        [&](const InstalledEntry& e){return e.app_id==app_id;}),g_install_log.end());
    save_install_log();
}
// ─── System install detection ─────────────────────────────────────────────────
static bool check_app_installed_system(const AppInfo& app) {
    const std::vector<std::string> paths = {
        "/usr/bin/" + app.id,
        "/usr/local/bin/" + app.id,
        "/bin/" + app.id,
        "/snap/bin/" + app.id,
    };

    for (const auto& path : paths) {
        if (access(path.c_str(), X_OK) == 0)
            return true;
    }

    return false;
}

static std::string detect_installed_pm(const AppInfo& app) {
    if (is_pm_available("flatpak") && system(("flatpak list --app 2>/dev/null | grep -qi '"+app.id+"'").c_str())==0) return "flatpak";
    if (is_pm_available("snap")    && system(("snap list 2>/dev/null | grep -qi '"+app.id+"'").c_str())==0)    return "snap";
    if (is_pm_available("apt")     && system(("dpkg -l '"+app.id+"' 2>/dev/null | grep -q '^ii'").c_str())==0) return "apt";
    if (is_pm_available("dnf")     && system(("rpm -q '"+app.id+"' 2>/dev/null").c_str())==0)                   return "dnf";
    if (is_pm_available("pacman")  && system(("pacman -Q '"+app.id+"' 2>/dev/null").c_str())==0)                return "pacman";
    return "unknown";
}

// ─── JSON parsing ─────────────────────────────────────────────────────────────
static AppInfo parse_app_object(const std::string& obj) {
    AppInfo app;
    app.id=json_get_string(obj,"id"); app.name=json_get_string(obj,"name");
    app.summary=json_get_string(obj,"summary"); app.description=json_get_string(obj,"description");
    app.category=json_get_string(obj,"category"); app.icon=json_get_string(obj,"icon");
    app.version=json_get_string(obj,"version"); app.author=json_get_string(obj,"author");
    app.license=json_get_string(obj,"license"); app.website=json_get_string(obj,"website");
    app.script=json_get_string(obj,"script"); app.tags=json_get_array(obj,"tags");
    app.installed=false;
    for (auto& mobj : json_get_object_array(obj,"install_methods")) {
        InstallMethod m;
        m.pm=json_get_string(mobj,"pm"); m.package=json_get_string(mobj,"package");
        m.script=json_get_string(mobj,"script");
        std::string sv=json_get_string(mobj,"sudo");
        if (sv=="true"||sv=="false"){m.needs_sudo=(sv=="true");m.sudo_override=true;}
        else {m.needs_sudo=pm_needs_sudo(m.pm);m.sudo_override=false;}
        m.unlocked=is_pm_available(m.pm);
        if (!m.pm.empty()) app.install_methods.push_back(m);
    }
    // Legacy single script
    if (app.install_methods.empty() && !app.script.empty()) {
        InstallMethod m; m.pm="script"; m.package=app.id; m.script=app.script;
        m.needs_sudo=true; m.sudo_override=false; m.unlocked=true;
        app.install_methods.push_back(m);
    }
    return app;
}

static std::string app_to_json(const AppInfo& app) {
    auto esc=[](const std::string& s){std::string r;for(char c:s){if(c=='"')r+="\\\"";else if(c=='\\')r+="\\\\";else if(c=='\n')r+="\\n";else r+=c;}return r;};
    std::string j="{\n";
    j+="  \"id\": \""+esc(app.id)+"\",\n";
    j+="  \"name\": \""+esc(app.name)+"\",\n";
    j+="  \"summary\": \""+esc(app.summary)+"\",\n";
    j+="  \"description\": \""+esc(app.description)+"\",\n";
    j+="  \"category\": \""+esc(app.category)+"\",\n";
    j+="  \"icon\": \""+esc(app.icon)+"\",\n";
    j+="  \"version\": \""+esc(app.version)+"\",\n";
    j+="  \"author\": \""+esc(app.author)+"\",\n";
    j+="  \"license\": \""+esc(app.license)+"\",\n";
    j+="  \"website\": \""+esc(app.website)+"\",\n";
    j+="  \"script\": \""+esc(app.script)+"\",\n";
    j+="  \"tags\": [";
    for (size_t i=0;i<app.tags.size();i++){j+="\""+esc(app.tags[i])+"\"";if(i+1<app.tags.size())j+=", ";}
    j+="],\n  \"install_methods\": [\n";
    for (size_t i=0;i<app.install_methods.size();i++){
        auto& m=app.install_methods[i];
        j+="    {\"pm\":\""+esc(m.pm)+"\",\"package\":\""+esc(m.package)+"\"";
        if (!m.script.empty()) j+=",\"script\":\""+esc(m.script)+"\"";
        if (m.sudo_override) j+=std::string(",\"sudo\":")+( m.needs_sudo?"true":"false");
        j+="}"; if(i+1<app.install_methods.size())j+=","; j+="\n";
    }
    j+="  ]\n}";
    return j;
}

static AppInfo parse_single_json_file(const std::string& path) {
    std::ifstream f(path); if (!f.is_open()) return AppInfo{};
    std::string c((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); f.close();
    size_t s=c.find('{'); if(s==std::string::npos) return AppInfo{};
    int depth=0; size_t end=std::string::npos;
    for (size_t i=s;i<c.size();i++){if(c[i]=='{')depth++;else if(c[i]=='}'){depth--;if(!depth){end=i;break;}}}
    if(end==std::string::npos) return AppInfo{};
    return parse_app_object(c.substr(s,end-s+1));
}

static std::vector<AppInfo> parse_apps_json(const std::string& data_dir) {
    std::vector<AppInfo> apps;
    std::string subdir=data_dir+"/apps";
    GDir *dir=g_dir_open(subdir.c_str(),0,nullptr);
    if (dir) {
        std::vector<std::string> files;
        const gchar *fn;
        while ((fn=g_dir_read_name(dir))!=nullptr){std::string s(fn);if(s.size()>5&&s.substr(s.size()-5)==".json")files.push_back(s);}
        g_dir_close(dir); std::sort(files.begin(),files.end());
        for (auto& f:files){AppInfo a=parse_single_json_file(subdir+"/"+f);if(!a.id.empty())apps.push_back(a);}
        if (!apps.empty()) return apps;
    }
    std::ifstream f(data_dir+"/apps.json"); if (!f.is_open()) return apps;
    std::string c((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); f.close();
    int depth=0; size_t obj_start=std::string::npos;
    for (size_t i=0;i<c.size();i++){
        if(c[i]=='{'){if(!depth)obj_start=i;depth++;}
        else if(c[i]=='}'){depth--;if(!depth&&obj_start!=std::string::npos){AppInfo a=parse_app_object(c.substr(obj_start,i-obj_start+1));if(!a.id.empty())apps.push_back(a);obj_start=std::string::npos;}}
    }
    return apps;
}

static bool save_app_json(const AppInfo& app, const std::string& data_dir) {
    std::string d=data_dir+"/apps"; g_mkdir_with_parents(d.c_str(),0755);
    std::ofstream f(d+"/"+app.id+".json"); if(!f.is_open()) return false;
    f<<app_to_json(app); return true;
}

// ─── Category mapping ─────────────────────────────────────────────────────────
static std::map<std::string,std::string> CATEGORY_LABELS={
    {"","Tất cả"},{"internet","Internet"},{"multimedia","Đa phương tiện"},
    {"office","Văn phòng"},{"graphics","Đồ họa"},{"development","Lập trình"},
    {"system","Hệ thống"},{"games","Trò chơi"},{"education","Giáo dục"},{"utilities","Tiện ích"},
};
static std::map<std::string,std::string> CATEGORY_ICONS={
    {"","view-grid-symbolic"},{"internet","network-wireless-symbolic"},
    {"multimedia","multimedia-player-symbolic"},{"office","x-office-document-symbolic"},
    {"graphics","applications-graphics-symbolic"},{"development","edit-copy-symbolic"},
    {"system","preferences-system-symbolic"},{"games","applications-games-symbolic"},
    {"education","applications-science-symbolic"},{"utilities","applications-utilities-symbolic"},
};
// ─── AppState ─────────────────────────────────────────────────────────────────
struct AppState {
    std::vector<AppInfo> all_apps, filtered_apps;
    std::string current_category, search_query, scripts_dir, data_dir, icons_dir;
    GtkWidget *window, *search_entry, *apps_grid, *apps_scrolled;
    GtkWidget *count_label, *stack, *detail_box, *about_box;
    GtkWidget *sidebar_box, *sidebar_separator;
    bool sidebar_collapsed;
    AppInfo *current_detail_app;
    bool installing;
    std::string about_url, about_script;
    AppState():sidebar_box(nullptr),sidebar_separator(nullptr),sidebar_collapsed(false),
               current_detail_app(nullptr),installing(false){}
};
static AppState g_state;

// ─── Icon loading ─────────────────────────────────────────────────────────────
static GdkPixbuf* load_app_icon_pixbuf(const AppInfo& app, int size) {
    std::vector<std::string> cands={
        g_state.icons_dir+"/"+app.id+".png",g_state.icons_dir+"/"+app.id+".jpg",
        g_state.icons_dir+"/"+app.icon+".png",
    };
    for (auto& p:cands){if(p.find("//")==std::string::npos){GError*e=nullptr;GdkPixbuf*pb=gdk_pixbuf_new_from_file_at_scale(p.c_str(),size,size,TRUE,&e);if(pb)return pb;if(e)g_error_free(e);}}
    GtkIconTheme*theme=gtk_icon_theme_get_default();
    const char*name=app.icon.empty()?"application-x-executable":app.icon.c_str();
    GError*e=nullptr; GdkPixbuf*pb=gtk_icon_theme_load_icon(theme,name,size,GTK_ICON_LOOKUP_FORCE_SIZE,&e);
    if(!pb){if(e)g_error_free(e);pb=gtk_icon_theme_load_icon(theme,"application-x-executable",size,GTK_ICON_LOOKUP_FORCE_SIZE,nullptr);}
    return pb;
}
static GtkWidget* make_app_image(const AppInfo& app, int size) {
    GdkPixbuf*pb=load_app_icon_pixbuf(app,size);
    if(pb){GtkWidget*img=gtk_image_new_from_pixbuf(pb);g_object_unref(pb);return img;}
    GtkWidget*img=gtk_image_new_from_icon_name("application-x-executable",GTK_ICON_SIZE_INVALID);
    gtk_image_set_pixel_size(GTK_IMAGE(img),size); return img;
}

// ─── Utilities ────────────────────────────────────────────────────────────────
static std::string utf8_lower(const std::string& s){std::string r=s;for(auto&c:r)if(c>='A'&&c<='Z')c=c+32;return r;}

static bool app_matches_filter(const AppInfo& app,const std::string& cat,const std::string& q){
    if(!cat.empty()&&app.category!=cat)return false;
    if(q.empty())return true;
    std::string ql=utf8_lower(q);
    if(utf8_lower(app.name).find(ql)!=std::string::npos)return true;
    if(utf8_lower(app.summary).find(ql)!=std::string::npos)return true;
    for(auto&t:app.tags)if(utf8_lower(t).find(ql)!=std::string::npos)return true;
    return false;
}
static void apply_filter(){
    g_state.filtered_apps.clear();
    for(auto&app:g_state.all_apps) if(app_matches_filter(app,g_state.current_category,g_state.search_query))g_state.filtered_apps.push_back(app);
}
// ─── CSS ──────────────────────────────────────────────────────────────────────

static const char* APP_CSS = R"CSS(
*{font-family:"Cantarell",sans-serif;}
window{background-color:#f5f3ed;}
.custom-titlebar{background:#3a6146;padding:0 8px;min-height:46px;border-bottom:1px solid #2a4f36;box-shadow:0 2px 6px rgba(0,0,0,0.22);}
.custom-titlebar:backdrop{background:#2e4e38;}
.titlebar-title{color:#fff;font-size:15px;font-weight:bold;letter-spacing:0.3px;}
.titlebar-subtitle{color:rgba(255,255,255,0.7);font-size:10px;}
.search-box{background:#ffffff;border:1.5px solid rgba(255,255,255,0.9);border-radius:20px;padding:5px 14px;color:#1a1a1a;caret-color:#3a6146;min-width:240px;}
.search-box:focus{border-color:#a8d5b5;box-shadow:0 0 0 2px rgba(168,213,181,0.5);outline:none;}
.sidebar{background-color:#2e4e38;border-right:1px solid #1e3828;}
.sidebar-title{font-size:9px;font-weight:bold;color:rgba(255,255,255,0.45);letter-spacing:1.6px;padding:14px 14px 6px 14px;}
.sidebar-toggle-btn{background:rgba(255,255,255,0.07);border:none;border-top:1px solid rgba(255,255,255,0.1);border-radius:0;padding:10px;color:rgba(255,255,255,0.6);font-size:16px;min-height:40px;}
.sidebar-toggle-btn:hover{background:rgba(255,255,255,0.13);color:white;}
.category-btn{background:transparent;border:none;border-radius:8px;padding:9px 12px;margin:1px 6px;color:rgba(255,255,255,0.82);font-size:12px;}
.category-btn:hover{background:rgba(255,255,255,0.1);color:white;}
.category-btn.active{background:rgba(255,255,255,0.18);color:white;font-weight:bold;}
.category-count{font-size:10px;color:rgba(255,255,255,0.45);background:rgba(255,255,255,0.1);border-radius:10px;padding:1px 6px;}
.category-btn-compact{background:transparent;border:none;border-radius:8px;padding:10px 0;margin:1px 4px;color:rgba(255,255,255,0.7);min-width:40px;}
.category-btn-compact:hover{background:rgba(255,255,255,0.12);color:white;}
.category-btn-compact.active{background:rgba(255,255,255,0.2);color:white;}
.sidebar-about-btn{background:rgba(255,255,255,0.05);border:none;border-top:1px solid rgba(255,255,255,0.08);border-radius:0;padding:10px 12px;color:rgba(255,255,255,0.7);font-size:12px;}
.sidebar-about-btn:hover{background:rgba(255,255,255,0.12);color:white;}
.sidebar-about-btn.active{background:rgba(255,255,255,0.18);color:white;font-weight:bold;}
.app-card{background:#fffffe;border-radius:12px;border:1px solid #e2ddd4;padding:16px;}
.app-card:hover{border-color:#3a6146;box-shadow:0 4px 16px rgba(58,97,70,0.13);}
.app-card-installed{border-left:3px solid #3a6146;}
.app-name{font-size:14px;font-weight:bold;color:#1a2e22;}
.app-summary{font-size:12px;color:#6b6358;}
.app-version{font-size:11px;color:#9a9285;}
.app-author{font-size:11px;color:#9a9285;}
.installed-badge{font-size:10px;border-radius:6px;padding:2px 8px;color:white;background-color:#3a6146;}
.not-installed-badge{font-size:10px;border-radius:6px;padding:2px 8px;color:#3a6146;background-color:#d4e8da;border:1px solid #a8c8b4;}
.cat-badge{font-size:10px;border-radius:6px;padding:2px 8px;color:white;background-color:#3a6146;}
.cat-internet{background-color:#00838f;}.cat-multimedia{background-color:#ad1457;}
.cat-office{background-color:#2e7d32;}.cat-graphics{background-color:#6a1b9a;}
.cat-development{background-color:#bf360c;}.cat-system{background-color:#4e5d6a;}
.cat-games{background-color:#b71c1c;}.cat-education{background-color:#283593;}
.cat-utilities{background-color:#4e342e;}
.install-btn{background:linear-gradient(135deg,#3a6146,#2b4e37);color:white;border:none;border-radius:8px;padding:9px 22px;font-size:13px;font-weight:bold;min-width:120px;}
.install-btn:hover{background:linear-gradient(135deg,#2e4e38,#1e3828);box-shadow:0 3px 10px rgba(58,97,70,0.4);}
.install-btn:disabled{background:#ccc;color:#888;}
.uninstall-btn{background:white;color:#c62828;border:1.5px solid #e57373;border-radius:8px;padding:9px 18px;font-size:13px;font-weight:bold;min-width:120px;}
.uninstall-btn:hover{background:#fdecea;border-color:#c62828;}
.method-btn{background:white;border:1.5px solid #a8c8b4;border-radius:8px;padding:10px 16px;color:#1a2e22;font-size:13px;}
.method-btn:hover{background:#f0f8f2;border-color:#3a6146;}
.method-btn-locked{background:#f5f5f5;border-color:#ddd;color:#aaa;}
.pm-badge{font-size:10px;border-radius:4px;padding:2px 7px;background:#e8f0fe;color:#1a56c4;border:1px solid #c5d8f8;}
.pm-badge-locked{font-size:10px;border-radius:4px;padding:2px 7px;background:#f0f0f0;color:#aaa;border:1px solid #ddd;}
.detail-header{background:#fffffe;border-bottom:1px solid #e2ddd4;}
.detail-name{font-size:26px;font-weight:bold;color:#1a2e22;}
.detail-summary{font-size:14px;color:#5a5348;margin-top:4px;}
.detail-description{font-size:14px;color:#3d3830;}
.detail-meta-label{font-size:10px;font-weight:bold;color:#9a9285;letter-spacing:0.9px;}
.detail-meta-value{font-size:13px;color:#3a3228;margin-top:2px;}
.tag-chip{background:#d4e8da;color:#2b4e37;border-radius:14px;padding:3px 11px;font-size:11px;margin:2px;}
.back-btn{background:transparent;border:none;color:#3a6146;border-radius:6px;padding:6px 12px;font-size:13px;font-weight:bold;}
.back-btn:hover{background:#e8f0eb;color:#2b4e37;}
.sudo-dialog-title{font-size:15px;font-weight:bold;color:#1a2e22;}
.sudo-dialog-subtitle{font-size:12px;color:#6b6358;}
.sudo-password-entry{border:1.5px solid #ccc;border-radius:8px;padding:8px 12px;font-size:14px;min-width:240px;background:white;color:#1a1a1a;}
.sudo-password-entry:focus{border-color:#3a6146;box-shadow:0 0 0 2px rgba(58,97,70,0.2);}
.sudo-error-label{color:#c62828;font-size:12px;}
/*
.sudo-entry-error{border-color: #c62828; box-shadow: 0 0 0 2px rgba(198,40,40,0.25);}
*/
.sudo-ok-btn{background:linear-gradient(135deg,#3a6146,#2b4e37);color:white;border:none;border-radius:8px;padding:8px 20px;font-weight:bold;}
.sudo-ok-btn:hover{background:linear-gradient(135deg,#2e4e38,#1e3828);}
/*
.sudo-ok-btn:active{background:linear-gradient(135deg,#1e3828,#2e4e38);color:white;}
*/
.sudo-ok-btn:disabled{background:linear-gradient(135deg,#ccc,#bbb);}

.sudo-cancel-btn{background:white;color:#3a3228;border:1px solid #ccc;border-radius:8px;padding:8px 16px;}
.sudo-cancel-btn:hover{background:#f0ece5;}
.install-dialog-title{font-size:15px;font-weight:bold;color:#1a2e22;}
.terminal-box{background:#1a231e;color:#7ee8a2;border-radius:8px;padding:12px;font-family:"FiraCode Nerd Font Mono","Fira Code",monospace;font-size:12px;}
.progress-bar{min-height:6px;border-radius:3px;}
.progress-bar progress{background:linear-gradient(90deg,#3a6146,#7ee8a2);border-radius:3px;}
.status-bar{border:none;font-size:14px;color: #7a7264;}
.empty-state{color:#9a9285;font-size:15px;padding:60px;}
.empty-state-icon{color:#c8c4ba;}
.about-box{background:#fffffe;}
.about-title{font-size:24px;font-weight:bold;color:#1a2e22;}
.about-subtitle{font-size:14px;color:#5a5348;}
.about-body{font-size:13px;color:#3d3830;}
.about-action-btn{background:linear-gradient(135deg,#3a6146,#2b4e37);color:white;border:none;border-radius:8px;padding:10px 22px;font-size:13px;font-weight:bold;}
.about-action-btn:hover{background:linear-gradient(135deg,#2e4e38,#1e3828);}
.about-script-btn{background:white;color:#3a6146;border:1.5px solid #a8c8b4;border-radius:8px;padding:10px 18px;font-size:13px;font-weight:bold;}
.about-script-btn:hover{background:#f0f8f2;}
.demo-explore-btn{background:linear-gradient(135deg,#1565c0,#283593);color:white;border:none;border-radius:8px;padding:7px 14px;font-size:12px;font-weight:bold;}
.demo-explore-btn:hover{background:linear-gradient(135deg,#0d47a1,#1a237e);}
scrolledwindow{border:none;}
scrollbar{background:transparent;border:none;}
scrollbar slider{background:rgba(58,97,70,0.18);border-radius:6px;min-width:6px;min-height:6px;}
scrollbar slider:hover{background:rgba(58,97,70,0.35);}

.titlebar-button{border: #f5f3ed 1px solid;border-radius: 0;color:white;min-width: 36px;min-height: 32px;}
.titlebar-button:hover {background: alpha(white, 0.08);color: white;}
.titlebar-close{border: #f5f3ed 1px solid;border-radius: 0;color:white;min-width: 36px;min-height: 32px;}
.titlebar-close:hover {background: #e81123;color: white;}

.username-value {color: #6b6358;font-size: 14px;}
)CSS";

// ─── Forward declarations ─────────────────────────────────────────────────────
static void rebuild_apps_grid();
static void show_app_detail(AppInfo* app);
static void show_apps_list();

static std::vector<GtkWidget*> g_category_buttons;
struct CategoryButtonData { std::string category; GtkWidget *button; };

static void on_category_clicked(GtkButton *btn, gpointer ud) {
    CategoryButtonData *d=(CategoryButtonData*)ud;
    g_state.current_category=d->category;
    for(auto*b:g_category_buttons){GtkStyleContext*c=gtk_widget_get_style_context(b);gtk_style_context_remove_class(c,"active");}
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(btn)),"active");
    apply_filter(); show_apps_list();
}

// ─── Terminal dialog ──────────────────────────────────────────────────────────
struct InstallData {
    AppInfo app;
    GtkWidget *dialog,*text_view,*progress,*status_label,*close_btn;
    //GIOChannel *channel; GPid pid;
    bool success,finished,is_uninstall;
    InstallData():dialog(nullptr),text_view(nullptr),progress(nullptr),status_label(nullptr),
                  close_btn(nullptr),/*channel(nullptr),pid(0),*/success(false),finished(false),is_uninstall(false){}
};

struct DoneCtx { std::function<void(bool)> cb; InstallData *d; };
static void on_vte_child_exit(VteTerminal *term, gint status, gpointer user_data)
{
    InstallData *d =
        static_cast<InstallData*>(user_data);

    d->success = (status == 0);
    d->finished = true;

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(d->progress),
        1.0
    );

    gtk_label_set_text(
        GTK_LABEL(d->status_label),
        d->success ?
            (d->is_uninstall ?
                "✓ Gỡ cài đặt thành công!" :
                "✓ Cài đặt thành công!")
            :
            (d->is_uninstall ?
                "✗ Gỡ cài đặt thất bại!" :
                "✗ Cài đặt thất bại!")
    );

    gtk_widget_set_sensitive(
        d->close_btn,
        TRUE
    );
}
static void run_terminal_dialog(const std::string& title,const std::string& cmd,
                                 const AppInfo& app,bool is_uninstall,
                                 std::function<void(bool)> on_done) {
    InstallData *data=new InstallData(); data->app=app; data->is_uninstall=is_uninstall;
    GtkWidget *dlg=gtk_window_new(GTK_WINDOW_TOPLEVEL); data->dialog=dlg;
    gtk_window_set_title(GTK_WINDOW(dlg),title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(dlg),620,460);

    gtk_window_set_transient_for(GTK_WINDOW(dlg),GTK_WINDOW(g_state.window));
    gtk_window_set_modal(GTK_WINDOW(dlg),TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg),GTK_WIN_POS_CENTER_ON_PARENT);

    gtk_widget_show_all(dlg);
    GtkWidget *vbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_container_add(GTK_CONTAINER(dlg),vbox);

    // Header
    GtkWidget *hdr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_widget_set_margin_start(hdr,20);gtk_widget_set_margin_end(hdr,20);
    gtk_widget_set_margin_top(hdr,16);gtk_widget_set_margin_bottom(hdr,12);
    gtk_box_pack_start(GTK_BOX(vbox),hdr,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(hdr),make_app_image(app,48),FALSE,FALSE,0);
    GtkWidget *tvbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_box_pack_start(GTK_BOX(hdr),tvbox,TRUE,TRUE,0);
    GtkWidget *tlbl=gtk_label_new(title.c_str());
    gtk_widget_set_halign(tlbl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(tlbl),"install-dialog-title");
    gtk_box_pack_start(GTK_BOX(tvbox),tlbl,FALSE,FALSE,0);
    GtkWidget *slbl=gtk_label_new(("v"+app.version+" — "+app.author).c_str());
    gtk_widget_set_halign(slbl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(slbl),"app-author");
    gtk_box_pack_start(GTK_BOX(tvbox),slbl,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    // Terminal
    GtkWidget *scrolled=gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),GTK_POLICY_AUTOMATIC,GTK_POLICY_ALWAYS);
    gtk_widget_set_margin_start(scrolled,16);gtk_widget_set_margin_end(scrolled,16);
    gtk_widget_set_margin_top(scrolled,12);
    gtk_box_pack_start(GTK_BOX(vbox),scrolled,TRUE,TRUE,0);

    VteTerminal *term =VTE_TERMINAL(vte_terminal_new());
    gtk_container_add(GTK_CONTAINER(scrolled),GTK_WIDGET(term));

    GtkWidget *pb=gtk_progress_bar_new(); data->progress=pb;
    gtk_widget_set_margin_start(pb,16);gtk_widget_set_margin_end(pb,16);gtk_widget_set_margin_top(pb,8);
    gtk_style_context_add_class(gtk_widget_get_style_context(pb),"progress-bar");
    gtk_box_pack_start(GTK_BOX(vbox),pb,FALSE,FALSE,0);

    GtkWidget *stlbl=gtk_label_new("Đang chạy..."); data->status_label=stlbl;
    gtk_widget_set_margin_start(stlbl,16);gtk_widget_set_margin_end(stlbl,16);
    gtk_widget_set_margin_top(stlbl,6);gtk_widget_set_margin_bottom(stlbl,6);
    gtk_widget_set_halign(stlbl,GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox),stlbl,FALSE,FALSE,0);
    gtk_style_context_add_class(gtk_widget_get_style_context(stlbl),"status-bar");

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    GtkWidget *btnrow=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_margin_start(btnrow,16);gtk_widget_set_margin_end(btnrow,16);
    gtk_widget_set_margin_top(btnrow,8);gtk_widget_set_margin_bottom(btnrow,12);
    gtk_widget_set_halign(btnrow,GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(vbox),btnrow,FALSE,FALSE,0);

    GtkWidget *cbtn=gtk_button_new_with_label("Đóng"); data->close_btn=cbtn;
    gtk_widget_set_sensitive(cbtn,FALSE);
    DoneCtx *dctx=new DoneCtx{on_done,data};
    g_signal_connect_data(cbtn,"clicked",G_CALLBACK(+[](GtkButton*,gpointer ud){
        DoneCtx*dc=(DoneCtx*)ud; bool ok=dc->d->success;
        gtk_widget_destroy(dc->d->dialog); if(dc->cb)dc->cb(ok); delete dc;
    }),dctx,[](gpointer,GClosure*){},(GConnectFlags)0);
    gtk_box_pack_end(GTK_BOX(btnrow),cbtn,FALSE,FALSE,0);
    gtk_widget_show_all(dlg);


    char *argv[] = {
        (char*)"/bin/bash",
        (char*)"-c",
        (char*)cmd.c_str(),
        nullptr
    };

    vte_terminal_spawn_async(
        term,
        VTE_PTY_DEFAULT,
        nullptr,
        argv,
        nullptr,
        GSpawnFlags(0),
        nullptr,
        nullptr,
        nullptr,
        -1,
        nullptr,
        nullptr,
        nullptr
    );


    g_signal_connect(
        term,
        "child-exited",
        G_CALLBACK(on_vte_child_exit),
        data
    );
}
// ─── Sudo dialog ──────────────────────────────────────────────────────────────
static bool verify_sudo_password(const std::string& password) {
    std::string safe;
    for (char c : password) { if (c=='\'') safe+="'\\''"; else safe+=c; }
    return system(("echo '" + safe + "' | sudo -S -k true 2>/dev/null").c_str()) == 0;
}
struct SudoDialogData {
    GtkWidget *dialog,*entry,*ok_btn;
    GtkWidget *error_lbl, *spinner;
    std::string password; bool confirmed;
    std::function<void(const std::string&)> on_confirm;
};
struct VerifyResult { SudoDialogData *d; bool success; };

static gboolean on_verify_done(gpointer ud) {
    VerifyResult *res = (VerifyResult*)ud;
    SudoDialogData *d = res->d;
    bool ok = res->success;
    delete res;

    gtk_widget_set_sensitive(d->ok_btn, TRUE);
    gtk_widget_set_sensitive(d->entry, TRUE);
    gtk_spinner_stop(GTK_SPINNER(d->spinner));
    //gtk_widget_hide(d->spinner);

    if (ok) {
        d->confirmed = true;
        gtk_widget_destroy(d->dialog);
    } else {
        gtk_label_set_text(GTK_LABEL(d->error_lbl), "✗ Mật khẩu không đúng, thử lại");
        gtk_widget_show(d->error_lbl);
        gtk_entry_set_text(GTK_ENTRY(d->entry), "");
        gtk_widget_grab_focus(d->entry);
        GtkStyleContext *ctx = gtk_widget_get_style_context(d->entry);
        gtk_style_context_add_class(ctx, "sudo-entry-error");
        g_timeout_add(1500, +[](gpointer ud2) -> gboolean {
            GtkWidget *e = (GtkWidget*)ud2;
            if (GTK_IS_WIDGET(e))
                gtk_style_context_remove_class(
                    gtk_widget_get_style_context(e), "sudo-entry-error");
            return FALSE;
        }, d->entry);
    }
    return FALSE;
}

static gpointer verify_thread(gpointer ud) {
    SudoDialogData *d = (SudoDialogData*)ud;
    bool ok = verify_sudo_password(d->password);
    g_idle_add(on_verify_done, new VerifyResult{d, ok});
    return nullptr;
}
static void on_sudo_ok(GtkButton*, gpointer ud) {
    SudoDialogData *d = (SudoDialogData*)ud;
    std::string pwd = gtk_entry_get_text(GTK_ENTRY(d->entry));
    if (pwd.empty()) {
        gtk_label_set_text(GTK_LABEL(d->error_lbl), "✗ Vui lòng nhập mật khẩu");
        gtk_widget_show(d->error_lbl);
        return;
    }
    d->password = pwd;
    gtk_widget_set_sensitive(d->ok_btn, FALSE);
    gtk_widget_set_sensitive(d->entry, FALSE);
    gtk_label_set_text(GTK_LABEL(d->error_lbl), "Đang xác thực...");
    gtk_widget_show(d->error_lbl);
    gtk_widget_show(d->spinner);
    gtk_spinner_start(GTK_SPINNER(d->spinner));
    g_thread_new("sudo-verify", verify_thread, d);
}
static void on_sudo_cancel(GtkButton*,gpointer ud){((SudoDialogData*)ud)->confirmed=false;gtk_widget_destroy(((SudoDialogData*)ud)->dialog);}
static void on_sudo_activate(GtkEntry*,gpointer ud){gtk_button_clicked(GTK_BUTTON(((SudoDialogData*)ud)->ok_btn));}
static void on_sudo_destroy(GtkWidget*,gpointer ud){
    SudoDialogData*d=(SudoDialogData*)ud;
    if(d->confirmed&&!d->password.empty()&&d->on_confirm)d->on_confirm(d->password);
    delete d;
}
static void show_sudo_dialog(const std::string& app_name, std::function<void(const std::string&)> cb){
    SudoDialogData*d=new SudoDialogData(); d->confirmed=false; d->on_confirm=cb;
    GtkWidget*dlg=gtk_window_new(GTK_WINDOW_TOPLEVEL); d->dialog=dlg;
    gtk_window_set_title(GTK_WINDOW(dlg),"Xác nhận quyền quản trị");
    gtk_window_set_resizable(GTK_WINDOW(dlg),FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(dlg),GTK_WINDOW(g_state.window));
    gtk_window_set_modal(GTK_WINDOW(dlg),TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg),GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_default_size(GTK_WINDOW(dlg),400,-1);
    g_signal_connect(dlg,"destroy",G_CALLBACK(on_sudo_destroy),d);

    GtkWidget*vbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(dlg),vbox);
    GtkWidget*top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,16);
    gtk_widget_set_margin_start(top,24);gtk_widget_set_margin_end(top,24);
    gtk_widget_set_margin_top(top,24);gtk_widget_set_margin_bottom(top,16);
    gtk_box_pack_start(GTK_BOX(vbox),top,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(top),gtk_image_new_from_icon_name("dialog-password-symbolic",GTK_ICON_SIZE_DIALOG),FALSE,FALSE,0);
    GtkWidget*tv=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_valign(tv,GTK_ALIGN_CENTER);gtk_box_pack_start(GTK_BOX(top),tv,TRUE,TRUE,0);
    GtkWidget*tl=gtk_label_new(("Cài đặt "+app_name).c_str());gtk_widget_set_halign(tl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(tl),"sudo-dialog-title");gtk_box_pack_start(GTK_BOX(tv),tl,FALSE,FALSE,0);
    GtkWidget*sl=gtk_label_new("Yêu cầu quyền quản trị.\nNhập mật khẩu sudo để tiếp tục. \n--- Nhập đại đi, không có check đâu, nhưng phải nhập cho đủ quy trình :3 ---");
    gtk_label_set_line_wrap(GTK_LABEL(sl),TRUE);gtk_widget_set_halign(sl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sl),"sudo-dialog-subtitle");gtk_box_pack_start(GTK_BOX(tv),sl,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    GtkWidget*pa=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_widget_set_margin_start(pa,24);gtk_widget_set_margin_end(pa,24);
    gtk_widget_set_margin_top(pa,16);gtk_widget_set_margin_bottom(pa,8);
    gtk_box_pack_start(GTK_BOX(vbox),pa,FALSE,FALSE,0);

    // username row
    GtkWidget*ur=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_pack_start(GTK_BOX(pa),ur,FALSE,FALSE,0);
    GtkWidget*ul=gtk_label_new("Người dùng:");gtk_widget_set_size_request(ul,100,-1);gtk_widget_set_halign(ul,GTK_ALIGN_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(ul),"sudo-dialog-subtitle");gtk_box_pack_start(GTK_BOX(ur),ul,FALSE,FALSE,0);
    GtkWidget *username = gtk_label_new(g_get_user_name());
    gtk_style_context_add_class(gtk_widget_get_style_context(username),"username-value");
    gtk_box_pack_start(GTK_BOX(ur), username, FALSE, FALSE, 0);
    // password row
    GtkWidget*pr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_pack_start(GTK_BOX(pa),pr,FALSE,FALSE,0);
    GtkWidget*pl=gtk_label_new("Mật khẩu:");gtk_widget_set_size_request(pl,100,-1);gtk_widget_set_halign(pl,GTK_ALIGN_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(pl),"sudo-dialog-subtitle");gtk_box_pack_start(GTK_BOX(pr),pl,FALSE,FALSE,0);
    GtkWidget*entry=gtk_entry_new();d->entry=entry;
    gtk_entry_set_visibility(GTK_ENTRY(entry),FALSE);gtk_entry_set_invisible_char(GTK_ENTRY(entry),0x25CF);
    gtk_entry_set_input_purpose(GTK_ENTRY(entry),GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"Nhập mật khẩu...");
    gtk_style_context_add_class(gtk_widget_get_style_context(entry),"sudo-password-entry");
    gtk_box_pack_start(GTK_BOX(pr),entry,TRUE,TRUE,0);
    g_signal_connect(entry,"activate",G_CALLBACK(on_sudo_activate),d);

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    GtkWidget*br=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_margin_start(br,16);gtk_widget_set_margin_end(br,16);
    gtk_widget_set_margin_top(br,10);gtk_widget_set_margin_bottom(br,14);
    gtk_widget_set_halign(br,GTK_ALIGN_END);gtk_box_pack_start(GTK_BOX(vbox),br,FALSE,FALSE,0);
    GtkWidget*cancel_btn=gtk_button_new_with_label("Hủy");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn),"sudo-cancel-btn");
    g_signal_connect(cancel_btn,"clicked",G_CALLBACK(on_sudo_cancel),d);gtk_box_pack_start(GTK_BOX(br),cancel_btn,FALSE,FALSE,0);
    GtkWidget*ob=gtk_button_new_with_label("Xác nhận");d->ok_btn=ob;
    gtk_style_context_add_class(gtk_widget_get_style_context(ob),"sudo-ok-btn");
    g_signal_connect(ob,"clicked",G_CALLBACK(on_sudo_ok),d);gtk_box_pack_start(GTK_BOX(br),ob,FALSE,FALSE,0);
    gtk_widget_show_all(dlg);
    //gtk_widget_hide(d->spinner);
    //gtk_widget_hide(d->error_lbl);
    gtk_widget_grab_focus(entry);

    // Error/status row
    GtkWidget *err_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(err_row, 114);
    gtk_widget_set_margin_top(err_row, 2);
    gtk_box_pack_start(GTK_BOX(pa), err_row, FALSE, FALSE, 0);

    GtkWidget *spinner = gtk_spinner_new(); d->spinner = spinner;
    gtk_widget_set_size_request(spinner, 16, 16);
    gtk_box_pack_start(GTK_BOX(err_row), spinner, FALSE, FALSE, 0);

    GtkWidget *err_lbl = gtk_label_new(""); d->error_lbl = err_lbl;
    gtk_widget_set_halign(err_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(err_lbl), "sudo-error-label");
    gtk_box_pack_start(GTK_BOX(err_row), err_lbl, FALSE, FALSE, 0);
}

// ─── Build install command ────────────────────────────────────────────────────
static std::string build_cmd(const InstallMethod& m, const std::string& scripts_dir,
                              const std::string& password, bool uninstall=false) {
    std::string safe_pwd;
    for(char c:password){if(c=='\'')safe_pwd+="'\\''";else safe_pwd+=c;}
    std::string sudo_prefix=(m.needs_sudo&&!password.empty())?"echo '"+safe_pwd+"' | sudo -S ":"";
    if (!m.script.empty()) return sudo_prefix+"bash \""+scripts_dir+"/"+m.script+"\" 2>&1";
    std::string pkg=m.package.empty()?"":m.package;
    if(m.pm=="apt"||m.pm=="apt-get"){
        return uninstall?sudo_prefix+"apt-get remove -y \""+pkg+"\" && sudo apt-get autoremove -y 2>&1"
                        :sudo_prefix+"apt-get update &&"
                        +sudo_prefix+" apt-get install -y \""+pkg+"\" 2>&1";
    }
    if(m.pm=="dnf"||m.pm=="yum"){
        return uninstall?sudo_prefix+m.pm+" remove -y \""+pkg+"\" 2>&1"
                        :sudo_prefix+m.pm+" install -y \""+pkg+"\" 2>&1";
    }
    if(m.pm=="pacman"){
        return uninstall?sudo_prefix+"pacman -Rns --noconfirm \""+pkg+"\" 2>&1"
                        :sudo_prefix+"pacman -S --noconfirm \""+pkg+"\" 2>&1";
    }
    if(m.pm=="yay"||m.pm=="paru"){
        return uninstall?m.pm+" -Rns --noconfirm \""+pkg+"\" 2>&1"
                        :m.pm+" -S --noconfirm \""+pkg+"\" 2>&1";
    }
    if(m.pm=="zypper"){
        return uninstall?sudo_prefix+"zypper remove -y \""+pkg+"\" 2>&1"
                        :sudo_prefix+"zypper install -y \""+pkg+"\" 2>&1";
    }
    if(m.pm=="flatpak"){
        return uninstall?"flatpak uninstall -y --delete-data \""+pkg+"\" && flatpak uninstall --unused -y 2>&1"
                        :"flatpak install -y flathub \""+pkg+"\" 2>&1";
    }
    if(m.pm=="snap"){
        return uninstall?sudo_prefix+"snap remove \""+pkg+"\" 2>&1"
                        :sudo_prefix+"snap install \""+pkg+"\" 2>&1";
    }
    if(m.pm=="brew")  return uninstall?"brew uninstall \""+pkg+"\" 2>&1":"brew install \""+pkg+"\" 2>&1";
    if(m.pm=="nix-env") return uninstall?"nix-env -e \""+pkg+"\" 2>&1":"nix-env -iA nixpkgs.\""+pkg+"\" 2>&1";
    if(m.pm=="pip"||m.pm=="pip3") return uninstall?m.pm+" uninstall -y \""+pkg+"\" 2>&1":m.pm+" install \""+pkg+"\" 2>&1";
    if(m.pm=="cargo") return uninstall?"cargo uninstall \""+pkg+"\" 2>&1":"cargo install \""+pkg+"\" 2>&1";
    if(m.pm=="npm")   return uninstall?sudo_prefix+"npm uninstall -g \""+pkg+"\" 2>&1":sudo_prefix+"npm install -g \""+pkg+"\" 2>&1";
    return sudo_prefix+"bash \""+scripts_dir+"/install-generic.sh\" \""+pkg+"\" 2>&1";
}

// ─── Install flow ─────────────────────────────────────────────────────────────
static void execute_install(const AppInfo& app, const InstallMethod& m, const std::string& pwd) {
    std::string cmd=build_cmd(m,g_state.scripts_dir,pwd,false);
    run_terminal_dialog("Cài đặt "+app.name+" ("+pm_display(m.pm)+")",cmd,app,false,
        [app,m](bool ok){
            if(ok){
                InstalledEntry e; e.app_id=app.id; e.pm=m.pm; e.package=m.package; e.version=app.version;
                add_to_log(e);
                for(auto&a:g_state.all_apps)if(a.id==app.id){a.installed=true;a.installed_via=m.pm;break;}
                apply_filter();
            }
        });
}

static void ask_flatpak_scope(const AppInfo& app, InstallMethod m) {
    GtkWidget*dlg=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dlg),"Phạm vi Flatpak");
    gtk_window_set_resizable(GTK_WINDOW(dlg),FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(dlg),GTK_WINDOW(g_state.window));
    gtk_window_set_modal(GTK_WINDOW(dlg),TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg),GTK_WIN_POS_CENTER_ON_PARENT);
    GtkWidget*vbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
    gtk_widget_set_margin_start(vbox,24);gtk_widget_set_margin_end(vbox,24);
    gtk_widget_set_margin_top(vbox,20);gtk_widget_set_margin_bottom(vbox,20);
    gtk_container_add(GTK_CONTAINER(dlg),vbox);
    GtkWidget*tl=gtk_label_new("Chọn phạm vi cài đặt Flatpak:");
    gtk_widget_set_halign(tl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(tl),"sudo-dialog-title");
    gtk_box_pack_start(GTK_BOX(vbox),tl,FALSE,FALSE,0);
    GtkWidget*sl=gtk_label_new("User: chỉ tài khoản hiện tại, không cần sudo.\nSystem: toàn máy, cần sudo.");
    gtk_label_set_line_wrap(GTK_LABEL(sl),TRUE);gtk_widget_set_halign(sl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sl),"sudo-dialog-subtitle");
    gtk_box_pack_start(GTK_BOX(vbox),sl,FALSE,FALSE,0);
    GtkWidget*br=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_halign(br,GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(vbox),br,FALSE,FALSE,0);

    struct FpCtx{AppInfo app;InstallMethod m;GtkWidget*dlg;};
    GtkWidget*cb=gtk_button_new_with_label("Hủy");
    gtk_style_context_add_class(gtk_widget_get_style_context(cb),"sudo-cancel-btn");
    g_signal_connect_swapped(cb,"clicked",G_CALLBACK(gtk_widget_destroy),dlg);
    gtk_box_pack_start(GTK_BOX(br),cb,FALSE,FALSE,0);

    FpCtx*ctx1=new FpCtx{app,m,dlg};
    GtkWidget*ub=gtk_button_new_with_label("User (không cần sudo)");
    gtk_style_context_add_class(gtk_widget_get_style_context(ub),"sudo-cancel-btn");
    g_signal_connect_data(ub,"clicked",G_CALLBACK(+[](GtkButton*,gpointer ud){
        FpCtx*c=(FpCtx*)ud;AppInfo ap=c->app;InstallMethod mm=c->m;
        gtk_widget_destroy(c->dlg);
        std::string cmd="flatpak install -y --user flathub \""+mm.package+"\" 2>&1";
        run_terminal_dialog("Flatpak user: "+ap.name,cmd,ap,false,[ap,mm](bool ok){
            if(ok){InstalledEntry e;e.app_id=ap.id;e.pm="flatpak";e.package=mm.package;e.version=ap.version;add_to_log(e);
                   for(auto&a:g_state.all_apps)if(a.id==ap.id){a.installed=true;a.installed_via="flatpak";break;}apply_filter();}
        });
    }),ctx1,[](gpointer p,GClosure*){delete(FpCtx*)p;},(GConnectFlags)0);
    gtk_box_pack_start(GTK_BOX(br),ub,FALSE,FALSE,0);

    FpCtx*ctx2=new FpCtx{app,m,dlg};
    GtkWidget*sb=gtk_button_new_with_label("System (cần sudo)");
    gtk_style_context_add_class(gtk_widget_get_style_context(sb),"sudo-ok-btn");
    g_signal_connect_data(sb,"clicked",G_CALLBACK(+[](GtkButton*,gpointer ud){
        FpCtx*c=(FpCtx*)ud;AppInfo ap=c->app;InstallMethod mm=c->m;
        gtk_widget_destroy(c->dlg);
        show_sudo_dialog(ap.name,[ap,mm](const std::string&pwd){
            std::string safe;for(char ch:pwd){if(ch=='\'')safe+="'\\''";else safe+=ch;}
            std::string cmd="echo '"+safe+"' | sudo -S flatpak install -y flathub \""+mm.package+"\" 2>&1";
            run_terminal_dialog("Flatpak system: "+ap.name,cmd,ap,false,[ap,mm](bool ok){
                if(ok){InstalledEntry e;e.app_id=ap.id;e.pm="flatpak";e.package=mm.package;e.version=ap.version;add_to_log(e);
                       for(auto&a:g_state.all_apps)if(a.id==ap.id){a.installed=true;a.installed_via="flatpak";break;}apply_filter();}
            });
        });
    }),ctx2,[](gpointer p,GClosure*){delete(FpCtx*)p;},(GConnectFlags)0);
    gtk_box_pack_start(GTK_BOX(br),sb,FALSE,FALSE,0);
    gtk_widget_show_all(dlg);
}

static void show_method_chooser(AppInfo& app, bool for_uninstall=false) {
    auto& methods=app.install_methods;
    std::vector<InstallMethod*> avail;
    for(auto&m:methods) if(m.unlocked||m.pm=="script") avail.push_back(&m);

    if (for_uninstall) {
        // find installed method from log
        InstalledEntry*e=find_log_entry(app.id);
        std::string pm=e?e->pm:"unknown";
        InstallMethod *um=nullptr;
        for(auto&m:methods)if(m.pm==pm){um=&m;break;}
        if(!um&&!avail.empty())um=avail[0];
        auto do_uninstall=[&](const InstallMethod& m){
            if(m.needs_sudo){
                show_sudo_dialog(app.name,[app,m](const std::string&pwd){
                    std::string cmd=build_cmd(m,g_state.scripts_dir,pwd,true);
                    run_terminal_dialog("Gỡ cài đặt "+app.name,cmd,app,true,[app](bool ok){
                        if(ok){remove_from_log(app.id);for(auto&a:g_state.all_apps)if(a.id==app.id){a.installed=false;a.installed_via="";}apply_filter();}
                    });
                });
            } else {
                std::string cmd=build_cmd(m,g_state.scripts_dir,"",true);
                run_terminal_dialog("Gỡ cài đặt "+app.name,cmd,app,true,[app](bool ok){
                    if(ok){remove_from_log(app.id);for(auto&a:g_state.all_apps)if(a.id==app.id){a.installed=false;a.installed_via="";}apply_filter();}
                });
            }
        };
        if(um){do_uninstall(*um);}else{
            InstallMethod fb;fb.pm=pm;fb.package=e?e->package:app.id;
            fb.needs_sudo=pm_needs_sudo(pm);fb.sudo_override=false;fb.unlocked=true;fb.script="";
            do_uninstall(fb);
        }
        return;
    }

    // Install path
    if (avail.empty()) {
        // No compatible PM — warn dialog
        GtkWidget*dlg=gtk_message_dialog_new(GTK_WINDOW(g_state.window),GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,GTK_BUTTONS_NONE,
            "Không tìm thấy package manager phù hợp trên máy này.\n\nTiếp tục chỉ chạy script cài đặt mặc định.");
        gtk_dialog_add_button(GTK_DIALOG(dlg),"Hủy",GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dlg),"Tiếp tục với script",GTK_RESPONSE_OK);
        gtk_dialog_set_default_response(GTK_DIALOG(dlg),GTK_RESPONSE_CANCEL);
        int resp=gtk_dialog_run(GTK_DIALOG(dlg)); gtk_widget_destroy(dlg);
        if(resp!=GTK_RESPONSE_OK) return;
        InstallMethod fb;fb.pm="script";fb.script=app.script;fb.package=app.id;
        fb.needs_sudo=true;fb.sudo_override=false;fb.unlocked=true;
        show_sudo_dialog(app.name,[app,fb](const std::string&pwd){execute_install(app,fb,pwd);});
        return;
    }

    if (avail.size()==1) {
        InstallMethod*m=avail[0];
        if(m->pm=="flatpak"){ask_flatpak_scope(app,*m);return;}
        InstallMethod m_copy=*m;
        if(m_copy.needs_sudo) show_sudo_dialog(app.name,[app,m_copy](const std::string&pwd){execute_install(app,m_copy,pwd);});
        else execute_install(app,m_copy,"");
        return;
    }

    // Multiple methods chooser
    GtkWidget*dlg=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dlg),"Chọn phương pháp cài đặt");
    gtk_window_set_resizable(GTK_WINDOW(dlg),FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(dlg),GTK_WINDOW(g_state.window));
    gtk_window_set_modal(GTK_WINDOW(dlg),TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg),GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_default_size(GTK_WINDOW(dlg),440,-1);
    GtkWidget*vbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(dlg),vbox);

    // Header
    GtkWidget*hdr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
    gtk_widget_set_margin_start(hdr,20);gtk_widget_set_margin_end(hdr,20);
    gtk_widget_set_margin_top(hdr,16);gtk_widget_set_margin_bottom(hdr,12);
    gtk_box_pack_start(GTK_BOX(vbox),hdr,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(hdr),make_app_image(app,48),FALSE,FALSE,0);
    GtkWidget*htb=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_box_pack_start(GTK_BOX(hdr),htb,TRUE,TRUE,0);
    GtkWidget*htl=gtk_label_new(("Cài đặt "+app.name).c_str());
    gtk_widget_set_halign(htl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(htl),"install-dialog-title");
    gtk_box_pack_start(GTK_BOX(htb),htl,FALSE,FALSE,0);
    GtkWidget*hsl=gtk_label_new("Chọn phương pháp cài đặt:");
    gtk_widget_set_halign(hsl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(hsl),"sudo-dialog-subtitle");
    gtk_box_pack_start(GTK_BOX(htb),hsl,FALSE,FALSE,0);

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    GtkWidget*mbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_widget_set_margin_start(mbox,16);gtk_widget_set_margin_end(mbox,16);
    gtk_widget_set_margin_top(mbox,12);gtk_widget_set_margin_bottom(mbox,12);
    gtk_box_pack_start(GTK_BOX(vbox),mbox,FALSE,FALSE,0);

    struct MCtx{AppInfo app;InstallMethod m;GtkWidget*dlg;};
    for(auto*m:avail){
        GtkWidget*mbtn=gtk_button_new();
        GtkWidget*inner=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
        gtk_widget_set_margin_start(inner,4);gtk_widget_set_margin_end(inner,4);
        GtkWidget*badge=gtk_label_new(m->pm.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(badge),"pm-badge");
        GtkWidget*plbl=gtk_label_new(m->package.empty()?app.id.c_str():m->package.c_str());
        gtk_widget_set_halign(plbl,GTK_ALIGN_START);
        GtkWidget*slbl2=gtk_label_new(m->needs_sudo?" (sudo)":"");
        gtk_style_context_add_class(gtk_widget_get_style_context(slbl2),"app-version");
        gtk_box_pack_start(GTK_BOX(inner),badge,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(inner),plbl,TRUE,TRUE,0);
        gtk_box_pack_end(GTK_BOX(inner),slbl2,FALSE,FALSE,0);
        gtk_container_add(GTK_CONTAINER(mbtn),inner);
        gtk_style_context_add_class(gtk_widget_get_style_context(mbtn),"method-btn");
        MCtx*mctx=new MCtx{app,*m,dlg};
        g_signal_connect_data(mbtn,"clicked",G_CALLBACK(+[](GtkButton*,gpointer ud){
            MCtx*c=(MCtx*)ud; AppInfo ap=c->app; InstallMethod mm=c->m;
            gtk_widget_destroy(c->dlg);
            if(mm.pm=="flatpak"){ask_flatpak_scope(ap,mm);return;}
            if(mm.needs_sudo) show_sudo_dialog(ap.name,[ap,mm](const std::string&pwd){execute_install(ap,mm,pwd);});
            else execute_install(ap,mm,"");
        }),mctx,[](gpointer p,GClosure*){delete(MCtx*)p;},(GConnectFlags)0);
        gtk_box_pack_start(GTK_BOX(mbox),mbtn,FALSE,FALSE,0);
    }

    // Show locked methods
    bool has_locked=false;
    for(auto&m:methods)if(!m.unlocked&&m.pm!="script"){has_locked=true;break;}
    if(has_locked){
        GtkWidget*sep2=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(sep2,4);gtk_widget_set_margin_bottom(sep2,4);
        gtk_box_pack_start(GTK_BOX(mbox),sep2,FALSE,FALSE,0);
        GtkWidget*ll=gtk_label_new("Không khả dụng trên máy này:");
        gtk_widget_set_halign(ll,GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(ll),"sidebar-title");
        gtk_box_pack_start(GTK_BOX(mbox),ll,FALSE,FALSE,0);
        for(auto&m:methods){
            if(m.unlocked||m.pm=="script")continue;
            GtkWidget*mb2=gtk_button_new();
            GtkWidget*in2=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
            GtkWidget*b2=gtk_label_new(m.pm.c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(b2),"pm-badge-locked");
            GtkWidget*p2=gtk_label_new(m.package.c_str());gtk_widget_set_halign(p2,GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(in2),b2,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(in2),p2,TRUE,TRUE,0);
            gtk_container_add(GTK_CONTAINER(mb2),in2);
            gtk_style_context_add_class(gtk_widget_get_style_context(mb2),"method-btn-locked");
            gtk_widget_set_sensitive(mb2,FALSE);gtk_box_pack_start(GTK_BOX(mbox),mb2,FALSE,FALSE,0);
        }
    }

    gtk_box_pack_start(GTK_BOX(vbox),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);
    GtkWidget*cr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_set_halign(cr,GTK_ALIGN_END);
    gtk_widget_set_margin_start(cr,16);gtk_widget_set_margin_end(cr,16);
    gtk_widget_set_margin_top(cr,8);gtk_widget_set_margin_bottom(cr,12);
    gtk_box_pack_start(GTK_BOX(vbox),cr,FALSE,FALSE,0);
    GtkWidget*cancbtn=gtk_button_new_with_label("Hủy");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancbtn),"sudo-cancel-btn");
    g_signal_connect_swapped(cancbtn,"clicked",G_CALLBACK(gtk_widget_destroy),dlg);
    gtk_box_pack_start(GTK_BOX(cr),cancbtn,FALSE,FALSE,0);
    gtk_widget_show_all(dlg);
}

static void on_install_clicked(GtkButton*,gpointer ud){
    AppInfo*app=(AppInfo*)ud; if(!app)return; show_method_chooser(*app,false);
}

static void on_uninstall_clicked(GtkButton*,gpointer ud){
    AppInfo*app=(AppInfo*)ud; if(!app)return;
    GtkWidget*confirm=gtk_message_dialog_new(GTK_WINDOW(g_state.window),GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"Gỡ cài đặt %s?",app->name.c_str());
    gtk_dialog_add_button(GTK_DIALOG(confirm),"Hủy",GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(confirm),"Gỡ cài đặt",GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(confirm),GTK_RESPONSE_CANCEL);
    int resp=gtk_dialog_run(GTK_DIALOG(confirm)); gtk_widget_destroy(confirm);
    if(resp==GTK_RESPONSE_OK) show_method_chooser(*app,true);
}
static void on_back_clicked(GtkButton*,gpointer){show_apps_list();}
// ─── Cards & Grid ─────────────────────────────────────────────────────────────
struct CardClickData{AppInfo*app;};
static gboolean on_card_press(GtkWidget*,GdkEventButton*ev,gpointer ud){
    if(ev->button==1&&ev->type==GDK_BUTTON_PRESS){show_app_detail(((CardClickData*)ud)->app);return TRUE;}return FALSE;
}
static gboolean on_card_enter(GtkWidget*w,GdkEventCrossing*,gpointer){
    gdk_window_set_cursor(gtk_widget_get_window(w),gdk_cursor_new_for_display(gdk_display_get_default(),GDK_HAND2));return FALSE;
}
static gboolean on_card_leave(GtkWidget*w,GdkEventCrossing*,gpointer){gdk_window_set_cursor(gtk_widget_get_window(w),nullptr);return FALSE;}

static GtkWidget* create_app_card(AppInfo *app){
    GtkWidget*card=gtk_event_box_new();
    gtk_widget_add_events(card,GDK_BUTTON_PRESS_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK);
    GtkWidget*inner=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_style_context_add_class(gtk_widget_get_style_context(inner),"app-card");
    if(app->installed) gtk_style_context_add_class(gtk_widget_get_style_context(inner),"app-card-installed");
    gtk_container_add(GTK_CONTAINER(card),inner);
    // top row
    GtkWidget*top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_pack_start(GTK_BOX(inner),top,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(top),make_app_image(*app,48),FALSE,FALSE,0);
    GtkWidget*nbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_valign(nbox,GTK_ALIGN_CENTER);gtk_box_pack_start(GTK_BOX(top),nbox,TRUE,TRUE,0);
    GtkWidget*nl=gtk_label_new(app->name.c_str());gtk_widget_set_halign(nl,GTK_ALIGN_START);gtk_label_set_ellipsize(GTK_LABEL(nl),PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(nl),"app-name");gtk_box_pack_start(GTK_BOX(nbox),nl,FALSE,FALSE,0);
    GtkWidget*al=gtk_label_new(app->author.c_str());gtk_widget_set_halign(al,GTK_ALIGN_START);gtk_label_set_ellipsize(GTK_LABEL(al),PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(al),"app-author");gtk_box_pack_start(GTK_BOX(nbox),al,FALSE,FALSE,0);
    // summary
    GtkWidget*sl=gtk_label_new(app->summary.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(sl),TRUE);gtk_label_set_lines(GTK_LABEL(sl),2);gtk_label_set_ellipsize(GTK_LABEL(sl),PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(sl,GTK_ALIGN_START);gtk_style_context_add_class(gtk_widget_get_style_context(sl),"app-summary");
    gtk_box_pack_start(GTK_BOX(inner),sl,FALSE,FALSE,0);
    // bottom row
    GtkWidget*bot=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_top(bot,4);gtk_box_pack_start(GTK_BOX(inner),bot,FALSE,FALSE,0);
    std::string cat_text=CATEGORY_LABELS.count(app->category)?CATEGORY_LABELS[app->category]:app->category;
    GtkWidget*cb=gtk_label_new(cat_text.c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(cb),"cat-badge");
    gtk_style_context_add_class(gtk_widget_get_style_context(cb),("cat-"+app->category).c_str());
    gtk_box_pack_start(GTK_BOX(bot),cb,FALSE,FALSE,0);
    if(app->installed){
        std::string ibtext="✓ Đã cài"+(app->installed_via.empty()?"":" ("+app->installed_via+")");
        GtkWidget*ib=gtk_label_new(ibtext.c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(ib),"installed-badge");
        gtk_box_pack_start(GTK_BOX(bot),ib,FALSE,FALSE,0);
    }
    GtkWidget*vl=gtk_label_new(("v"+app->version).c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(vl),"app-version");
    gtk_box_pack_end(GTK_BOX(bot),vl,FALSE,FALSE,0);
    CardClickData*cd=new CardClickData{app};
    g_signal_connect(card,"button-press-event",G_CALLBACK(on_card_press),cd);
    g_signal_connect(card,"enter-notify-event",G_CALLBACK(on_card_enter),nullptr);
    g_signal_connect(card,"leave-notify-event",G_CALLBACK(on_card_leave),nullptr);
    return card;
}

static GtkWidget* create_demo_card(){
    GtkWidget*card=gtk_event_box_new();
    gtk_widget_add_events(card,GDK_BUTTON_PRESS_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK);
    GtkWidget*inner=gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
    gtk_style_context_add_class(gtk_widget_get_style_context(inner),"app-card");
    gtk_container_add(GTK_CONTAINER(card),inner);
    GtkWidget*top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_box_pack_start(GTK_BOX(inner),top,FALSE,FALSE,0);
    GtkWidget*ico=gtk_image_new_from_icon_name("starred-symbolic",GTK_ICON_SIZE_INVALID);
    gtk_image_set_pixel_size(GTK_IMAGE(ico),48);gtk_box_pack_start(GTK_BOX(top),ico,FALSE,FALSE,0);
    GtkWidget*nbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_valign(nbox,GTK_ALIGN_CENTER);gtk_box_pack_start(GTK_BOX(top),nbox,TRUE,TRUE,0);
    GtkWidget*nl=gtk_label_new("Khám phá thêm");gtk_widget_set_halign(nl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(nl),"app-name");gtk_box_pack_start(GTK_BOX(nbox),nl,FALSE,FALSE,0);
    GtkWidget*al=gtk_label_new("VNLF Community");gtk_widget_set_halign(al,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(al),"app-author");gtk_box_pack_start(GTK_BOX(nbox),al,FALSE,FALSE,0);
    GtkWidget*sl=gtk_label_new("Khám phá thêm các ứng dụng và tài nguyên từ cộng đồng VNLF.");
    gtk_label_set_line_wrap(GTK_LABEL(sl),TRUE);gtk_label_set_lines(GTK_LABEL(sl),2);gtk_widget_set_halign(sl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sl),"app-summary");gtk_box_pack_start(GTK_BOX(inner),sl,FALSE,FALSE,0);
    GtkWidget*bot=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_top(bot,4);gtk_box_pack_start(GTK_BOX(inner),bot,FALSE,FALSE,0);
    GtkWidget*eb=gtk_button_new_with_label("⚡ Khám phá thêm");
    gtk_style_context_add_class(gtk_widget_get_style_context(eb),"demo-explore-btn");
    gtk_widget_set_tooltip_text(eb,"Chạy lệnh khám phá (thêm lệnh vào g_state.about_script)");
    g_signal_connect(eb,"clicked",G_CALLBACK(+[](GtkButton*,gpointer){
        std::string cmd=g_state.about_script.empty()
            ?"/usr/local/bin/Store-x86_64.AppImage &"
            :"bash \""+g_state.about_script+"\" &";
        system(cmd.c_str());
    }),nullptr);
    gtk_box_pack_start(GTK_BOX(bot),eb,FALSE,FALSE,0);
    g_signal_connect(card,"enter-notify-event",G_CALLBACK(on_card_enter),nullptr);
    g_signal_connect(card,"leave-notify-event",G_CALLBACK(on_card_leave),nullptr);
    return card;
}

static void rebuild_apps_grid(){
    GList*ch=gtk_container_get_children(GTK_CONTAINER(g_state.apps_grid));
    for(GList*l=ch;l;l=l->next)gtk_widget_destroy(GTK_WIDGET(l->data));g_list_free(ch);
    bool show_all=(g_state.current_category.empty()&&g_state.search_query.empty());
    if(g_state.filtered_apps.empty()){
        GtkWidget*eb=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);
        gtk_widget_set_valign(eb,GTK_ALIGN_CENTER);gtk_widget_set_halign(eb,GTK_ALIGN_CENTER);gtk_widget_set_margin_top(eb,80);
        GtkWidget*ei=gtk_image_new_from_icon_name("system-search-symbolic",GTK_ICON_SIZE_DIALOG);
        gtk_style_context_add_class(gtk_widget_get_style_context(ei),"empty-state-icon");gtk_box_pack_start(GTK_BOX(eb),ei,FALSE,FALSE,0);
        GtkWidget*el=gtk_label_new("Không tìm thấy ứng dụng phù hợp");
        gtk_style_context_add_class(gtk_widget_get_style_context(el),"empty-state");gtk_box_pack_start(GTK_BOX(eb),el,FALSE,FALSE,0);
        gtk_grid_attach(GTK_GRID(g_state.apps_grid),eb,0,0,3,1);
        gtk_widget_show_all(g_state.apps_grid); return;
    }
    int col=0,row=0; const int COLS=3;
    for(size_t i=0;i<g_state.filtered_apps.size();i++){
        GtkWidget*card=create_app_card(&g_state.filtered_apps[i]);
        gtk_widget_set_size_request(card,300,150);
        gtk_grid_attach(GTK_GRID(g_state.apps_grid),card,col,row,1,1);
        col++;if(col>=COLS){col=0;row++;}
    }
    if(show_all){
        GtkWidget*demo=create_demo_card();gtk_widget_set_size_request(demo,300,150);
        gtk_grid_attach(GTK_GRID(g_state.apps_grid),demo,col,row,1,1);
    }
    gtk_label_set_text(GTK_LABEL(g_state.count_label),(std::to_string(g_state.filtered_apps.size())+" ứng dụng - hãy lựa chọn sáng suốt - Thần đèn (không có nói như vậy)").c_str());
    gtk_widget_show_all(g_state.apps_grid);
}
// ─── Detail view ──────────────────────────────────────────────────────────────
static void show_app_detail(AppInfo *app){
    if(!app)return; g_state.current_detail_app=app;
    GList*ch=gtk_container_get_children(GTK_CONTAINER(g_state.detail_box));
    for(GList*l=ch;l;l=l->next)gtk_widget_destroy(GTK_WIDGET(l->data));g_list_free(ch);

    // Back row
    GtkWidget*brow=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    gtk_widget_set_margin_start(brow,12);gtk_widget_set_margin_top(brow,10);gtk_widget_set_margin_bottom(brow,2);
    gtk_box_pack_start(GTK_BOX(g_state.detail_box),brow,FALSE,FALSE,0);
    GtkWidget*bbtn=gtk_button_new_with_label("← Quay lại");
    gtk_style_context_add_class(gtk_widget_get_style_context(bbtn),"back-btn");
    g_signal_connect(bbtn,"clicked",G_CALLBACK(on_back_clicked),nullptr);
    gtk_box_pack_start(GTK_BOX(brow),bbtn,FALSE,FALSE,0);

    // Header
    GtkWidget*hdr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,20);
    gtk_style_context_add_class(gtk_widget_get_style_context(hdr),"detail-header");
    gtk_widget_set_margin_start(hdr,24);gtk_widget_set_margin_end(hdr,24);
    gtk_widget_set_margin_top(hdr,16);gtk_widget_set_margin_bottom(hdr,24);
    gtk_box_pack_start(GTK_BOX(g_state.detail_box),hdr,FALSE,FALSE,0);

    GtkWidget*ico=make_app_image(*app,96);gtk_widget_set_valign(ico,GTK_ALIGN_CENTER);gtk_box_pack_start(GTK_BOX(hdr),ico,FALSE,FALSE,0);

    GtkWidget*ivbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_valign(ivbox,GTK_ALIGN_CENTER);gtk_box_pack_start(GTK_BOX(hdr),ivbox,TRUE,TRUE,0);
    GtkWidget*nl=gtk_label_new(app->name.c_str());gtk_widget_set_halign(nl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(nl),"detail-name");gtk_box_pack_start(GTK_BOX(ivbox),nl,FALSE,FALSE,0);
    GtkWidget*sl=gtk_label_new(app->summary.c_str());gtk_label_set_line_wrap(GTK_LABEL(sl),TRUE);gtk_widget_set_halign(sl,GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(sl),"detail-summary");gtk_box_pack_start(GTK_BOX(ivbox),sl,FALSE,FALSE,0);

    // Meta row
    GtkWidget*mr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,16);gtk_widget_set_margin_top(mr,4);gtk_box_pack_start(GTK_BOX(ivbox),mr,FALSE,FALSE,0);
    auto add_m=[&](const std::string&t,const char*css){GtkWidget*l=gtk_label_new(t.c_str());gtk_style_context_add_class(gtk_widget_get_style_context(l),css);gtk_box_pack_start(GTK_BOX(mr),l,FALSE,FALSE,0);};
    add_m("v"+app->version,"app-version");add_m("•","app-version");add_m(app->author,"app-author");add_m("•","app-version");add_m(app->license,"app-version");
    if(app->installed){add_m("•","app-version");GtkWidget*ib=gtk_label_new(("✓ Đã cài"+(app->installed_via.empty()?"":" via "+app->installed_via)).c_str());gtk_style_context_add_class(gtk_widget_get_style_context(ib),"installed-badge");gtk_box_pack_start(GTK_BOX(mr),ib,FALSE,FALSE,0);}

    // PM badges row
    if(!app->install_methods.empty()){
        GtkWidget*pmr=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_widget_set_margin_top(pmr,4);gtk_box_pack_start(GTK_BOX(ivbox),pmr,FALSE,FALSE,0);
        for(auto&m:app->install_methods){GtkWidget*b=gtk_label_new(m.pm.c_str());gtk_style_context_add_class(gtk_widget_get_style_context(b),m.unlocked?"pm-badge":"pm-badge-locked");gtk_box_pack_start(GTK_BOX(pmr),b,FALSE,FALSE,0);}
    }

    // Action buttons
    GtkWidget*bvbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_valign(bvbox,GTK_ALIGN_CENTER);gtk_box_pack_end(GTK_BOX(hdr),bvbox,FALSE,FALSE,0);
    if(!app->installed){
        GtkWidget*ibtn=gtk_button_new();
        GtkWidget*bc=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_start(bc,4);gtk_widget_set_margin_end(bc,4);
        gtk_box_pack_start(GTK_BOX(bc),gtk_image_new_from_icon_name("system-software-install-symbolic",GTK_ICON_SIZE_BUTTON),FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(bc),gtk_label_new("Cài đặt"),FALSE,FALSE,0);
        gtk_container_add(GTK_CONTAINER(ibtn),bc);
        gtk_style_context_add_class(gtk_widget_get_style_context(ibtn),"install-btn");
        g_signal_connect(ibtn,"clicked",G_CALLBACK(on_install_clicked),app);
        gtk_box_pack_start(GTK_BOX(bvbox),ibtn,FALSE,FALSE,0);
    } else {
        GtkWidget*ubtn=gtk_button_new();
        GtkWidget*bc=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_start(bc,4);gtk_widget_set_margin_end(bc,4);
        gtk_box_pack_start(GTK_BOX(bc),gtk_image_new_from_icon_name("edit-delete-symbolic",GTK_ICON_SIZE_BUTTON),FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(bc),gtk_label_new("Gỡ cài đặt"),FALSE,FALSE,0);
        gtk_container_add(GTK_CONTAINER(ubtn),bc);
        gtk_style_context_add_class(gtk_widget_get_style_context(ubtn),"uninstall-btn");
        g_signal_connect(ubtn,"clicked",G_CALLBACK(on_uninstall_clicked),app);
        gtk_box_pack_start(GTK_BOX(bvbox),ubtn,FALSE,FALSE,0);
    }

    gtk_box_pack_start(GTK_BOX(g_state.detail_box),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),FALSE,FALSE,0);

    // Content scroll
    GtkWidget*cscroll=gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cscroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(g_state.detail_box),cscroll,TRUE,TRUE,0);
    GtkWidget*cbox=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_container_add(GTK_CONTAINER(cscroll),cbox);

    // Left: description + methods + tags
    GtkWidget*dbox=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);
    gtk_widget_set_margin_start(dbox,24);gtk_widget_set_margin_end(dbox,24);
    gtk_widget_set_margin_top(dbox,20);gtk_widget_set_margin_bottom(dbox,20);
    gtk_box_pack_start(GTK_BOX(cbox),dbox,TRUE,TRUE,0);

    auto add_section=[&](const char*title){
        GtkWidget*l=gtk_label_new(title);gtk_widget_set_halign(l,GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(l),"detail-meta-label");
        gtk_box_pack_start(GTK_BOX(dbox),l,FALSE,FALSE,0);
    };

    add_section("Mô tả");
    GtkWidget*dl=gtk_label_new(app->description.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(dl),TRUE);gtk_label_set_line_wrap_mode(GTK_LABEL(dl),PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(dl),0.0);gtk_label_set_max_width_chars(GTK_LABEL(dl),60);
    gtk_style_context_add_class(gtk_widget_get_style_context(dl),"detail-description");
    gtk_box_pack_start(GTK_BOX(dbox),dl,FALSE,FALSE,0);

    if(!app->install_methods.empty()){
        add_section("Phương pháp cài đặt");
        for(auto&m:app->install_methods){
            GtkWidget*mr2=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_bottom(mr2,4);
            GtkWidget*b2=gtk_label_new(m.pm.c_str());gtk_style_context_add_class(gtk_widget_get_style_context(b2),m.unlocked?"pm-badge":"pm-badge-locked");
            GtkWidget*p2=gtk_label_new(m.package.c_str());gtk_widget_set_halign(p2,GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(p2),"detail-meta-value");
            GtkWidget*s2=gtk_label_new(m.unlocked?"✓ Khả dụng":"✗ Không có trên máy");
            gtk_style_context_add_class(gtk_widget_get_style_context(s2),"app-version");
            gtk_box_pack_start(GTK_BOX(mr2),b2,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(mr2),p2,FALSE,FALSE,0);
            gtk_box_pack_end(GTK_BOX(mr2),s2,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(dbox),mr2,FALSE,FALSE,0);
        }
    }

    if(!app->tags.empty()){
        add_section("Thẻ");
        GtkWidget*flow=gtk_flow_box_new();gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow),10);
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow),GTK_SELECTION_NONE);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow),4);gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow),4);
        for(auto&tag:app->tags){GtkWidget*chip=gtk_label_new(tag.c_str());gtk_style_context_add_class(gtk_widget_get_style_context(chip),"tag-chip");gtk_flow_box_insert(GTK_FLOW_BOX(flow),chip,-1);}
        gtk_box_pack_start(GTK_BOX(dbox),flow,FALSE,FALSE,0);
    }

    // Right: meta panel
    GtkWidget*mpanel=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_widget_set_size_request(mpanel,240,-1);gtk_widget_set_margin_end(mpanel,24);gtk_widget_set_margin_top(mpanel,20);
    gtk_box_pack_end(GTK_BOX(cbox),mpanel,FALSE,FALSE,0);

    struct MI{std::string label,value;bool is_link;};
    std::vector<MI> meta={
        {"Phiên bản",app->version,false},{"Tác giả",app->author,false},
        {"Giấy phép",app->license,false},
        {"Danh mục",CATEGORY_LABELS.count(app->category)?CATEGORY_LABELS[app->category]:app->category,false},
        {"Website",app->website,true},
    };
    if(app->installed&&!app->installed_via.empty()) meta.push_back({"Đã cài qua",pm_display(app->installed_via),false});

    for(auto&item:meta){
        GtkWidget*ib=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_widget_set_margin_bottom(ib,14);
        GtkWidget*ml=gtk_label_new(item.label.c_str());gtk_widget_set_halign(ml,GTK_ALIGN_START);
        gtk_style_context_add_class(gtk_widget_get_style_context(ml),"detail-meta-label");gtk_box_pack_start(GTK_BOX(ib),ml,FALSE,FALSE,0);
        GtkWidget*vl;
        if(item.is_link){vl=gtk_link_button_new_with_label(item.value.c_str(),item.value.c_str());gtk_widget_set_halign(vl,GTK_ALIGN_START);}
        else{vl=gtk_label_new(item.value.c_str());gtk_widget_set_halign(vl,GTK_ALIGN_START);gtk_label_set_line_wrap(GTK_LABEL(vl),TRUE);gtk_style_context_add_class(gtk_widget_get_style_context(vl),"detail-meta-value");}
        gtk_box_pack_start(GTK_BOX(ib),vl,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(mpanel),ib,FALSE,FALSE,0);
    }

    gtk_widget_show_all(g_state.detail_box);
    gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack),"detail");
}

static void show_apps_list(){
    g_state.current_detail_app=nullptr;
    gtk_stack_set_visible_child_name(GTK_STACK(g_state.stack),"grid");
    rebuild_apps_grid();
}


// ─── 3 Buttons at pack end ─────────────────────────────────────────────────
static void on_close(GtkButton*, gpointer){gtk_main_quit();}
static void on_minimize(GtkButton*, gpointer window){gtk_window_iconify(GTK_WINDOW(window));}
static void on_maximize(GtkButton*, gpointer window)
{
    GtkWindow *win = GTK_WINDOW(window);

    if (gtk_window_is_maximized(win))
        gtk_window_unmaximize(win);
    else
        gtk_window_maximize(win);
}



// ─── Setup window ─────────────────────────────────────────────────────────────
static void setup_window(){
    GtkCssProvider*css=gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,APP_CSS,-1,nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget*window=gtk_window_new(GTK_WINDOW_TOPLEVEL); g_state.window=window;
    gtk_window_set_title(GTK_WINDOW(window),"Thần đèn");
    gtk_window_set_default_size(GTK_WINDOW(window),1100,700);
    gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_CENTER);
    g_signal_connect(window,"destroy",G_CALLBACK(gtk_main_quit),nullptr);

    GtkWidget*header_bar=gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar),FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(header_bar),"custom-titlebar");

    GtkWidget *btn_close=gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_max=gtk_button_new_from_icon_name("window-maximize-symbolic",GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_min=gtk_button_new_from_icon_name("window-minimize-symbolic",GTK_ICON_SIZE_BUTTON);
    
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close),"titlebar-close");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_max),"titlebar-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_min),"titlebar-button");
    
    g_signal_connect(btn_close,"clicked",G_CALLBACK(on_close),nullptr);
    g_signal_connect(btn_max,"clicked",G_CALLBACK(on_maximize),window);
    g_signal_connect(btn_min,"clicked",G_CALLBACK(on_minimize),window);
    
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar),btn_close);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar),btn_max);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar),btn_min);

    
/*
    GtkWidget *close_image =gtk_button_get_image(GTK_BUTTON(btn_close));
    gtk_image_set_pixel_size(GTK_IMAGE(close_image),24);
    GtkWidget *max_image =gtk_button_get_image(GTK_BUTTON(btn_max));
    gtk_image_set_pixel_size(GTK_IMAGE(max_image),24);
    GtkWidget *min_image =gtk_button_get_image(GTK_BUTTON(btn_min));
    gtk_image_set_pixel_size(GTK_IMAGE(min_image),24);
*/
    GtkWidget*title_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_set_halign(title_box,GTK_ALIGN_CENTER);
    GtkWidget*title_lbl=gtk_label_new("Thần đèn");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl),"titlebar-title");
    gtk_box_pack_start(GTK_BOX(title_box),title_lbl,FALSE,FALSE,0);
    GtkWidget*sub_lbl=gtk_label_new("hiện lên và nói...");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl),"titlebar-subtitle");
    gtk_box_pack_start(GTK_BOX(title_box),sub_lbl,FALSE,FALSE,0);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header_bar),title_box);
    gtk_window_set_titlebar(GTK_WINDOW(window),header_bar);

    GtkWidget*main_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_container_add(GTK_CONTAINER(window),main_box);
    GtkWidget*content_box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
    gtk_box_pack_start(GTK_BOX(main_box),content_box,TRUE,TRUE,0);

    GtkWidget*right_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_box_pack_start(GTK_BOX(content_box),right_box,TRUE,TRUE,0);

    GtkWidget*top_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_margin_start(top_bar,16);gtk_widget_set_margin_end(top_bar,16);
    gtk_widget_set_margin_top(top_bar,10);gtk_widget_set_margin_bottom(top_bar,10);
    gtk_box_pack_start(GTK_BOX(right_box),top_bar,FALSE,FALSE,0);
    g_state.count_label=gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(g_state.count_label),"app-author");
    gtk_widget_set_halign(g_state.count_label,GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(top_bar),g_state.count_label,TRUE,TRUE,0);

    GtkWidget*stack=gtk_stack_new(); g_state.stack=stack;
    gtk_stack_set_transition_type(GTK_STACK(stack),GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack),150);
    gtk_box_pack_start(GTK_BOX(right_box),stack,TRUE,TRUE,0);

    GtkWidget*grid_scrolled=gtk_scrolled_window_new(nullptr,nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_scrolled),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    g_state.apps_scrolled=grid_scrolled;
    gtk_stack_add_named(GTK_STACK(stack),grid_scrolled,"grid");
    GtkWidget*grid=gtk_grid_new(); g_state.apps_grid=grid;
    gtk_grid_set_row_spacing(GTK_GRID(grid),12);gtk_grid_set_column_spacing(GTK_GRID(grid),12);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);
    gtk_widget_set_margin_start(grid,16);gtk_widget_set_margin_end(grid,16);
    gtk_widget_set_margin_top(grid,4);gtk_widget_set_margin_bottom(grid,16);
    gtk_container_add(GTK_CONTAINER(grid_scrolled),grid);

    GtkWidget*detail_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); g_state.detail_box=detail_box;
    gtk_stack_add_named(GTK_STACK(stack),detail_box,"detail");

    GtkWidget*status_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_bar),"status-bar");
    gtk_box_pack_end(GTK_BOX(main_box),status_bar,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(status_bar),gtk_label_new("Thần đèn — đang thực hiện điều ước..."),FALSE,FALSE,0);

    gtk_widget_show_all(window);
    gtk_stack_set_visible_child_name(GTK_STACK(stack),"grid");
    apply_filter(); rebuild_apps_grid();

}
// ─── Main ─────────────────────────────────────────────────────────────────────
static std::string get_exe_dir(){
    char buf[4096]={0};
    ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
    if(n<=0)return ".";
    std::string p(buf,n); size_t sl=p.rfind('/');
    return sl==std::string::npos?".":p.substr(0,sl);
}

int main(int argc, char *argv[]){
    gtk_init(&argc,&argv);

    // 1. Detect package managers
    detect_package_managers();

    std::string exe_dir=get_exe_dir();

    // 2. Data dir (supports data/apps/*.json or data/apps.json)
    std::vector<std::string> data_cands={exe_dir+"/data",exe_dir+"/../data","data","."};
    for(auto&d:data_cands){
        std::ifstream t1(d+"/apps.json");
        GDir*t2=g_dir_open((d+"/apps").c_str(),0,nullptr);
        bool ok=t1.good()||t2; if(t2)g_dir_close(t2);
        if(ok){g_state.data_dir=d;break;}
    }
    if(g_state.data_dir.empty()) g_state.data_dir=exe_dir+"/data";
    g_state.icons_dir=g_state.data_dir+"/icons";
    g_mkdir_with_parents(g_state.icons_dir.c_str(),0755);

    // 3. Scripts dir
    std::vector<std::string> sc_cands={exe_dir+"/scripts",exe_dir+"/../scripts","scripts","."};
    for(auto&d:sc_cands){std::ifstream t(d+"/install-generic.sh");if(t.good()){g_state.scripts_dir=d;break;}}
    if(g_state.scripts_dir.empty()) g_state.scripts_dir=exe_dir+"/scripts";

    // 4. Install log: ~/.local/share/vnlf-store/installed.json
    const char*home=g_get_home_dir();
    g_log_path=std::string(home?home:".")+"/.local/share/vnlf-store/installed.json";
    load_install_log();

    // 5. Load app definitions
    g_state.all_apps=parse_apps_json(g_state.data_dir);
    if(g_state.all_apps.empty()){
        // Fallback sample app
        AppInfo sample;
        sample.id="firefox"; sample.name="Firefox";
        sample.summary="Trình duyệt web nhanh và bảo mật";
        sample.description="Mozilla Firefox là trình duyệt web mã nguồn mở miễn phí.";
        sample.category="internet"; sample.icon="firefox";
        sample.version="124.0"; sample.author="Mozilla Foundation";
        sample.license="MPL-2.0"; sample.website="https://www.mozilla.org/";
        sample.tags={"trình duyệt","web"};
        InstallMethod m; m.pm="apt"; m.package="firefox";
        m.needs_sudo=true; m.sudo_override=false; m.unlocked=is_pm_available("apt");
        sample.install_methods.push_back(m);
        g_state.all_apps.push_back(sample);
    }

    // 6. Mark installed apps (from log + system detection, dedup)
    for(auto&app:g_state.all_apps){
        InstalledEntry*e=find_log_entry(app.id);
        if(e){ app.installed=true; app.installed_via=e->pm; }
        else if(check_app_installed_system(app)){
            app.installed=true;
            app.installed_via=detect_installed_pm(app);
            InstalledEntry ne;
            ne.app_id=app.id; ne.pm=app.installed_via;
            ne.package=app.id; ne.version=app.version;
            add_to_log(ne);
        }
    }

    // 8. Fonts
    std::vector<std::string> f_cands={exe_dir+"/fonts",exe_dir+"/../fonts","fonts"};
    for(auto&d:f_cands){
        GDir*fd=g_dir_open(d.c_str(),0,nullptr);
        if(fd){bool has=false;const gchar*fn;
            while((fn=g_dir_read_name(fd))!=nullptr){std::string s(fn);
                if(s.size()>4&&(s.substr(s.size()-4)==".ttf"||s.substr(s.size()-4)==".otf")){has=true;break;}}
            g_dir_close(fd);if(has){g_fonts_dir=d;break;}}
    }
    load_app_fonts();

    
    std::cout<<"[VNLF] Data:    "<<g_state.data_dir<<"\n";
    std::cout<<"[VNLF] Icons:   "<<g_state.icons_dir<<"\n";
    std::cout<<"[VNLF] Scripts: "<<g_state.scripts_dir<<"\n";
    std::cout<<"[VNLF] Log:     "<<g_log_path<<"\n";
    std::cout<<"[VNLF] Fonts:   "<<(g_fonts_dir.empty()?"(system)":g_fonts_dir)<<"\n";
    std::cout<<"[VNLF] Apps:    "<<g_state.all_apps.size()<<"\n";

    setup_window();
    gtk_main();
    return 0;
}
