#![allow(dead_code)]

#[path = "mod_crypto.rs"]
mod mod_crypto;

use serde::Serialize;
use std::ffi::OsStr;
use std::fs::{self, File};
use std::io::{self, BufRead, Cursor, Write};
use std::path::{Component, Path, PathBuf};
use zip::unstable::write::FileOptionsExt;
use zip::write::FileOptions;
use zip::{CompressionMethod, ZipWriter};

const COMPONENT_PASSWORD: &[u8] = b"1337devkey";

#[derive(Serialize)]
pub struct Roadmap {
    header: &'static str,
    exec: String,
    name: String,
    callby: String,
    version: String,
    description: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    author: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    license: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    default_args: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    env: Option<std::collections::HashMap<String, String>>,
}

#[allow(dead_code)]
pub fn create_interactive<R: BufRead, W: Write>(
    directory: &Path,
    input: &mut R,
    output: &mut W,
) -> io::Result<PathBuf> {
    let directory = directory.canonicalize()?;
    if !directory.is_dir() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "component path must be a directory",
        ));
    }

    let exec_input = prompt(input, output, "Main file path (relative to directory): ")?;
    let exec_file = {
        let path = PathBuf::from(exec_input);
        if path.is_absolute() {
            path
        } else {
            directory.join(path)
        }
    }
    .canonicalize()?;
    let exec = relative_exec(&directory, &exec_file)?;
    let roadmap = Roadmap {
        header: "rdmapv2",
        exec,
        name: prompt(input, output, "Component name: ")?,
        callby: prompt(input, output, "Command name (callby): ")?,
        version: prompt(input, output, "Version: ")?,
        description: prompt(input, output, "Description: ")?,
        author: None,
        license: None,
        default_args: None,
        env: None,
    };
    validate_callby(&roadmap.callby)?;
    let package = create_package(&directory, &roadmap)?;
    writeln!(output, "Created component: {}", package.display())?;
    Ok(package)
}

#[allow(dead_code)]
pub fn create_from_fields(
    directory: &Path,
    main_file: &Path,
    name: String,
    callby: String,
    version: String,
    description: String,
) -> io::Result<PathBuf> {
    create_from_fields_v2(
        directory,
        main_file,
        name,
        callby,
        version,
        description,
        None,
        None,
        None,
        None,
    )
}

#[allow(dead_code)]
pub fn create_from_fields_v2(
    directory: &Path,
    main_file: &Path,
    name: String,
    callby: String,
    version: String,
    description: String,
    author: Option<String>,
    license: Option<String>,
    default_args: Option<Vec<String>>,
    env: Option<std::collections::HashMap<String, String>>,
) -> io::Result<PathBuf> {
    let directory = directory.canonicalize()?;
    if !directory.is_dir() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "component path must be a directory",
        ));
    }
    let main_file = if main_file.is_absolute() {
        main_file.to_path_buf()
    } else {
        directory.join(main_file)
    }
    .canonicalize()?;
    if !main_file.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "main component path must be a file",
        ));
    }
    let roadmap = Roadmap {
        header: "rdmapv2",
        exec: relative_exec(&directory, &main_file)?,
        name: required_field(name, "component name")?,
        callby: required_field(callby, "callby")?,
        version: required_field(version, "version")?,
        description: required_field(description, "description")?,
        author,
        license,
        default_args,
        env,
    };
    validate_callby(&roadmap.callby)?;
    create_package(&directory, &roadmap)
}

pub const RMOD_MAGIC: &[u8; 8] = b"RMODv6\0\xFF";

fn write_mod_package(
    roadmap: &Roadmap,
    payload: &[u8],
    output_mod_path: &Path,
) -> io::Result<PathBuf> {
    let manifest_bytes = serde_json::to_vec_pretty(roadmap)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    if manifest_bytes.is_empty() || manifest_bytes.len() > 64 * 1024 {
        return Err(invalid_input(
            "ReTerm module manifest exceeds the 64 KiB limit",
        ));
    }
    if payload.is_empty() || payload.len() > 512 * 1024 * 1024 {
        return Err(invalid_input(
            "ReTerm module payload is empty or exceeds the 512 MiB limit",
        ));
    }
    let mut archive = ZipWriter::new(Cursor::new(Vec::new()));
    let options = FileOptions::default().compression_method(CompressionMethod::Deflated);
    archive
        .start_file("find.rdmap", options)
        .map_err(zip_error)?;
    archive.write_all(&manifest_bytes)?;
    archive
        .start_file(&roadmap.exec, options)
        .map_err(zip_error)?;
    archive.write_all(payload)?;
    let archive_bytes = archive.finish().map_err(zip_error)?.into_inner();
    let encrypted_container = mod_crypto::encrypt_mod_payload(&archive_bytes)?;

    let mut mod_bytes = Vec::with_capacity(RMOD_MAGIC.len() + encrypted_container.len());
    mod_bytes.extend_from_slice(RMOD_MAGIC);
    mod_bytes.extend_from_slice(&encrypted_container);
    if let Some(parent) = output_mod_path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)?;
    }
    fs::write(output_mod_path, &mod_bytes)?;
    Ok(output_mod_path.to_path_buf())
}

// ==================== Legacy source translators ====================

// --- Token type ---
#[derive(Debug, Clone, PartialEq)]
enum Tok {
    Str(String),
    Num(String),
    Id(String),
    Punc(char),
    Op(String),
    Nl,
}

// --- Tokenizer ---
fn tokenize(source: &str) -> Vec<Tok> {
    let ch: Vec<char> = source.chars().collect();
    let mut p = 0;
    let mut out = Vec::new();
    while p < ch.len() {
        match ch[p] {
            ' ' | '\t' | '\r' => {
                p += 1;
            }
            '\n' => {
                out.push(Tok::Nl);
                p += 1;
            }
            '"' | '\'' => {
                let q = ch[p];
                p += 1;
                // triple-quote check
                let triple = p + 1 < ch.len() && ch[p] == q && ch[p + 1] == q;
                let mut s = String::new();
                if triple {
                    p += 2;
                    while p + 2 < ch.len() {
                        if ch[p] == q && ch[p + 1] == q && ch[p + 2] == q {
                            p += 3;
                            break;
                        }
                        if ch[p] == '\\' && p + 1 < ch.len() {
                            p += 1;
                            s.push(decode_escape(ch[p]));
                            p += 1;
                        } else {
                            s.push(ch[p]);
                            p += 1;
                        }
                    }
                } else {
                    while p < ch.len() && ch[p] != q && ch[p] != '\n' {
                        if ch[p] == '\\' && p + 1 < ch.len() {
                            p += 1;
                            s.push(decode_escape(ch[p]));
                            p += 1;
                        } else {
                            s.push(ch[p]);
                            p += 1;
                        }
                    }
                    if p < ch.len() && ch[p] == q {
                        p += 1;
                    }
                }
                out.push(Tok::Str(s));
            }
            '/' if p + 1 < ch.len() && ch[p + 1] == '/' => {
                while p < ch.len() && ch[p] != '\n' {
                    p += 1;
                }
            }
            '/' if p + 1 < ch.len() && ch[p + 1] == '*' => {
                p += 2;
                while p + 1 < ch.len() {
                    if ch[p] == '*' && ch[p + 1] == '/' {
                        p += 2;
                        break;
                    }
                    p += 1;
                }
            }
            '#' => {
                while p < ch.len() && ch[p] != '\n' {
                    p += 1;
                }
            }
            c if c.is_ascii_digit() => {
                let mut n = String::new();
                while p < ch.len()
                    && (ch[p].is_ascii_digit()
                        || ch[p] == '.'
                        || ch[p] == '_'
                        || ch[p] == 'x'
                        || ch[p] == 'X')
                {
                    if ch[p] != '_' {
                        n.push(ch[p]);
                    }
                    p += 1;
                }
                // include hex digits
                if n.starts_with("0x") || n.starts_with("0X") {
                    while p < ch.len() && ch[p].is_ascii_hexdigit() {
                        n.push(ch[p]);
                        p += 1;
                    }
                }
                out.push(Tok::Num(n));
            }
            c if c.is_ascii_alphabetic() || c == '_' => {
                let mut id = String::new();
                while p < ch.len() && (ch[p].is_ascii_alphanumeric() || ch[p] == '_') {
                    id.push(ch[p]);
                    p += 1;
                }
                // Python f-string: f"..." or f'...'
                if (id == "f" || id == "F" || id == "b" || id == "r")
                    && p < ch.len()
                    && (ch[p] == '"' || ch[p] == '\'')
                {
                    let is_fstring = id == "f" || id == "F";
                    let q = ch[p];
                    p += 1;
                    let mut s = String::new();
                    while p < ch.len() && ch[p] != q && ch[p] != '\n' {
                        if is_fstring && ch[p] == '{' {
                            p += 1;
                            if p < ch.len() && ch[p] == '{' {
                                s.push('{');
                                p += 1;
                                continue;
                            }
                            s.push_str("${");
                            while p < ch.len() && ch[p] != '}' && ch[p] != '\n' {
                                s.push(ch[p]);
                                p += 1;
                            }
                            if p < ch.len() && ch[p] == '}' {
                                p += 1;
                            }
                            s.push('}');
                        } else if ch[p] == '\\' && p + 1 < ch.len() {
                            p += 1;
                            s.push(decode_escape(ch[p]));
                            p += 1;
                        } else {
                            s.push(ch[p]);
                            p += 1;
                        }
                    }
                    if p < ch.len() && ch[p] == q {
                        p += 1;
                    }
                    out.push(Tok::Str(s));
                } else {
                    out.push(Tok::Id(id));
                }
            }
            c => {
                // two-char operators
                if p + 1 < ch.len() {
                    let two: String = [c, ch[p + 1]].iter().collect();
                    if matches!(
                        two.as_str(),
                        "==" | "!="
                            | "<="
                            | ">="
                            | "&&"
                            | "||"
                            | "->"
                            | "+="
                            | "-="
                            | "*="
                            | "/="
                            | "::"
                            | "<<"
                            | ">>"
                    ) {
                        out.push(Tok::Op(two));
                        p += 2;
                        continue;
                    }
                }
                match c {
                    '=' | '+' | '-' | '*' | '/' | '%' | '<' | '>' | '!' | '&' | '|' | '^' | '~' => {
                        out.push(Tok::Op(c.to_string()));
                        p += 1;
                    }
                    '(' | ')' | '{' | '}' | '[' | ']' | ',' | ';' | ':' | '.' | '@' => {
                        out.push(Tok::Punc(c));
                        p += 1;
                    }
                    _ => {
                        p += 1;
                    }
                }
            }
        }
    }
    out
}

