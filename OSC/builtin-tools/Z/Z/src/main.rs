mod parser {
    /// Z Language Parser
    /// ==================
    /// Syntax: Z\command("arg1", "arg2", ...)
    use std::fmt;

    // ---------------------------------------------------------------------------
    // AST
    // ---------------------------------------------------------------------------

    #[derive(Debug, Clone, PartialEq)]
    pub struct ZCommand {
        pub command: String,
        pub args: Vec<String>,
    }

    impl fmt::Display for ZCommand {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "Z\\{}({:?})", self.command, self.args)
        }
    }

    // ---------------------------------------------------------------------------
    // Parser
    // ---------------------------------------------------------------------------

    /// Parse a single line like: Z\burn("hello", "world")
    fn parse_line(line: &str) -> Result<Option<ZCommand>, String> {
        let trimmed = strip_comment(line).trim();
        if trimmed.is_empty() {
            return Ok(None);
        }

        // Must start with Z\ prefix
        let rest = trimmed
            .strip_prefix("Z\\")
            .ok_or_else(|| format!("expected 'Z\\' prefix, got: {trimmed}"))?;

        // Find the command name (everything before the first '(')
        let paren_pos = rest
            .find('(')
            .ok_or_else(|| format!("expected '(' after command name: {trimmed}"))?;

        let command = &rest[..paren_pos];
        if command.is_empty() {
            return Err(format!("empty command name: {trimmed}"));
        }

        // Validate command name is alphanumeric/underscore
        if !command
            .split('.')
            .all(|part| !part.is_empty() && part.chars().all(|c| c.is_alphanumeric() || c == '_'))
        {
            return Err(format!("invalid command name '{command}': {trimmed}"));
        }

        // The rest should be (...) — grab the inner content
        let args_section = &rest[paren_pos..];
        if !args_section.starts_with('(') || !args_section.ends_with(')') {
            return Err(format!("malformed arguments: {trimmed}"));
        }
        let inner = &args_section[1..args_section.len() - 1];

        // Parse quoted string arguments
        let args = parse_args(inner)?;

        Ok(Some(ZCommand {
            command: command.to_string(),
            args,
        }))
    }

    fn strip_comment(line: &str) -> &str {
        let mut chars = line.char_indices().peekable();
        let mut in_string = false;
        let mut escaped = false;

        while let Some((index, character)) = chars.next() {
            if in_string {
                if escaped {
                    escaped = false;
                } else {
                    match character {
                        '\\' => escaped = true,
                        '"' => in_string = false,
                        _ => {}
                    }
                }
            } else if character == '"' {
                in_string = true;
            } else if character == '/'
                && chars
                    .peek()
                    .is_some_and(|(_, next_character)| *next_character == '/')
            {
                return &line[..index];
            }
        }

        line
    }

    /// Parse comma-separated quoted string arguments: "arg1", "arg2"
    fn parse_args(input: &str) -> Result<Vec<String>, String> {
        let trimmed = input.trim();
        if trimmed.is_empty() {
            return Ok(vec![]);
        }

        let mut args = Vec::new();
        let mut chars = trimmed.chars().peekable();

        loop {
            // Skip whitespace
            while chars.peek().is_some_and(|c| c.is_whitespace()) {
                chars.next();
            }

            // Expect opening quote
            match chars.next() {
                Some('"') => {}
                Some(c) => return Err(format!("expected '\"', got '{c}'")),
                None => break,
            }

            // Collect until closing quote
            let mut arg = String::new();
            loop {
                match chars.next() {
                    Some('"') => break,
                    Some('\\') => {
                        // Handle escape sequences
                        match chars.next() {
                            Some('n') => arg.push('\n'),
                            Some('t') => arg.push('\t'),
                            Some('\\') => arg.push('\\'),
                            Some('"') => arg.push('"'),
                            Some(c) => {
                                arg.push('\\');
                                arg.push(c);
                            }
                            None => return Err("unterminated escape sequence".to_string()),
                        }
                    }
                    Some(c) => arg.push(c),
                    None => return Err("unterminated string".to_string()),
                }
            }
            args.push(arg);

            // Skip whitespace
            while chars.peek().is_some_and(|c| c.is_whitespace()) {
                chars.next();
            }

            // Expect comma or end
            match chars.peek() {
                Some(',') => {
                    chars.next();
                }
                Some(c) => return Err(format!("expected ',' or end of args, got '{c}'")),
                None => break,
            }
        }

        Ok(args)
    }

    pub fn parse_invocation(input: &str) -> Result<(String, Vec<String>), String> {
        let input = input.trim();
        let name_end = input
            .find('(')
            .ok_or_else(|| format!("expected module call, got '{input}'"))?;
        let name = input[..name_end].trim();
        if name.is_empty()
            || !name.split('.').all(|part| {
                !part.is_empty() && part.chars().all(|c| c.is_alphanumeric() || c == '_')
            })
        {
            return Err(format!("invalid module name in call '{input}'"));
        }

        let mut remaining = &input[name_end..];
        let mut args = Vec::new();
        loop {
            remaining = remaining.trim_start();
            if !remaining.starts_with('(') {
                return Err(format!("expected '(' in module call '{input}'"));
            }
            let (group, rest) = parenthesized_group(remaining)?;
            args.extend(parse_args(group)?);
            remaining = rest.trim_start();
            if remaining.is_empty() {
                break;
            }
            remaining = remaining.strip_prefix('+').ok_or_else(|| {
                format!("expected '+' between module-call arguments in '{input}'")
            })?;
        }
        Ok((name.to_string(), args))
    }

    fn parenthesized_group(input: &str) -> Result<(&str, &str), String> {
        let mut in_string = false;
        let mut escaped = false;
        let mut depth = 0usize;
        let mut start = None;
        for (index, character) in input.char_indices() {
            if in_string {
                if escaped {
                    escaped = false;
                } else if character == '\\' {
                    escaped = true;
                } else if character == '"' {
                    in_string = false;
                }
                continue;
            }
            match character {
                '"' => in_string = true,
                '(' => {
                    depth += 1;
                    if depth == 1 {
                        start = Some(index + character.len_utf8());
                    }
                }
                ')' => {
                    depth = depth
                        .checked_sub(1)
                        .ok_or_else(|| "unexpected ')' in module call".to_string())?;
                    if depth == 0 {
                        let start =
                            start.ok_or_else(|| "missing '(' in module call".to_string())?;
                        return Ok((&input[start..index], &input[index + 1..]));
                    }
                }
                _ => {}
            }
        }
        Err("unterminated argument list in module call".to_string())
    }

    /// Parse an entire Z source string into a list of commands.
    pub fn parse_program(source: &str) -> Result<Vec<ZCommand>, String> {
        let mut commands = Vec::new();

        for (i, line) in source.lines().enumerate() {
            match parse_line(line) {
                Ok(Some(cmd)) => commands.push(cmd),
                Ok(None) => {} // blank line
                Err(e) => return Err(format!("line {}: {}", i + 1, e)),
            }
        }

        Ok(commands)
    }

    // ---------------------------------------------------------------------------
    // Tests
    // ---------------------------------------------------------------------------

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn test_parse_single_command() {
            let source = "Z\\burn(\"hello\")";
            let program = parse_program(source).unwrap();
            assert_eq!(program.len(), 1);
            assert_eq!(program[0].command, "burn");
            assert_eq!(program[0].args, vec!["hello"]);
        }

        #[test]
        fn test_parse_multiple_commands() {
            let source = "Z\\burn(\"hello\")\nZ\\spam(\"hi\")\n";
            let program = parse_program(source).unwrap();
            assert_eq!(program.len(), 2);
            assert_eq!(program[0].command, "burn");
            assert_eq!(program[0].args, vec!["hello"]);
            assert_eq!(program[1].command, "spam");
            assert_eq!(program[1].args, vec!["hi"]);
        }

        #[test]
        fn test_parse_multiple_args() {
            let source = "Z\\burn(\"hello\", \"world\")";
            let program = parse_program(source).unwrap();
            assert_eq!(program.len(), 1);
            assert_eq!(program[0].args, vec!["hello", "world"]);
        }

        #[test]
        fn test_parse_trailing_comma() {
            let program = parse_program("Z\\burn(\"hello\",)").unwrap();
            assert_eq!(program[0].args, vec!["hello"]);
        }

        #[test]
        fn test_parse_legalshit() {
            let source = "Z\\legalshit(\"READ OUR FAKE TOS OR DIE\")";
            let program = parse_program(source).unwrap();
            assert_eq!(program[0].command, "legalshit");
            assert_eq!(program[0].args, vec!["READ OUR FAKE TOS OR DIE"]);
        }

        #[test]
        fn test_parse_empty_lines_skipped() {
            let source = "\nZ\\burn(\"hey\")\n\n";
            let program = parse_program(source).unwrap();
            assert_eq!(program.len(), 1);
        }

        #[test]
        fn test_parse_escape_sequences() {
            let source = "Z\\burn(\"hello\\nworld\")";
            let program = parse_program(source).unwrap();
            assert_eq!(program[0].args, vec!["hello\nworld"]);
        }

        #[test]
        fn test_parse_error_no_prefix() {
            let result = parse_program("burn(\"hello\")");
            assert!(result.is_err());
        }

        #[test]
        fn test_parse_error_unterminated_string() {
            let result = parse_program("Z\\burn(\"hello)");
            assert!(result.is_err());
        }

        #[test]
        fn test_parse_error_extra_closing_parenthesis() {
            let result = parse_program("Z\\burn(\"hello\"))");
            assert!(result.is_err());
        }

        #[test]
        fn test_parse_comments_outside_strings() {
            let program = parse_program(
                "// a full-line comment\nZ\\burn(\"https://example.com\") // trailing comment",
            )
            .unwrap();
            assert_eq!(program.len(), 1);
            assert_eq!(program[0].args, vec!["https://example.com"]);
        }
    }
}

mod runtime {
    /// Z Language Runtime
    /// ===================
    /// Executes parsed Z commands.
    use crate::parser::ZCommand;

    // ---------------------------------------------------------------------------
    // Windows native popups
    // ---------------------------------------------------------------------------

    #[cfg(all(windows, not(test)))]
    fn show_message_box(title: &str, text: &str, flags: u32) {
        use std::ffi::OsStr;
        use std::iter::once;
        use std::os::windows::ffi::OsStrExt;

        fn to_wide(s: &str) -> Vec<u16> {
            OsStr::new(s).encode_wide().chain(once(0)).collect()
        }

        let title_w = to_wide(title);
        let text_w = to_wide(text);

        unsafe {
            windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxW(
                std::ptr::null_mut(),
                text_w.as_ptr(),
                title_w.as_ptr(),
                flags,
            );
        }
    }

    #[cfg(any(not(windows), test))]
    fn show_message_box(_title: &str, text: &str, _flags: u32) {
        println!("[POPUP] {text}");
    }

    // ---------------------------------------------------------------------------
    // Command implementations
    // ---------------------------------------------------------------------------

    /// burn — prints text to stdout
    fn exec_burn(args: &[String]) -> Result<String, String> {
        let output = args.join("\n");
        println!("{output}");
        Ok(output)
    }

    /// spam — fires a popup notification
    fn exec_spam(args: &[String]) -> Result<String, String> {
        let text = args.join("\n");
        // MB_ICONINFORMATION = 0x40
        show_message_box("Z\\spam", &text, 0x00000040);
        Ok(text)
    }

    /// legalshit — shows a fake TOS dialog
    fn exec_legalshit(args: &[String]) -> Result<String, String> {
        let text = args.join("\n");
        // MB_YESNO | MB_ICONWARNING = 0x34
        show_message_box("Z\\legalshit — AGREE OR ELSE", &text, 0x00000034);
        Ok(text)
    }

    // ---------------------------------------------------------------------------
    // Dispatcher
    // ---------------------------------------------------------------------------

    fn dispatch(cmd: &ZCommand) -> Result<String, String> {
        match cmd.command.as_str() {
            "burn" => exec_burn(&cmd.args),
            "spam" => exec_spam(&cmd.args),
            "legalshit" => exec_legalshit(&cmd.args),
            other => Err(format!("unknown command: '{other}'")),
        }
    }

    /// Execute a parsed Z program.
    pub fn run_program(commands: &[ZCommand]) -> Result<String, String> {
        let mut outputs = Vec::new();

        for cmd in commands {
            let result = dispatch(cmd)?;
            outputs.push(result);
        }

        Ok(outputs.join("\n"))
    }

    // ---------------------------------------------------------------------------
    // Tests
    // ---------------------------------------------------------------------------

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::parser::parse_program;

        #[test]
        fn test_run_burn() {
            let program = parse_program("Z\\burn(\"hello\")").unwrap();
            let output = run_program(&program).unwrap();
            assert!(output.contains("hello"));
        }

        #[test]
        fn test_run_legalshit() {
            // On CI / non-interactive, this just returns the text
            let program = parse_program("Z\\legalshit(\"terms\")").unwrap();
            let output = run_program(&program).unwrap();
            assert!(output.contains("terms"));
        }

