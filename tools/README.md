# tools

## gemini_keys.py: Gemini API keys for devices

Manages API keys in the GCP project named in `.gcp-project` via the logged-in `gcloud` CLI.
Python 3 stdlib only. Key values are never printed (only `AIza...abcd`), are sent in headers
or request bodies (never in URLs or command lines), and are stored with mode 0600 under `secrets/`.

```sh
tools/gemini_keys.py list                         # display name, uid, create time, restrictions
tools/gemini_keys.py create NAME                  # new key limited to generativelanguage.googleapis.com
                                                  #   -> secrets/keys/NAME
tools/gemini_keys.py check                        # test secrets/GEMINI_API_KEY (HTTP status + Live models)
tools/gemini_keys.py check NAME                   # test secrets/keys/NAME
tools/gemini_keys.py check --file PATH
tools/gemini_keys.py rotate NAME [--device HOST]  # see below
tools/gemini_keys.py delete UID --yes             # refuses without --yes
```

`create`, `rotate` and `delete` accept `--dry-run`, which prints the gcloud commands instead of
running them. Every command accepts `--project ID` to override `.gcp-project`.

**rotate NAME [--device HOST]** (a bare `--device` means `$BAP_DEVICE`):
1. finds the uid of the key currently in `secrets/GEMINI_API_KEY`
2. creates `NAME-yyyymmdd` and saves it to `secrets/keys/NAME-yyyymmdd`
3. checks that `GET /v1beta/models` returns HTTP 200 with the new key (retries for up to 2 min while the key propagates)
4. with `--device`, POSTs it to `http://HOST/api/config` and confirms with `api_key_masked`
5. replaces `secrets/GEMINI_API_KEY`
6. prints the old key's uid and the `gcloud services api-keys delete ...` command. The old key is
   **not** deleted, so you can delete it once nothing uses it.

If verification fails, it stops before touching the device or `GEMINI_API_KEY` and prints how to delete
the new key. If the device push or confirmation fails, `GEMINI_API_KEY` is left unchanged.

If `check` gets HTTP 429 / `RESOURCE_EXHAUSTED`, it prints how to link a billing account for the paid tier.

Note: the device API is plain HTTP on the LAN, so the key travels unencrypted on the local network
during `--device` pushes.

## bap-desktop (Rust)

Hiện hạn mức của các coding agent trên màn hình Bắp. Chạy trên máy tính, chỉ đọc file log cục bộ
và gửi một thẻ trạng thái ngắn tới thiết bị trong mạng nhà (`POST /api/notify`). Không gửi gì ra
Internet.

- **Codex:** đọc `rate_limits` mới nhất trong `~/.codex/sessions/**/rollout-*.jsonl` (phần trăm đã
  dùng, cửa sổ, giờ đặt lại), đúng như Codex CLI tự báo.
- **Claude Code:** cộng token trong khối 5 giờ hiện tại và hôm nay từ `~/.claude/projects/*/*.jsonl`.
  Claude không ghi hạn mức gói cước ra file, nên chỉ hiện số token.
- Khi một hạn mức vượt 80%, nhân vật nói một câu nhắc (mỗi cảnh báo chỉ nói một lần).

```sh
cd tools/bap-desktop && cargo build --release
./target/release/bap-desktop agents --print --once     # xem thẻ sẽ gửi
./target/release/bap-desktop agents --device bap-xxxx.local # gửi mỗi 120 giây
./target/release/bap-desktop say "Đã build xong firmware"  # nhờ nhân vật nói
```

Chạy nền khi đăng nhập (systemd user service):

```sh
mkdir -p ~/.config/systemd/user
cp tools/bap-desktop/bap-desktop.service ~/.config/systemd/user/
systemctl --user enable --now bap-desktop
```