fn decode_escape(c: char) -> char {
    match c {
        'n' => '\n',
        't' => '\t',
        'r' => '\r',
        '0' => '\0',
        c => c,
    }
}

// --- Intermediate representation ---
#[derive(Debug, Clone)]
enum RbcOp {
    Print(String),
    Write(String),
    SetEnv(String, String),
    Exec(String),
    WriteFile(String, String),
    ReadFile(String),
    Sleep(u64),
    Exit(i32),
    SetVar(String, String),
    Label(String),
    Jump(String),
    JumpZ(String),
    Cmp(String),
    Incr(String),
}

// --- Bytecode emitter ---
fn emit_bytecode(ops: &[RbcOp]) -> Vec<u8> {
    let mut b = Vec::new();
    for op in ops {
        match op {
            RbcOp::Print(s) => push_op(&mut b, 0x01, s),
            RbcOp::Write(s) => push_op(&mut b, 0x10, s),
            RbcOp::SetEnv(k, v) => push_op(&mut b, 0x02, &format!("{k}={v}")),
            RbcOp::Exec(s) => push_op(&mut b, 0x03, s),
            RbcOp::WriteFile(p, c) => push_op(&mut b, 0x04, &format!("{p}::{c}")),
            RbcOp::ReadFile(p) => push_op(&mut b, 0x05, p),
            RbcOp::Sleep(ms) => push_op(&mut b, 0x06, &ms.to_string()),
            RbcOp::Exit(code) => push_op(&mut b, 0x08, &code.to_string()),
            RbcOp::SetVar(k, v) => push_op(&mut b, 0x09, &format!("{k}={v}")),
            RbcOp::Label(name) => push_op(&mut b, 0x0A, name),
            RbcOp::Jump(lbl) => push_op(&mut b, 0x0B, lbl),
            RbcOp::JumpZ(lbl) => push_op(&mut b, 0x0C, lbl),
            RbcOp::Cmp(expr) => push_op(&mut b, 0x0D, expr),
            RbcOp::Incr(var) => push_op(&mut b, 0x0E, var),
        }
    }
    b
}

fn push_op(bytecode: &mut Vec<u8>, opcode: u8, payload: &str) {
    let pb = payload.as_bytes();
    bytecode.push(opcode);
    bytecode.extend_from_slice(&(pb.len() as u32).to_le_bytes());
    bytecode.extend_from_slice(pb);
}

// --- Token helpers ---

/// Extract balanced content inside parens/brackets starting at index `start`.
/// Returns (inner tokens, index after closing delimiter).
fn extract_balanced(toks: &[Tok], start: usize) -> (Vec<Tok>, usize) {
    let open = match toks.get(start) {
        Some(Tok::Punc(c)) => *c,
        _ => return (vec![], start),
    };
    let close = match open {
        '(' => ')',
        '[' => ']',
        '{' => '}',
        _ => return (vec![], start),
    };
    let mut depth = 0;
    let mut inner = Vec::new();
    let mut i = start;
    while i < toks.len() {
        match &toks[i] {
            Tok::Punc(c) if *c == open => {
                if depth > 0 {
                    inner.push(toks[i].clone());
                }
                depth += 1;
            }
            Tok::Punc(c) if *c == close => {
                depth -= 1;
                if depth == 0 {
                    return (inner, i + 1);
                }
                inner.push(toks[i].clone());
            }
            _ => {
                if depth > 0 {
                    inner.push(toks[i].clone());
                }
            }
        }
        i += 1;
    }
    (inner, i)
}

/// Split a token list by commas at depth 0. Returns a vec of argument token-lists.
fn split_args(toks: &[Tok]) -> Vec<Vec<Tok>> {
    let mut args: Vec<Vec<Tok>> = vec![vec![]];
    let mut depth = 0;
    for t in toks {
        match t {
            Tok::Punc('(' | '[' | '{') => {
                depth += 1;
                args.last_mut().unwrap().push(t.clone());
            }
            Tok::Punc(')' | ']' | '}') => {
                depth -= 1;
                args.last_mut().unwrap().push(t.clone());
            }
            Tok::Punc(',') if depth == 0 => {
                args.push(vec![]);
            }
            _ => {
                args.last_mut().unwrap().push(t.clone());
            }
        }
    }
    args.into_iter().filter(|a| !a.is_empty()).collect()
}

/// Convert a token list to its string value: strings are unquoted, everything else concatenated.
fn toks_to_value(toks: &[Tok]) -> String {
    let mut s = String::new();
    for t in toks {
        match t {
            Tok::Str(v) => s.push_str(v),
            Tok::Num(v) => s.push_str(v),
            Tok::Id(v) => {
                if !s.is_empty() && !s.ends_with('.') && !s.ends_with("${") {
                    s.push(' ');
                }
                s.push_str(v);
            }
            Tok::Op(v) => {
                s.push(' ');
                s.push_str(v);
                s.push(' ');
            }
            Tok::Punc(c) => s.push(*c),
            Tok::Nl => {}
        }
    }
    s.trim().to_string()
}

/// Get the first string literal from a token list (e.g. from function call args).
fn first_string(toks: &[Tok]) -> Option<String> {
    toks.iter().find_map(|t| match t {
        Tok::Str(s) => Some(s.clone()),
        _ => None,
    })
}

/// Get the first number from a token list.
fn first_num(toks: &[Tok]) -> Option<u64> {
    toks.iter().find_map(|t| match t {
        Tok::Num(n) => n.parse().ok(),
        _ => None,
    })
}

/// Find the index of an Id token with a given name.
fn find_id(toks: &[Tok], name: &str) -> Option<usize> {
    toks.iter()
        .position(|t| matches!(t, Tok::Id(n) if n == name))
}

/// Find the first '(' after a given position.
fn find_paren(toks: &[Tok], start: usize) -> Option<usize> {
    toks[start..]
        .iter()
        .position(|t| matches!(t, Tok::Punc('(')))
        .map(|i| i + start)
}

/// Extract the string argument of a function call: name(...) -> string content.
fn call_string_arg(toks: &[Tok], func_pos: usize) -> Option<String> {
    let paren = find_paren(toks, func_pos)?;
    let (inner, _) = extract_balanced(toks, paren);
    first_string(&inner).or_else(|| {
        let v = toks_to_value(&inner);
        if v.is_empty() {
            None
        } else {
            Some(v)
        }
    })
}

fn call_arguments(toks: &[Tok], func_pos: usize) -> Option<Vec<Vec<Tok>>> {
    let paren = find_paren(toks, func_pos)?;
    let (inner, _) = extract_balanced(toks, paren);
    Some(split_args(&inner))
}

fn toks_to_runtime_value(toks: &[Tok]) -> String {
    let mut value = String::new();
    for token in toks {
        match token {
            Tok::Str(text) | Tok::Num(text) => value.push_str(text),
            Tok::Id(name)
                if matches!(
                    name.as_str(),
                    "true" | "false" | "True" | "False" | "None" | "null"
                ) =>
            {
                value.push_str(name);
            }
            Tok::Id(name) => {
                value.push_str("${");
                value.push_str(name);
                value.push('}');
            }
            Tok::Op(operator) => value.push_str(operator),
            Tok::Punc(character) => value.push(*character),
            Tok::Nl => {}
        }
    }
    value
}

fn formatted_call_text(toks: &[Tok], func_pos: usize) -> Option<String> {
    let args = call_arguments(toks, func_pos)?;
    let Some(format) = args.first().and_then(|argument| first_string(argument)) else {
        return Some(
            args.iter()
                .map(|argument| toks_to_runtime_value(argument))
                .collect::<Vec<_>>()
                .join(" "),
        );
    };
    let mut replacements = args
        .iter()
        .skip(1)
        .map(|argument| toks_to_runtime_value(argument));
    let mut result = String::with_capacity(format.len());
    let chars = format.chars().collect::<Vec<_>>();
    let mut index = 0;
    while index < chars.len() {
        if chars[index] == '{' && chars.get(index + 1) == Some(&'}') {
            if let Some(replacement) = replacements.next() {
                result.push_str(&replacement);
                index += 2;
                continue;
            }
        }
        result.push(chars[index]);
        index += 1;
    }
    Some(result)
}

fn call_expression_text(toks: &[Tok], func_pos: usize) -> Option<String> {
    let args = call_arguments(toks, func_pos)?;
    let expression = args.first()?;
    let mut value = String::new();
    for token in expression {
        match token {
            Tok::Str(text) | Tok::Num(text) => value.push_str(text),
            Tok::Id(name)
                if matches!(
                    name.as_str(),
                    "true" | "false" | "True" | "False" | "None" | "null"
                ) =>
            {
                value.push_str(name);
            }
            Tok::Id(name) => {
                value.push_str("${");
                value.push_str(name);
                value.push('}');
            }
            Tok::Op(operator) if operator == "+" => {}
            Tok::Op(operator) => value.push_str(operator),
            Tok::Punc(character) => value.push(*character),
            Tok::Nl => {}
        }
    }
    Some(value)
}