        #[test]
        fn test_run_multiple() {
            let source = "Z\\burn(\"hello\")\nZ\\burn(\"world\")";
            let program = parse_program(source).unwrap();
            let output = run_program(&program).unwrap();
            assert!(output.contains("hello"));
            assert!(output.contains("world"));
        }

        #[test]
        fn test_run_unknown_command() {
            let program = vec![ZCommand {
                command: "explode".to_string(),
                args: vec!["boom".to_string()],
            }];
            let result = run_program(&program);
            assert!(result.is_err());
        }
    }
}

mod language {
    use crate::parser::{self, ZCommand};
    use std::collections::HashMap;
    use std::fs;
    use std::path::{Path, PathBuf};

    #[derive(Debug)]
    enum Statement {
        Command(ZCommand),
        Call {
            module: String,
            function: Option<String>,
            args: Vec<String>,
        },
    }

    #[derive(Debug)]
    struct Module {
        imports: Vec<PathBuf>,
        statements: Vec<Statement>,
        functions: HashMap<String, Vec<ZCommand>>,
        flag_functions: HashMap<String, String>,
        callby: Option<String>,
    }

    pub fn expand_file(path: &Path) -> Result<Vec<ZCommand>, String> {
        let source = fs::read_to_string(path)
            .map_err(|error| format!("can't read Z program '{}': {error}", path.display()))?;
        if source.trim_start().starts_with('<') {
            return xml::expand_file(path, &mut Vec::new());
        }
        let mut stack = Vec::new();
        expand_module(path, true, None, None, &mut stack)
    }

    mod xml {
        use super::*;

        #[derive(Debug, Clone, PartialEq)]
        struct Node {
            name: String,
            attributes: HashMap<String, String>,
            children: Vec<Node>,
            text: String,
        }

        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        enum Type {
            String,
            I64,
            Bool,
        }

        #[derive(Debug, Clone, PartialEq)]
        enum Value {
            String(String),
            I64(i64),
            Bool(bool),
        }

        #[derive(Debug, Clone)]
        struct Function {
            params: Vec<(String, Type)>,
            return_type: Option<Type>,
            body: Vec<Node>,
        }

        type Scope = HashMap<String, (Type, Value)>;

        enum Flow {
            Continue,
            Return(Value),
        }

        const MAX_EXECUTION_STEPS: usize = 100_000;

        pub fn expand_file(path: &Path, stack: &mut Vec<PathBuf>) -> Result<Vec<ZCommand>, String> {
            let path = path
                .canonicalize()
                .map_err(|error| format!("can't resolve '{}': {error}", path.display()))?;
            if stack.contains(&path) {
                return Err(format!(
                    "cyclic XML Z import involving '{}'",
                    path.display()
                ));
            }
            stack.push(path.clone());
            let result = expand_file_inner(&path, stack);
            stack.pop();
            result
        }

        fn expand_file_inner(
            path: &Path,
            stack: &mut Vec<PathBuf>,
        ) -> Result<Vec<ZCommand>, String> {
            let source = fs::read_to_string(path).map_err(|error| {
                format!("can't read XML Z program '{}': {error}", path.display())
            })?;
            let root = parse_document(&source)?;
            if root.name != "program" {
                return Err(format!(
                    "XML Z file '{}' must have a <program> root",
                    path.display()
                ));
            }

            let mut functions = HashMap::new();
            collect_functions(&root, &mut functions)?;
            collect_imported_functions(
                &root,
                path.parent().unwrap_or_else(|| Path::new(".")),
                &mut functions,
                stack,
            )?;
            let mut globals = Scope::new();
            let mut output = Vec::new();
            let mut evaluator = Evaluator {
                functions: &functions,
                scopes: vec![Scope::new()],
                output: &mut output,
                call_depth: 0,
                execution_steps: 0,
            };
            let base = path.parent().unwrap_or_else(|| Path::new("."));
            if matches!(
                evaluator.execute_nodes(&root.children, base, stack, &mut globals)?,
                Flow::Return(_)
            ) {
                return Err("<return> can only be used inside a function".to_string());
            }
            Ok(output)
        }

        fn collect_functions(
            root: &Node,
            functions: &mut HashMap<String, Function>,
        ) -> Result<(), String> {
            for node in &root.children {
                if node.name == "import" {
                    continue;
                }
                if node.name == "fn" {
                    let name = required_attribute(node, "name")?;
                    let mut params = Vec::new();
                    for child in node.children.iter().filter(|child| child.name == "param") {
                        params.push((
                            required_attribute(child, "name")?.to_string(),
                            parse_type(&required_attribute(child, "type")?)?,
                        ));
                    }
                    let return_type = node
                        .attributes
                        .get("return")
                        .map(|name| parse_type(name))
                        .transpose()?;
                    let function = Function {
                        params,
                        return_type,
                        body: node
                            .children
                            .iter()
                            .filter(|child| child.name != "param")
                            .cloned()
                            .collect(),
                    };
                    if functions.insert(name.to_string(), function).is_some() {
                        return Err(format!("function '{name}' is defined more than once"));
                    }
                }
            }
            Ok(())
        }

        fn collect_imported_functions(
            root: &Node,
            base: &Path,
            functions: &mut HashMap<String, Function>,
            stack: &mut Vec<PathBuf>,
        ) -> Result<(), String> {
            for node in root.children.iter().filter(|node| node.name == "import") {
                let imported_path = resolve_xml_import(base, required_attribute(node, "file")?)?
                    .canonicalize()
                    .map_err(|error| format!("can't resolve imported XML Z file: {error}"))?;
                if stack.contains(&imported_path) {
                    return Err(format!(
                        "cyclic XML Z import involving '{}'",
                        imported_path.display()
                    ));
                }
                stack.push(imported_path.clone());
                let result = (|| {
                    let source = fs::read_to_string(&imported_path).map_err(|error| {
                        format!(
                            "can't read imported XML Z file '{}': {error}",
                            imported_path.display()
                        )
                    })?;
                    let imported_root = parse_document(&source)?;
                    if imported_root.name != "program" {
                        return Err(format!(
                            "XML Z file '{}' must have a <program> root",
                            imported_path.display()
                        ));
                    }
                    collect_functions(&imported_root, functions)?;
                    collect_imported_functions(
                        &imported_root,
                        imported_path.parent().unwrap_or_else(|| Path::new(".")),
                        functions,
                        stack,
                    )
                })();
                stack.pop();
                result?;
            }
            Ok(())
        }

        struct Evaluator<'a> {
            functions: &'a HashMap<String, Function>,
            scopes: Vec<Scope>,
            output: &'a mut Vec<ZCommand>,
            call_depth: usize,
            execution_steps: usize,
        }

        impl Evaluator<'_> {
            fn tick(&mut self) -> Result<(), String> {
                if self.execution_steps >= MAX_EXECUTION_STEPS {
                    return Err(format!(
                        "XML execution step limit exceeded ({MAX_EXECUTION_STEPS}); check for an infinite loop"
                    ));
                }
                self.execution_steps += 1;
                Ok(())
            }

            fn execute_nodes(
                &mut self,
                nodes: &[Node],
                base: &Path,
                imports: &mut Vec<PathBuf>,
                globals: &mut Scope,
            ) -> Result<Flow, String> {
                for node in nodes {
                    self.tick()?;
                    match node.name.as_str() {
                        "fn" | "param" => {}
                        "import" => {
                            let requested = required_attribute(node, "file")?;
                            let imported = resolve_xml_import(base, requested)?;
                            let imported_commands = expand_file(&imported, imports)?;
                            self.output.extend(imported_commands);
                        }
                        "let" => {
                            let name = required_attribute(node, "name")?;
                            let declared = parse_type(&required_attribute(node, "type")?)?;
                            let value = self.evaluate(
                                node.attributes
                                    .get("value")
                                    .map(String::as_str)
                                    .unwrap_or(node.text.trim()),
                            )?;
                            self.bind(name, declared, value)?;
                        }
                        "set" => {
                            let name = required_attribute(node, "name")?;
                            let value = self.evaluate(
                                node.attributes
                                    .get("value")
                                    .map(String::as_str)
                                    .unwrap_or(node.text.trim()),
                            )?;
                            self.assign(name, value)?;
                        }
                        "if" => {
                            let condition = self.evaluate(required_attribute(node, "test")?)?;
                            let Value::Bool(condition) = condition else {
                                return Err("<if test> expression must be boolean".to_string());
                            };
                            let branch = if condition {
                                node.children
                                    .iter()
                                    .filter(|child| child.name != "else")
                                    .collect::<Vec<_>>()
                            } else {
                                node.children
                                    .iter()
                                    .filter(|child| child.name == "else")
                                    .flat_map(|child| child.children.iter())
                                    .collect::<Vec<_>>()
                            };
                            let branch: Vec<Node> = branch.into_iter().cloned().collect();
                            if let Flow::Return(value) =
                                self.execute_nodes(&branch, base, imports, globals)?
                            {
                                return Ok(Flow::Return(value));
                            }
                        }
                        "while" => loop {
                            self.tick()?;
                            let condition = self.evaluate(required_attribute(node, "test")?)?;
                            let Value::Bool(condition) = condition else {
                                return Err("<while test> expression must be boolean".to_string());
                            };
                            if !condition {
                                break;
                            }
                            if let Flow::Return(value) =
                                self.execute_nodes(&node.children, base, imports, globals)?
                            {
                                return Ok(Flow::Return(value));
                            }
                        },
                        "call" => {
                            let name = required_attribute(node, "name")?;
                            let mut args = Vec::new();
                            for argument in node.children.iter().filter(|child| child.name == "arg")
                            {
                                args.push(
                                    self.evaluate(
                                        argument
                                            .attributes
                                            .get("value")
                                            .map(String::as_str)
                                            .unwrap_or(argument.text.trim()),
                                    )?,
                                );
                            }
                            let result = self.call_function(name, args, base, imports, globals)?;
                            if let Some(target) = node.attributes.get("result") {
                                let value = result.ok_or_else(|| {
                                    format!(
                                        "void function '{name}' cannot assign a call result to '{target}'"
                                    )
                                })?;
                                self.assign(target, value)?;
                            }
                        }
                        "return" => {
                            if self.call_depth == 0 {
                                return Err(
                                    "<return> can only be used inside a function".to_string()
                                );
                            }
                            let value = self.evaluate(required_attribute(node, "value")?)?;
                            return Ok(Flow::Return(value));
                        }
                        "burn" | "spam" | "legalshit" => {
                            let text = interpolate(
                                node.attributes
                                    .get("value")
                                    .map(String::as_str)
                                    .unwrap_or(&node.text),
                                self,
                            )?;
                            self.output.push(ZCommand {
                                command: node.name.clone(),
                                args: vec![text],
                            });
                        }
                        name => return Err(format!("unknown XML Z element <{name}>")),
                    }
                }
                Ok(Flow::Continue)
            }

            fn call_function(
                &mut self,
                name: &str,
                args: Vec<Value>,
                base: &Path,
                imports: &mut Vec<PathBuf>,
                globals: &mut Scope,
            ) -> Result<Option<Value>, String> {
                if self.call_depth >= 256 {
                    return Err(format!("maximum function call depth exceeded in '{name}'"));
                }
                let function = self
                    .functions
                    .get(name)
                    .ok_or_else(|| format!("function '{name}' is not defined"))?
                    .clone();
                if args.len() != function.params.len() {
                    return Err(format!(
                        "function '{name}' expects {} arguments but received {}",
                        function.params.len(),
                        args.len()
                    ));
                }
                let mut scope = Scope::new();
                for ((param_name, expected), value) in function.params.iter().zip(args) {
                    validate_type(&value, *expected, param_name)?;
                    scope.insert(param_name.clone(), (*expected, value));
                }
                self.scopes.push(scope);
                self.call_depth += 1;
                let flow = self.execute_nodes(&function.body, base, imports, globals);
                self.call_depth -= 1;
                self.scopes.pop();
                let value = match flow? {
                    Flow::Return(value) => Some(value),
                    Flow::Continue => None,
                };
                match (function.return_type, value) {
                    (Some(expected), Some(value)) => {
                        validate_type(&value, expected, name)?;
                        Ok(Some(value))
                    }
                    (Some(_), None) => Err(format!("function '{name}' must return a value")),
                    (None, Some(_)) => Err(format!("void function '{name}' cannot return a value")),
                    (None, None) => Ok(None),
                }
            }

            fn bind(&mut self, name: &str, declared: Type, value: Value) -> Result<(), String> {
                validate_type(&value, declared, name)?;
                let scope = self
                    .scopes
                    .last_mut()
                    .ok_or_else(|| "missing variable scope".to_string())?;
                if scope.contains_key(name) {
                    return Err(format!(
                        "variable '{name}' is already declared in this scope"
                    ));
                }
                scope.insert(name.to_string(), (declared, value));
                Ok(())
            }

