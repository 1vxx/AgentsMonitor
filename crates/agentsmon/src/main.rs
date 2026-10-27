//! agentsmon: one binary for the daemon, the hook forwarder and the CLI.

mod adapters;
mod api;
mod claude;
mod client;
mod codex;
mod config;
mod desktop;
mod hub;
mod janitor;
mod otlp;
mod reducer;
mod service;
mod store;
mod summary;
mod sys;
mod tail;
mod titles;
mod util;

use std::collections::HashSet;
use std::io::Read;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::sync::Arc;
use std::time::Duration;

use clap::{Parser, Subcommand};
use tokio::net::{TcpListener, UnixListener};

use crate::config::Config;
use crate::hub::Hub;
use crate::store::Store;

#[derive(Parser)]
#[command(name = "agentsmon", version, about = "Session monitor for Claude Code, Codex and other coding agents")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Run the daemon in the foreground.
    Serve {
        /// Config file (default: ~/.agentsmon/config.toml).
        #[arg(long)]
        config: Option<PathBuf>,
    },
    /// Forward an agent's hook payload (stdin) to the daemon. Always exits 0
    /// and asks the agent for nothing (prints nothing; `{}` for Codex, whose
    /// Stop hook expects JSON), so it can never get in the agent's way.
    Hook {
        /// Which agent the payload comes from: claude-code, codex, ...
        #[arg(long)]
        agent: String,
    },
    /// Show what every live session is doing.
    Status {
        #[arg(long)]
        json: bool,
    },
    /// Run the daemon at login (macOS launchd).
    Service {
        #[command(subcommand)]
        action: ServiceAction,
    },
    /// Print an example config file.
    Config,
}

#[derive(Subcommand)]
enum ServiceAction {
    /// Install the launchd agent and start it.
    Install,
    /// Stop the launchd agent and remove it.
    Uninstall,
    /// Show whether the launchd agent is loaded and running.
    Status,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let result = match cli.command {
        Command::Serve { config } => serve(config.as_deref()),
        Command::Hook { agent } => {
            hook(&agent);
            if agent == agentsmon_protocol::agents::CODEX {
                println!("{{}}");
            }
            Ok(())
        }
        Command::Status { json } => status(json),
        Command::Service { action } => match action {
            ServiceAction::Install => service::install(),
            ServiceAction::Uninstall => service::uninstall(),
            ServiceAction::Status => service::status(),
        },
        Command::Config => {
            print!("{}", config::EXAMPLE);
            Ok(())
        }
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("agentsmon: {e}");
            ExitCode::FAILURE
        }
    }
}

// ---------------------------------------------------------------------------
// hook
// ---------------------------------------------------------------------------

const HOOK_TIMEOUT: Duration = Duration::from_millis(300);
const HOOK_MAX_BYTES: u64 = 4 << 20;