fn c_printf_text(toks: &[Tok], func_pos: usize) -> Option<String> {
    let args = call_arguments(toks, func_pos)?;
    let format = args.first().and_then(|argument| first_string(argument))?;
    let mut replacements = args
        .iter()
        .skip(1)
        .map(|argument| toks_to_runtime_value(argument));
    let chars = format.chars().collect::<Vec<_>>();
    let mut result = String::with_capacity(format.len());
    let mut index = 0;
    while index < chars.len() {
        if chars[index] == '%' {
            if chars.get(index + 1) == Some(&'%') {
                result.push('%');
                index += 2;
                continue;
            }
            let mut end = index + 1;
            while end < chars.len() && !matches!(chars[end], 'a'..='z' | 'A'..='Z') {
                end += 1;
            }
            if end < chars.len() && matches!(chars[end], 's' | 'd' | 'i' | 'u' | 'f' | 'c') {
                if let Some(replacement) = replacements.next() {
                    result.push_str(&replacement);
                    index = end + 1;
                    continue;
                }
            }
        }
        result.push(chars[index]);
        index += 1;
    }
    Some(result)
}

/// Extract the numeric argument of a function call.
fn call_num_arg(toks: &[Tok], func_pos: usize) -> Option<u64> {
    let paren = find_paren(toks, func_pos)?;
    let (inner, _) = extract_balanced(toks, paren);
    first_num(&inner)
}

/// Check if tokens contain a dotted method call like `obj.method(`.
fn has_dot_method(toks: &[Tok], method: &str) -> bool {
    for w in toks.windows(3) {
        if matches!(&w[0], Tok::Id(_))
            && w[1] == Tok::Punc('.')
            && matches!(&w[2], Tok::Id(m) if m == method)
        {
            return true;
        }
    }
    false
}

/// Check if tokens have an assignment operator at the top level.
fn has_assign(toks: &[Tok]) -> Option<usize> {
    let mut depth = 0;
    for (i, t) in toks.iter().enumerate() {
        match t {
            Tok::Punc('(' | '[' | '{') => depth += 1,
            Tok::Punc(')' | ']' | '}') => depth -= 1,
            Tok::Op(op) if op == "=" && depth == 0 => return Some(i),
            _ => {}
        }
    }
    None
}

/// Convert tokens to a comparison expression string for the VM.
fn toks_to_cmp(toks: &[Tok]) -> String {
    let mut s = String::new();
    for t in toks {
        match t {
            Tok::Str(v) => s.push_str(v),
            Tok::Num(v) => s.push_str(v),
            Tok::Id(v) => {
                // Wrap bare identifiers as variable references
                if !s.ends_with('.') {
                    s.push_str(&format!("${{{v}}}"));
                } else {
                    s.push_str(v);
                }
            }
            Tok::Op(v) => {
                s.push(' ');
                s.push_str(v);
                s.push(' ');
            }
            Tok::Punc('.') => s.push('.'),
            Tok::Punc(c) => s.push(*c),
            Tok::Nl => {}
        }
    }
    s.trim().to_string()
}

// ===== Python Parser =====

fn compile_python_source(source: &str) -> Vec<RbcOp> {
    let lines: Vec<&str> = source.lines().collect();
    let mut ops = Vec::new();
    let mut lc: usize = 0;
    let mut idx = 0;
    parse_py_block(&lines, &mut idx, 0, &mut ops, &mut lc);
    ops
}

fn py_indent(line: &str) -> usize {
    line.len() - line.trim_start().len()
}

fn py_body_indent(lines: &[&str], from: usize) -> usize {
    for i in from..lines.len() {
        let t = lines[i].trim();
        if !t.is_empty() && !t.starts_with('#') {
            return py_indent(lines[i]);
        }
    }
    from + 4 // default
}

fn parse_py_block(
    lines: &[&str],
    idx: &mut usize,
    min_indent: usize,
    ops: &mut Vec<RbcOp>,
    lc: &mut usize,
) {
    while *idx < lines.len() {
        let raw = lines[*idx];
        let trimmed = raw.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            *idx += 1;
            continue;
        }
        let indent = py_indent(raw);
        if indent < min_indent {
            return;
        }
        let toks = tokenize(trimmed);
        if toks.is_empty() {
            *idx += 1;
            continue;
        }
        parse_py_stmt(lines, idx, indent, &toks, ops, lc);
    }
}

fn parse_py_stmt(
    lines: &[&str],
    idx: &mut usize,
    indent: usize,
    toks: &[Tok],
    ops: &mut Vec<RbcOp>,
    lc: &mut usize,
) {
    let first = match &toks[0] {
        Tok::Id(s) => s.as_str(),
        _ => {
            *idx += 1;
            return;
        }
    };
    match first {
        "print" => {
            if let Some(args) = call_arguments(toks, 0) {
                let output = args
                    .iter()
                    .map(|argument| toks_to_runtime_value(argument))
                    .collect::<Vec<_>>()
                    .join(" ");
                ops.push(RbcOp::Print(output));
            }
            *idx += 1;
        }
        "import" | "from" | "pass" | "class" => {
            *idx += 1;
        }
        "if" => {
            let lid = *lc;
            *lc += 1;
            // condition: everything between 'if' and ':'
            let cond_toks = extract_before_colon(&toks[1..]);
            ops.push(RbcOp::Cmp(toks_to_cmp(&cond_toks)));
            ops.push(RbcOp::JumpZ(format!("__else_{lid}")));
            *idx += 1;
            let bi = py_body_indent(lines, *idx);
            parse_py_block(lines, idx, bi, ops, lc);
            // check for elif/else
            if *idx < lines.len() {
                let nt = lines[*idx].trim();
                if nt.starts_with("elif ") {
                    ops.push(RbcOp::Jump(format!("__endif_{lid}")));
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                    // re-parse as if
                    let elif_toks = tokenize(&nt[2..]); // "if ..."
                    parse_py_stmt(lines, idx, indent, &elif_toks, ops, lc);
                } else if nt.starts_with("else") && nt.contains(':') {
                    ops.push(RbcOp::Jump(format!("__endif_{lid}")));
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                    *idx += 1;
                    let bi = py_body_indent(lines, *idx);
                    parse_py_block(lines, idx, bi, ops, lc);
                } else {
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                }
            } else {
                ops.push(RbcOp::Label(format!("__else_{lid}")));
            }
            ops.push(RbcOp::Label(format!("__endif_{lid}")));
        }
        "for" => {
            let lid = *lc;
            *lc += 1;
            // for VAR in range(N):
            let var_name = match toks.get(1) {
                Some(Tok::Id(v)) => v.clone(),
                _ => format!("__i_{lid}"),
            };
            // find range(N)
            let limit = if let Some(rp) = find_id(toks, "range") {
                call_num_arg(toks, rp)
                    .map(|n| n.to_string())
                    .or_else(|| call_string_arg(toks, rp).map(|s| format!("${{{s}}}")))
                    .unwrap_or_else(|| "0".into())
            } else {
                "0".into()
            };
            ops.push(RbcOp::SetVar(format!("__iter_{lid}"), "0".into()));
            ops.push(RbcOp::SetVar(format!("__limit_{lid}"), limit));
            ops.push(RbcOp::Label(format!("__for_{lid}")));
            ops.push(RbcOp::Cmp(format!(
                "${{__iter_{lid}}} < ${{__limit_{lid}}}"
            )));
            ops.push(RbcOp::JumpZ(format!("__endfor_{lid}")));
            ops.push(RbcOp::SetVar(var_name, format!("${{__iter_{lid}}}")));
            *idx += 1;
            let bi = py_body_indent(lines, *idx);
            parse_py_block(lines, idx, bi, ops, lc);
            ops.push(RbcOp::Incr(format!("__iter_{lid}")));
            ops.push(RbcOp::Jump(format!("__for_{lid}")));
            ops.push(RbcOp::Label(format!("__endfor_{lid}")));
        }
        "while" => {
            let lid = *lc;
            *lc += 1;
            let cond_toks = extract_before_colon(&toks[1..]);
            ops.push(RbcOp::Label(format!("__while_{lid}")));
            ops.push(RbcOp::Cmp(toks_to_cmp(&cond_toks)));
            ops.push(RbcOp::JumpZ(format!("__endwhile_{lid}")));
            *idx += 1;
            let bi = py_body_indent(lines, *idx);
            parse_py_block(lines, idx, bi, ops, lc);
            ops.push(RbcOp::Jump(format!("__while_{lid}")));
            ops.push(RbcOp::Label(format!("__endwhile_{lid}")));
        }
        "def" => {
            if let Some(Tok::Id(name)) = toks.get(1) {
                ops.push(RbcOp::Label(name.clone()));
            }
            *idx += 1;
            let bi = py_body_indent(lines, *idx);
            parse_py_block(lines, idx, bi, ops, lc);
        }
        "return" => {
            if toks.len() > 1 {
                let val = toks_to_value(&toks[1..]);
                if let Ok(n) = val.trim().parse::<i32>() {
                    ops.push(RbcOp::Exit(n));
                }
            }
            *idx += 1;
        }
        "exit" | "quit" => {
            let n = call_num_arg(toks, 0).unwrap_or(0) as i32;
            ops.push(RbcOp::Exit(n));
            *idx += 1;
        }
        "os" => {
            if has_dot_method(toks, "system") {
                if let Some(cmd) = call_string_arg(toks, find_id(toks, "system").unwrap_or(0)) {
                    ops.push(RbcOp::Exec(cmd));
                }
            } else if has_dot_method(toks, "putenv") {
                let pos = find_id(toks, "putenv").unwrap_or(0);
                if let Some(p) = find_paren(toks, pos) {
                    let (inner, _) = extract_balanced(toks, p);
                    let args = split_args(&inner);
                    if args.len() >= 2 {
                        let k = toks_to_value(&args[0]);
                        let v = toks_to_value(&args[1]);
                        ops.push(RbcOp::SetEnv(k, v));
                    }
                }
            }
            *idx += 1;
        }
        "subprocess" => {
            if let Some(cmd) = first_string(toks) {
                ops.push(RbcOp::Exec(cmd));
            }
            *idx += 1;
        }
        "time" => {
            if has_dot_method(toks, "sleep") {
                let n = call_num_arg(toks, find_id(toks, "sleep").unwrap_or(0)).unwrap_or(1000);
                ops.push(RbcOp::Sleep(n));
            }
            *idx += 1;
        }
        "sys" => {
            if has_dot_method(toks, "exit") {
                let n = call_num_arg(toks, find_id(toks, "exit").unwrap_or(0)).unwrap_or(0) as i32;
                ops.push(RbcOp::Exit(n));
            }
            *idx += 1;
        }
        "open" => {
            if has_dot_method(toks, "write") {
                let path = call_string_arg(toks, 0).unwrap_or_default();
                let content =
                    call_string_arg(toks, find_id(toks, "write").unwrap_or(0)).unwrap_or_default();
                ops.push(RbcOp::WriteFile(path, content));
            } else if has_dot_method(toks, "read") {
                let path = call_string_arg(toks, 0).unwrap_or_default();
                ops.push(RbcOp::ReadFile(path));
            }
            *idx += 1;
        }
        _ => {
            // Variable assignment or function call
            if let Some(eq) = has_assign(toks) {
                let name = toks_to_value(&toks[..eq]);
                let val = toks_to_value(&toks[eq + 1..]);
                ops.push(RbcOp::SetVar(name, val));
            } else if find_paren(toks, 0).is_some() {
                // function call — emit as print of the call string
                if let Some(s) = call_string_arg(toks, 0) {
                    ops.push(RbcOp::Print(s));
                }
            }
            *idx += 1;
        }
    }
}

