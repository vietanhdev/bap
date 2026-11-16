//! bap-desktop: desktop companion for the Bap voice assistant device.
//!
//!   bap-desktop agents [--device bap-xxxx.local | $BAP_DEVICE] [--interval 120] [--once] [--print]
//!       Show Codex / Claude Code usage limits on the device.
//!   bap-desktop say "text" [--device bap-xxxx.local | $BAP_DEVICE]
//!       Ask the active character to say something (e.g. from scripts, CI, cron).

mod agents;

fn usage() -> ! {
    eprintln!("usage: bap-desktop <agents|say> [options]\n  agents [--device HOST] [--interval SECS] [--once] [--print]\n  say TEXT [--device HOST]");
    std::process::exit(2);
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(cmd) = args.first() else { usage() };
    let rest = &args[1..];
    let opt = |name: &str| rest.iter().position(|a| a == name).and_then(|i| rest.get(i + 1)).cloned();
    match cmd.as_str() {
        "agents" => agents::run(rest),
        "say" => {
            let Some(text) = rest.iter().find(|a| !a.starts_with("--") && Some(*a) != opt("--device").as_ref()) else { usage() };
            let device = opt("--device").or_else(|| std::env::var("BAP_DEVICE").ok()).unwrap_or_else(|| { eprintln!("set --device <bap-xxxx.local or IP> or BAP_DEVICE"); std::process::exit(2) });
            let body = serde_json::json!({ "text": text }).to_string();
            match agents::post(&device, "/api/say", &body) {
                Ok(200) => println!("ok"),
                Ok(code) => { eprintln!("device replied HTTP {code}"); std::process::exit(1) }
                Err(e) => { eprintln!("cannot reach {device}: {e}"); std::process::exit(1) }
            }
        }
        _ => usage(),
    }
}
