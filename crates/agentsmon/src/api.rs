//! HTTP and WebSocket API. The same routes serve the Unix socket (trusted:
//! owner-only file) and the TCP listener (loopback by default; a token when
//! it is not, and an Origin check against pages in a browser).

use std::collections::HashSet;
use std::sync::Arc;
use std::sync::atomic::Ordering;

use agentsmon_protocol::{Envelope, TokenReport, TokenSeries};
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket, WebSocketUpgrade};
use axum::extract::{DefaultBodyLimit, Path, Query, Request, State};
use axum::http::{HeaderMap, HeaderValue, Method, StatusCode, header};
use axum::middleware::{self, Next};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use serde::Deserialize;
use serde_json::json;
use tokio::sync::broadcast::error::RecvError;

use crate::hub::Hub;
use crate::janitor::{self, Scope};
use crate::otlp;
use crate::store::{self, TokenGroupBy};
use crate::util::now_ms;

#[derive(Clone)]
pub struct Api {
    pub hub: Arc<Hub>,
}

/// What the TCP listener lets in.
#[derive(Clone)]
pub struct Guard {
    pub token: Option<String>,
    pub allowed_origins: Arc<HashSet<String>>,
}

pub fn router(hub: Arc<Hub>) -> Router {
    Router::new()
        .route("/v1/health", get(health))
        .route("/v1/ingest", post(ingest))
        .route("/v1/snapshot", get(snapshot))
        .route("/v1/summary", get(summary))
        .route("/v1/sessions", get(sessions))
        .route("/v1/sessions/{key}", get(session).delete(forget))
        .route("/v1/sessions/{key}/events", get(session_events))
        .route("/v1/events", get(events))
        .route("/v1/quotas", get(quotas))
        .route("/v1/quotas/history", get(quota_history))
        .route("/v1/tokens", get(tokens))
        .route("/v1/tokens/series", get(token_series))
        .route("/v1/stats", get(stats))
        .route("/v1/maintenance", post(maintenance))
        .route("/v1/ws", get(ws))
        // OpenTelemetry (OTLP/HTTP JSON): agents export their token counts here.
        .route("/v1/logs", post(otlp_logs).layer(DefaultBodyLimit::max(32 << 20)))
        .route("/v1/metrics", post(otlp_metrics).layer(DefaultBodyLimit::max(32 << 20)))
        .with_state(Api { hub })
}

pub fn guarded(router: Router, guard: Guard) -> Router {
    router.layer(middleware::from_fn_with_state(guard, check))
}

fn is_loopback_origin(origin: &str) -> bool {
    let rest = origin.strip_prefix("http://").or_else(|| origin.strip_prefix("https://")).unwrap_or("");
    let host = rest.split('/').next().unwrap_or("");
    let host = host.rsplit_once(':').map_or(host, |(h, port)| if port.chars().all(|c| c.is_ascii_digit()) { h } else { host });
    matches!(host, "localhost" | "127.0.0.1" | "[::1]")
}

async fn check(State(guard): State<Guard>, req: Request, next: Next) -> Response {
    let headers = req.headers();
    // A page in a browser always says where it is from; other clients do not.
    let origin = headers.get(header::ORIGIN).and_then(|o| o.to_str().ok()).map(str::to_string);
    if let Some(o) = &origin
        && !is_loopback_origin(o)
        && !guard.allowed_origins.contains(o)
    {
        return (StatusCode::FORBIDDEN, "origin not allowed").into_response();
    }
    if let Some(token) = &guard.token {
        let bearer = headers
            .get(header::AUTHORIZATION)
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.strip_prefix("Bearer "));
        let query = req.uri().query().and_then(|q| {
            q.split('&').find_map(|kv| kv.strip_prefix("token="))
        });
        if bearer.or(query) != Some(token.as_str()) && req.uri().path() != "/v1/health" {
            return (StatusCode::UNAUTHORIZED, "token required").into_response();
        }
    }
    if req.method() == Method::OPTIONS {
        return with_cors(StatusCode::NO_CONTENT.into_response(), origin.as_deref());
    }
    let res = next.run(req).await;
    with_cors(res, origin.as_deref())
}

fn with_cors(mut res: Response, origin: Option<&str>) -> Response {
    if let Some(o) = origin.and_then(|o| HeaderValue::from_str(o).ok()) {
        let h = res.headers_mut();
        h.insert(header::ACCESS_CONTROL_ALLOW_ORIGIN, o);
        h.insert(header::ACCESS_CONTROL_ALLOW_METHODS, HeaderValue::from_static("GET, POST, DELETE"));
        h.insert(header::ACCESS_CONTROL_ALLOW_HEADERS, HeaderValue::from_static("authorization, content-type"));
        h.insert(header::VARY, HeaderValue::from_static("origin"));
    }
    res
}