fn extract_before_colon(toks: &[Tok]) -> Vec<Tok> {
    let mut out = Vec::new();
    for t in toks {
        if matches!(t, Tok::Punc(':')) {
            break;
        }
        out.push(t.clone());
    }
    out
}

// ===== Rust Parser =====

fn compile_rust_source(source: &str) -> Vec<RbcOp> {
    let toks = tokenize(source);
    let mut ops = Vec::new();
    let mut lc: usize = 0;
    let mut i = 0;
    parse_rs_block(&toks, &mut i, &mut ops, &mut lc);
    ops
}

fn parse_rs_block(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    while *i < toks.len() {
        skip_nl(toks, i);
        if *i >= toks.len() {
            break;
        }
        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
            break;
        }
        parse_rs_stmt(toks, i, ops, lc);
    }
}

fn parse_rs_stmt(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    skip_nl(toks, i);
    if *i >= toks.len() {
        return;
    }
    match &toks[*i] {
        Tok::Id(kw) => match kw.as_str() {
            "use" | "extern" | "mod" | "pub" | "impl" | "struct" | "enum" | "trait" | "type"
            | "const" | "static" => {
                skip_to_semi_or_brace(toks, i);
            }
            "println" | "eprintln" | "print" | "eprint" => {
                let call_start = *i;
                let add_newline =
                    matches!(&toks[*i], Tok::Id(name) if name == "println" || name == "eprintln");
                *i += 1; // skip println
                if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "!") {
                    *i += 1;
                }
                if let Some(p) = find_paren_at(toks, *i) {
                    let (_, end) = extract_balanced(toks, p);
                    if let Some(s) = formatted_call_text(toks, call_start) {
                        ops.push(if add_newline {
                            RbcOp::Print(s)
                        } else {
                            RbcOp::Write(s)
                        });
                    }
                    *i = end;
                }
                skip_semi(toks, i);
            }
            "let" => {
                *i += 1;
                // skip 'mut'
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "mut") {
                    *i += 1;
                }
                if let Some(Tok::Id(name)) = toks.get(*i).cloned() {
                    *i += 1;
                    // optional type annotation
                    if matches!(toks.get(*i), Some(Tok::Punc(':'))) {
                        *i += 1;
                        while *i < toks.len()
                            && !matches!(toks.get(*i), Some(Tok::Op(o)) if o == "=")
                            && !matches!(toks.get(*i), Some(Tok::Punc(';')))
                        {
                            *i += 1;
                        }
                    }
                    if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "=") {
                        *i += 1;
                        let val = collect_to_semi(toks, i);
                        let val_str = toks_to_value(&val);
                        ops.push(RbcOp::SetVar(name, val_str));
                    }
                }
                skip_semi(toks, i);
            }
            "if" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = collect_to_brace(toks, i);
                ops.push(RbcOp::Cmp(toks_to_cmp(&cond)));
                ops.push(RbcOp::JumpZ(format!("__else_{lid}")));
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_rs_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "else") {
                    ops.push(RbcOp::Jump(format!("__endif_{lid}")));
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                    *i += 1;
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "if") {
                        parse_rs_stmt(toks, i, ops, lc);
                    } else if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_rs_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    }
                } else {
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                }
                ops.push(RbcOp::Label(format!("__endif_{lid}")));
            }
            "for" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let var = match toks.get(*i) {
                    Some(Tok::Id(v)) => {
                        *i += 1;
                        v.clone()
                    }
                    _ => format!("__i_{lid}"),
                };
                // skip 'in'
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "in") {
                    *i += 1;
                }
                // range: 0..N or range(N)
                let limit = parse_rs_range(toks, i);
                ops.push(RbcOp::SetVar(format!("__iter_{lid}"), "0".into()));
                ops.push(RbcOp::SetVar(format!("__limit_{lid}"), limit));
                ops.push(RbcOp::Label(format!("__for_{lid}")));
                ops.push(RbcOp::Cmp(format!(
                    "${{__iter_{lid}}} < ${{__limit_{lid}}}"
                )));
                ops.push(RbcOp::JumpZ(format!("__endfor_{lid}")));
                ops.push(RbcOp::SetVar(var, format!("${{__iter_{lid}}}")));
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_rs_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
                ops.push(RbcOp::Incr(format!("__iter_{lid}")));
                ops.push(RbcOp::Jump(format!("__for_{lid}")));
                ops.push(RbcOp::Label(format!("__endfor_{lid}")));
            }
            "while" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = collect_to_brace(toks, i);
                ops.push(RbcOp::Label(format!("__while_{lid}")));
                ops.push(RbcOp::Cmp(toks_to_cmp(&cond)));
                ops.push(RbcOp::JumpZ(format!("__endwhile_{lid}")));
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_rs_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
                ops.push(RbcOp::Jump(format!("__while_{lid}")));
                ops.push(RbcOp::Label(format!("__endwhile_{lid}")));
            }
            "loop" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                ops.push(RbcOp::Label(format!("__loop_{lid}")));
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_rs_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
                ops.push(RbcOp::Jump(format!("__loop_{lid}")));
            }
            "fn" => {
                *i += 1;
                if let Some(Tok::Id(name)) = toks.get(*i) {
                    if name != "main" {
                        ops.push(RbcOp::Label(name.clone()));
                    }
                    *i += 1;
                }
                // skip params
                if let Some(p) = find_paren_at(toks, *i) {
                    let (_, end) = extract_balanced(toks, p);
                    *i = end;
                }
                // skip return type
                while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                }
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_rs_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
            }
            "std" => {
                // std::env::set_var, std::process::exit, std::thread::sleep, etc.
                let full = collect_dotted(toks, i);
                if full.contains("set_var") {
                    if let Some(p) = find_paren_at(toks, *i) {
                        let (inner, end) = extract_balanced(toks, p);
                        *i = end;
                        let args = split_args(&inner);
                        if args.len() >= 2 {
                            ops.push(RbcOp::SetEnv(
                                toks_to_value(&args[0]),
                                toks_to_value(&args[1]),
                            ));
                        }
                    }
                } else if full.contains("exit") {
                    let n = find_paren_at(toks, *i)
                        .and_then(|p| {
                            let (inner, _) = extract_balanced(toks, p);
                            first_num(&inner)
                        })
                        .unwrap_or(0);
                    skip_to_semi_or_brace(toks, i);
                    ops.push(RbcOp::Exit(n as i32));
                } else if full.contains("sleep") {
                    let n = find_paren_at(toks, *i)
                        .and_then(|p| {
                            let (inner, _) = extract_balanced(toks, p);
                            first_num(&inner)
                        })
                        .unwrap_or(1000);
                    skip_to_semi_or_brace(toks, i);
                    ops.push(RbcOp::Sleep(n));
                } else {
                    skip_to_semi_or_brace(toks, i);
                }
                skip_semi(toks, i);
            }
            "Command" => {
                if let Some(s) = first_string(&toks[*i..]) {
                    ops.push(RbcOp::Exec(s));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "fs" => {
                let full = collect_dotted(toks, i);
                if full.contains("write") && !full.contains("write_all") {
                    if let Some(p) = find_paren_at(toks, *i) {
                        let (inner, end) = extract_balanced(toks, p);
                        *i = end;
                        let args = split_args(&inner);
                        if args.len() >= 2 {
                            ops.push(RbcOp::WriteFile(
                                toks_to_value(&args[0]),
                                toks_to_value(&args[1]),
                            ));
                        }
                    }
                } else if full.contains("read_to_string") {
                    if let Some(s) = call_string_arg(&toks[*i..], 0) {
                        ops.push(RbcOp::ReadFile(s));
                    }
                    skip_to_semi_or_brace(toks, i);
                }
                skip_semi(toks, i);
            }
            "process" => {
                if has_dot_method(&toks[*i..], "exit") {
                    let n = find_id(&toks[*i..], "exit")
                        .and_then(|p| call_num_arg(&toks[*i..], p))
                        .unwrap_or(0) as i32;
                    ops.push(RbcOp::Exit(n));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "thread" => {
                if has_dot_method(&toks[*i..], "sleep") {
                    let n = find_id(&toks[*i..], "sleep")
                        .and_then(|p| {
                            let paren = find_paren(&toks[*i..], p)?;
                            let (inner, _) = extract_balanced(&toks[*i..], paren);
                            first_num(&inner)
                        })
                        .unwrap_or(1000);
                    ops.push(RbcOp::Sleep(n));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            _ => {
                // Variable assignment or expression
                if let Some(eq) = has_assign(&toks[*i..]) {
                    let name = toks_to_value(&toks[*i..*i + eq]);
                    *i += eq + 1;
                    let val = collect_to_semi(toks, i);
                    ops.push(RbcOp::SetVar(name, toks_to_value(&val)));
                } else {
                    skip_to_semi_or_brace(toks, i);
                }
                skip_semi(toks, i);
            }
        },
        _ => {
            *i += 1;
        }
    }
}

fn parse_rs_range(toks: &[Tok], i: &mut usize) -> String {
    // 0..N or 0..=N
    let mut parts = Vec::new();
    while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc('{'))) {
        parts.push(toks[*i].clone());
        *i += 1;
    }
    // Find .. operator
    for (j, t) in parts.iter().enumerate() {
        if matches!(t, Tok::Punc('.'))
            && j + 1 < parts.len()
            && matches!(parts[j + 1], Tok::Punc('.'))
        {
            let right = toks_to_value(&parts[j + 2..]);
            return right.trim_start_matches('=').to_string();
        }
    }
    toks_to_value(&parts)
}

fn collect_dotted(toks: &[Tok], i: &mut usize) -> String {
    let mut s = String::new();
    while *i < toks.len() {
        match &toks[*i] {
            Tok::Id(v) => {
                s.push_str(v);
                *i += 1;
            }
            Tok::Punc('.') => {
                s.push('.');
                *i += 1;
            }
            Tok::Op(o) if o == "::" => {
                s.push_str("::");
                *i += 1;
            }
            _ => break,
        }
    }
    s
}

// ===== C/C++ Parser =====

fn compile_c_source(source: &str) -> Vec<RbcOp> {
    compile_cpp_source(source)
}

fn compile_cpp_source(source: &str) -> Vec<RbcOp> {
    // Pre-strip preprocessor lines
    let filtered: String = source
        .lines()
        .map(|l| {
            if l.trim_start().starts_with('#') {
                ""
            } else {
                l
            }
        })
        .collect::<Vec<_>>()
        .join("\n");
    let toks = tokenize(&filtered);
    let mut ops = Vec::new();
    let mut lc: usize = 0;
    let mut i = 0;
    parse_c_block(&toks, &mut i, &mut ops, &mut lc);
    ops
}

fn parse_c_block(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    while *i < toks.len() {
        skip_nl(toks, i);
        if *i >= toks.len() {
            break;
        }
        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
            break;
        }
        parse_c_stmt(toks, i, ops, lc);
    }
}

