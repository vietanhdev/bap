//! Coding-agent limits: reads local usage and pushes a short status card to the Bap device on
//! the LAN (POST /api/notify). Nothing leaves the machine except that card.
//!
//! Sources (read-only):
//! - Codex: newest ~/.codex/sessions/**/rollout-*.jsonl, last "rate_limits" object
//!   (used_percent, window_minutes, resets_at) as reported by the Codex CLI itself.
//! - Claude Code: ~/.claude/projects/*/*.jsonl assistant messages; tokens in the current
//!   5-hour block and today (Claude does not log its plan limit locally, so no percentage).
//!
//! Usage: bap-desktop agents [--device bap.local] [--interval 120] [--once] [--print]

use serde_json::Value;
use std::collections::HashSet;
use std::fs;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpStream, ToSocketAddrs};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

#[derive(Debug, Default, Clone)]
struct CodexLimit {
    used_percent: f64,
    window_minutes: u64,
    resets_at: u64,
}

#[derive(Debug, Default)]
struct Status {
    codex_primary: Option<CodexLimit>,
    codex_secondary: Option<CodexLimit>,
    claude_block_tokens: u64,
    claude_today_tokens: u64,
    claude_block_messages: u64,
}

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".into()))
}

fn now_secs() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

fn newest_files(dir: &Path, ext: &str, limit: usize) -> Vec<PathBuf> {
    let mut files: Vec<(SystemTime, PathBuf)> = Vec::new();
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        let Ok(entries) = fs::read_dir(&d) else { continue };
        for e in entries.flatten() {
            let p = e.path();
            if p.is_dir() {
                stack.push(p);
            } else if p.extension().and_then(|x| x.to_str()) == Some(ext) {
                if let Ok(m) = e.metadata().and_then(|m| m.modified()) {
                    files.push((m, p));
                }
            }
        }
    }
    files.sort_by(|a, b| b.0.cmp(&a.0));
    files.into_iter().take(limit).map(|(_, p)| p).collect()
}

fn parse_limit(v: &Value) -> Option<CodexLimit> {
    Some(CodexLimit {
        used_percent: v.get("used_percent")?.as_f64()?,
        window_minutes: v.get("window_minutes").and_then(Value::as_u64).unwrap_or(0),
        resets_at: v.get("resets_at").and_then(Value::as_u64).unwrap_or(0),
    })
}

fn find_rate_limits(v: &Value) -> Option<&Value> {
    match v {
        Value::Object(map) => {
            if let Some(r) = map.get("rate_limits") {
                if r.is_object() {
                    return Some(r);
                }
            }
            map.values().find_map(find_rate_limits)
        }
        Value::Array(items) => items.iter().find_map(find_rate_limits),
        _ => None,
    }
}

fn read_codex(status: &mut Status) {
    for file in newest_files(&home().join(".codex/sessions"), "jsonl", 3) {
        let Ok(f) = fs::File::open(&file) else { continue };
        let mut last: Option<Value> = None;
        for line in BufReader::new(f).lines().map_while(Result::ok) {
            if !line.contains("rate_limits") {
                continue;
            }
            if let Ok(v) = serde_json::from_str::<Value>(&line) {
                if let Some(r) = find_rate_limits(&v) {
                    last = Some(r.clone());
                }
            }
        }
        if let Some(r) = last {
            status.codex_primary = r.get("primary").and_then(parse_limit);
            status.codex_secondary = r.get("secondary").and_then(parse_limit);
            return;
        }
    }
}

/// Seconds since epoch for an RFC 3339 UTC timestamp like 2026-11-16T13:33:23.423Z.
fn parse_ts(s: &str) -> Option<u64> {
    let b = s.as_bytes();
    if b.len() < 19 {
        return None;
    }
    let n = |a: usize, l: usize| s.get(a..a + l)?.parse::<i64>().ok();
    let (y, mo, d, h, mi, se) = (n(0, 4)?, n(5, 2)?, n(8, 2)?, n(11, 2)?, n(14, 2)?, n(17, 2)?);
    let a = (14 - mo) / 12;
    let yy = y + 4800 - a;
    let mm = mo + 12 * a - 3;
    let jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    Some(((jdn - 2440588) * 86400 + h * 3600 + mi * 60 + se) as u64)
}

fn read_claude(status: &mut Status) {
    let now = now_secs();
    let block_start = now.saturating_sub(5 * 3600);
    let tz_offset = 7 * 3600; // Vietnam time for "today"
    let day_start = (now + tz_offset) / 86400 * 86400 - tz_offset;
    let mut seen: HashSet<String> = HashSet::new();
    let cutoff = SystemTime::now() - Duration::from_secs(26 * 3600);
    let Ok(projects) = fs::read_dir(home().join(".claude/projects")) else { return };
    for project in projects.flatten() {
        let Ok(files) = fs::read_dir(project.path()) else { continue };
        for file in files.flatten() {
            let p = file.path();
            let recent = file.metadata().and_then(|m| m.modified()).map(|m| m > cutoff).unwrap_or(false);
            if p.extension().and_then(|x| x.to_str()) != Some("jsonl") || !recent {
                continue;
            }
            let Ok(f) = fs::File::open(&p) else { continue };
            for line in BufReader::new(f).lines().map_while(Result::ok) {
                if !line.contains("\"usage\"") {
                    continue;
                }
                let Ok(v) = serde_json::from_str::<Value>(&line) else { continue };
                let Some(ts) = v.get("timestamp").and_then(Value::as_str).and_then(parse_ts) else { continue };
                if ts < day_start.min(block_start) {
                    continue;
                }
                let msg = &v["message"];
                let key = format!(
                    "{}:{}",
                    msg.get("id").and_then(Value::as_str).unwrap_or(""),
                    v.get("requestId").and_then(Value::as_str).unwrap_or("")
                );
                if !seen.insert(key) {
                    continue; // streaming writes the same message more than once
                }
                let u = &msg["usage"];
                let get = |k: &str| u.get(k).and_then(Value::as_u64).unwrap_or(0);
                let tokens = get("input_tokens") + get("output_tokens") + get("cache_creation_input_tokens");
                if ts >= block_start {
                    status.claude_block_tokens += tokens;
                    status.claude_block_messages += 1;
                }
                if ts >= day_start {
                    status.claude_today_tokens += tokens;
                }
            }
        }
    }
}