            fn assign(&mut self, name: &str, value: Value) -> Result<(), String> {
                for scope in self.scopes.iter_mut().rev() {
                    if let Some((declared, slot)) = scope.get_mut(name) {
                        validate_type(&value, *declared, name)?;
                        *slot = value;
                        return Ok(());
                    }
                }
                Err(format!("cannot assign to undeclared variable '{name}'"))
            }

            fn lookup(&self, name: &str) -> Result<Value, String> {
                for scope in self.scopes.iter().rev() {
                    if let Some((_, value)) = scope.get(name) {
                        return Ok(value.clone());
                    }
                }
                Err(format!("unknown variable '{name}'"))
            }

            fn evaluate(&self, input: &str) -> Result<Value, String> {
                let tokens = lex_expression(input)?;
                let mut parser = ExpressionParser {
                    tokens: &tokens,
                    position: 0,
                    evaluator: self,
                };
                let value = parser.parse_expression(0)?;
                if parser.position != tokens.len() {
                    return Err(format!("unexpected token in expression '{}'", input));
                }
                Ok(value)
            }
        }

        fn interpolate(text: &str, evaluator: &Evaluator<'_>) -> Result<String, String> {
            let mut result = String::new();
            let mut remaining = text;
            while let Some(open) = remaining.find('{') {
                result.push_str(&remaining[..open]);
                let after_open = &remaining[open + 1..];
                let close = after_open
                    .find('}')
                    .ok_or_else(|| "unterminated `{...}` variable interpolation".to_string())?;
                let value = evaluator.evaluate(after_open[..close].trim())?;
                result.push_str(&display_value(&value));
                remaining = &after_open[close + 1..];
            }
            result.push_str(remaining);
            Ok(result)
        }

        #[derive(Debug, Clone, PartialEq)]
        enum Token {
            Integer(i64),
            String(String),
            Boolean(bool),
            Identifier(String),
            Operator(String),
            LeftParen,
            RightParen,
        }

        fn lex_expression(input: &str) -> Result<Vec<Token>, String> {
            let chars: Vec<char> = input.chars().collect();
            let mut tokens = Vec::new();
            let mut index = 0;
            while index < chars.len() {
                let character = chars[index];
                if character.is_whitespace() {
                    index += 1;
                } else if character == '"' {
                    index += 1;
                    let mut value = String::new();
                    let mut closed = false;
                    while index < chars.len() {
                        match chars[index] {
                            '"' => {
                                closed = true;
                                index += 1;
                                break;
                            }
                            '\\' if index + 1 < chars.len() => {
                                index += 1;
                                value.push(match chars[index] {
                                    'n' => '\n',
                                    't' => '\t',
                                    other => other,
                                });
                                index += 1;
                            }
                            other => {
                                value.push(other);
                                index += 1;
                            }
                        }
                    }
                    if !closed {
                        return Err("unterminated string in expression".to_string());
                    }
                    tokens.push(Token::String(value));
                } else if character.is_ascii_digit() {
                    let start = index;
                    index += 1;
                    while index < chars.len() && chars[index].is_ascii_digit() {
                        index += 1;
                    }
                    let number: String = chars[start..index].iter().collect();
                    tokens.push(Token::Integer(number.parse().map_err(|_| {
                        format!("integer literal '{number}' is out of range")
                    })?));
                } else if character.is_ascii_alphabetic() || character == '_' {
                    let start = index;
                    index += 1;
                    while index < chars.len()
                        && (chars[index].is_ascii_alphanumeric() || chars[index] == '_')
                    {
                        index += 1;
                    }
                    let name: String = chars[start..index].iter().collect();
                    tokens.push(match name.as_str() {
                        "true" => Token::Boolean(true),
                        "false" => Token::Boolean(false),
                        _ => Token::Identifier(name),
                    });
                } else if character == '(' {
                    tokens.push(Token::LeftParen);
                    index += 1;
                } else if character == ')' {
                    tokens.push(Token::RightParen);
                    index += 1;
                } else {
                    let two: String = chars[index..chars.len().min(index + 2)].iter().collect();
                    if ["==", "!=", "<=", ">=", "&&", "||"].contains(&two.as_str()) {
                        tokens.push(Token::Operator(two));
                        index += 2;
                    } else if "+-*/%<>!".contains(character) {
                        tokens.push(Token::Operator(character.to_string()));
                        index += 1;
                    } else {
                        return Err(format!("invalid character '{character}' in expression"));
                    }
                }
            }
            Ok(tokens)
        }

        struct ExpressionParser<'a, 'b> {
            tokens: &'a [Token],
            position: usize,
            evaluator: &'b Evaluator<'b>,
        }

        impl ExpressionParser<'_, '_> {
            fn parse_expression(&mut self, min_precedence: u8) -> Result<Value, String> {
                self.parse_expression_with_mode(min_precedence, true)
            }

            fn parse_expression_with_mode(
                &mut self,
                min_precedence: u8,
                evaluate: bool,
            ) -> Result<Value, String> {
                let mut left = self.parse_prefix(evaluate)?;
                loop {
                    let Some(Token::Operator(operator)) = self.tokens.get(self.position) else {
                        break;
                    };
                    let Some(precedence) = precedence(operator) else {
                        break;
                    };
                    if precedence < min_precedence {
                        break;
                    }
                    let operator = operator.clone();
                    self.position += 1;
                    let should_short_circuit = match (&operator[..], &left) {
                        ("&&", Value::Bool(false)) => true,
                        ("||", Value::Bool(true)) => true,
                        ("&&", Value::Bool(true)) | ("||", Value::Bool(false)) => false,
                        ("&&" | "||", _) if evaluate => {
                            return Err(format!(
                                "operator '{operator}' requires bool operands, got {left:?}"
                            ));
                        }
                        _ => false,
                    };
                    let right = self.parse_expression_with_mode(
                        precedence + 1,
                        evaluate && !should_short_circuit,
                    )?;
                    left = if !evaluate {
                        Value::Bool(false)
                    } else if should_short_circuit {
                        Value::Bool(operator == "||")
                    } else {
                        apply_operator(&operator, left, right)?
                    };
                }
                Ok(left)
            }

            fn parse_prefix(&mut self, evaluate: bool) -> Result<Value, String> {
                match self.tokens.get(self.position).cloned() {
                    Some(Token::Integer(value)) => {
                        self.position += 1;
                        Ok(Value::I64(value))
                    }
                    Some(Token::String(value)) => {
                        self.position += 1;
                        Ok(Value::String(value))
                    }
                    Some(Token::Boolean(value)) => {
                        self.position += 1;
                        Ok(Value::Bool(value))
                    }
                    Some(Token::Identifier(name)) => {
                        self.position += 1;
                        if evaluate {
                            self.evaluator.lookup(&name)
                        } else {
                            Ok(Value::Bool(false))
                        }
                    }
                    Some(Token::Operator(operator)) if operator == "-" || operator == "!" => {
                        self.position += 1;
                        let value = self.parse_expression_with_mode(7, evaluate)?;
                        if !evaluate {
                            return Ok(Value::Bool(false));
                        }
                        match (operator.as_str(), value) {
                            ("-", Value::I64(value)) => value
                                .checked_neg()
                                .map(Value::I64)
                                .ok_or_else(|| "integer overflow".to_string()),
                            ("!", Value::Bool(value)) => Ok(Value::Bool(!value)),
                            ("-", _) => Err("unary '-' requires an i64".to_string()),
                            _ => Err("unary '!' requires a bool".to_string()),
                        }
                    }
                    Some(Token::LeftParen) => {
                        self.position += 1;
                        let value = self.parse_expression_with_mode(0, evaluate)?;
                        if self.tokens.get(self.position) != Some(&Token::RightParen) {
                            return Err("expected ')' in expression".to_string());
                        }
                        self.position += 1;
                        Ok(value)
                    }
                    _ => Err("expected a value in expression".to_string()),
                }
            }
        }

        fn precedence(operator: &str) -> Option<u8> {
            Some(match operator {
                "||" => 1,
                "&&" => 2,
                "==" | "!=" => 3,
                "<" | "<=" | ">" | ">=" => 4,
                "+" | "-" => 5,
                "*" | "/" | "%" => 6,
                _ => return None,
            })
        }

        fn apply_operator(operator: &str, left: Value, right: Value) -> Result<Value, String> {
            match (operator, left, right) {
                ("+", Value::I64(a), Value::I64(b)) => a
                    .checked_add(b)
                    .map(Value::I64)
                    .ok_or_else(|| "integer overflow in '+'".to_string()),
                ("-", Value::I64(a), Value::I64(b)) => a
                    .checked_sub(b)
                    .map(Value::I64)
                    .ok_or_else(|| "integer overflow in '-'".to_string()),
                ("*", Value::I64(a), Value::I64(b)) => a
                    .checked_mul(b)
                    .map(Value::I64)
                    .ok_or_else(|| "integer overflow in '*'".to_string()),
                ("/", Value::I64(_), Value::I64(0)) => Err("division by zero".to_string()),
                ("/", Value::I64(a), Value::I64(b)) => a
                    .checked_div(b)
                    .map(Value::I64)
                    .ok_or_else(|| "integer overflow in '/'".to_string()),
                ("%", Value::I64(_), Value::I64(0)) => Err("remainder by zero".to_string()),
                ("%", Value::I64(a), Value::I64(b)) => a
                    .checked_rem(b)
                    .map(Value::I64)
                    .ok_or_else(|| "integer overflow in '%'".to_string()),
                ("+", Value::String(a), Value::String(b)) => Ok(Value::String(a + &b)),
                ("==", a, b) => Ok(Value::Bool(a == b)),
                ("!=", a, b) => Ok(Value::Bool(a != b)),
                ("<", Value::I64(a), Value::I64(b)) => Ok(Value::Bool(a < b)),
                ("<=", Value::I64(a), Value::I64(b)) => Ok(Value::Bool(a <= b)),
                (">", Value::I64(a), Value::I64(b)) => Ok(Value::Bool(a > b)),
                (">=", Value::I64(a), Value::I64(b)) => Ok(Value::Bool(a >= b)),
                ("&&", Value::Bool(a), Value::Bool(b)) => Ok(Value::Bool(a && b)),
                ("||", Value::Bool(a), Value::Bool(b)) => Ok(Value::Bool(a || b)),
                (operator, _, _) => Err(format!(
                    "operator '{operator}' received incompatible values"
                )),
            }
        }

        fn parse_type(name: &str) -> Result<Type, String> {
            match name {
                "string" | "str" => Ok(Type::String),
                "i64" | "int" => Ok(Type::I64),
                "bool" => Ok(Type::Bool),
                other => Err(format!("unsupported type '{other}'")),
            }
        }

        fn validate_type(value: &Value, expected: Type, name: &str) -> Result<(), String> {
            let actual = match value {
                Value::String(_) => Type::String,
                Value::I64(_) => Type::I64,
                Value::Bool(_) => Type::Bool,
            };
            if actual != expected {
                return Err(format!(
                    "'{name}' requires type {}, got {}",
                    type_name(expected),
                    type_name(actual)
                ));
            }
            Ok(())
        }

        fn type_name(value: Type) -> &'static str {
            match value {
                Type::String => "string",
                Type::I64 => "i64",
                Type::Bool => "bool",
            }
        }

        fn display_value(value: &Value) -> String {
            match value {
                Value::String(value) => value.clone(),
                Value::I64(value) => value.to_string(),
                Value::Bool(value) => value.to_string(),
            }
        }

        fn required_attribute<'a>(node: &'a Node, name: &str) -> Result<&'a str, String> {
            node.attributes
                .get(name)
                .map(String::as_str)
                .ok_or_else(|| format!("<{}> requires '{name}' attribute", node.name))
        }

        fn resolve_xml_import(base: &Path, requested: &str) -> Result<PathBuf, String> {
            let mut path = base.join(requested);
            if path.extension().is_none() {
                path.set_extension("z");
            }
            path.canonicalize()
                .map_err(|error| format!("can't import '{}': {error}", path.display()))
        }

        fn parse_document(source: &str) -> Result<Node, String> {
            let document = roxmltree::Document::parse(source)
                .map_err(|error| format!("invalid XML document: {error}"))?;
            let root = document.root_element();
            Ok(convert_element(root))
        }

        fn convert_element(element: roxmltree::Node<'_, '_>) -> Node {
            let name = element.tag_name().name().to_string();
            let attributes = element
                .attributes()
                .map(|attribute| (attribute.name().to_string(), attribute.value().to_string()))
                .collect();
            let mut children = Vec::new();
            let mut text = String::new();
            for child in element.children() {
                if child.is_element() {
                    children.push(convert_element(child));
                } else if child.is_text() {
                    if let Some(value) = child.text() {
                        text.push_str(value);
                    }
                }
            }
            Node {
                name,
                attributes,
                children,
                text,
            }
        }

        #[cfg(test)]
        mod tests {
            use super::*;
            use std::sync::atomic::{AtomicUsize, Ordering};

            static NEXT_TEST_ID: AtomicUsize = AtomicUsize::new(0);

            fn expand_test_program(source: &str) -> Result<Vec<ZCommand>, String> {
                let directory = std::env::temp_dir().join(format!(
                    "z-lang-xml-test-{}-{}",
                    std::process::id(),
                    NEXT_TEST_ID.fetch_add(1, Ordering::Relaxed)
                ));
                fs::create_dir_all(&directory)
                    .map_err(|error| format!("failed creating test directory: {error}"))?;
                let path = directory.join("main.xmlz");
                let result = (|| {
                    fs::write(&path, source)
                        .map_err(|error| format!("failed writing test program: {error}"))?;
                    expand_file(&path, &mut Vec::new())
                })();
                let _ = fs::remove_file(&path);
                let _ = fs::remove_dir(&directory);
                result
            }

            #[test]
            fn call_result_assigns_returned_typed_value() {
                let program = r#"
                    <program>
                      <fn name="twice" return="i64">
                        <param name="n" type="i64"/>
                        <return value="n * 2"/>
                      </fn>
                      <let name="input" type="i64" value="21"/>
                      <let name="answer" type="i64" value="0"/>
                      <call name="twice" result="answer">
                        <arg value="input"/>
                      </call>
                      <burn value="Answer: {answer}"/>
                    </program>
                "#;

                let commands = expand_test_program(program).unwrap();
                assert_eq!(commands.len(), 1);
                assert_eq!(commands[0].command, "burn");
                assert_eq!(commands[0].args, ["Answer: 42"]);
            }

            #[test]
            fn call_result_rejects_void_function() {
                let program = r#"
                    <program>
                      <fn name="noop"/>
                      <let name="result" type="bool" value="false"/>
                      <call name="noop" result="result"/>
                    </program>
                "#;

                let error = expand_test_program(program).unwrap_err();
                assert!(error.contains("void function 'noop'"));
            }

            #[test]
            fn return_is_rejected_outside_function() {
                let program = r#"<program><return value="1"/></program>"#;
                assert!(
                    expand_test_program(program)
                        .unwrap_err()
                        .contains("<return> can only be used inside a function")
                );
            }

            #[test]
            fn logical_operators_short_circuit() {
                let program = r#"
                    <program>
                      <if test="false &amp;&amp; (1 / 0 == 0)">
                        <burn value="wrong"/>
                        <else><burn value="and skipped"/></else>
                      </if>
                      <if test="true || (1 / 0 == 0)">
                        <burn value="or skipped"/>
                      </if>
                    </program>
                "#;

                let commands = expand_test_program(program).unwrap();
                assert_eq!(
                    commands
                        .iter()
                        .map(|command| command.args[0].as_str())
                        .collect::<Vec<_>>(),
                    ["and skipped", "or skipped"]
                );
            }

            #[test]
            fn infinite_empty_loop_fails_at_execution_step_limit() {
                let program = r#"<program><while test="true"/></program>"#;
                let error = expand_test_program(program).unwrap_err();
                assert!(error.contains("XML execution step limit exceeded"));
            }

            #[test]
            fn xml_parser_handles_declaration_comments_and_entities() {
                let root = parse_document(
                    r#"<?xml version="1.0"?><program><!-- comment --><burn value="A &amp; B"/></program>"#,
                )
                .unwrap();
                assert_eq!(root.name, "program");
                assert_eq!(root.children.len(), 1);
                assert_eq!(root.children[0].attributes["value"], "A & B");
            }
        }
    }

    fn expand_module(
        path: &Path,
        run_body: bool,
        call_args: Option<&[String]>,
        function: Option<&str>,
        stack: &mut Vec<PathBuf>,
    ) -> Result<Vec<ZCommand>, String> {
        let path = path
            .canonicalize()
            .map_err(|error| format!("can't resolve Z module '{}': {error}", path.display()))?;
        if stack.contains(&path) {
            return Err(format!(
                "cyclic Z module call involving '{}'",
                path.display()
            ));
        }
        stack.push(path.clone());
        let result = expand_module_inner(&path, run_body, call_args, function, stack);
        stack.pop();
        result
    }

    fn expand_module_inner(
        path: &Path,
        run_body: bool,
        call_args: Option<&[String]>,
        function: Option<&str>,
        stack: &mut Vec<PathBuf>,
    ) -> Result<Vec<ZCommand>, String> {
        let source = fs::read_to_string(path)
            .map_err(|error| format!("can't read Z module '{}': {error}", path.display()))?;
        let module = parse_module(&source, path)?;

        if !run_body {
            let requested_function = if let Some(function) = function {
                function.to_string()
            } else {
                let flag = call_args
                    .unwrap_or_default()
                    .iter()
                    .find(|argument| argument.starts_with('-'))
                    .map(|argument| argument.trim_start_matches('-'))
                    .ok_or_else(|| {
                        format!(
                            "module '{}' requires a function name or a dispatch flag",
                            path.display()
                        )
                    })?;
                module.flag_functions.get(flag).cloned().ok_or_else(|| {
                    format!(
                        "module '{}' has no function for flag '-{flag}'",
                        path.display()
                    )
                })?
            };
            let values: Vec<String> = call_args
                .unwrap_or_default()
                .iter()
                .filter(|argument| !argument.starts_with('-'))
                .cloned()
                .collect();
            return expand_function(&module, &requested_function, &values);
        }

        let mut expanded = Vec::new();
        for statement in &module.statements {
            match statement {
                Statement::Command(command) => expanded.push(command.clone()),
                Statement::Call {
                    module: target,
                    function,
                    args,
                } => {
                    let mut imported = None;
                    for candidate in &module.imports {
                        if module_matches(candidate, target) || import_callby(candidate, target)? {
                            imported = Some(candidate);
                            break;
                        }
                    }
                    let imported = imported.ok_or_else(|| {
                        format!("module '{target}' is not imported by '{}'", path.display())
                    })?;
                    expanded.extend(expand_module(
                        imported,
                        false,
                        Some(args),
                        function.as_deref(),
                        stack,
                    )?);
                }
            }
        }

        if let Some(function_name) = function {
            return expand_function(&module, function_name, call_args.unwrap_or_default());
        }
        Ok(expanded)
    }

    fn expand_function(
        module: &Module,
        function_name: &str,
        args: &[String],
    ) -> Result<Vec<ZCommand>, String> {
        let selected = if let Some(name) = module
            .functions
            .keys()
            .find(|name| name.eq_ignore_ascii_case(function_name))
        {
            name
        } else {
            return Err(format!(
                "function '{function_name}' was not defined in the imported Z module"
            ));
        };
        let body = module
            .functions
            .get(selected)
            .ok_or_else(|| format!("function '{selected}' disappeared during expansion"))?;
        let mut expanded = body.clone();
        for command in &mut expanded {
            for arg in &mut command.args {
                for (index, value) in args.iter().enumerate() {
                    *arg = arg.replace(&format!("${}", index + 1), value);
                }
            }
        }
        Ok(expanded)
    }

    fn parse_module(source: &str, path: &Path) -> Result<Module, String> {
        let lines: Vec<&str> = source.lines().collect();
        let mut module = Module {
            imports: Vec::new(),
            statements: Vec::new(),
            functions: HashMap::new(),
            flag_functions: HashMap::new(),
            callby: None,
        };
        let mut index = 0;

        while index < lines.len() {
            let line = lines[index].trim();
            index += 1;
            if line.is_empty() || line.starts_with("//") {
                continue;
            }

            if let Some(function_name) = function_definition_name(line)? {
                let mut body_source = String::new();
                let mut closed = false;
                while index < lines.len() {
                    let body_line = lines[index];
                    index += 1;
                    if body_line.trim() == "\"" {
                        closed = true;
                        break;
                    }
                    body_source.push_str(body_line);
                    body_source.push('\n');
                }
                if !closed {
                    return Err(format!(
                        "unterminated body for function '{function_name}' in '{}'",
                        path.display()
                    ));
                }
                if module.functions.contains_key(&function_name) {
                    return Err(format!(
                        "function '{function_name}' is defined more than once"
                    ));
                }
                module.functions.insert(
                    function_name,
                    parser::parse_program(&body_source)
                        .map_err(|error| format!("invalid function body: {error}"))?,
                );
                continue;
            }

            if line.starts_with("Z\\if.flag(") {
                let condition = parser::parse_program(line)
                    .map_err(|error| format!("invalid flag condition: {error}"))?
                    .into_iter()
                    .next()
                    .ok_or_else(|| "missing flag condition".to_string())?;
                let flag = condition
                    .args
                    .first()
                    .ok_or_else(|| "Z\\if.flag requires a flag name".to_string())?
                    .trim_start_matches('-')
                    .to_string();
                let next = lines
                    .get(index)
                    .ok_or_else(|| {
                        "Z\\if.flag requires a following Z\\then run statement".to_string()
                    })?
                    .trim();
                let function_name = next
                    .strip_prefix("Z\\then run $")
                    .ok_or_else(|| {
                        format!(
                            "expected 'Z\\then run $function' after flag condition, got '{next}'"
                        )
                    })?
                    .trim();
                if function_name.is_empty() {
                    return Err("Z\\then run requires a function name".to_string());
                }
                module
                    .flag_functions
                    .insert(flag, function_name.to_string());
                index += 1;
                continue;
            }

            if is_load_form(line) {
                let import = parse_load_form(line)?;
                module.imports.push(resolve_import(path, &import)?);
                continue;
            }

            if line.starts_with("Z\\")
                && let Some(command) = parse_z_line(line)?
            {
                match command.command.to_ascii_lowercase().as_str() {
                    "import" => {
                        let import = command
                            .args
                            .first()
                            .ok_or_else(|| "Z\\import requires a .z module path".to_string())?;
                        module.imports.push(resolve_import(path, import)?);
                    }
                    "callby" => {
                        let name = command.args.first().ok_or_else(|| {
                            "Z\\callby requires an exported module name".to_string()
                        })?;
                        module.callby = Some(name.clone());
                    }
                    _ if command.command.contains('.') => {
                        let (module_name, function_name) = command
                            .command
                            .split_once('.')
                            .ok_or_else(|| "invalid qualified function call".to_string())?;
                        module.statements.push(Statement::Call {
                            module: module_name.to_string(),
                            function: Some(function_name.to_string()),
                            args: command.args,
                        });
                    }
                    _ => module.statements.push(Statement::Command(command)),
                }
                continue;
            }

            let (module_name, args) = parser::parse_invocation(line)
                .map_err(|error| format!("{}:{}: {error}", path.display(), index))?;
            module.statements.push(Statement::Call {
                module: module_name,
                function: None,
                args,
            });
        }

        Ok(module)
    }

    fn parse_z_line(line: &str) -> Result<Option<ZCommand>, String> {
        parser::parse_program(line).map(|commands| commands.into_iter().next())
    }

    fn function_definition_name(line: &str) -> Result<Option<String>, String> {
        let Some(rest) = line.strip_prefix("Z\\def.") else {
            return Ok(None);
        };
        let (name, opening_quote) = rest
            .split_once(char::is_whitespace)
            .ok_or_else(|| "function definition must start a quoted body".to_string())?;
        if opening_quote.trim() != "\"" {
            return Err(format!(
                "expected opening quote after function name in '{line}'"
            ));
        }
        if name.is_empty()
            || !name
                .chars()
                .all(|character| character.is_alphanumeric() || character == '_')
        {
            return Err(format!("invalid function name '{name}'"));
        }
        Ok(Some(name.to_string()))
    }

    fn is_load_form(line: &str) -> bool {
        line.starts_with("Z\\Load ") || line.starts_with("Z\\load ")
    }

    fn parse_load_form(line: &str) -> Result<String, String> {
        let value = line
            .split_once(char::is_whitespace)
            .map(|(_, value)| value.trim())
            .unwrap_or_default();
        let value = value
            .strip_prefix('"')
            .and_then(|value| value.strip_suffix('"'))
            .ok_or_else(|| "Z\\Load syntax is Z\\Load \"module.z\"".to_string())?;
        Ok(value.to_string())
    }

    fn resolve_import(importer: &Path, import: &str) -> Result<PathBuf, String> {
        let requested = Path::new(import);
        if requested
            .extension()
            .is_some_and(|extension| !extension.eq_ignore_ascii_case("z"))
        {
            return Err(format!(
                "Z source imports currently require .z files; native DLL/EXE imports need an explicit function ABI: '{import}'"
            ));
        }
        let mut target = importer
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .join(requested);
        if target.extension().is_none() {
            target.set_extension("z");
        }
        target
            .canonicalize()
            .map_err(|error| format!("can't import Z module '{}': {error}", target.display()))
    }

    fn module_matches(path: &Path, requested: &str) -> bool {
        path.file_stem()
            .and_then(|stem| stem.to_str())
            .is_some_and(|stem| stem.eq_ignore_ascii_case(requested))
    }

    fn import_callby(path: &Path, requested: &str) -> Result<bool, String> {
        let source = fs::read_to_string(path)
            .map_err(|error| format!("can't read Z module '{}': {error}", path.display()))?;
        let module = parse_module(&source, path)?;
        Ok(module
            .callby
            .is_some_and(|name| name.eq_ignore_ascii_case(requested)))
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::fs;

        #[test]
        fn parser_reads_composed_module_call() {
            let (name, args) = parser::parse_invocation("woof(\"-c\")+(\"and/x/nice\")").unwrap();
            assert_eq!(name, "woof");
            assert_eq!(args, ["-c", "and/x/nice"]);
        }

        #[test]
        fn imported_module_dispatches_flags_for_run_and_compile_input() {
            let directory =
                std::env::temp_dir().join(format!("z-lang-import-test-{}", std::process::id()));
            fs::create_dir_all(&directory).unwrap();
            let root_path = directory.join("main.z");
            let module_path = directory.join("woof.z");
            fs::write(
                &module_path,
                "Z\\def.coolmode \"\nZ\\burn(\"cool/$1\")\n\"\nZ\\def.dumbmode \"\nZ\\burn(\"dumb/$1\")\n\"\nZ\\callby(\"Woof\")\nZ\\if.flag(\"c\")\nZ\\then run $coolmode\nZ\\if.flag(\"d\")\nZ\\then run $dumbmode\n",
            )
            .unwrap();
            fs::write(
                &root_path,
                "Z\\import(\"woof\")\nwoof(\"-c\")+(\"and/x/nice\")\n",
            )
            .unwrap();

            let result = expand_file(&root_path).unwrap();
            assert_eq!(result.len(), 1);
            assert_eq!(result[0].args, ["cool/and/x/nice"]);

            fs::write(
                &root_path,
                "Z\\import(\"woof\")\nZ\\woof.dumbmode(\"passed\")\n",
            )
            .unwrap();
            let result = expand_file(&root_path).unwrap();
            assert_eq!(result[0].args, ["dumb/passed"]);

            fs::remove_file(&root_path).unwrap();
            fs::remove_file(&module_path).unwrap();
            fs::remove_dir(&directory).unwrap();
        }

        #[test]
        fn module_flags_select_and_expand_function_body() {
            let module = parse_module(
                "Z\\def.coolmode \"\nZ\\burn(\"cool\")\n\"\nZ\\def.dumbmode \"\nZ\\burn(\"dumb\")\n\"\nZ\\callby(\"Woof\")\nZ\\if.flag(\"c\")\nZ\\then run $coolmode\nZ\\if.flag(\"d\")\nZ\\then run $dumbmode",
                Path::new("woof.z"),
            )
            .unwrap();

            let selected = module.flag_functions.get("c").unwrap();
            assert_eq!(selected, "coolmode");
            assert_eq!(
                expand_function(&module, selected, &["value".to_string()]).unwrap()[0].args,
                ["cool"]
            );
        }
    }
}