fn parse_c_stmt(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    skip_nl(toks, i);
    if *i >= toks.len() {
        return;
    }
    match &toks[*i] {
        Tok::Id(kw) => match kw.as_str() {
            "printf" | "puts" => {
                let output = if kw == "printf" {
                    c_printf_text(toks, *i).map(RbcOp::Write)
                } else {
                    call_string_arg(toks, *i).map(RbcOp::Print)
                };
                if let Some(operation) = output {
                    ops.push(operation);
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "cout" => {
                *i += 1;
                // cout << "..." << endl;
                let mut parts = Vec::new();
                let mut add_newline = false;
                while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc(';'))) {
                    if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "<<") {
                        *i += 1;
                        continue;
                    }
                    if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "endl") {
                        add_newline = true;
                        *i += 1;
                        continue;
                    }
                    if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "std") {
                        *i += 1;
                        continue;
                    }
                    if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "::") {
                        *i += 1;
                        continue;
                    }
                    if let Some(Tok::Str(s)) = toks.get(*i) {
                        parts.push(s.clone());
                    } else if let Some(Tok::Id(name)) = toks.get(*i) {
                        parts.push(format!("${{{name}}}"));
                    } else if let Some(Tok::Num(number)) = toks.get(*i) {
                        parts.push(number.clone());
                    }
                    *i += 1;
                }
                if !parts.is_empty() {
                    let text = parts.join("");
                    ops.push(if add_newline {
                        RbcOp::Print(text)
                    } else {
                        RbcOp::Write(text)
                    });
                }
                skip_semi(toks, i);
            }
            "system" => {
                if let Some(s) = call_string_arg(toks, *i) {
                    ops.push(RbcOp::Exec(s));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "setenv" | "_putenv" | "putenv" => {
                if let Some(p) = find_paren_at(toks, *i + 1).or_else(|| find_paren_at(toks, *i)) {
                    let (inner, _) = extract_balanced(toks, p);
                    let args = split_args(&inner);
                    if args.len() >= 2 {
                        ops.push(RbcOp::SetEnv(
                            toks_to_value(&args[0]),
                            toks_to_value(&args[1]),
                        ));
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "sleep" | "Sleep" | "usleep" => {
                let n = call_num_arg(toks, *i).unwrap_or(1000);
                ops.push(RbcOp::Sleep(if kw == "usleep" { n / 1000 } else { n }));
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "exit" => {
                let n = call_num_arg(toks, *i).unwrap_or(0) as i32;
                ops.push(RbcOp::Exit(n));
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "return" => {
                *i += 1;
                if let Some(Tok::Num(n)) = toks.get(*i) {
                    if let Ok(code) = n.parse::<i32>() {
                        ops.push(RbcOp::Exit(code));
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "if" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    toks_to_cmp(&inner)
                } else {
                    String::new()
                };
                ops.push(RbcOp::Cmp(cond));
                ops.push(RbcOp::JumpZ(format!("__else_{lid}")));
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_c_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                } else {
                    parse_c_stmt(toks, i, ops, lc);
                }
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "else") {
                    ops.push(RbcOp::Jump(format!("__endif_{lid}")));
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                    *i += 1;
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_c_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    } else {
                        parse_c_stmt(toks, i, ops, lc);
                    }
                } else {
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                }
                ops.push(RbcOp::Label(format!("__endif_{lid}")));
            }
            "for" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                // for (init; cond; incr) { body }
                if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    let parts = split_by_semi(&inner);
                    // init
                    if let Some(init) = parts.first() {
                        if let Some(eq) = has_assign(init) {
                            let name = toks_to_value(&init[..eq])
                                .replace("int ", "")
                                .replace("size_t ", "")
                                .trim()
                                .to_string();
                            let val = toks_to_value(&init[eq + 1..]);
                            ops.push(RbcOp::SetVar(name, val));
                        }
                    }
                    // condition
                    ops.push(RbcOp::Label(format!("__for_{lid}")));
                    if let Some(cond) = parts.get(1) {
                        ops.push(RbcOp::Cmp(toks_to_cmp(cond)));
                        ops.push(RbcOp::JumpZ(format!("__endfor_{lid}")));
                    }
                    // body
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_c_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    } else {
                        parse_c_stmt(toks, i, ops, lc);
                    }
                    // increment
                    if let Some(incr) = parts.get(2) {
                        let incr_s = toks_to_value(incr);
                        if incr_s.contains("++") {
                            let var = incr_s.replace("++", "").trim().to_string();
                            ops.push(RbcOp::Incr(var));
                        }
                    }
                    ops.push(RbcOp::Jump(format!("__for_{lid}")));
                    ops.push(RbcOp::Label(format!("__endfor_{lid}")));
                }
            }
            "while" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    toks_to_cmp(&inner)
                } else {
                    String::new()
                };
                ops.push(RbcOp::Label(format!("__while_{lid}")));
                ops.push(RbcOp::Cmp(cond));
                ops.push(RbcOp::JumpZ(format!("__endwhile_{lid}")));
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_c_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                } else {
                    parse_c_stmt(toks, i, ops, lc);
                }
                ops.push(RbcOp::Jump(format!("__while_{lid}")));
                ops.push(RbcOp::Label(format!("__endwhile_{lid}")));
            }
            "int" | "void" | "char" | "float" | "double" | "long" | "short" | "unsigned"
            | "signed" | "auto" | "bool" => {
                *i += 1;
                // Check if it's main()
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "main") {
                    *i += 1;
                    if let Some(p) = find_paren_at(toks, *i) {
                        let (_, end) = extract_balanced(toks, p);
                        *i = end;
                    }
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_c_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    }
                } else if matches!(toks.get(*i), Some(Tok::Id(_))) {
                    // variable declaration or function definition
                    let name_tok = toks[*i].clone();
                    *i += 1;
                    if matches!(toks.get(*i), Some(Tok::Punc('('))) {
                        // function definition
                        if let Tok::Id(fname) = &name_tok {
                            ops.push(RbcOp::Label(fname.clone()));
                        }
                        let (_, end) = extract_balanced(toks, *i);
                        *i = end;
                        skip_nl(toks, i);
                        if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                            *i += 1;
                            parse_c_block(toks, i, ops, lc);
                            if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                                *i += 1;
                            }
                        }
                    } else if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "=") {
                        *i += 1;
                        let val = collect_to_semi(toks, i);
                        if let Tok::Id(vn) = name_tok {
                            ops.push(RbcOp::SetVar(vn, toks_to_value(&val)));
                        }
                        skip_semi(toks, i);
                    } else {
                        skip_to_semi_or_brace(toks, i);
                        skip_semi(toks, i);
                    }
                } else {
                    skip_to_semi_or_brace(toks, i);
                    skip_semi(toks, i);
                }
            }
            "std" => {
                let full = collect_dotted(toks, i);
                if full.contains("cout") {
                    // redirect to cout handler
                    let mut fake = vec![Tok::Id("cout".into())];
                    while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc(';'))) {
                        fake.push(toks[*i].clone());
                        *i += 1;
                    }
                    fake.push(Tok::Punc(';'));
                    let mut fi = 0;
                    parse_c_stmt(&fake, &mut fi, ops, lc);
                } else {
                    skip_to_semi_or_brace(toks, i);
                    skip_semi(toks, i);
                }
            }
            _ => {
                // check for assignment
                if let Some(eq) = has_assign(&toks[*i..]) {
                    if eq > 0 && eq < 5 {
                        let name = toks_to_value(&toks[*i..*i + eq]);
                        *i += eq + 1;
                        let val = collect_to_semi(toks, i);
                        ops.push(RbcOp::SetVar(name, toks_to_value(&val)));
                        skip_semi(toks, i);
                        return;
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
        },
        Tok::Punc('{') => {
            *i += 1;
            parse_c_block(toks, i, ops, lc);
            if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                *i += 1;
            }
        }
        _ => {
            *i += 1;
        }
    }
}

