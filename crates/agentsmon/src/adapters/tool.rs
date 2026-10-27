use serde_json::Value;

use crate::util::preview;

const SUBJECT_CHARS: usize = 160;

/// What a tool call is about, for a one-line label: the command, the file,
/// the pattern, the URL. Mirrors `subjectOf` in the Claude Code plugin.
pub fn subject_for(tool: &str, input: &Value) -> Option<String> {
    let field = |name: &str| input.get(name).and_then(Value::as_str);
    let text = match tool {
        "Bash" | "shell" | "exec_command" => field("command").or(field("cmd")),
        "Read" | "Write" | "Edit" | "MultiEdit" | "NotebookEdit" => field("file_path").or(field("notebook_path")),
        "Grep" | "Glob" => field("pattern"),
        "WebFetch" => field("url"),
        "WebSearch" => field("query"),
        "Agent" | "Task" => field("description"),
        "Skill" => field("skill"),
        _ if is_question(tool) => input
            .get("questions")
            .and_then(|q| q.get(0))
            .and_then(|q| ["question", "title", "header"].into_iter().find_map(|k| q.get(k).and_then(Value::as_str))),
        // Codex's patches name their files in the patch text.
        "apply_patch" => field("command").or(field("input")).and_then(|patch| {
            patch.lines().find_map(|l| {
                ["*** Update File: ", "*** Add File: ", "*** Delete File: "].into_iter().find_map(|p| l.strip_prefix(p))
            })
        }),
        _ if tool.starts_with("mcp__") => tool.split("__").nth(1),
        _ => ["file_path", "path", "command", "pattern", "url", "query", "description"]
            .into_iter()
            .find_map(field),
    }?;
    preview(text, SUBJECT_CHARS)
}

/// Tools that ask the person a question: Claude Code's, and Codex's (also
/// reached through MCP, `mcp__<server>__request_user_input`).
pub fn is_question(tool: &str) -> bool {
    matches!(
        tool.rsplit("__").next().unwrap_or(tool),
        "AskUserQuestion" | "request_user_input" | "request_user_input_async"
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn picks_the_meaningful_field() {
        assert_eq!(subject_for("Bash", &json!({"command": "ls -la"})).as_deref(), Some("ls -la"));
        assert_eq!(subject_for("Edit", &json!({"file_path": "/a/b.rs"})).as_deref(), Some("/a/b.rs"));
        assert_eq!(subject_for("mcp__github__get_pr", &json!({})).as_deref(), Some("github"));
        assert_eq!(subject_for("Mystery", &json!({"path": "x"})).as_deref(), Some("x"));
        assert_eq!(subject_for("Mystery", &json!({})), None);
    }

    #[test]
    fn reads_codex_tools() {
        let patch = "*** Begin Patch\n*** Add File: a.txt\n+x\n*** End Patch\n";
        assert_eq!(subject_for("apply_patch", &json!({"command": patch})).as_deref(), Some("a.txt"));
        assert_eq!(subject_for("apply_patch", &json!({"command": "junk"})), None);
        let asked = json!({"questions": [{"title": "Which one?"}]});
        assert_eq!(subject_for("request_user_input_async", &asked).as_deref(), Some("Which one?"));
        let asked = json!({"questions": [{"header": "Pick", "question": "Which one?"}]});
        assert_eq!(subject_for("request_user_input", &asked).as_deref(), Some("Which one?"));
        assert!(is_question("mcp__codex_apps__request_user_input") && !is_question("exec_command"));
    }
}
