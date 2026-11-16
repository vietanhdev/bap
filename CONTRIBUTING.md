# Đóng góp cho Bắp

Cảm ơn bạn đã muốn giúp Bắp. Vài quy ước ngắn:

## Nhánh

- `main` luôn build được. Không push thẳng lên `main`, hãy mở pull request.
- Đặt tên nhánh theo loại việc: `feat/<mo-ta-ngan>`, `fix/<mo-ta>`, `docs/<mo-ta>`, `chore/<mo-ta>`.
  Ví dụ: `feat/tool-gia-xang`, `fix/wheel-double-click`.

## Commit

- Theo kiểu [Conventional Commits](https://www.conventionalcommits.org/): `feat: ...`, `fix: ...`,
  `docs: ...`, `refactor: ...`, `test: ...`, `chore: ...`. Có thể thêm phạm vi: `feat(tools): ...`.
- Dòng đầu ngắn (dưới 72 ký tự), viết tiếng Anh hoặc tiếng Việt đều được, nói rõ thay đổi gì.
- Mỗi commit làm một việc. Thay đổi trong `vendor/` phải đi kèm file patch mới
  (`firmware/scripts/export_patch.sh`).

## Trước khi mở PR

```bash
# kiểm thử trên máy
for t in firmware/tests/*.cc; do g++ -std=c++17 -I firmware/src/assistant -o /tmp/t "$t" && /tmp/t || echo "FAIL $t"; done
# kiểm tra dữ liệu nhân vật và ứng dụng
python3 firmware/scripts/validate_data.py
# định dạng code
clang-format -i firmware/src/*/*.cc firmware/src/*/*.h
# build firmware (cần Docker)
MODE=gemini firmware/build.sh
```

Nếu đổi tính năng, hãy thử trên máy thật và ghi lại bạn đã thử gì trong mô tả PR.

## Tuyệt đối không commit bí mật

- Không bao giờ commit: `secrets/`, `backup/` (bản dump flash có khóa thiết bị), `server/.env`,
  `server/data/.config.yaml`, `vendor/`, `firmware/build/`, `.gcp-project`, file log build.
- Không dán API key (chuỗi bắt đầu bằng `AIza...`) vào code, tài liệu, issue hay ảnh chụp màn hình.
  Key chỉ nằm trong `secrets/GEMINI_API_KEY` trên máy bạn hoặc trong NVS của thiết bị.
- File `xiaozhi.bin` build ở máy bạn có chứa API key: không chia sẻ, không đính kèm vào issue.
- Trước khi commit, chạy `gitleaks git --pre-commit --staged` hoặc `git diff --cached | grep -n AIza`.
  CI cũng chạy gitleaks, nhưng một key đã lên GitHub thì coi như đã lộ: hãy thu hồi key ngay.