fn split_by_semi(toks: &[Tok]) -> Vec<Vec<Tok>> {
    let mut parts: Vec<Vec<Tok>> = vec![vec![]];
    for t in toks {
        if matches!(t, Tok::Punc(';')) {
            parts.push(vec![]);
        } else {
            parts.last_mut().unwrap().push(t.clone());
        }
    }
    parts.into_iter().filter(|p| !p.is_empty()).collect()
}

// ===== Java Parser =====

fn compile_java_source(source: &str) -> Vec<RbcOp> {
    let toks = tokenize(source);
    let mut ops = Vec::new();
    let mut lc: usize = 0;
    let mut i = 0;
    parse_java_block(&toks, &mut i, &mut ops, &mut lc);
    ops
}

fn parse_java_block(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    while *i < toks.len() {
        skip_nl(toks, i);
        if *i >= toks.len() {
            break;
        }
        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
            break;
        }
        parse_java_stmt(toks, i, ops, lc);
    }
}

fn parse_java_stmt(toks: &[Tok], i: &mut usize, ops: &mut Vec<RbcOp>, lc: &mut usize) {
    skip_nl(toks, i);
    if *i >= toks.len() {
        return;
    }
    match &toks[*i] {
        Tok::Id(kw) => match kw.as_str() {
            "package" | "import" => {
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "public" | "private" | "protected" | "abstract" | "final" | "synchronized" => {
                *i += 1;
                parse_java_stmt(toks, i, ops, lc);
            }
            "class" | "interface" | "enum" => {
                *i += 1;
                // skip class name and extends/implements
                while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                }
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_java_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                }
            }
            "static" => {
                *i += 1;
                parse_java_stmt(toks, i, ops, lc);
            }
            "void" | "int" | "String" | "boolean" | "char" | "float" | "double" | "long"
            | "byte" | "short" => {
                *i += 1;
                // method def or variable
                if let Some(Tok::Id(name)) = toks.get(*i).cloned() {
                    *i += 1;
                    if matches!(toks.get(*i), Some(Tok::Punc('('))) {
                        // method definition
                        if name != "main" {
                            ops.push(RbcOp::Label(name));
                        }
                        let (_, end) = extract_balanced(toks, *i);
                        *i = end;
                        // skip throws
                        while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                            *i += 1;
                        }
                        if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                            *i += 1;
                            parse_java_block(toks, i, ops, lc);
                            if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                                *i += 1;
                            }
                        }
                    } else if matches!(toks.get(*i), Some(Tok::Op(o)) if o == "=") {
                        *i += 1;
                        let val = collect_to_semi(toks, i);
                        ops.push(RbcOp::SetVar(name, toks_to_value(&val)));
                        skip_semi(toks, i);
                    } else {
                        skip_to_semi_or_brace(toks, i);
                        skip_semi(toks, i);
                    }
                } else {
                    skip_to_semi_or_brace(toks, i);
                    skip_semi(toks, i);
                }
            }
            "System" => {
                *i += 1;
                let rest = collect_dotted(toks, i);
                if rest.contains("out.println") || rest.contains("out.print") {
                    if let Some(text) = call_expression_text(toks, *i) {
                        ops.push(if rest.contains("out.println") {
                            RbcOp::Print(text)
                        } else {
                            RbcOp::Write(text)
                        });
                    }
                    skip_to_semi_or_brace(toks, i);
                } else if rest.contains("exit") {
                    let n = call_num_arg(toks, *i).unwrap_or(0) as i32;
                    ops.push(RbcOp::Exit(n));
                    skip_to_semi_or_brace(toks, i);
                } else if rest.contains("setProperty") || rest.contains("getenv") {
                    if let Some(p) = find_paren_at(toks, *i) {
                        let (inner, _) = extract_balanced(toks, p);
                        let args = split_args(&inner);
                        if args.len() >= 2 {
                            ops.push(RbcOp::SetEnv(
                                toks_to_value(&args[0]),
                                toks_to_value(&args[1]),
                            ));
                        }
                    }
                    skip_to_semi_or_brace(toks, i);
                } else {
                    skip_to_semi_or_brace(toks, i);
                }
                skip_semi(toks, i);
            }
            "Runtime" => {
                if let Some(s) = first_string(&toks[*i..]) {
                    ops.push(RbcOp::Exec(s));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "Thread" => {
                if has_dot_method(&toks[*i..], "sleep") {
                    let n = find_id(&toks[*i..], "sleep")
                        .and_then(|p| call_num_arg(&toks[*i..], p))
                        .unwrap_or(1000);
                    ops.push(RbcOp::Sleep(n));
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "Files" => {
                if has_dot_method(&toks[*i..], "writeString") {
                    if let Some(p) = find_id(&toks[*i..], "writeString")
                        .and_then(|pp| find_paren(&toks[*i..], pp))
                    {
                        let (inner, _) = extract_balanced(&toks[*i..], p);
                        let args = split_args(&inner);
                        if args.len() >= 2 {
                            ops.push(RbcOp::WriteFile(
                                toks_to_value(&args[0]),
                                toks_to_value(&args[1]),
                            ));
                        }
                    }
                } else if has_dot_method(&toks[*i..], "readString") {
                    if let Some(s) = first_string(&toks[*i..]) {
                        ops.push(RbcOp::ReadFile(s));
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            "if" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    toks_to_cmp(&inner)
                } else {
                    String::new()
                };
                ops.push(RbcOp::Cmp(cond));
                ops.push(RbcOp::JumpZ(format!("__else_{lid}")));
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_java_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                } else {
                    parse_java_stmt(toks, i, ops, lc);
                }
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Id(s)) if s == "else") {
                    ops.push(RbcOp::Jump(format!("__endif_{lid}")));
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                    *i += 1;
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_java_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    } else {
                        parse_java_stmt(toks, i, ops, lc);
                    }
                } else {
                    ops.push(RbcOp::Label(format!("__else_{lid}")));
                }
                ops.push(RbcOp::Label(format!("__endif_{lid}")));
            }
            "for" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    let parts = split_by_semi(&inner);
                    if let Some(init) = parts.first() {
                        // skip type keywords
                        let init_trimmed: Vec<_> = init.iter().filter(|t| !matches!(t, Tok::Id(s) if s == "int" || s == "long" || s == "var")).cloned().collect();
                        if let Some(eq) = has_assign(&init_trimmed) {
                            ops.push(RbcOp::SetVar(
                                toks_to_value(&init_trimmed[..eq]),
                                toks_to_value(&init_trimmed[eq + 1..]),
                            ));
                        }
                    }
                    ops.push(RbcOp::Label(format!("__for_{lid}")));
                    if let Some(cond) = parts.get(1) {
                        ops.push(RbcOp::Cmp(toks_to_cmp(cond)));
                        ops.push(RbcOp::JumpZ(format!("__endfor_{lid}")));
                    }
                    skip_nl(toks, i);
                    if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                        *i += 1;
                        parse_java_block(toks, i, ops, lc);
                        if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                            *i += 1;
                        }
                    } else {
                        parse_java_stmt(toks, i, ops, lc);
                    }
                    if let Some(incr) = parts.get(2) {
                        let s = toks_to_value(incr);
                        if s.contains("++") {
                            ops.push(RbcOp::Incr(s.replace("++", "").trim().into()));
                        }
                    }
                    ops.push(RbcOp::Jump(format!("__for_{lid}")));
                    ops.push(RbcOp::Label(format!("__endfor_{lid}")));
                }
            }
            "while" => {
                *i += 1;
                let lid = *lc;
                *lc += 1;
                let cond = if let Some(p) = find_paren_at(toks, *i) {
                    let (inner, end) = extract_balanced(toks, p);
                    *i = end;
                    toks_to_cmp(&inner)
                } else {
                    String::new()
                };
                ops.push(RbcOp::Label(format!("__while_{lid}")));
                ops.push(RbcOp::Cmp(cond));
                ops.push(RbcOp::JumpZ(format!("__endwhile_{lid}")));
                skip_nl(toks, i);
                if matches!(toks.get(*i), Some(Tok::Punc('{'))) {
                    *i += 1;
                    parse_java_block(toks, i, ops, lc);
                    if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                        *i += 1;
                    }
                } else {
                    parse_java_stmt(toks, i, ops, lc);
                }
                ops.push(RbcOp::Jump(format!("__while_{lid}")));
                ops.push(RbcOp::Label(format!("__endwhile_{lid}")));
            }
            "return" => {
                *i += 1;
                if let Some(Tok::Num(n)) = toks.get(*i) {
                    if let Ok(c) = n.parse::<i32>() {
                        ops.push(RbcOp::Exit(c));
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
            _ => {
                if let Some(eq) = has_assign(&toks[*i..]) {
                    if eq > 0 && eq < 5 {
                        let name = toks_to_value(&toks[*i..*i + eq]);
                        *i += eq + 1;
                        let val = collect_to_semi(toks, i);
                        ops.push(RbcOp::SetVar(name, toks_to_value(&val)));
                        skip_semi(toks, i);
                        return;
                    }
                }
                skip_to_semi_or_brace(toks, i);
                skip_semi(toks, i);
            }
        },
        Tok::Punc('{') => {
            *i += 1;
            parse_java_block(toks, i, ops, lc);
            if matches!(toks.get(*i), Some(Tok::Punc('}'))) {
                *i += 1;
            }
        }
        _ => {
            *i += 1;
        }
    }
}

