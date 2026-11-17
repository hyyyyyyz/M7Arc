//! A small JSON writer for the machine-readable output.

use std::fmt::Write;

#[derive(Debug, Clone, PartialEq)]
pub enum Json {
    Null,
    Bool(bool),
    Int(i64),
    UInt(u64),
    Str(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

impl Json {
    pub fn object() -> Json {
        Json::Object(Vec::new())
    }

    /// Appends a member (objects only).
    pub fn with(mut self, key: &str, value: impl Into<Json>) -> Json {
        if let Json::Object(members) = &mut self {
            members.push((key.to_string(), value.into()));
        }
        self
    }

    pub fn strings<S: AsRef<str>>(items: &[S]) -> Json {
        Json::Array(items.iter().map(|s| Json::Str(s.as_ref().to_string())).collect())
    }

    pub fn render(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }

    fn write(&self, out: &mut String) {
        match self {
            Json::Null => out.push_str("null"),
            Json::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Json::Int(n) => write!(out, "{n}").unwrap(),
            Json::UInt(n) => write!(out, "{n}").unwrap(),
            Json::Str(s) => write_string(s, out),
            Json::Array(items) => {
                out.push('[');
                for (i, item) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    item.write(out);
                }
                out.push(']');
            }
            Json::Object(members) => {
                out.push('{');
                for (i, (key, value)) in members.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    write_string(key, out);
                    out.push(':');
                    value.write(out);
                }
                out.push('}');
            }
        }
    }
}

fn write_string(s: &str, out: &mut String) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => write!(out, "\\u{:04x}", c as u32).unwrap(),
            c => out.push(c),
        }
    }
    out.push('"');
}

impl From<bool> for Json {
    fn from(v: bool) -> Json {
        Json::Bool(v)
    }
}
impl From<u64> for Json {
    fn from(v: u64) -> Json {
        Json::UInt(v)
    }
}
impl From<u32> for Json {
    fn from(v: u32) -> Json {
        Json::UInt(v as u64)
    }
}
impl From<i64> for Json {
    fn from(v: i64) -> Json {
        Json::Int(v)
    }
}
impl From<&str> for Json {
    fn from(v: &str) -> Json {
        Json::Str(v.to_string())
    }
}
impl From<String> for Json {
    fn from(v: String) -> Json {
        Json::Str(v)
    }
}
impl From<Vec<Json>> for Json {
    fn from(v: Vec<Json>) -> Json {
        Json::Array(v)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renders_and_escapes() {
        let value = Json::object()
            .with("path", "a \"b\"\n\\c\u{1}")
            .with("size", 12u64)
            .with("neg", -3i64)
            .with("ok", true)
            .with("list", Json::strings(&["x", "中文"]))
            .with("none", Json::Null);
        assert_eq!(
            value.render(),
            r#"{"path":"a \"b\"\n\\c\u0001","size":12,"neg":-3,"ok":true,"list":["x","中文"],"none":null}"#
        );
    }
}
