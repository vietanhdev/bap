#pragma once
// Runtime settings (NVS namespace "assistant", editable from the web UI) layered over the
// built-in data in firmware/data/assistant.json: characters, shared rules and apps.
#include <string>
#include <vector>

namespace assistant {

// How a character is drawn on the device (see assistant_face.cc). Colors are "#RRGGBB".
struct Look {
    std::string shape, body, accent, ink, cheek;
};

struct Character {
    Look look;
    std::string id, name, kind, tagline, voice, persona;       // effective values
    std::string default_voice, default_persona, default_name;  // built-in values
};

struct Station {
    std::string id, name, url;
};

struct App {
    std::string id, title, desc, prompt;
};

struct Config {
    std::string api_key, model, city, character;  // character = active character id
    // Billing-enabled key, used only while the free api_key is rejected (see gemini_live_protocol).
    std::string api_key_paid;
    double lat, lon;
    int idle_s;
    bool search;           // Google Search grounding (needs a billing-enabled key)
    bool greet;            // greet when the camera sees a person
    bool night_dim;        // dim the screen 22:00-06:00 while resting
    bool presence;         // camera wakes the screen and the character reacts when someone comes
    bool morning;          // first person seen 6:00-10:00: morning briefing (needs presence)
    bool quiet;            // do not disturb 22:00-06:00: wake word off
    bool rest;             // dim the screen after a few quiet minutes with nobody around
    std::string language;  // id from assistant.json "languages" (vi, en, ko, zh)
};

Config LoadConfig();
void SaveConfig(const Config& c);

std::vector<Character> Characters();
Character ActiveCharacter();
// Empty strings clear the override and restore the built-in value.
void SaveCharacterOverride(const std::string& id, const std::string& name, const std::string& voice,
                           const std::string& persona);
bool SetActiveCharacter(const std::string& id_or_name);
struct Language {
    std::string id, name, prompt;
};
std::vector<Language> Languages();
// Device UI text in the chosen language (assistant.json languages[].ui); Vietnamese otherwise.
std::string Localize(const char* vietnamese);
std::string SystemPrompt();  // active persona + shared rules
std::vector<App> Apps();
std::vector<Station> Stations();

// Face asset name for an emotion: system icon aliases, then "<character>_<emotion>".
std::string FaceName(const char* emotion);
// Expression name for an emotion or system icon name (aliases resolved).
std::string FaceAlias(const char* emotion);

// Small JSON string lists kept in NVS (notes, memory, alarms, hooks).
std::string LoadList(const char* key);  // JSON array text, "[]" if empty
void SaveList(const char* key, const std::string& json);

}  // namespace assistant