// ===== Shared parser helpers =====

fn skip_nl(toks: &[Tok], i: &mut usize) {
    while *i < toks.len() && matches!(toks.get(*i), Some(Tok::Nl)) {
        *i += 1;
    }
}

fn skip_semi(toks: &[Tok], i: &mut usize) {
    if matches!(toks.get(*i), Some(Tok::Punc(';'))) {
        *i += 1;
    }
}

fn skip_to_semi_or_brace(toks: &[Tok], i: &mut usize) {
    let mut depth = 0;
    while *i < toks.len() {
        match toks.get(*i) {
            Some(Tok::Punc(';')) if depth == 0 => return,
            Some(Tok::Punc('{')) => {
                depth += 1;
                *i += 1;
            }
            Some(Tok::Punc('}')) => {
                if depth == 0 {
                    return;
                }
                depth -= 1;
                *i += 1;
            }
            _ => {
                *i += 1;
            }
        }
    }
}

fn find_paren_at(toks: &[Tok], i: usize) -> Option<usize> {
    if matches!(toks.get(i), Some(Tok::Punc('('))) {
        Some(i)
    } else {
        None
    }
}

fn collect_to_semi(toks: &[Tok], i: &mut usize) -> Vec<Tok> {
    let mut out = Vec::new();
    let mut depth = 0;
    while *i < toks.len() {
        match &toks[*i] {
            Tok::Punc(';') if depth == 0 => break,
            Tok::Nl if depth == 0 => break,
            Tok::Punc('(' | '[' | '{') => {
                depth += 1;
                out.push(toks[*i].clone());
                *i += 1;
            }
            Tok::Punc(')' | ']' | '}') => {
                depth -= 1;
                out.push(toks[*i].clone());
                *i += 1;
            }
            _ => {
                out.push(toks[*i].clone());
                *i += 1;
            }
        }
    }
    out
}

fn collect_to_brace(toks: &[Tok], i: &mut usize) -> Vec<Tok> {
    let mut out = Vec::new();
    while *i < toks.len() && !matches!(toks.get(*i), Some(Tok::Punc('{'))) {
        if !matches!(toks.get(*i), Some(Tok::Nl)) {
            out.push(toks[*i].clone());
        }
        *i += 1;
    }
    out
}

// ===== Public compile API =====

pub fn compile_source_to_bytecode(source_code: &str, file_ext: &str) -> io::Result<Vec<u8>> {
    let ext = file_ext.trim_start_matches('.').to_ascii_lowercase();
    if ext != "rts" {
        return Err(invalid_input(
            "only legacy ReTerm Script (.rts) can be translated to ReTerm bytecode",
        ));
    }
    let bytecode = compile_script_to_bytecode(source_code)?;
    if bytecode.is_empty() {
        return Err(invalid_input("ReTerm script contains no instructions"));
    }
    Ok(bytecode)
}

pub fn compile_script_to_bytecode(script_source: &str) -> io::Result<Vec<u8>> {
    let mut bytecode = Vec::new();
    for line in script_source.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with("//") {
            continue;
        }
        let mut parts = line.splitn(2, char::is_whitespace);
        let cmd = parts.next().unwrap_or_default();
        let args = parts.next().unwrap_or_default();
        let (opcode, payload) = match cmd.to_ascii_uppercase().as_str() {
            "PRINT" | "ECHO" => (0x01_u8, args.trim()),
            "WRITEOUT" => (0x10_u8, args.trim()),
            "SETENV" | "ENV" => (0x02_u8, args.trim()),
            "EXEC" | "RUN" => (0x03_u8, args.trim()),
            "WRITEFILE" | "WRITE" => (0x04_u8, args.trim()),
            "READFILE" | "CAT" => (0x05_u8, args.trim()),
            "SLEEP" => (0x06_u8, args.trim()),
            "ECHO_ARGS" | "ARGS" => (0x07_u8, args.trim()),
            "EXIT" => (0x08_u8, args.trim()),
            "SETVAR" | "SET" => (0x09_u8, args.trim()),
            "LABEL" => (0x0A_u8, args.trim()),
            "JUMP" | "GOTO" => (0x0B_u8, args.trim()),
            "JUMPZ" | "JZ" => (0x0C_u8, args.trim()),
            "CMP" | "COMPARE" => (0x0D_u8, args.trim()),
            "INCR" | "INC" => (0x0E_u8, args.trim()),
            "NOP" => (0x0F_u8, ""),
            "NUKE" => (0x11_u8, args.trim()),
            "COMPALL" => (0x12_u8, args.trim()),
            "SHELLPASS" => (0x13_u8, args.trim()),
            _ => {
                return Err(invalid_input(format!(
                    "unknown ReTerm Script instruction `{cmd}`"
                )))
            }
        };
        push_op(&mut bytecode, opcode, payload);
    }
    Ok(bytecode)
}

#[allow(dead_code)]
pub fn create_mod_package_from_script(
    script_source: &str,
    output_mod_path: &Path,
    name: String,
    callby: String,
    version: String,
    description: String,
    author: Option<String>,
    license: Option<String>,
    default_args: Option<Vec<String>>,
    env: Option<std::collections::HashMap<String, String>>,
) -> io::Result<PathBuf> {
    create_mod_package_from_source(
        script_source,
        "rts",
        output_mod_path,
        name,
        callby,
        version,
        description,
        author,
        license,
        default_args,
        env,
    )
}

pub fn create_mod_package_from_source(
    source_code: &str,
    extension: &str,
    output_mod_path: &Path,
    name: String,
    callby: String,
    version: String,
    description: String,
    author: Option<String>,
    license: Option<String>,
    default_args: Option<Vec<String>>,
    env: Option<std::collections::HashMap<String, String>>,
) -> io::Result<PathBuf> {
    validate_mod_output_path(output_mod_path)?;
    let callby = required_field(callby, "callby")?;
    validate_callby(&callby)?;

    let extension = extension.trim_start_matches('.').to_ascii_lowercase();
    if !matches!(
        extension.as_str(),
        "rs" | "py" | "c" | "cpp" | "cc" | "cxx" | "java" | "rts"
    ) {
        return Err(invalid_input(format!(
            "unsupported source extension `.{extension}`; modules must contain source code"
        )));
    }
    let exec_filename = format!("{callby}.{extension}");
    let roadmap = Roadmap {
        header: "rdmapv3",
        exec: exec_filename,
        name: required_field(name, "component name")?,
        callby: callby.clone(),
        version: required_field(version, "version")?,
        description: required_field(description, "description")?,
        author,
        license,
        default_args,
        env,
    };

    if source_code.trim().is_empty() {
        return Err(invalid_input("source file must not be empty"));
    }
    if extension == "rts" {
        compile_source_to_bytecode(source_code, "rts")?;
    }
    let payload_bytes = source_code.as_bytes().to_vec();

    write_mod_package(&roadmap, &payload_bytes, output_mod_path)
}

#[allow(dead_code)]
pub fn create_mod_package(
    source_file_path: &Path,
    output_mod_path: &Path,
    name: String,
    callby: String,
    version: String,
    description: String,
    author: Option<String>,
    license: Option<String>,
    default_args: Option<Vec<String>>,
    env: Option<std::collections::HashMap<String, String>>,
) -> io::Result<PathBuf> {
    let source_file_path = source_file_path.canonicalize()?;
    if !source_file_path.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "main module source path must be a file",
        ));
    }

    let ext = source_file_path
        .extension()
        .and_then(OsStr::to_str)
        .unwrap_or_default()
        .to_ascii_lowercase();

    if matches!(ext.as_str(), "exe" | "dll") {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "native executable and library binaries cannot be converted into portable .mod files; provide source code instead",
        ));
    }

    validate_mod_output_path(output_mod_path)?;
    if source_file_path.canonicalize()?
        == output_mod_path
            .canonicalize()
            .unwrap_or_else(|_| output_mod_path.to_path_buf())
    {
        return Err(invalid_input(
            "module output path must not overwrite the source file",
        ));
    }
    let source_bytes = fs::read(&source_file_path)?;
    if source_bytes.starts_with(b"MZ") {
        return Err(invalid_input(
            "native executable and library binaries cannot be converted into portable .mod files; provide source code instead",
        ));
    }
    let source_text = std::str::from_utf8(&source_bytes)
        .map_err(|_| invalid_input("source file must be valid UTF-8 text"))?;
    create_mod_package_from_source(
        &source_text,
        &ext,
        output_mod_path,
        name,
        callby,
        version,
        description,
        author,
        license,
        default_args,
        env,
    )
}

fn validate_mod_output_path(path: &Path) -> io::Result<()> {
    if !path
        .extension()
        .and_then(OsStr::to_str)
        .is_some_and(|extension| extension.eq_ignore_ascii_case("mod"))
    {
        return Err(invalid_input(
            "module output path must have a .mod extension",
        ));
    }
    Ok(())
}