mod compiler {
    use crate::parser::ZCommand;
    use std::fs;
    use std::path::Path;

    const FILE_ALIGNMENT: usize = 0x200;
    const SECTION_ALIGNMENT: usize = 0x1000;
    const IMAGE_BASE: u64 = 0x1_4000_0000;
    const PE_HEADER_OFFSET: usize = 0x80;
    const OPTIONAL_HEADER_SIZE: usize = 0xf0;
    const IMPORT_DIRECTORY_SIZE: usize = 40;
    const KERNEL_INT_OFFSET: usize = 40;
    const KERNEL_IAT_OFFSET: usize = 72;
    const USER_INT_OFFSET: usize = 104;
    const USER_IAT_OFFSET: usize = 120;
    const IMPORT_NAMES_OFFSET: usize = 136;

    const GET_STD_HANDLE: usize = 0;
    const WRITE_FILE: usize = 1;
    const EXIT_PROCESS: usize = 2;
    const MESSAGE_BOX: usize = 3;

    pub fn compile_commands(commands: &[ZCommand], output: &Path) -> Result<(), String> {
        if !cfg!(windows) {
            return Err(
                "native .exe compilation currently targets Windows x86-64 only".to_string(),
            );
        }
        if !output
            .extension()
            .and_then(|extension| extension.to_str())
            .is_some_and(|extension| extension.eq_ignore_ascii_case("exe"))
        {
            return Err("the output path must end in '.exe'".to_string());
        }
        let executable = generate_executable(&commands)?;

        fs::write(output, executable)
            .map_err(|error| format!("can't write '{}': {error}", output.display()))?;

        println!("Compiled native Z program → '{}'", output.display());
        Ok(())
    }

