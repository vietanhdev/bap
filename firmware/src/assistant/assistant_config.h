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
    std::string id, name, kind, tagline, voice, persona;      // effective values
    std::string default_voice, default_persona, default_name; // built-in values
};

struct App {
    std::string id, title, desc, prompt;
};

struct Config {
    std::string api_key, model, city, character;  // character = active character id
    double lat, lon;
    int idle_s;
    bool search;      // Google Search grounding (needs a billing-enabled key)
    bool greet;       // greet when the camera sees a person
    bool night_dim;   // dim the screen 22:00-06:00 while resting
};

Config LoadConfig();
void SaveConfig(const Config& c);

std::vector<Character> Characters();
Character ActiveCharacter();
// Empty strings clear the override and restore the built-in value.
void SaveCharacterOverride(const std::string& id, const std::string& name, const std::string& voice,
                           const std::string& persona);
bool SetActiveCharacter(const std::string& id_or_name);
std::string SystemPrompt();   // active persona + shared rules
std::vector<App> Apps();

// Face asset name for an emotion: system icon aliases, then "<character>_<emotion>".
std::string FaceName(const char* emotion);
// Expression name for an emotion or system icon name (aliases resolved).
std::string FaceAlias(const char* emotion);

// Small JSON string lists kept in NVS (notes, memory, alarms, hooks).
std::string LoadList(const char* key);                 // JSON array text, "[]" if empty
void SaveList(const char* key, const std::string& json);

}  // namespace assistant