fn internal(e: impl std::fmt::Display) -> Response {
    (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()).into_response()
}

async fn health(State(api): State<Api>) -> Json<serde_json::Value> {
    Json(json!({
        "ok": true,
        "name": api.hub.info.name,
        "version": api.hub.info.version,
        "protocol": api.hub.info.protocol,
        "uptime_ms": now_ms() - api.hub.info.started_at,
    }))
}

/// Takes one envelope or an array of them. Never fails a batch for one bad
/// event: the report says which were rejected.
async fn ingest(State(api): State<Api>, body: Bytes) -> Response {
    let batch: Vec<Envelope> = match body.first().copied() {
        Some(b'[') => match serde_json::from_slice(&body) {
            Ok(b) => b,
            // One bad element must not cost the others.
            Err(_) => match serde_json::from_slice::<Vec<serde_json::Value>>(&body) {
                Ok(values) => values.into_iter().filter_map(|v| serde_json::from_value(v).ok()).collect(),
                Err(e) => return (StatusCode::BAD_REQUEST, e.to_string()).into_response(),
            },
        },
        _ => match serde_json::from_slice(&body) {
            Ok(one) => vec![one],
            Err(e) => return (StatusCode::BAD_REQUEST, e.to_string()).into_response(),
        },
    };
    Json(api.hub.ingest(batch)).into_response()
}

async fn snapshot(State(api): State<Api>) -> Response {
    Json(api.hub.snapshot()).into_response()
}

// Stamped with the time it is read: a display polling it keeps its clock by
// `ts`, and the summary itself is only rebuilt when something changed
async fn summary(State(api): State<Api>) -> Response {
    let mut summary = api.hub.summary();
    summary.ts = now_ms();
    Json(summary).into_response()
}

#[derive(Deserialize)]
struct SessionsQuery {
    /// `live` (default: running and recently ended) or `all` (the history).
    scope: Option<String>,
    agent: Option<String>,
    limit: Option<u32>,
    offset: Option<u32>,
}

async fn sessions(State(api): State<Api>, Query(q): Query<SessionsQuery>) -> Response {
    let limit = q.limit.unwrap_or(100).min(1000);
    if q.scope.as_deref() == Some("all") {
        let hub = api.hub.clone();
        let offset = q.offset.unwrap_or(0);
        return match tokio::task::spawn_blocking(move || {
            hub.store().read(|c| store::list_sessions(c, q.agent.as_deref(), limit, offset))
        })
        .await
        {
            Ok(Ok(list)) => {
                // The store lags the hub by a batch; prefer the hub's copy.
                let list: Vec<_> = list.into_iter().map(|s| api.hub.session(&s.key).unwrap_or(s)).collect();
                Json(list).into_response()
            }
            Ok(Err(e)) => internal(e),
            Err(e) => internal(e),
        };
    }
    let list: Vec<_> = api
        .hub
        .sessions()
        .into_iter()
        .filter(|s| q.agent.as_deref().is_none_or(|a| a == s.agent))
        .take(limit as usize)
        .collect();
    Json(list).into_response()
}

async fn session(State(api): State<Api>, Path(key): Path<String>) -> Response {
    if let Some(s) = api.hub.session(&key) {
        return Json(s).into_response();
    }
    let hub = api.hub.clone();
    match tokio::task::spawn_blocking(move || hub.store().read(|c| store::session(c, &key))).await {
        Ok(Ok(Some(s))) => Json(s).into_response(),
        Ok(Ok(None)) => StatusCode::NOT_FOUND.into_response(),
        Ok(Err(e)) => internal(e),
        Err(e) => internal(e),
    }
}

async fn forget(State(api): State<Api>, Path(key): Path<String>) -> Response {
    api.hub.forget(&key);
    StatusCode::NO_CONTENT.into_response()
}

#[derive(Deserialize)]
struct EventsQuery {
    before: Option<u64>,
    limit: Option<u32>,
    key: Option<String>,
}

async fn read_events(api: Api, key: Option<String>, q: EventsQuery) -> Response {
    let limit = q.limit.unwrap_or(200).min(2000);
    let hub = api.hub.clone();
    match tokio::task::spawn_blocking(move || hub.store().read(|c| store::events(c, key.as_deref(), q.before, limit))).await {
        Ok(Ok(list)) => Json(list).into_response(),
        Ok(Err(e)) => internal(e),
        Err(e) => internal(e),
    }
}

