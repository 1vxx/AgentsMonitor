//! The OpenTelemetry (OTLP/HTTP, JSON encoding) door: agents export their
//! own per-request token counts here. Only what [`adapters::usage_from_otel`]
//! recognises is kept; every other log record and all metrics are dropped.

use std::collections::HashMap;

use agentsmon_protocol::UsageRecord;
use serde_json::Value;

use crate::adapters;

/// An OTLP `AnyValue`, flattened to what attributes carry.
#[derive(Clone, Debug, PartialEq)]
pub enum AttrValue {
    Str(String),
    Int(i64),
    Float(f64),
    Bool(bool),
}

impl AttrValue {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            AttrValue::Str(s) => Some(s),
            _ => None,
        }
    }

    /// Integer attributes; OTLP JSON spells int64 as a string or a number.
    pub fn as_u64(&self) -> Option<u64> {
        match self {
            AttrValue::Int(n) => u64::try_from(*n).ok(),
            AttrValue::Float(f) if *f >= 0.0 && f.fract() == 0.0 => Some(*f as u64),
            AttrValue::Str(s) => s.parse().ok(),
            _ => None,
        }
    }
}

pub type Attrs = HashMap<String, AttrValue>;

fn any_value(v: &Value) -> Option<AttrValue> {
    if let Some(s) = v.get("stringValue").and_then(Value::as_str) {
        return Some(AttrValue::Str(s.to_string()));
    }
    if let Some(i) = v.get("intValue") {
        return i.as_i64().or_else(|| i.as_str().and_then(|s| s.parse().ok())).map(AttrValue::Int);
    }
    if let Some(f) = v.get("doubleValue").and_then(Value::as_f64) {
        return Some(AttrValue::Float(f));
    }
    v.get("boolValue").and_then(Value::as_bool).map(AttrValue::Bool)
}

fn attrs(list: Option<&Value>) -> Attrs {
    list.and_then(Value::as_array)
        .map(|items| {
            items
                .iter()
                .filter_map(|kv| Some((kv.get("key")?.as_str()?.to_string(), any_value(kv.get("value")?)?)))
                .collect()
        })
        .unwrap_or_default()
}

/// Nanoseconds since the epoch, as a string or a number, to milliseconds.
fn nanos_to_ms(v: Option<&Value>) -> Option<i64> {
    let v = v?;
    let n: u128 = v.as_u64().map(u128::from).or_else(|| v.as_str().and_then(|s| s.parse().ok()))?;
    (n > 0).then_some((n / 1_000_000) as i64)
}

/// What one OTLP logs export held.
#[derive(Debug, Default)]
pub struct LogsExport {
    pub records: Vec<UsageRecord>,
    /// Every `service.name` in the export, recognised or not.
    pub services: Vec<String>,
}