    pub fn compile_file(input: &Path, output: &Path) -> Result<(), String> {
        if !cfg!(windows) {
            return Err(
                "native .exe compilation currently targets Windows x86-64 only".to_string(),
            );
        }
        if !output
            .extension()
            .and_then(|extension| extension.to_str())
            .is_some_and(|extension| extension.eq_ignore_ascii_case("exe"))
        {
            return Err("the output path must end in '.exe'".to_string());
        }
        let source = fs::read_to_string(input)
            .map_err(|error| format!("can't read '{}': {error}", input.display()))?;
        if source.trim_start().starts_with('<') {
            let executable = compile_native_xml(&source)?;
            fs::write(output, executable)
                .map_err(|error| format!("can't write '{}': {error}", output.display()))?;
            println!("Compiled native Z program → '{}'", output.display());
            return Ok(());
        }
        let commands = crate::language::expand_file(input)?;
        compile_commands(&commands, output)
    }

    fn compile_native_xml(source: &str) -> Result<Vec<u8>, String> {
        let document = roxmltree::Document::parse(source)
            .map_err(|error| format!("invalid XML document: {error}"))?;
        let root = document.root_element();
        if root.tag_name().name() != "program" {
            return Err("XML Z file must have a <program> root".to_string());
        }

        let mut builder = NativeXmlBuilder {
            variables: std::collections::HashMap::new(),
            commands: Vec::new(),
            next_label: 0,
            scope_depth: 0,
        };
        builder.compile_nodes(root.children().filter(|node| node.is_element()).collect())?;
        generate_native_executable(&builder.commands)
    }

    struct NativeXmlBuilder {
        variables: std::collections::HashMap<String, (usize, &'static str)>,
        commands: Vec<CommandData>,
        next_label: usize,
        scope_depth: usize,
    }

    impl NativeXmlBuilder {
        fn compile_nodes<'a, 'input: 'a>(
            &mut self,
            nodes: Vec<roxmltree::Node<'a, 'input>>,
        ) -> Result<(), String> {
            for node in nodes {
                match node.tag_name().name() {
                    "let" => {
                        if self.scope_depth != 0 {
                            return Err(
                                "native XML compilation currently requires variable declarations at the program root"
                                    .to_string(),
                            );
                        }
                        let name = node.attribute("name").ok_or("<let> requires 'name'")?;
                        let kind = node.attribute("type").ok_or("<let> requires 'type'")?;
                        let value = node
                            .attribute("value")
                            .or_else(|| node.text())
                            .unwrap_or("")
                            .trim();
                        if self.variables.contains_key(name) {
                            return Err(format!("variable '{name}' is already declared"));
                        }
                        let slot = self.variables.len();
                        let kind = match kind {
                            "i64" | "int" => "i64",
                            "bool" => "bool",
                            other => {
                                return Err(format!(
                                    "native XML compilation does not support type '{other}'"
                                ));
                            }
                        };
                        let (value_type, expression) = compile_native_expression(
                            value,
                            &self.variables,
                            &mut self.next_label,
                        )?;
                        if value_type != kind {
                            return Err(format!(
                                "initializer for '{name}' must be {kind}, got {value_type}"
                            ));
                        }
                        self.commands.extend(expression);
                        self.commands.push(CommandData::StoreVariable { slot });
                        self.variables.insert(name.to_string(), (slot, kind));
                    }
                    "set" => {
                        let name = node.attribute("name").ok_or("<set> requires 'name'")?;
                        let (slot, expected) = *self.variables.get(name).ok_or_else(|| {
                            format!("cannot assign to undeclared variable '{name}'")
                        })?;
                        let value = node
                            .attribute("value")
                            .or_else(|| node.text())
                            .unwrap_or("")
                            .trim();
                        let (value_type, expression) = compile_native_expression(
                            value,
                            &self.variables,
                            &mut self.next_label,
                        )?;
                        if value_type != expected {
                            return Err(format!(
                                "assignment to '{name}' requires {expected}, got {value_type}"
                            ));
                        }
                        self.commands.extend(expression);
                        self.commands.push(CommandData::StoreVariable { slot });
                    }
                    "burn" => {
                        let text = node
                            .attribute("value")
                            .or_else(|| node.text())
                            .unwrap_or("");
                        if text.contains('{') || text.contains('}') {
                            return Err(
                                "native XML compilation currently requires literal <burn> text; runtime interpolation is not implemented"
                                    .to_string(),
                            );
                        }
                        self.commands
                            .push(CommandData::NativeBurn(text.to_string()));
                    }
                    "if" => {
                        let test = node.attribute("test").ok_or("<if> requires 'test'")?;
                        let (kind, expression) =
                            compile_native_expression(test, &self.variables, &mut self.next_label)?;
                        if kind != "bool" {
                            return Err("<if test> expression must be boolean".to_string());
                        }
                        self.commands.extend(expression);
                        let else_label = self.label();
                        let end_label = self.label();
                        self.commands.push(CommandData::JumpIfZero(else_label));
                        for child in node
                            .children()
                            .filter(|child| child.is_element() && child.tag_name().name() != "else")
                        {
                            self.compile_node(child)?;
                        }
                        self.commands.push(CommandData::Jump(end_label));
                        self.commands.push(CommandData::Label(else_label));
                        for child in node
                            .children()
                            .filter(|child| child.is_element() && child.tag_name().name() == "else")
                        {
                            self.compile_block(
                                child.children().filter(|part| part.is_element()).collect(),
                            )?;
                        }
                        self.commands.push(CommandData::Label(end_label));
                    }
                    "while" => {
                        let test = node.attribute("test").ok_or("<while> requires 'test'")?;
                        let (kind, expression) =
                            compile_native_expression(test, &self.variables, &mut self.next_label)?;
                        if kind != "bool" {
                            return Err("<while test> expression must be boolean".to_string());
                        }
                        let start_label = self.label();
                        let end_label = self.label();
                        self.commands.push(CommandData::Label(start_label));
                        self.commands.extend(expression);
                        self.commands.push(CommandData::JumpIfZero(end_label));
                        self.compile_block(
                            node.children().filter(|child| child.is_element()).collect(),
                        )?;
                        self.commands.push(CommandData::Jump(start_label));
                        self.commands.push(CommandData::Label(end_label));
                    }
                    "spam" | "legalshit" => {
                        return Err(format!(
                            "native XML compilation does not support <{}>; use line-oriented commands",
                            node.tag_name().name()
                        ));
                    }
                    "fn" | "param" | "call" | "return" | "import" => {
                        return Err(format!(
                            "native XML compilation does not support <{}> yet",
                            node.tag_name().name()
                        ));
                    }
                    other => return Err(format!("unknown XML Z element <{other}>")),
                }
            }
            Ok(())
        }

        fn compile_node<'a, 'input>(
            &mut self,
            node: roxmltree::Node<'a, 'input>,
        ) -> Result<(), String> {
            self.compile_block(vec![node])
        }

        fn compile_block<'a, 'input: 'a>(
            &mut self,
            nodes: Vec<roxmltree::Node<'a, 'input>>,
        ) -> Result<(), String> {
            self.scope_depth += 1;
            let result = self.compile_nodes(nodes);
            self.scope_depth -= 1;
            result
        }

