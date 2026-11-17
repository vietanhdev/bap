# Adding a tool

A tool is something the assistant can do or look up (weather, a timer, a webhook). Tools are
registered on the device's MCP server and are offered to Gemini automatically as function
declarations; you do not touch the Gemini protocol code. See
[architecture.md](architecture.md#mcp-tools-bridged-to-gemini-function-calls) for how the bridge
works.

Before writing C++, check whether you need a tool at all:

- **A fixed script for the model** (a game, a routine, a guided exercise): add an *app* to
  `firmware/data/assistant.json` instead. No code needed.
- **Calling your own service** (Home Assistant, n8n, a script on your server): add a *hook* in the
  web UI (`http://bap-xxxx.local`, section for extensions). The built-in `extension.call` tool posts
  `{"action": <name>, "text": <request>}` to the hook URL and reads back the reply.

Write a native tool when it needs device features, has to be fast, or must work without a server.

## 1. Register the tool

Tools live in `assistant::RegisterTools()` in `firmware/src/assistant/assistant_tools.cc`. Add a
`mcp.AddTool(...)` call next to the related tools:

```cpp
    mcp.AddTool("vn.gia_xang",
                "Xem giá xăng dầu bán lẻ hôm nay tại Việt Nam (RON 95, E5, dầu DO). Dùng khi người "
                "dùng hỏi giá xăng.",
                PropertyList({Property("loai", kPropertyTypeString, std::string(""))}),
                [](const PropertyList& p) -> ToolResult {
                    auto body = HttpGet("https://example.vn/gia-xang.json");
                    if (!body) return std::unexpected(body.error());
                    // ... parse with cJSON, build a short spoken answer ...
                    return std::string("Xăng RON 95 hôm nay 21.000 đồng một lít.");
                });
```

Reference for the pieces (upstream `main/mcp_server.h`):

| Piece | Notes |
| --- | --- |
| Name | `group.action`, lowercase ASCII, for example `vn.weather`, `timer.set`. Dots become `_` for Gemini (`ToFunctionName()` in `gemini_live_protocol.cc`), so names must stay unique after that and within 64 characters. Keep the existing Vietnamese naming style. |
| Description | Vietnamese, one or two sentences. Say **when** to use it with example phrases a user would say. This is the only thing the model sees, so it decides whether the tool is called. |
| `Property(name, kPropertyTypeString)` | Required string argument |
| `Property(name, kPropertyTypeString, std::string(""))` | Optional string with a default |
| `Property(name, kPropertyTypeInteger, min, max)` | Required integer in a range |
| `Property(name, kPropertyTypeInteger, default, min, max)` | Optional integer |
| `Property(name, kPropertyTypeBoolean, false)` | Optional boolean |
| Read arguments | `p["name"].value<int>()`, `.value<std::string>()`, `.value<bool>()` |
| Return type | `ReturnValue` (a `std::string`, `bool`, `int` or `cJSON*`), or `ToolResult` when the tool can fail: return `std::unexpected("message")` for errors |

Helpers already in `assistant_tools.cc` (anonymous namespace):

- `HttpGet(url)` returns `std::expected<std::string, std::string>`; 8 s timeout, user agent set.
- `UrlEncode()`, `Between()`, `CleanXmlText()` for simple HTML or RSS scraping.
- `Now(&tm)` returns false until the clock is synced; always handle that case.
- `ListItems(key)` / `StoreItems(key, items)` for a small persistent string list in NVS (keys are
  at most 15 characters).
- `LoadConfig()` for the user's city and coordinates.

Guidelines:

- **Return short, speakable Vietnamese text.** The reply is read aloud: no markdown, no long
  tables. Trim large responses (the webhook tool caps replies at 1200 bytes).
- **Don't block for long.** Tool callbacks run in the MCP server context; keep network calls
  under about 10 seconds. For anything that should happen later (a reminder), store it and let the
  scheduler (`SchedulerTask()`) act on it.
- **Need the device to do something after replying** (switch character, restart the session)?
  Look at `character.switch`: it replies first and does the work in a short-lived FreeRTOS task.
- **No secrets in tools.** Use public endpoints, or keys stored in NVS via the web UI, never
  hard-coded keys.
- Tools that only the user (web UI) should trigger: register them with `mcp.AddUserOnlyTool(...)`;
  they are not exposed to Gemini.
- Keep descriptions concise. Upstream `tools/list` pages its reply at about 8000 bytes and the
  bridge (`BuildToolDeclarations()`) currently reads only the first page, so a very long tool list
  can silently drop tools.

## 2. Tell the model about it (optional)

The shared rules in `common_rules` (`firmware/data/assistant.json`) list the kinds of tools the
assistant has ("Bạn có công cụ để tra lịch âm, thời tiết, ..."). If the new tool covers a new
area, add a few words there so every character knows to use it rather than guessing.

## 3. Test

1. If the logic is pure (parsing, date math), put it in a header without ESP-IDF dependencies and
   add a host test in `firmware/tests/` (see `firmware/tests/README.md`).
2. Build and flash the app:

   ```bash
   firmware/build.sh && firmware/flash_app.sh     # or: firmware/ota.sh bap-xxxx.local
   ```

3. Check the serial log for `Add tool: vn.gia_xang` and `Exposed N device tools to Gemini`
   (N should have increased by one).
4. Call the tool directly, without the model, from the web UI's API:

   ```bash
   curl -s -X POST http://bap-xxxx.local/api/tool -H 'X-Bap: 1' -H 'Content-Type: application/json' \
     -d '{"name":"vn.gia_xang","arguments":{"loai":"RON 95"}}'
   ```

5. Ask the assistant with natural phrases ("giá xăng hôm nay bao nhiêu?") and check the log for
   `Tool call: vn_gia_xang`.

## 4. Checklist for the PR

- [ ] `clang-format -i firmware/src/assistant/assistant_tools.cc`
- [ ] Description says when to use the tool, in Vietnamese
- [ ] Errors return `std::unexpected(...)` with a short Vietnamese message
- [ ] No API keys or personal URLs in the code
- [ ] Tested on a device; README feature list updated if user-visible
