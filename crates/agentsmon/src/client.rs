//! A blocking HTTP/1.1 client over the Unix socket, for the short-lived
//! commands (`hook`, `status`). No async runtime: a hook pays only for a
//! connect, one write and one read.

use std::io::{self, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::Path;
use std::time::Duration;

pub struct Response {
    pub status: u16,
    pub body: Vec<u8>,
}

pub fn request(socket: &Path, method: &str, path: &str, body: Option<&[u8]>, timeout: Duration) -> io::Result<Response> {
    let mut stream = UnixStream::connect(socket)?;
    stream.set_read_timeout(Some(timeout))?;
    stream.set_write_timeout(Some(timeout))?;

    let mut head = format!("{method} {path} HTTP/1.1\r\nHost: agentsmon\r\nConnection: close\r\n");
    if let Some(b) = body {
        head.push_str(&format!("Content-Type: application/json\r\nContent-Length: {}\r\n", b.len()));
    }
    head.push_str("\r\n");
    let mut req = head.into_bytes();
    if let Some(b) = body {
        req.extend_from_slice(b);
    }
    stream.write_all(&req)?;

    let mut raw = Vec::with_capacity(4096);
    stream.read_to_end(&mut raw)?;
    parse(&raw)
}

fn bad(what: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, what.to_string())
}

fn parse(raw: &[u8]) -> io::Result<Response> {
    let split = raw.windows(4).position(|w| w == b"\r\n\r\n").ok_or_else(|| bad("no header end"))?;
    let head = std::str::from_utf8(&raw[..split]).map_err(|_| bad("header not utf-8"))?;
    let status = head
        .split_whitespace()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .ok_or_else(|| bad("no status"))?;
    let rest = &raw[split + 4..];
    let chunked = head
        .lines()
        .any(|l| l.to_ascii_lowercase().starts_with("transfer-encoding:") && l.to_ascii_lowercase().contains("chunked"));
    let body = if chunked { dechunk(rest)? } else { rest.to_vec() };
    Ok(Response { status, body })
}

fn dechunk(mut rest: &[u8]) -> io::Result<Vec<u8>> {
    let mut out = Vec::new();
    loop {
        let line_end = rest.windows(2).position(|w| w == b"\r\n").ok_or_else(|| bad("chunk size"))?;
        let size_text = std::str::from_utf8(&rest[..line_end]).map_err(|_| bad("chunk size"))?;
        let size = usize::from_str_radix(size_text.split(';').next().unwrap_or("").trim(), 16).map_err(|_| bad("chunk size"))?;
        rest = &rest[line_end + 2..];
        if size == 0 {
            return Ok(out);
        }
        if rest.len() < size + 2 {
            return Err(bad("short chunk"));
        }
        out.extend_from_slice(&rest[..size]);
        rest = &rest[size + 2..];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_plain_and_chunked() {
        let r = parse(b"HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nhi").unwrap();
        assert_eq!((r.status, r.body.as_slice()), (200, &b"hi"[..]));
        let r = parse(b"HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n").unwrap();
        assert_eq!((r.status, r.body.as_slice()), (201, &b"hello"[..]));
    }
}