        fn label(&mut self) -> usize {
            let label = self.next_label;
            self.next_label += 1;
            label
        }
    }

    fn compile_native_expression(
        source: &str,
        variables: &std::collections::HashMap<String, (usize, &'static str)>,
        next_label: &mut usize,
    ) -> Result<(&'static str, Vec<CommandData>), String> {
        let tokens = lex_native_expression(source)?;
        let mut parser = NativeExpressionParser {
            tokens: &tokens,
            position: 0,
            variables,
            commands: Vec::new(),
            next_label,
        };
        let kind = parser.parse_expression(0)?;
        if parser.position != tokens.len() {
            return Err(format!("unexpected token in expression '{source}'"));
        }
        Ok((kind, parser.commands))
    }

    #[derive(Clone, Debug, PartialEq)]
    enum NativeToken {
        Integer(i64),
        Boolean(bool),
        Identifier(String),
        Operator(String),
        LeftParen,
        RightParen,
    }

    fn lex_native_expression(source: &str) -> Result<Vec<NativeToken>, String> {
        let chars: Vec<char> = source.chars().collect();
        let mut tokens = Vec::new();
        let mut index = 0;
        while index < chars.len() {
            let character = chars[index];
            if character.is_whitespace() {
                index += 1;
            } else if character.is_ascii_digit() {
                let start = index;
                index += 1;
                while index < chars.len() && chars[index].is_ascii_digit() {
                    index += 1;
                }
                let text: String = chars[start..index].iter().collect();
                tokens
                    .push(NativeToken::Integer(text.parse().map_err(|_| {
                        format!("integer literal '{text}' is out of range")
                    })?));
            } else if character.is_ascii_alphabetic() || character == '_' {
                let start = index;
                index += 1;
                while index < chars.len()
                    && (chars[index].is_ascii_alphanumeric() || chars[index] == '_')
                {
                    index += 1;
                }
                let name: String = chars[start..index].iter().collect();
                tokens.push(match name.as_str() {
                    "true" => NativeToken::Boolean(true),
                    "false" => NativeToken::Boolean(false),
                    _ => NativeToken::Identifier(name),
                });
            } else if character == '(' {
                tokens.push(NativeToken::LeftParen);
                index += 1;
            } else if character == ')' {
                tokens.push(NativeToken::RightParen);
                index += 1;
            } else {
                let two: String = chars[index..chars.len().min(index + 2)].iter().collect();
                if ["==", "!=", "<=", ">=", "&&", "||"].contains(&two.as_str()) {
                    tokens.push(NativeToken::Operator(two));
                    index += 2;
                } else if "+-*/%<>!".contains(character) {
                    tokens.push(NativeToken::Operator(character.to_string()));
                    index += 1;
                } else {
                    return Err(format!(
                        "invalid character '{character}' in native expression"
                    ));
                }
            }
        }
        if tokens.is_empty() {
            return Err("empty expression".to_string());
        }
        Ok(tokens)
    }

    struct NativeExpressionParser<'a> {
        tokens: &'a [NativeToken],
        position: usize,
        variables: &'a std::collections::HashMap<String, (usize, &'static str)>,
        commands: Vec<CommandData>,
        next_label: &'a mut usize,
    }

    impl NativeExpressionParser<'_> {
        fn parse_expression(&mut self, minimum: u8) -> Result<&'static str, String> {
            let mut left = self.parse_prefix()?;
            loop {
                let Some(NativeToken::Operator(operator)) = self.tokens.get(self.position) else {
                    break;
                };
                let Some(precedence) = native_precedence(operator) else {
                    break;
                };
                if precedence < minimum {
                    break;
                }
                let operator = operator.clone();
                self.position += 1;
                if operator == "&&" || operator == "||" {
                    if left != "bool" {
                        return Err(format!(
                            "operator '{operator}' requires boolean operands, got {left}"
                        ));
                    }
                    let shortcut_label = self.label();
                    let end_label = self.label();
                    self.commands.push(if operator == "&&" {
                        CommandData::JumpIfZero(shortcut_label)
                    } else {
                        CommandData::JumpIfNonZero(shortcut_label)
                    });
                    let right = self.parse_expression(precedence + 1)?;
                    if right != "bool" {
                        return Err(format!(
                            "operator '{operator}' requires boolean operands, got {right}"
                        ));
                    }
                    self.commands.push(CommandData::Jump(end_label));
                    self.commands.push(CommandData::Label(shortcut_label));
                    self.commands
                        .push(CommandData::LoadImmediate(i64::from(operator == "||")));
                    self.commands.push(CommandData::Label(end_label));
                    left = "bool";
                    continue;
                }
                self.commands.push(CommandData::PushRax);
                let right = self.parse_expression(precedence + 1)?;
                let result = match operator.as_str() {
                    "==" | "!=" if left == right => "bool",
                    "<" | "<=" | ">" | ">=" if left == "i64" && right == "i64" => "bool",
                    "+" | "-" | "*" | "/" | "%" if left == "i64" && right == "i64" => "i64",
                    _ => {
                        return Err(format!(
                            "operator '{operator}' cannot be applied to {left} and {right}"
                        ));
                    }
                };
                self.commands.push(CommandData::PopRcx);
                self.commands.push(CommandData::Binary(operator));
                left = result;
            }
            Ok(left)
        }

        fn label(&mut self) -> usize {
            let label = *self.next_label;
            *self.next_label += 1;
            label
        }

        fn parse_prefix(&mut self) -> Result<&'static str, String> {
            match self.tokens.get(self.position).cloned() {
                Some(NativeToken::Integer(value)) => {
                    self.position += 1;
                    self.commands.push(CommandData::LoadImmediate(value));
                    Ok("i64")
                }
                Some(NativeToken::Boolean(value)) => {
                    self.position += 1;
                    self.commands
                        .push(CommandData::LoadImmediate(i64::from(value)));
                    Ok("bool")
                }
                Some(NativeToken::Identifier(name)) => {
                    self.position += 1;
                    let (slot, kind) = *self
                        .variables
                        .get(&name)
                        .ok_or_else(|| format!("unknown variable '{name}'"))?;
                    self.commands.push(CommandData::LoadVariable { slot });
                    Ok(kind)
                }
                Some(NativeToken::Operator(operator)) if operator == "-" || operator == "!" => {
                    self.position += 1;
                    let kind = self.parse_expression(7)?;
                    match (operator.as_str(), kind) {
                        ("-", "i64") => {
                            self.commands.push(CommandData::Unary(operator));
                            Ok("i64")
                        }
                        ("!", "bool") => {
                            self.commands.push(CommandData::Unary(operator));
                            Ok("bool")
                        }
                        _ => Err(format!("unary '{operator}' cannot be applied to {kind}")),
                    }
                }
                Some(NativeToken::LeftParen) => {
                    self.position += 1;
                    let kind = self.parse_expression(0)?;
                    if self.tokens.get(self.position) != Some(&NativeToken::RightParen) {
                        return Err("expected ')' in native expression".to_string());
                    }
                    self.position += 1;
                    Ok(kind)
                }
                _ => Err("expected a value in native expression".to_string()),
            }
        }
    }

    fn native_precedence(operator: &str) -> Option<u8> {
        Some(match operator {
            "||" => 1,
            "&&" => 2,
            "==" | "!=" => 3,
            "<" | "<=" | ">" | ">=" => 4,
            "+" | "-" => 5,
            "*" | "/" | "%" => 6,
            _ => return None,
        })
    }

    fn generate_executable(commands: &[ZCommand]) -> Result<Vec<u8>, String> {
        let mut data = DataSection::new();
        let mut command_data = Vec::with_capacity(commands.len());

        for command in commands {
            match command.command.as_str() {
                "burn" => {
                    let mut text = command.args.join("\n").into_bytes();
                    text.push(b'\n');
                    let length = u32::try_from(text.len())
                        .map_err(|_| "burn output is too large for a Windows WriteFile call")?;
                    let text_offset = data.add_bytes(&text);
                    command_data.push(CommandData::Burn {
                        text_offset,
                        length,
                    });
                }
                "spam" => {
                    let title_offset = data.add_wide("Z\\spam");
                    let text_offset = data.add_wide(&command.args.join("\n"));
                    command_data.push(CommandData::Popup {
                        title_offset,
                        text_offset,
                        flags: 0x40,
                    });
                }
                "legalshit" => {
                    let title_offset = data.add_wide("Z\\legalshit — AGREE OR ELSE");
                    let text_offset = data.add_wide(&command.args.join("\n"));
                    command_data.push(CommandData::Popup {
                        title_offset,
                        text_offset,
                        flags: 0x34,
                    });
                }
                other => return Err(format!("unknown command: '{other}'")),
            }
        }

        generate_executable_from_data(command_data, data)
    }

    fn generate_native_executable(commands: &[CommandData]) -> Result<Vec<u8>, String> {
        let mut data = DataSection::new();
        let mut command_data = Vec::with_capacity(commands.len());
        for command in commands {
            match command {
                CommandData::NativeBurn(text) => {
                    let mut bytes = text.as_bytes().to_vec();
                    bytes.push(b'\n');
                    let length = u32::try_from(bytes.len())
                        .map_err(|_| "burn output is too large".to_string())?;
                    let text_offset = data.add_bytes(&bytes);
                    command_data.push(CommandData::Burn {
                        text_offset,
                        length,
                    });
                }
                other => command_data.push(other.clone()),
            }
        }
        generate_executable_from_data(command_data, data)
    }

    fn generate_executable_from_data(
        command_data: Vec<CommandData>,
        data: DataSection,
    ) -> Result<Vec<u8>, String> {
        let imports = Imports::new();
        let text_rva = SECTION_ALIGNMENT;
        let rdata_rva = align_up(
            text_rva
                .checked_add(emit_code(&command_data, &imports, text_rva, 0)?.len())
                .ok_or_else(|| "generated code is too large".to_string())?,
            SECTION_ALIGNMENT,
        )?;
        let code = emit_code(&command_data, &imports, text_rva, rdata_rva)?;
        let rdata = data.finish(rdata_rva)?;

        build_pe(&code, &rdata, rdata_rva, imports.directory_size())
    }

    #[derive(Clone)]
    enum CommandData {
        Burn {
            text_offset: usize,
            length: u32,
        },
        Popup {
            title_offset: usize,
            text_offset: usize,
            flags: u32,
        },
        NativeBurn(String),
        LoadImmediate(i64),
        LoadVariable {
            slot: usize,
        },
        StoreVariable {
            slot: usize,
        },
        PushRax,
        PopRcx,
        Unary(String),
        Binary(String),
        Label(usize),
        Jump(usize),
        JumpIfZero(usize),
        JumpIfNonZero(usize),
    }

    struct Imports {
        kernel_iat: usize,
        user_iat: usize,
        directory_size: usize,
    }

    impl Imports {
        fn new() -> Self {
            Self {
                kernel_iat: KERNEL_IAT_OFFSET,
                user_iat: USER_IAT_OFFSET,
                directory_size: IMPORT_DIRECTORY_SIZE,
            }
        }

        fn iat_offset(&self, import: usize) -> usize {
            if import == MESSAGE_BOX {
                self.user_iat
            } else {
                self.kernel_iat + import * 8
            }
        }

        fn directory_size(&self) -> usize {
            self.directory_size
        }
    }

    impl DataSection {
        fn new() -> Self {
            Self {
                bytes: vec![0; IMPORT_NAMES_OFFSET],
                import_names: Vec::new(),
                dll_names: [0; 2],
            }
        }

        fn add_bytes(&mut self, value: &[u8]) -> usize {
            let offset = self.bytes.len();
            self.bytes.extend_from_slice(value);
            offset
        }

        fn add_wide(&mut self, value: &str) -> usize {
            if self.bytes.len() % 2 != 0 {
                self.bytes.push(0);
            }
            let offset = self.bytes.len();
            for unit in value.encode_utf16().chain(std::iter::once(0)) {
                self.bytes.extend_from_slice(&unit.to_le_bytes());
            }
            offset
        }

        fn finish(mut self, rdata_rva: usize) -> Result<Vec<u8>, String> {
            for name in ["GetStdHandle", "WriteFile", "ExitProcess", "MessageBoxW"] {
                self.import_names
                    .push(append_import_name(&mut self.bytes, name));
            }
            self.dll_names[0] = self.bytes.len();
            self.bytes.extend_from_slice(b"kernel32.dll\0");
            self.dll_names[1] = self.bytes.len();
            self.bytes.extend_from_slice(b"user32.dll\0");

            write_u32(
                &mut self.bytes,
                0,
                checked_u32(rdata_rva + KERNEL_INT_OFFSET)?,
            );
            write_u32(
                &mut self.bytes,
                12,
                section_rva(rdata_rva, self.dll_names[0])?,
            );
            write_u32(
                &mut self.bytes,
                32,
                section_rva(rdata_rva, self.dll_names[1])?,
            );
            write_u32(
                &mut self.bytes,
                16,
                checked_u32(rdata_rva + KERNEL_IAT_OFFSET)?,
            );
            write_u32(
                &mut self.bytes,
                20,
                checked_u32(rdata_rva + USER_INT_OFFSET)?,
            );
            write_u32(
                &mut self.bytes,
                36,
                checked_u32(rdata_rva + USER_IAT_OFFSET)?,
            );

            for (table_offset, name_index) in [
                (KERNEL_INT_OFFSET, GET_STD_HANDLE),
                (KERNEL_INT_OFFSET + 8, WRITE_FILE),
                (KERNEL_INT_OFFSET + 16, EXIT_PROCESS),
                (USER_INT_OFFSET, MESSAGE_BOX),
                (KERNEL_IAT_OFFSET, GET_STD_HANDLE),
                (KERNEL_IAT_OFFSET + 8, WRITE_FILE),
                (KERNEL_IAT_OFFSET + 16, EXIT_PROCESS),
                (USER_IAT_OFFSET, MESSAGE_BOX),
            ] {
                write_u64(
                    &mut self.bytes,
                    table_offset,
                    u64::from(section_rva(rdata_rva, self.import_names[name_index])?),
                );
            }

            Ok(self.bytes)
        }
    }

    struct DataSection {
        bytes: Vec<u8>,
        import_names: Vec<usize>,
        dll_names: [usize; 2],
    }

    fn append_import_name(bytes: &mut Vec<u8>, name: &str) -> usize {
        if bytes.len() % 2 != 0 {
            bytes.push(0);
        }
        let offset = bytes.len();
        bytes.extend_from_slice(&[0, 0]);
        bytes.extend_from_slice(name.as_bytes());
        bytes.push(0);
        offset
    }

    fn section_rva(base: usize, offset: usize) -> Result<u32, String> {
        checked_u32(
            base.checked_add(offset)
                .ok_or_else(|| "import table is too large".to_string())?,
        )
    }

    fn emit_code(
        commands: &[CommandData],
        imports: &Imports,
        text_rva: usize,
        rdata_rva: usize,
    ) -> Result<Vec<u8>, String> {
        let mut code = Vec::new();
        let mut failures = Vec::new();
        let mut fixups = Vec::new();
        let mut labels = std::collections::HashMap::new();
        let mut branches = Vec::new();
        let local_count = commands
            .iter()
            .filter_map(|command| match command {
                CommandData::LoadVariable { slot } | CommandData::StoreVariable { slot } => {
                    Some(slot + 1)
                }
                _ => None,
            })
            .max()
            .unwrap_or(0);
        let frame_size = align_up(
            0x30usize
                .checked_add(
                    local_count
                        .checked_mul(8)
                        .ok_or("native stack frame is too large")?,
                )
                .ok_or("native stack frame is too large")?,
            16,
        )?;

        code.extend_from_slice(&[0x48, 0x83, 0xe4, 0xf0]); // and rsp, -16
        if frame_size <= u8::MAX as usize {
            code.extend_from_slice(&[0x48, 0x83, 0xec, frame_size as u8]);
        } else {
            code.extend_from_slice(&[0x48, 0x81, 0xec]);
            code.extend_from_slice(
                &u32::try_from(frame_size)
                    .map_err(|_| "native stack frame is too large".to_string())?
                    .to_le_bytes(),
            );
        }

        for command in commands {
            match command {
                CommandData::Burn {
                    text_offset,
                    length,
                } => {
                    code.extend_from_slice(&[0x48, 0xc7, 0xc1, 0xf5, 0xff, 0xff, 0xff]);
                    emit_call(&mut code, imports, GET_STD_HANDLE, text_rva, rdata_rva)?;
                    code.extend_from_slice(&[0x48, 0x85, 0xc0]);
                    failures.push(emit_jump(&mut code, 0x84));
                    code.extend_from_slice(&[0x48, 0x83, 0xf8, 0xff]);
                    failures.push(emit_jump(&mut code, 0x84));
                    code.extend_from_slice(&[0x48, 0x89, 0xc1]);
                    emit_lea(&mut code, 0x48, 0x15, *text_offset, &mut fixups)?;
                    code.extend_from_slice(&[0x41, 0xb8]);
                    code.extend_from_slice(&length.to_le_bytes());
                    code.extend_from_slice(&[0x4c, 0x8d, 0x4c, 0x24, 0x28]);
                    code.extend_from_slice(&[0x48, 0xc7, 0x44, 0x24, 0x20, 0, 0, 0, 0]);
                    emit_call(&mut code, imports, WRITE_FILE, text_rva, rdata_rva)?;
                    code.extend_from_slice(&[0x85, 0xc0]);
                    failures.push(emit_jump(&mut code, 0x84));
                    code.extend_from_slice(&[0x81, 0x7c, 0x24, 0x28]);
                    code.extend_from_slice(&length.to_le_bytes());
                    failures.push(emit_jump(&mut code, 0x85));
                }
                CommandData::Popup {
                    title_offset,
                    text_offset,
                    flags,
                } => {
                    code.extend_from_slice(&[0x48, 0x31, 0xc9]);
                    emit_lea(&mut code, 0x48, 0x15, *text_offset, &mut fixups)?;
                    emit_lea(&mut code, 0x4c, 0x05, *title_offset, &mut fixups)?;
                    code.extend_from_slice(&[0x41, 0xb9]);
                    code.extend_from_slice(&flags.to_le_bytes());
                    emit_call(&mut code, imports, MESSAGE_BOX, text_rva, rdata_rva)?;
                }
                CommandData::LoadImmediate(value) => {
                    code.extend_from_slice(&[0x48, 0xb8]);
                    code.extend_from_slice(&value.to_le_bytes());
                }
                CommandData::LoadVariable { slot } => {
                    emit_stack_slot(&mut code, &[0x48, 0x8b, 0x84, 0x24], *slot)?;
                }
                CommandData::StoreVariable { slot } => {
                    emit_stack_slot(&mut code, &[0x48, 0x89, 0x84, 0x24], *slot)?;
                }
                CommandData::PushRax => code.push(0x50),
                CommandData::PopRcx => code.push(0x59),
                CommandData::Unary(operator) => match operator.as_str() {
                    "-" => code.extend_from_slice(&[0x48, 0xf7, 0xd8]),
                    "!" => code.extend_from_slice(&[0x48, 0x83, 0xf0, 0x01]),
                    _ => return Err(format!("unsupported unary operator '{operator}'")),
                },
                CommandData::Binary(operator) => match operator.as_str() {
                    "+" => code.extend_from_slice(&[0x48, 0x01, 0xc8]),
                    "-" => code.extend_from_slice(&[0x48, 0x29, 0xc1, 0x48, 0x89, 0xc8]),
                    "*" => code.extend_from_slice(&[0x48, 0x0f, 0xaf, 0xc1]),
                    "/" | "%" => {
                        code.extend_from_slice(&[0x48, 0x87, 0xc8, 0x48, 0x99, 0x48, 0xf7, 0xf9]);
                        if operator == "%" {
                            code.extend_from_slice(&[0x48, 0x89, 0xd0]);
                        }
                    }
                    "&&" => code.extend_from_slice(&[0x48, 0x21, 0xc8]),
                    "||" => code.extend_from_slice(&[0x48, 0x09, 0xc8]),
                    "==" | "!=" | "<" | "<=" | ">" | ">=" => {
                        code.extend_from_slice(&[0x48, 0x39, 0xc1]);
                        code.extend_from_slice(&[
                            0x0f,
                            match operator.as_str() {
                                "==" => 0x94,
                                "!=" => 0x95,
                                "<" => 0x9c,
                                "<=" => 0x9e,
                                ">" => 0x9f,
                                ">=" => 0x9d,
                                _ => unreachable!(),
                            },
                            0xc0,
                        ]);
                        code.extend_from_slice(&[0x48, 0x0f, 0xb6, 0xc0]);
                    }
                    _ => return Err(format!("unsupported binary operator '{operator}'")),
                },
                CommandData::Label(label) => {
                    labels.insert(*label, code.len());
                }
                CommandData::Jump(label) => {
                    code.push(0xe9);
                    let displacement = code.len();
                    code.extend_from_slice(&[0; 4]);
                    branches.push((displacement, *label));
                }
                CommandData::JumpIfZero(label) => {
                    code.extend_from_slice(&[0x48, 0x85, 0xc0]);
                    let displacement = emit_jump(&mut code, 0x84);
                    branches.push((displacement, *label));
                }
                CommandData::JumpIfNonZero(label) => {
                    code.extend_from_slice(&[0x48, 0x85, 0xc0]);
                    let displacement = emit_jump(&mut code, 0x85);
                    branches.push((displacement, *label));
                }
                CommandData::NativeBurn(_) => {
                    return Err("unresolved native output string".to_string());
                }
            }
        }

        code.extend_from_slice(&[0x48, 0x31, 0xc9]);
        emit_call(&mut code, imports, EXIT_PROCESS, text_rva, rdata_rva)?;
        code.extend_from_slice(&[0x0f, 0x0b]);

        let failure_label = code.len();
        code.extend_from_slice(&[0xb9, 1, 0, 0, 0]);
        emit_call(&mut code, imports, EXIT_PROCESS, text_rva, rdata_rva)?;
        code.extend_from_slice(&[0x0f, 0x0b]);

        for branch in failures {
            patch_rel32(&mut code, branch, failure_label)?;
        }
        for (displacement, label) in branches {
            let target = labels
                .get(&label)
                .copied()
                .ok_or_else(|| format!("missing native branch target {label}"))?;
            patch_rel32(&mut code, displacement, target)?;
        }
        for fixup in fixups {
            let target_rva = rdata_rva
                .checked_add(fixup.data_offset)
                .ok_or_else(|| "generated data is too large".to_string())?;
            patch_rip_rel32(&mut code, fixup, text_rva, target_rva)?;
        }

        Ok(code)
    }

    struct RipFixup {
        displacement_offset: usize,
        instruction_end: usize,
        data_offset: usize,
    }

    fn emit_lea(
        code: &mut Vec<u8>,
        rex: u8,
        modrm: u8,
        data_offset: usize,
        fixups: &mut Vec<RipFixup>,
    ) -> Result<(), String> {
        code.extend_from_slice(&[rex, 0x8d, modrm]);
        let displacement_offset = code.len();
        code.extend_from_slice(&[0; 4]);
        fixups.push(RipFixup {
            displacement_offset,
            instruction_end: code.len(),
            data_offset,
        });
        Ok(())
    }

    fn emit_stack_slot(code: &mut Vec<u8>, prefix: &[u8], slot: usize) -> Result<(), String> {
        let offset = 0x30usize
            .checked_add(
                slot.checked_mul(8)
                    .ok_or("native stack frame is too large")?,
            )
            .ok_or("native stack frame is too large")?;
        let offset =
            u32::try_from(offset).map_err(|_| "native stack frame is too large".to_string())?;
        code.extend_from_slice(prefix);
        code.extend_from_slice(&offset.to_le_bytes());
        Ok(())
    }

    fn emit_call(
        code: &mut Vec<u8>,
        imports: &Imports,
        import: usize,
        text_rva: usize,
        rdata_rva: usize,
    ) -> Result<(), String> {
        code.extend_from_slice(&[0xff, 0x15]);
        let displacement_offset = code.len();
        code.extend_from_slice(&[0; 4]);
        let fixup = RipFixup {
            displacement_offset,
            instruction_end: code.len(),
            data_offset: imports.iat_offset(import),
        };
        let target_rva = rdata_rva
            .checked_add(fixup.data_offset)
            .ok_or_else(|| "generated import table is too large".to_string())?;
        patch_rip_rel32(code, fixup, text_rva, target_rva)?;
        Ok(())
    }

    fn emit_jump(code: &mut Vec<u8>, condition: u8) -> usize {
        code.extend_from_slice(&[0x0f, condition]);
        let displacement_offset = code.len();
        code.extend_from_slice(&[0; 4]);
        displacement_offset
    }

    fn patch_rip_rel32(
        code: &mut [u8],
        fixup: RipFixup,
        text_rva: usize,
        target_rva: usize,
    ) -> Result<(), String> {
        let next_instruction_rva = text_rva
            .checked_add(fixup.instruction_end)
            .ok_or_else(|| "generated code is too large".to_string())?;
        let displacement = i64::try_from(target_rva)
            .and_then(|target| i64::try_from(next_instruction_rva).map(|next| target - next))
            .map_err(|_| "generated code or data is too large".to_string())?;
        write_i32(
            code,
            fixup.displacement_offset,
            i32::try_from(displacement).map_err(|_| "generated code exceeds x86-64 limits")?,
        );
        Ok(())
    }

    fn patch_rel32(
        code: &mut [u8],
        displacement_offset: usize,
        target: usize,
    ) -> Result<(), String> {
        let next_instruction = displacement_offset + 4;
        let displacement = i64::try_from(target)
            .and_then(|target| i64::try_from(next_instruction).map(|next| target - next))
            .map_err(|_| "generated code is too large".to_string())?;
        write_i32(
            code,
            displacement_offset,
            i32::try_from(displacement).map_err(|_| "generated code exceeds x86-64 limits")?,
        );
        Ok(())
    }

    fn build_pe(
        code: &[u8],
        rdata: &[u8],
        rdata_rva: usize,
        import_directory_size: usize,
    ) -> Result<Vec<u8>, String> {
        let text_raw_size = align_up(code.len(), FILE_ALIGNMENT)?;
        let rdata_raw_size = align_up(rdata.len(), FILE_ALIGNMENT)?;
        let text_raw_offset = FILE_ALIGNMENT;
        let rdata_raw_offset = text_raw_offset
            .checked_add(text_raw_size)
            .ok_or_else(|| "executable is too large".to_string())?;
        let image_size = align_up(
            rdata_rva
                .checked_add(rdata.len())
                .ok_or_else(|| "executable is too large".to_string())?,
            SECTION_ALIGNMENT,
        )?;
        let mut image = vec![0; FILE_ALIGNMENT];

        image[0..2].copy_from_slice(b"MZ");
        write_u32(&mut image, 0x3c, PE_HEADER_OFFSET as u32);
        image[PE_HEADER_OFFSET..PE_HEADER_OFFSET + 4].copy_from_slice(b"PE\0\0");

        let coff = PE_HEADER_OFFSET + 4;
        write_u16(&mut image, coff, 0x8664);
        write_u16(&mut image, coff + 2, 2);
        write_u16(&mut image, coff + 16, OPTIONAL_HEADER_SIZE as u16);
        write_u16(&mut image, coff + 18, 0x0022);

        let optional = coff + 20;
        write_u16(&mut image, optional, 0x20b);
        image[optional + 2] = 14;
        write_u32(&mut image, optional + 4, checked_u32(text_raw_size)?);
        write_u32(&mut image, optional + 8, checked_u32(rdata_raw_size)?);
        write_u32(&mut image, optional + 16, checked_u32(SECTION_ALIGNMENT)?);
        write_u32(&mut image, optional + 20, checked_u32(SECTION_ALIGNMENT)?);
        write_u64(&mut image, optional + 24, IMAGE_BASE);
        write_u32(&mut image, optional + 32, checked_u32(SECTION_ALIGNMENT)?);
        write_u32(&mut image, optional + 36, checked_u32(FILE_ALIGNMENT)?);
        write_u16(&mut image, optional + 40, 6);
        write_u16(&mut image, optional + 48, 6);
        write_u32(&mut image, optional + 56, checked_u32(image_size)?);
        write_u32(&mut image, optional + 60, FILE_ALIGNMENT as u32);
        write_u16(&mut image, optional + 68, 3);
        write_u16(&mut image, optional + 70, 0x8100);
        write_u64(&mut image, optional + 72, 0x10_0000);
        write_u64(&mut image, optional + 80, 0x1000);
        write_u64(&mut image, optional + 88, 0x10_0000);
        write_u64(&mut image, optional + 96, 0x1000);
        write_u32(&mut image, optional + 108, 16);
        write_u32(&mut image, optional + 120, checked_u32(rdata_rva)?);
        write_u32(
            &mut image,
            optional + 124,
            checked_u32(import_directory_size)?,
        );
        write_u32(
            &mut image,
            optional + 208,
            checked_u32(rdata_rva + KERNEL_IAT_OFFSET)?,
        );
        write_u32(
            &mut image,
            optional + 212,
            checked_u32((USER_IAT_OFFSET + 16) - KERNEL_IAT_OFFSET)?,
        );

        let sections = optional + OPTIONAL_HEADER_SIZE;
        write_section(
            &mut image,
            sections,
            b".text",
            code.len(),
            SECTION_ALIGNMENT,
            text_raw_size,
            text_raw_offset,
            0x6000_0020,
        )?;
        write_section(
            &mut image,
            sections + 40,
            b".rdata",
            rdata.len(),
            rdata_rva,
            rdata_raw_size,
            rdata_raw_offset,
            0xc000_0040,
        )?;

        image.resize(rdata_raw_offset + rdata_raw_size, 0);
        image[text_raw_offset..text_raw_offset + code.len()].copy_from_slice(code);
        image[rdata_raw_offset..rdata_raw_offset + rdata.len()].copy_from_slice(rdata);
        Ok(image)
    }

    fn write_section(
        image: &mut [u8],
        offset: usize,
        name: &[u8],
        virtual_size: usize,
        virtual_address: usize,
        raw_size: usize,
        raw_offset: usize,
        characteristics: u32,
    ) -> Result<(), String> {
        image[offset..offset + name.len()].copy_from_slice(name);
        write_u32(image, offset + 8, checked_u32(virtual_size)?);
        write_u32(image, offset + 12, checked_u32(virtual_address)?);
        write_u32(image, offset + 16, checked_u32(raw_size)?);
        write_u32(image, offset + 20, checked_u32(raw_offset)?);
        write_u32(image, offset + 36, characteristics);
        Ok(())
    }

    fn align_up(value: usize, alignment: usize) -> Result<usize, String> {
        value
            .checked_add(alignment - 1)
            .map(|value| value & !(alignment - 1))
            .ok_or_else(|| "executable is too large".to_string())
    }

    fn checked_u32(value: usize) -> Result<u32, String> {
        u32::try_from(value).map_err(|_| "executable is too large for PE32+".to_string())
    }

    fn write_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn write_u64(bytes: &mut [u8], offset: usize, value: u64) {
        bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
    }

    fn write_i32(bytes: &mut [u8], offset: usize, value: i32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        #[cfg(windows)]
        use std::sync::atomic::{AtomicUsize, Ordering};

        #[cfg(windows)]
        static NEXT_NATIVE_TEST_ID: AtomicUsize = AtomicUsize::new(0);

        #[test]
        fn emits_pe32_plus_executable_with_native_imports() {
            let commands =
                crate::parser::parse_program("Z\\burn(\"hello\")\nZ\\spam(\"notice\")").unwrap();
            let executable = generate_executable(&commands).unwrap();

            assert_eq!(&executable[..2], b"MZ");
            assert_eq!(
                &executable[PE_HEADER_OFFSET..PE_HEADER_OFFSET + 4],
                b"PE\0\0"
            );
            assert_eq!(
                u16::from_le_bytes([
                    executable[PE_HEADER_OFFSET + 4],
                    executable[PE_HEADER_OFFSET + 5]
                ]),
                0x8664
            );
            assert!(
                executable
                    .windows(b"GetStdHandle\0".len())
                    .any(|window| window == b"GetStdHandle\0")
            );
            assert!(
                executable
                    .windows(b"MessageBoxW\0".len())
                    .any(|window| window == b"MessageBoxW\0")
            );
            assert!(executable.windows(5).any(|window| window == b"hello"));
            assert!(!executable.windows(5).any(|window| window == b"rustc"));
        }

        #[test]
        fn rejects_commands_with_no_native_implementation() {
            let commands = vec![ZCommand {
                command: "unknown".to_string(),
                args: Vec::new(),
            }];

            assert_eq!(
                generate_executable(&commands).unwrap_err(),
                "unknown command: 'unknown'"
            );
        }

        #[test]
        fn popup_emits_message_as_text_and_title_in_correct_registers() {
            let text_rva = 0x1000;
            let rdata_rva = 0x3000;
            let imports = Imports::new();
            let code = emit_code(
                &[CommandData::Popup {
                    title_offset: 20,
                    text_offset: 40,
                    flags: 0x40,
                }],
                &imports,
                text_rva,
                rdata_rva,
            )
            .unwrap();

            assert_eq!(&code[11..14], &[0x48, 0x8d, 0x15]);
            assert_eq!(&code[18..21], &[0x4c, 0x8d, 0x05]);
            let text_displacement =
                i32::from_le_bytes(code[14..18].try_into().expect("four-byte displacement"));
            let title_displacement =
                i32::from_le_bytes(code[21..25].try_into().expect("four-byte displacement"));
            let text_target = (text_rva as i64 + 18 + i64::from(text_displacement)) as usize;
            let title_target = (text_rva as i64 + 25 + i64::from(title_displacement)) as usize;

            assert_eq!(text_target, rdata_rva + 40);
            assert_eq!(title_target, rdata_rva + 20);
        }

        #[test]
        fn native_xml_compilation_emits_runtime_branches() {
            let executable = compile_native_xml(
                r#"<program>
                    <let name="count" type="i64" value="0"/>
                    <while test="count &lt; 3">
                      <burn value="step"/>
                      <set name="count" value="count + 1"/>
                    </while>
                    <if test="count == 3"><burn value="finished"/></if>
                </program>"#,
            )
            .unwrap();

            assert_eq!(&executable[..2], b"MZ");
            assert!(executable.windows(4).any(|window| window == b"step"));
            assert!(executable.windows(8).any(|window| window == b"finished"));
            assert!(executable.windows(2).any(|window| window == [0x0f, 0x84]));
        }

        #[cfg(windows)]
        #[test]
        fn compiles_and_runs_xml_functions_loops_and_expressions() {
            let directory = std::env::temp_dir().join(format!(
                "z-lang-native-xml-test-{}-{}",
                std::process::id(),
                NEXT_NATIVE_TEST_ID.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&directory).unwrap();
            let source_path = directory.join("program.z");
            let executable_path = directory.join("program.exe");
            fs::write(
                &source_path,
                r#"
                    <program>
                      <fn name="increment" return="i64">
                        <param name="value" type="i64"/>
                        <return value="value + 1"/>
                      </fn>
                      <let name="count" type="i64" value="0"/>
                      <while test="count &lt; 3">
                        <burn value="count={count}"/>
                        <call name="increment" result="count">
                          <arg value="count"/>
                        </call>
                      </while>
                      <if test="count == 3 &amp;&amp; true">
                        <burn value="complete"/>
                      </if>
                    </program>
                "#,
            )
            .unwrap();

            let result = (|| {
                let commands = crate::language::expand_file(&source_path)?;
                let executable = generate_executable(&commands)?;
                fs::write(&executable_path, executable)
                    .map_err(|error| format!("failed writing generated executable: {error}"))?;
                let output = std::process::Command::new(&executable_path)
                    .output()
                    .map_err(|error| format!("failed to run generated executable: {error}"))?;
                if !output.status.success() {
                    return Err(format!(
                        "generated executable exited with {}: {}",
                        output.status,
                        String::from_utf8_lossy(&output.stderr)
                    ));
                }
                Ok(String::from_utf8_lossy(&output.stdout).into_owned())
            })();

            let _ = fs::remove_file(&executable_path);
            let _ = fs::remove_file(&source_path);
            let _ = fs::remove_dir(&directory);

            assert_eq!(result.unwrap(), "count=0\ncount=1\ncount=2\ncomplete\n");
        }

        #[cfg(windows)]
        #[test]
        fn native_xml_loops_execute_in_the_generated_executable() {
            let directory = std::env::temp_dir().join(format!(
                "z-lang-native-flow-test-{}-{}",
                std::process::id(),
                NEXT_NATIVE_TEST_ID.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&directory).unwrap();
            let source_path = directory.join("program.z");
            let executable_path = directory.join("program.exe");
            let source = r#"<program>
                <let name="count" type="i64" value="0"/>
                <while test="count &lt; 3">
                  <burn value="step"/>
                  <set name="count" value="count + 1"/>
                </while>
                <if test="count == 3"><burn value="finished"/></if>
                <if test="false &amp;&amp; (1 / 0 == 0)">
                  <burn value="wrong"/>
                  <else><burn value="short-circuited"/></else>
                </if>
            </program>"#;
            fs::write(&source_path, source).unwrap();
            let result = (|| {
                compile_file(&source_path, &executable_path)?;
                let output = std::process::Command::new(&executable_path)
                    .output()
                    .map_err(|error| format!("failed to run native XML test: {error}"))?;
                if !output.status.success() {
                    return Err(format!(
                        "native XML executable exited with {}",
                        output.status
                    ));
                }
                Ok(String::from_utf8_lossy(&output.stdout).into_owned())
            })();
            let _ = fs::remove_file(&executable_path);
            let _ = fs::remove_file(&source_path);
            let _ = fs::remove_dir(&directory);
            assert_eq!(
                result.unwrap(),
                "step\nstep\nstep\nfinished\nshort-circuited\n"
            );
        }
    }
}

use std::env;
use std::path::PathBuf;
use std::process;

fn main() {
    let args: Vec<String> = env::args().collect();

    let result = match args.get(1).map(String::as_str) {
        Some("--help" | "-h") => {
            print_usage();
            return;
        }
        Some("compile") => compile_command(&args[2..]),
        Some(filepath) => run_command(filepath),
        None => {
            print_usage();
            Err("missing input file".to_string())
        }
    };

    if let Err(error) = result {
        eprintln!("Z error → {error}");
        process::exit(1);
    }
}

fn run_command(filepath: &str) -> Result<(), String> {
    let program = language::expand_file(PathBuf::from(filepath).as_path())?;

    runtime::run_program(&program)
        .map(|_| ())
        .map_err(|e| format!("runtime error: {e}"))
}

fn compile_command(args: &[String]) -> Result<(), String> {
    if args.is_empty() {
        return Err("usage: z-lang compile <file.z> [-o <output.exe>]".to_string());
    }

    let input = PathBuf::from(&args[0]);
    let mut output = None;
    let mut index = 1;
    while index < args.len() {
        match args[index].as_str() {
            "-o" if index + 1 < args.len() => {
                if output.is_some() {
                    return Err("output path specified more than once".to_string());
                }
                output = Some(PathBuf::from(&args[index + 1]));
                index += 2;
            }
            "-o" => return Err("expected a path after '-o'".to_string()),
            option => return Err(format!("unexpected compile argument '{option}'")),
        }
    }
    let output = output.unwrap_or_else(|| input.with_extension("exe"));

    compiler::compile_file(&input, &output)
}

fn print_usage() {
    println!(
        "Z language\n\
         Usage:\n\
         \tz-lang <file.z>                         Run a Z program\n\
         \tz-lang compile <file.z> [-o <file.exe>] Compile a native Windows executable"
    );
}