/// Wraps stdin, untouched, as a raw envelope: the daemon's adapter reads it.
fn hook(agent: &str) {
    let mut input = Vec::new();
    if std::io::stdin().take(HOOK_MAX_BYTES).read_to_end(&mut input).is_err() {
        return;
    }
    let payload = input.trim_ascii();
    if !payload.starts_with(b"{") {
        return;
    }
    let mut body = Vec::with_capacity(payload.len() + 64);
    body.extend_from_slice(b"{\"agent\":");
    body.extend_from_slice(&serde_json::to_vec(agent).unwrap_or_default());
    body.extend_from_slice(b",\"type\":\"raw\",\"payload\":");
    body.extend_from_slice(payload);
    body.push(b'}');
    let _ = client::request(&config::default_socket(), "POST", "/v1/ingest", Some(&body), HOOK_TIMEOUT);
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------

fn status(json: bool) -> Result<(), String> {
    let socket = config::default_socket();
    let res = client::request(&socket, "GET", "/v1/snapshot", None, Duration::from_secs(2))
        .map_err(|e| format!("daemon not reachable at {} ({e}); start it with `agentsmon serve`", socket.display()))?;
    if res.status != 200 {
        return Err(format!("daemon answered {}", res.status));
    }
    if json {
        println!("{}", String::from_utf8_lossy(&res.body));
        return Ok(());
    }
    let snap: agentsmon_protocol::Snapshot =
        serde_json::from_slice(&res.body).map_err(|e| format!("bad snapshot: {e}"))?;
    let now = util::now_ms();
    println!(
        "agentsmon {} · mood {:?} · {} live",
        snap.server.version, snap.summary.mood, snap.summary.counts.live
    );
    for q in &snap.quotas {
        let windows: Vec<String> = q.windows.iter().map(|w| format!("{} {:.0}%", w.kind, w.percent_used)).collect();
        println!("quota {}: {}", q.agent, windows.join(", "));
    }
    for s in snap.sessions.iter().filter(|s| !s.is_ended()) {
        let project = s.meta.project.clone().unwrap_or_else(|| s.session_id.clone());
        let tool = s.tools.last().map(|t| format!(" {}", t.tool)).unwrap_or_default();
        let stale = if s.stale { " (stale)" } else { "" };
        println!(
            "  {:<12} {:<24} {:<18}{}{}  {}s",
            s.agent,
            project,
            s.status.as_str(),
            tool,
            stale,
            (now - s.status_since) / 1000
        );
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// serve
// ---------------------------------------------------------------------------

fn serve(config_path: Option<&Path>) -> Result<(), String> {
    let cfg = Config::load(config_path)?;
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_env("AGENTSMON_LOG").unwrap_or_else(|_| "info".into()),
        )
        .with_target(false)
        .init();

    // One thread is plenty: the work per event is microseconds.
    let rt = tokio::runtime::Builder::new_current_thread().enable_all().build().map_err(|e| e.to_string())?;
    rt.block_on(run(cfg))
}

async fn run(cfg: Config) -> Result<(), String> {
    std::fs::create_dir_all(&cfg.home).map_err(|e| format!("{}: {e}", cfg.home.display()))?;
    let _ = std::fs::set_permissions(&cfg.home, std::fs::Permissions::from_mode(0o700));

    let store = Store::open(&cfg.db).map_err(|e| format!("{}: {e}", cfg.db.display()))?;
    let hub = Arc::new(Hub::new(store.clone(), cfg.keep_ended_hours, cfg.retention));
    let app = api::router(hub.clone());

    // The socket: refuse to start beside a running daemon, clear a dead one's.
    if cfg.socket.exists() {
        if std::os::unix::net::UnixStream::connect(&cfg.socket).is_ok() {
            return Err(format!("already running ({})", cfg.socket.display()));
        }
        let _ = std::fs::remove_file(&cfg.socket);
    }
    let uds = UnixListener::bind(&cfg.socket).map_err(|e| format!("{}: {e}", cfg.socket.display()))?;
    let _ = std::fs::set_permissions(&cfg.socket, std::fs::Permissions::from_mode(0o600));
    tracing::info!("listening on {}", cfg.socket.display());
    let uds_app = app.clone();
    tokio::spawn(async move {
        if let Err(e) = axum::serve(uds, uds_app).await {
            tracing::error!("unix socket server: {e}");
        }
    });

    if let Some(addr) = cfg.http {
        let tcp = TcpListener::bind(addr).await.map_err(|e| format!("{addr}: {e}"))?;
        let guard = api::Guard {
            token: cfg.token.clone(),
            allowed_origins: Arc::new(cfg.allowed_origins.iter().cloned().collect::<HashSet<_>>()),
        };
        tracing::info!("listening on http://{addr}{}", if cfg.token.is_some() { " (token)" } else { "" });
        let tcp_app = api::guarded(app, guard);
        tokio::spawn(async move {
            if let Err(e) = axum::serve(tcp, tcp_app).await {
                tracing::error!("http server: {e}");
            }
        });
    }

    let sweeper = hub.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(5));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tick.tick().await;
            sweeper.sweep();
        }
    });

    // Codex's token counts, context and rate limits, from its rollout logs.
    let codex_hub = hub.clone();
    tokio::spawn(async move {
        let mut rollouts = codex::Rollouts::new();
        let mut tick = tokio::time::interval(Duration::from_secs(2));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tick.tick().await;
            let hub = codex_hub.clone();
            rollouts = match tokio::task::spawn_blocking(move || {
                rollouts.pass(&hub);
                rollouts
            })
            .await
            {
                Ok(r) => r,
                Err(e) => {
                    tracing::error!("codex collector stopped: {e}");
                    return;
                }
            };
        }
    });

    // Claude Code's token counts from its transcripts, and its account's
    // usage limits.
    let claude_hub = hub.clone();
    let refresh = cfg.claude_refresh.clone();
    tokio::spawn(async move {
        let mut readers = (claude::Transcripts::new(), claude::Limits::new(refresh));
        let mut tick = tokio::time::interval(Duration::from_secs(2));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tick.tick().await;
            let hub = claude_hub.clone();
            readers = match tokio::task::spawn_blocking(move || {
                let (mut transcripts, mut limits) = readers;
                transcripts.pass(&hub);
                limits.pass(&hub, transcripts.active_at);
                (transcripts, limits)
            })
            .await
            {
                Ok(r) => r,
                Err(e) => {
                    tracing::error!("claude collector stopped: {e}");
                    return;
                }
            };
        }
    });

    let flusher = store.clone();
    // A session deleted or archived in Claude leaves the live view within seconds.
    let quick_hub = hub.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(15));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tick.tick().await;
            let hub = quick_hub.clone();
            let _ = tokio::task::spawn_blocking(move || janitor::quick_pass(&hub)).await;
        }
    });
    // Retention and Claude Code's deletions, once at start and every ten
    // minutes; each pass is a few cheap queries when nothing changed.
    let janitor_hub = hub.clone();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(600));
        tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
        loop {
            tick.tick().await;
            let hub = janitor_hub.clone();
            let _ = tokio::task::spawn_blocking(move || janitor::run(&hub, janitor::Scope::Retention)).await;
        }
    });

    // Ctrl+C, a service manager's stop, and the terminal closing (SIGHUP)
    // all end the daemon the same orderly way.
    use tokio::signal::unix::{SignalKind, signal};
    let mut term = signal(SignalKind::terminate()).map_err(|e| e.to_string())?;
    let mut hangup = signal(SignalKind::hangup()).map_err(|e| e.to_string())?;
    let why = tokio::select! {
        _ = tokio::signal::ctrl_c() => "interrupt",
        _ = term.recv() => "terminate",
        _ = hangup.recv() => "hangup",
    };
    tracing::info!("shutting down ({why})");
    let _ = std::fs::remove_file(&cfg.socket);
    // Everything ingested so far reaches the database before the process ends.
    let flushed = tokio::task::spawn_blocking(move || flusher.flush(Duration::from_secs(3))).await.unwrap_or(false);
    if !flushed {
        tracing::warn!("store did not finish writing within 3 s");
    }
    Ok(())
}
