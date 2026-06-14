// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include <vector>

typedef void (*callback_t)(void*);

struct fileinfo {
    std::string fileName;
    int         fileSize;
    bool        isDir() const { return fileSize < 0; }
};

extern fileinfo              fileInfo;
extern std::vector<fileinfo> fileVector;

extern void request_file_list(const char* dirname);
#ifdef USE_WIFI
void request_macros_http();   // fetch macros over HTTP (reliable for large files)
#endif

struct Macro {
    std::string name;
    std::string filename;
    std::string target;
};

extern std::vector<Macro*> macros;

extern void request_macros();

#ifdef USE_WIFI
// True if the last macros fetch got an HTTP 200 (the file was served).  Lets the
// UI distinguish "served but empty" from "couldn't reach it".
extern volatile bool g_macros_http_served;
#endif

extern void request_file_preview(const char* name, int firstline, int lastline);

extern std::string current_filename;
extern std::string wifi_mode, wifi_ip, wifi_connected, wifi_ssid;

void init_listener();
void init_file_list();

// Route a bare continuation line from handle_other() into the JSON streaming parser.
// Returns true if the line was consumed (a multi-line JSON response is in progress).
bool json_continuation_line(const char* line);

// True while a $Files/ListGCode or $File/SendJSON reply is expected.
//
// Network transports (Telnet, WebSocket) emit the reply JSON RAW — without the
// "[JSON:...]" wrapper that UartChannel adds — so over WiFi it arrives in
// handle_other() instead of handle_json().  This flag tells handle_other() to
// route those raw chunks into the same streaming parser handle_json() drives.
// Set when a request is issued, cleared when the JSON document completes.
extern volatile bool g_expecting_json;

// Cleared by set_disconnected_state() on a mid-transfer link drop.
extern bool g_json_accumulating;