fn relative_exec(directory: &Path, exec_file: &Path) -> io::Result<String> {
    let relative = exec_file
        .strip_prefix(directory)
        .map_err(|_| invalid_input("main component file must be inside the selected directory"))?;
    if !exec_file.is_file() {
        return Err(invalid_input("main component path must be a file"));
    }
    let relative = relative
        .to_str()
        .ok_or_else(|| invalid_input("main component path must be valid UTF-8"))?
        .replace('\\', "/");
    validate_exec(&relative)?;
    Ok(relative)
}

fn required_field(value: String, label: &str) -> io::Result<String> {
    if value.trim().is_empty() {
        return Err(invalid_input(format!("{label} must not be empty")));
    }
    Ok(value)
}

fn validate_callby(callby: &str) -> io::Result<()> {
    if callby.is_empty()
        || !callby
            .chars()
            .all(|ch| ch.is_ascii_alphanumeric() || matches!(ch, '_' | '-' | '.'))
    {
        return Err(invalid_input(
            "callby may contain only ASCII letters, numbers, '.', '_' and '-'",
        ));
    }
    Ok(())
}

#[allow(dead_code)]
fn prompt<R: BufRead, W: Write>(input: &mut R, output: &mut W, label: &str) -> io::Result<String> {
    write!(output, "{label}")?;
    output.flush()?;
    let mut value = String::new();
    if input.read_line(&mut value)? == 0 {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "input ended while creating component",
        ));
    }
    required_field(value.trim().to_owned(), "component field")
}

fn validate_exec(exec: &str) -> io::Result<PathBuf> {
    let path = Path::new(exec);
    if path.is_absolute()
        || path
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(invalid_input(
            "main file path must be relative and stay inside the component directory",
        ));
    }
    let extension = path
        .extension()
        .and_then(OsStr::to_str)
        .unwrap_or_default()
        .to_ascii_lowercase();
    let is_supported = matches!(extension.as_str(), "exe" | "vbs" | "cmd" | "bat")
        || cfg!(not(windows)) && extension == "sh";
    if !is_supported {
        return Err(invalid_input(
            "main file must be .exe, .vbs, .cmd, .bat, or (on Unix) .sh",
        ));
    }
    Ok(path.to_path_buf())
}

fn create_package(directory: &Path, roadmap: &Roadmap) -> io::Result<PathBuf> {
    let package_name = directory
        .file_name()
        .filter(|name| !name.is_empty())
        .unwrap_or_else(|| OsStr::new("component"));
    let package = directory
        .parent()
        .ok_or_else(|| invalid_input("directory has no parent"))?
        .join(format!("{}.comp", package_name.to_string_lossy()));
    if package.exists() {
        return Err(io::Error::new(
            io::ErrorKind::AlreadyExists,
            format!("output already exists: {}", package.display()),
        ));
    }

    let exec = validate_exec(&roadmap.exec)?;
    let mut files = Vec::new();
    collect_files(directory, directory, &mut files)?;
    if !files.iter().any(|path| path == &exec) {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            "main component file is missing from the source directory",
        ));
    }

    let parent = package
        .parent()
        .ok_or_else(|| invalid_input("output has no parent"))?;
    let temporary = parent.join(format!(
        ".reterm-compmaker-{}-{}.tmp",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(io::Error::other)?
            .as_nanos()
    ));
    let build_result = (|| {
        let file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)?;
        let mut archive = ZipWriter::new(file);
        let options = FileOptions::default()
            .compression_method(CompressionMethod::Deflated)
            .with_deprecated_encryption(COMPONENT_PASSWORD);
        let manifest = serde_json::to_vec_pretty(roadmap)
            .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
        archive
            .start_file("find.rdmap", options)
            .map_err(zip_error)?;
        archive.write_all(&manifest)?;

        for relative_path in files {
            let archive_path = relative_path
                .to_str()
                .ok_or_else(|| invalid_input("file path must be valid UTF-8"))?
                .replace('\\', "/");
            archive
                .start_file(archive_path, options)
                .map_err(zip_error)?;
            let mut source = File::open(directory.join(relative_path))?;
            io::copy(&mut source, &mut archive)?;
        }
        archive.finish().map_err(zip_error)?;
        fs::rename(&temporary, &package)?;
        Ok::<_, io::Error>(())
    })();
    if let Err(error) = build_result {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    Ok(package)
}

fn collect_files(root: &Path, directory: &Path, files: &mut Vec<PathBuf>) -> io::Result<()> {
    let mut entries = fs::read_dir(directory)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|entry| entry.file_name().to_string_lossy().to_ascii_lowercase());
    for entry in entries {
        let path = entry.path();
        let kind = entry.file_type()?;
        if kind.is_symlink() {
            return Err(invalid_input(
                "component directories may not contain symlinks",
            ));
        }
        if kind.is_dir() {
            collect_files(root, &path, files)?;
        } else if kind.is_file() {
            let relative = path
                .strip_prefix(root)
                .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?
                .to_path_buf();
            if relative != Path::new("find.rdmap") {
                files.push(relative);
            }
        } else {
            return Err(invalid_input(
                "component directory contains an unsupported filesystem entry",
            ));
        }
    }
    Ok(())
}

fn zip_error(error: zip::result::ZipError) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, error)
}

fn invalid_input(error: impl Into<Box<dyn std::error::Error + Send + Sync>>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, error)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maker_builds_package_compatible_with_component_install() {
        let temp = std::env::temp_dir().join(format!(
            "reterm-maker-core-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let source = temp.join("nano");
        let executable = source.join("bin").join("nano.exe");
        fs::create_dir_all(executable.parent().unwrap()).unwrap();
        fs::write(&executable, b"nano app").unwrap();
        let package = create_from_fields(
            &source,
            Path::new("bin/nano.exe"),
            "Nano".to_owned(),
            "nano".to_owned(),
            "1.0.0".to_owned(),
            "Nano app".to_owned(),
        )
        .unwrap();
        assert_eq!(package, temp.canonicalize().unwrap().join("nano.comp"));
        fs::remove_dir_all(temp).unwrap();
    }

    #[test]
    fn maker_builds_source_only_mod() {
        let temp = std::env::temp_dir().join(format!(
            "reterm-mod-maker-test-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&temp).unwrap();
        let source_file = temp.join("hello.py");
        fs::write(
            &source_file,
            b"print(\"Hello from ReTerm mod!\")\nexit(0)\n",
        )
        .unwrap();
        let mod_output = temp.join("app.mod");

        let result = create_mod_package(
            &source_file,
            &mod_output,
            "Portable Mod".to_owned(),
            "nativemod".to_owned(),
            "3.0.0".to_owned(),
            "Portable source module".to_owned(),
            Some("WebTek".to_owned()),
            Some("MIT".to_owned()),
            None,
            None,
        )
        .unwrap();

        assert_eq!(result, mod_output);
        let bytes = fs::read(&mod_output).unwrap();
        assert!(bytes.starts_with(RMOD_MAGIC));
        assert!(!bytes
            .windows(b"print(\"Hello from ReTerm mod!\")".len())
            .any(|window| window == b"print(\"Hello from ReTerm mod!\")"));
        let archive = mod_crypto::decrypt_mod_payload(&bytes[RMOD_MAGIC.len()..]).unwrap();
        let mut archive = zip::ZipArchive::new(Cursor::new(archive)).unwrap();
        assert!(archive.by_name("nativemod.py").is_ok());
        fs::remove_dir_all(temp).unwrap();
    }

    #[test]
    fn maker_rejects_win32_exe_binary() {
        let temp = std::env::temp_dir().join(format!(
            "reterm-mod-reject-exe-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&temp).unwrap();
        let exe_file = temp.join("app.exe");
        fs::write(&exe_file, b"MZ\x90\x00fake pe header").unwrap();
        let mod_output = temp.join("app.mod");

        let result = create_mod_package(
            &exe_file,
            &mod_output,
            "Bad Mod".to_owned(),
            "badmod".to_owned(),
            "1.0.0".to_owned(),
            "Should fail".to_owned(),
            None,
            None,
            None,
            None,
        );

        assert!(result.is_err());
        let err_msg = result.unwrap_err().to_string();
        assert!(
            err_msg.contains("source code"),
            "Expected a source-only module error, got: {err_msg}"
        );
        fs::remove_dir_all(temp).unwrap();
    }

    #[test]
    fn module_compiler_rejects_unknown_extensions_and_unparsed_source() {
        assert!(compile_source_to_bytecode("not a script", "txt").is_err());
        assert!(compile_source_to_bytecode("this is not supported", "py").is_err());
        assert!(compile_source_to_bytecode("struct Nothing {};", "c").is_err());
        assert!(compile_source_to_bytecode("class Nothing {}", "cpp").is_err());
        assert!(compile_source_to_bytecode("PRINT hello", ".rts").is_ok());
        assert!(compile_source_to_bytecode("PRINT hello", "rts").is_ok());
        assert!(compile_source_to_bytecode("PRNT misspelled", "rts").is_err());
    }

    #[test]
    fn maker_requires_a_mod_output_path() {
        let temp = std::env::temp_dir().join(format!(
            "reterm-mod-output-extension-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&temp).unwrap();
        let source_file = temp.join("hello.rts");
        fs::write(&source_file, "PRINT hello\n").unwrap();

        let result = create_mod_package(
            &source_file,
            &temp.join("wrong.rbc"),
            "Test".to_owned(),
            "test".to_owned(),
            "1.0.0".to_owned(),
            "Test".to_owned(),
            None,
            None,
            None,
            None,
        );

        assert!(result.unwrap_err().to_string().contains(".mod extension"));
        assert!(!temp.join("wrong.rbc").exists());
        fs::remove_dir_all(temp).unwrap();
    }
}