fn human_tokens(n: u64) -> String {
    match n {
        n if n >= 1_000_000 => format!("{:.1} triệu", n as f64 / 1e6),
        n if n >= 1_000 => format!("{} nghìn", n / 1000),
        n => n.to_string(),
    }
}

fn window_name(minutes: u64) -> String {
    match minutes {
        300 => "5 giờ".into(),
        10080 => "tuần".into(),
        m if m % 1440 == 0 => format!("{} ngày", m / 1440),
        m if m % 60 == 0 => format!("{} giờ", m / 60),
        m => format!("{m} phút"),
    }
}

fn resets_in(at: u64) -> String {
    let left = at.saturating_sub(now_secs());
    if left >= 86400 {
        format!("{} ngày", left / 86400)
    } else {
        format!("{} giờ {} phút", left / 3600, left % 3600 / 60)
    }
}

/// Short card for the round screen plus a spoken sentence when a limit is close.
fn card(s: &Status) -> (String, Option<String>, &'static str) {
    let mut lines = Vec::new();
    let mut worst = 0.0f64;
    let mut speak = None;
    for limit in [&s.codex_primary, &s.codex_secondary].into_iter().flatten() {
        worst = worst.max(limit.used_percent);
        lines.push(format!("Codex {:.0}% / {}", limit.used_percent, window_name(limit.window_minutes)));
        if limit.used_percent >= 80.0 {
            speak = Some(format!(
                "Codex đã dùng {:.0} phần trăm hạn mức {}, còn {} nữa mới đặt lại.",
                limit.used_percent,
                window_name(limit.window_minutes),
                resets_in(limit.resets_at)
            ));
        }
    }
    lines.push(format!("Claude {} token / 5 giờ", human_tokens(s.claude_block_tokens)));
    let emotion = if worst >= 90.0 { "shocked" } else if worst >= 80.0 { "embarrassed" } else { "cool" };
    (lines.join("\n"), speak, emotion)
}

fn base64(data: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in data.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (b[0] as u32) << 16 | (b[1] as u32) << 8 | b[2] as u32;
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(T[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

pub fn post(device: &str, path: &str, body: &str) -> std::io::Result<u16> {
    let addr = (device, 80)
        .to_socket_addrs()?
        .next()
        .ok_or_else(|| std::io::Error::other("cannot resolve device"))?;
    // Settings password, when one is set on the device (any user name works).
    let auth = match std::env::var("BAP_PASSWORD") {
        Ok(p) if !p.is_empty() => format!("Authorization: Basic {}\r\n", base64(format!("bap:{p}").as_bytes())),
        _ => String::new(),
    };
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_secs(5))?;
    stream.set_read_timeout(Some(Duration::from_secs(10)))?;
    write!(
        stream,
        "POST {path} HTTP/1.1\r\nHost: {device}\r\nContent-Type: application/json\r\nX-Bap: 1\r\n{auth}Content-Length: {}\r\nConnection: close\r\n\r\n{body}",
        body.len()
    )?;
    let mut reply = String::new();
    stream.read_to_string(&mut reply)?;
    Ok(reply.split_whitespace().nth(1).and_then(|c| c.parse().ok()).unwrap_or(0))
}

pub fn run(args: &[String]) {
    let opt = |name: &str| args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned();
    let device = opt("--device").unwrap_or_else(|| "bap.local".into());
    let interval: u64 = opt("--interval").and_then(|v| v.parse().ok()).unwrap_or(120);
    let once = args.iter().any(|a| a == "--once");
    let print_only = args.iter().any(|a| a == "--print");
    let mut last_spoken: Option<String> = None;
    loop {
        let mut s = Status::default();
        read_codex(&mut s);
        read_claude(&mut s);
        let (text, speak, emotion) = card(&s);
        // Speak a warning once per distinct message, not every interval.
        let say = match (&speak, &last_spoken) {
            (Some(a), Some(b)) if a == b => None,
            (Some(a), _) => Some(a.clone()),
            _ => None,
        };
        let body = serde_json::json!({
            "title": "Coding agents",
            "text": text,
            "emotion": emotion,
            "speak": say.clone().unwrap_or_default(),
        })
        .to_string();
        if print_only {
            println!("{body}");
        } else {
            match post(&device, "/api/notify", &body) {
                Ok(200) => {
                    if say.is_some() {
                        last_spoken = say;
                    }
                }
                Ok(code) => eprintln!("device replied HTTP {code}"),
                Err(e) => eprintln!("cannot reach {device}: {e}"),
            }
        }
        if once {
            break;
        }
        std::thread::sleep(Duration::from_secs(interval));
    }
}