/// Reads the token usage records out of an OTLP logs export.
pub fn usage_from_logs(body: &Value, now: i64) -> LogsExport {
    let mut out = LogsExport::default();
    for resource in body.get("resourceLogs").and_then(Value::as_array).into_iter().flatten() {
        let resource_attrs = attrs(resource.pointer("/resource/attributes"));
        let service = resource_attrs.get("service.name").and_then(AttrValue::as_str).unwrap_or("");
        if !out.services.iter().any(|s| s == service) {
            out.services.push(service.to_string());
        }
        for scope in resource.get("scopeLogs").and_then(Value::as_array).into_iter().flatten() {
            let scope_name = scope.pointer("/scope/name").and_then(Value::as_str).unwrap_or("");
            for record in scope.get("logRecords").and_then(Value::as_array).into_iter().flatten() {
                let ts = nanos_to_ms(record.get("timeUnixNano"))
                    .or_else(|| nanos_to_ms(record.get("observedTimeUnixNano")))
                    .unwrap_or(now)
                    .min(now);
                let body_name = record.pointer("/body/stringValue").and_then(Value::as_str);
                let source = adapters::OtelSource { service, scope: scope_name, body: body_name };
                if let Some(u) = adapters::usage_from_otel(&source, &attrs(record.get("attributes")), ts) {
                    out.records.push(u);
                }
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// The shape Claude Code 2.1 exports, trimmed to what matters.
    fn export(request_id: &str, input: Value) -> Value {
        json!({"resourceLogs": [{
            "resource": {"attributes": [{"key": "service.name", "value": {"stringValue": "claude-code"}}]},
            "scopeLogs": [{"scope": {"name": "com.anthropic.claude_code.events"}, "logRecords": [
                {"timeUnixNano": "1793089371166000000", "body": {"stringValue": "claude_code.user_prompt"},
                 "attributes": [{"key": "event.name", "value": {"stringValue": "user_prompt"}}]},
                {"timeUnixNano": "1793089371166000000", "body": {"stringValue": "claude_code.api_request"},
                 "attributes": [
                    {"key": "session.id", "value": {"stringValue": "259b65b4"}},
                    {"key": "event.name", "value": {"stringValue": "api_request"}},
                    {"key": "model", "value": {"stringValue": "claude-haiku-5-5"}},
                    {"key": "input_tokens", "value": {"intValue": input}},
                    {"key": "output_tokens", "value": {"intValue": 4}},
                    {"key": "cache_read_tokens", "value": {"intValue": 23750}},
                    {"key": "cache_creation_tokens", "value": {"intValue": "12368"}},
                    {"key": "cost_usd", "value": {"doubleValue": 0.0027}},
                    {"key": "request_id", "value": {"stringValue": request_id}},
                    {"key": "query_source", "value": {"stringValue": "sdk"}}
                 ]}
            ]}]
        }]})
    }

    #[test]
    fn reads_claude_codes_api_requests_only() {
        let export = usage_from_logs(&export("req_1", json!(2)), i64::MAX);
        assert_eq!(export.services, vec!["claude-code".to_string()]);
        let out = export.records;
        assert_eq!(out.len(), 1);
        let u = &out[0];
        assert_eq!(u.request_id, "req_1");
        assert_eq!(u.ts, 1_793_089_371_166);
        assert_eq!(u.session_id.as_deref(), Some("259b65b4"));
        assert_eq!((u.input, u.output, u.cache_read, u.cache_write), (2, 4, 23_750, 12_368));
        assert_eq!(u.source.as_deref(), Some("sdk"));
    }

    #[test]
    fn int64_may_be_a_string() {
        assert_eq!(usage_from_logs(&export("r", json!("7")), i64::MAX).records[0].input, 7);
    }

    #[test]
    fn the_desktop_apps_service_name_is_claude_code_too() {
        // Claude Desktop runs Claude Code with OTEL_SERVICE_NAME=claude-code-desktop.
        let mut body = export("d", json!(1));
        body["resourceLogs"][0]["resource"]["attributes"][0]["value"]["stringValue"] = json!("claude-code-desktop");
        let export = usage_from_logs(&body, i64::MAX);
        assert_eq!(export.records.len(), 1);
        assert_eq!(export.records[0].agent, "claude-code");
    }

    #[test]
    fn the_events_scope_identifies_claude_code_under_any_service_name() {
        let mut body = export("s", json!(1));
        body["resourceLogs"][0]["resource"]["attributes"][0]["value"]["stringValue"] = json!("my-renamed-service");
        assert_eq!(usage_from_logs(&body, i64::MAX).records.len(), 1);
    }

    #[test]
    fn unknown_services_and_junk_are_ignored() {
        let mut body = export("r", json!(1));
        body["resourceLogs"][0]["resource"]["attributes"][0]["value"]["stringValue"] = json!("something-else");
        body["resourceLogs"][0]["scopeLogs"][0]["scope"]["name"] = json!("other.scope");
        let export = usage_from_logs(&body, i64::MAX);
        assert!(export.records.is_empty());
        assert_eq!(export.services, vec!["something-else".to_string()]);
        assert!(usage_from_logs(&json!({"nope": 1}), 0).records.is_empty());
    }
}