async fn session_events(State(api): State<Api>, Path(key): Path<String>, Query(q): Query<EventsQuery>) -> Response {
    read_events(api, Some(key), q).await
}

async fn events(State(api): State<Api>, Query(mut q): Query<EventsQuery>) -> Response {
    let key = q.key.take();
    read_events(api, key, q).await
}

async fn quotas(State(api): State<Api>) -> Response {
    Json(api.hub.quotas()).into_response()
}

#[derive(Deserialize)]
struct QuotaHistoryQuery {
    agent: Option<String>,
    /// How far back, in hours (default a week).
    hours: Option<u32>,
}

async fn quota_history(State(api): State<Api>, Query(q): Query<QuotaHistoryQuery>) -> Response {
    let since = now_ms() - q.hours.unwrap_or(168).min(24 * 90) as i64 * 3_600_000;
    let hub = api.hub.clone();
    match tokio::task::spawn_blocking(move || hub.store().read(|c| store::quota_history(c, q.agent.as_deref(), since))).await {
        Ok(Ok(list)) => Json(list).into_response(),
        Ok(Err(e)) => internal(e),
        Err(e) => internal(e),
    }
}

fn is_json(headers: &HeaderMap) -> bool {
    headers
        .get(header::CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .is_none_or(|ct| ct.starts_with("application/json"))
}

const OTLP_JSON_ONLY: &str =
    "agentsmon reads OTLP as JSON: set OTEL_EXPORTER_OTLP_PROTOCOL=http/json (and no compression)";

/// OTLP logs: the per-request token counts are kept, everything else dropped.
async fn otlp_logs(State(api): State<Api>, headers: HeaderMap, body: Bytes) -> Response {
    if !is_json(&headers) || headers.contains_key(header::CONTENT_ENCODING) {
        tracing::warn!("{OTLP_JSON_ONLY}");
        return (StatusCode::UNSUPPORTED_MEDIA_TYPE, OTLP_JSON_ONLY).into_response();
    }
    let value: serde_json::Value = match serde_json::from_slice(&body) {
        Ok(v) => v,
        Err(e) => return (StatusCode::BAD_REQUEST, e.to_string()).into_response(),
    };
    let export = otlp::usage_from_logs(&value, now_ms());
    api.hub.note_otlp(&export.services);
    if !export.records.is_empty() {
        api.hub.ingest_usage(export.records);
    }
    Json(json!({ "partialSuccess": {} })).into_response()
}

/// OTLP metrics are not used (the logs carry each request); accepted so an
/// exporter pointed here does not report errors.
async fn otlp_metrics(headers: HeaderMap) -> Response {
    if !is_json(&headers) || headers.contains_key(header::CONTENT_ENCODING) {
        return (StatusCode::UNSUPPORTED_MEDIA_TYPE, OTLP_JSON_ONLY).into_response();
    }
    Json(json!({ "partialSuccess": {} })).into_response()
}

#[derive(Deserialize)]
struct TokensQuery {
    /// Window start, ms since the epoch (default: the last 24 hours).
    since: Option<i64>,
    until: Option<i64>,
    /// `agent`, `model`, `source` or `session`.
    group: Option<String>,
    bucket_ms: Option<i64>,
    /// Only this agent's requests (`claude-code`, `codex`, ...).
    agent: Option<String>,
}

impl TokensQuery {
    fn window(&self) -> (i64, i64) {
        let until = self.until.unwrap_or_else(|| now_ms() + 1);
        (self.since.unwrap_or(until - 86_400_000).max(0), until)
    }
}

async fn tokens(State(api): State<Api>, Query(q): Query<TokensQuery>) -> Response {
    let (since, until) = q.window();
    let group = match q.group.as_deref().map(TokenGroupBy::parse) {
        Some(None) => return (StatusCode::BAD_REQUEST, "group: agent, model, source or session").into_response(),
        Some(g) => g,
        None => None,
    };
    let hub = api.hub.clone();
    let read = tokio::task::spawn_blocking(move || {
        let agent = q.agent.as_deref().filter(|a| !a.is_empty());
        hub.store().read(|c| Ok((store::token_report(c, since, until, group, agent)?, store::last_usage_ts(c, agent)?)))
    })
    .await;
    match read {
        Ok(Ok(((totals, groups), last_ts))) => Json(TokenReport { since, until, totals, groups, last_ts }).into_response(),
        Ok(Err(e)) => internal(e),
        Err(e) => internal(e),
    }
}

async fn token_series(State(api): State<Api>, Query(q): Query<TokensQuery>) -> Response {
    let (since, until) = q.window();
    // At most 2000 buckets, none shorter than a minute.
    let bucket = q.bucket_ms.unwrap_or(3_600_000).max(60_000).max((until - since) / 2000);
    let hub = api.hub.clone();
    let read = move || {
        let agent = q.agent.as_deref().filter(|a| !a.is_empty());
        hub.store().read(|c| store::token_series(c, since, until, bucket, agent))
    };
    match tokio::task::spawn_blocking(read).await {
        Ok(Ok(points)) => Json(TokenSeries { since, bucket_ms: bucket, points }).into_response(),
        Ok(Err(e)) => internal(e),
        Err(e) => internal(e),
    }
}

#[derive(Deserialize)]
struct MaintenanceQuery {
    /// `all` to forget every ended session too, not only what the
    /// retention says.
    ended: Option<String>,
}

/// Runs a cleanup pass now and says what it did.
async fn maintenance(State(api): State<Api>, Query(q): Query<MaintenanceQuery>) -> Response {
    let scope = match q.ended.as_deref() {
        None => Scope::Retention,
        Some("all") => Scope::AllEnded,
        Some(_) => return (StatusCode::BAD_REQUEST, "ended: all, or leave it out").into_response(),
    };
    let hub = api.hub.clone();
    match tokio::task::spawn_blocking(move || janitor::run(&hub, scope)).await {
        Ok(report) => Json(report).into_response(),
        Err(e) => internal(e),
    }
}

async fn stats(State(api): State<Api>) -> Response {
    let hub = api.hub.clone();
    let db = tokio::task::spawn_blocking(move || hub.store().read(store::counts)).await.ok().and_then(Result::ok);
    let m = &api.hub.metrics;
    Json(json!({
        "uptime_ms": now_ms() - api.hub.info.started_at,
        "events_in": m.events_in.load(Ordering::Relaxed),
        "batches_in": m.batches_in.load(Ordering::Relaxed),
        "rejected": m.rejected.load(Ordering::Relaxed),
        "usage_in": m.usage_in.load(Ordering::Relaxed),
        "otlp": {
            "exports": m.otlp_exports.load(Ordering::Relaxed),
            "services": api.hub.otlp_services(),
        },
        "usage_duplicates": m.usage_duplicates.load(Ordering::Relaxed),
        "ws_clients": m.ws_clients.load(Ordering::Relaxed),
        "sessions_in_memory": api.hub.sessions().len(),
        "sessions_live": api.hub.live_count(),
        "sessions_hidden": api.hub.hidden_count(),
        "db": db,
        "db_bytes": api.hub.store().disk_bytes(),
        "retention": api.hub.retention,
        "live": api.hub.live,
        "rss_bytes": crate::sys::rss_bytes(),
        "cpu_ms": crate::sys::cpu_ms(),
    }))
    .into_response()
}

#[derive(Deserialize)]
struct WsQuery {
    /// `1` to also receive every history event as it happens.
    feed: Option<u8>,
}

async fn ws(State(api): State<Api>, Query(q): Query<WsQuery>, upgrade: WebSocketUpgrade, _h: HeaderMap) -> Response {
    let feed = q.feed == Some(1);
    upgrade.on_upgrade(move |socket| client(socket, api.hub, feed))
}

async fn client(mut socket: WebSocket, hub: Arc<Hub>, feed: bool) {
    hub.metrics.ws_clients.fetch_add(1, Ordering::Relaxed);
    // Subscribe before the snapshot so nothing falls between them.
    let mut rx = hub.subscribe();
    if socket.send(Message::Text(hub.snapshot_message())).await.is_ok() {
        loop {
            tokio::select! {
                msg = rx.recv() => match msg {
                    Ok(o) => {
                        if o.feed && !feed { continue; }
                        if socket.send(Message::Text(o.text)).await.is_err() { break; }
                    }
                    // Too slow to keep up: start it over from a snapshot.
                    Err(RecvError::Lagged(_)) => {
                        if socket.send(Message::Text(hub.snapshot_message())).await.is_err() { break; }
                    }
                    Err(RecvError::Closed) => break,
                },
                incoming = socket.recv() => match incoming {
                    Some(Ok(Message::Close(_))) | None | Some(Err(_)) => break,
                    _ => {}
                },
            }
        }
    }
    hub.metrics.ws_clients.fetch_sub(1, Ordering::Relaxed);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loopback_origins() {
        assert!(is_loopback_origin("http://localhost:5173"));
        assert!(is_loopback_origin("http://127.0.0.1"));
        assert!(is_loopback_origin("http://[::1]:4173"));
        assert!(!is_loopback_origin("https://evil.example"));
        assert!(!is_loopback_origin("http://localhost.evil.example"));
        assert!(!is_loopback_origin("null"));
    }
}
