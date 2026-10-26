// Source port of selected editing routines from GNU nano 9.2.
// Copyright (C) the GNU nano authors.
// This file is licensed under the GNU General Public License, version 3 or later.
// See appport/nano/nano/COPYING for the license text.

use crossterm::cursor::{Hide, MoveTo, Show};
use crossterm::event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use crossterm::style::{Attribute, Print, SetAttribute};
use crossterm::terminal::{self, Clear, ClearType, EnterAlternateScreen, LeaveAlternateScreen};
use crossterm::{execute, queue};
use std::fs::{self, File, OpenOptions};
use std::io::{self, IsTerminal, Write};
use std::path::{Path, PathBuf};
use std::time::SystemTime;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum LineEnding {
    Unix,
    Dos,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct FileStamp {
    modified: Option<SystemTime>,
    len: u64,
}

struct Buffer {
    lines: Vec<String>,
    row: usize,
    col: usize,
    preferred_col: Option<usize>,
    filename: Option<PathBuf>,
    modified: bool,
    line_ending: LineEnding,
    stamp: Option<FileStamp>,
    cut_buffer: String,
    search_term: String,
    view_top: usize,
    view_left: usize,
    message: String,
}

impl Buffer {
    fn empty(filename: Option<PathBuf>) -> Self {
        Self {
            lines: vec![String::new()],
            row: 0,
            col: 0,
            preferred_col: None,
            filename,
            modified: false,
            line_ending: LineEnding::Unix,
            stamp: None,
            cut_buffer: String::new(),
            search_term: String::new(),
            view_top: 0,
            view_left: 0,
            message: String::new(),
        }
    }

    fn open(path: PathBuf) -> io::Result<Self> {
        let bytes = match fs::read(&path) {
            Ok(bytes) => bytes,
            Err(error) if error.kind() == io::ErrorKind::NotFound => {
                return Ok(Self::empty(Some(path)));
            }
            Err(error) => return Err(error),
        };
        let text = String::from_utf8(bytes).map_err(|_| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "nano port currently edits UTF-8 text files only",
            )
        })?;
        let line_ending = if text.contains("\r\n") {
            LineEnding::Dos
        } else {
            LineEnding::Unix
        };
        let normalized = text.replace("\r\n", "\n");
        let mut buffer = Self::from_text(&normalized, Some(path.clone()), line_ending);
        buffer.stamp = Some(read_file_stamp(&path)?);
        Ok(buffer)
    }

    fn from_text(text: &str, filename: Option<PathBuf>, line_ending: LineEnding) -> Self {
        let mut buffer = Self::empty(filename);
        let normalized = text.replace("\r\n", "\n");
        buffer.lines = normalized.split('\n').map(str::to_owned).collect();
        if buffer.lines.is_empty() {
            buffer.lines.push(String::new());
        }
        buffer.line_ending = line_ending;
        buffer
    }

    fn to_text(&self) -> String {
        let text = self.lines.join("\n");
        match self.line_ending {
            LineEnding::Unix => text,
            LineEnding::Dos => text.replace('\n', "\r\n"),
        }
    }

    fn current_line_len(&self) -> usize {
        self.lines[self.row].chars().count()
    }

    fn insert_char(&mut self, ch: char) {
        if ch.is_control() {
            return;
        }
        let line = &mut self.lines[self.row];
        let index = byte_index(line, self.col);
        line.insert(index, ch);
        self.col += 1;
        self.changed();
    }

    fn insert_text(&mut self, text: &str) {
        if text.is_empty() {
            return;
        }
        let index = byte_index(&self.lines[self.row], self.col);
        let before = self.lines[self.row][..index].to_owned();
        let after = self.lines[self.row][index..].to_owned();
        let parts = text.split('\n').collect::<Vec<_>>();
        if parts.len() > 1 {
            let mut inserted = Vec::with_capacity(parts.len());
            inserted.push(format!("{before}{}", parts[0]));
            inserted.extend(
                parts[1..parts.len() - 1]
                    .iter()
                    .map(|part| (*part).to_owned()),
            );
            inserted.push(format!("{}{after}", parts[parts.len() - 1]));
            let inserted_count = inserted.len();
            self.lines.splice(self.row..=self.row, inserted);
            self.row += inserted_count - 1;
            self.col = parts[parts.len() - 1].chars().count();
        } else {
            self.lines[self.row] = format!("{before}{}{after}", parts[0]);
            self.col += parts[0].chars().count();
        }
        self.preferred_col = None;
        self.changed();
    }

    fn enter(&mut self) {
        let index = byte_index(&self.lines[self.row], self.col);
        let tail = self.lines[self.row].split_off(index);
        self.lines.insert(self.row + 1, tail);
        self.row += 1;
        self.col = 0;
        self.preferred_col = None;
        self.changed();
    }

    // Follows GNU nano's do_backspace/do_delete line-joining behavior.
    fn backspace(&mut self) {
        if self.col > 0 {
            let line = &mut self.lines[self.row];
            let start = byte_index(line, self.col - 1);
            let end = byte_index(line, self.col);
            line.replace_range(start..end, "");
            self.col -= 1;
            self.changed();
        } else if self.row > 0 {
            let current = self.lines.remove(self.row);
            self.row -= 1;
            self.col = self.current_line_len();
            self.lines[self.row].push_str(&current);
            self.changed();
        }
        self.preferred_col = None;
    }

    // Follows GNU nano's do_delete behavior, including joining adjacent lines.
    fn delete(&mut self) {
        if self.col < self.current_line_len() {
            let line = &mut self.lines[self.row];
            let start = byte_index(line, self.col);
            let end = byte_index(line, self.col + 1);
            line.replace_range(start..end, "");
            self.changed();
        } else if self.row + 1 < self.lines.len() {
            let next = self.lines.remove(self.row + 1);
            self.lines[self.row].push_str(&next);
            self.changed();
        }
    }

    fn cut(&mut self) {
        let line_len = self.current_line_len();
        if self.col == 0 {
            if self.lines.len() == 1 {
                self.cut_buffer = std::mem::take(&mut self.lines[0]);
            } else if self.row + 1 < self.lines.len() {
                self.cut_buffer = format!("{}\n", self.lines.remove(self.row));
                self.col = 0;
            } else {
                self.cut_buffer = format!("\n{}", self.lines.remove(self.row));
                self.row -= 1;
                self.col = self.current_line_len();
            }
        } else if self.col < line_len {
            let index = byte_index(&self.lines[self.row], self.col);
            self.cut_buffer = self.lines[self.row].split_off(index);
        } else if self.row + 1 < self.lines.len() {
            self.cut_buffer = "\n".to_owned();
            let next = self.lines.remove(self.row + 1);
            self.lines[self.row].push_str(&next);
        } else {
            return;
        }
        if self.lines.is_empty() {
            self.lines.push(String::new());
            self.row = 0;
            self.col = 0;
        }
        self.preferred_col = None;
        self.changed();
    }

    fn paste(&mut self) {
        if !self.cut_buffer.is_empty() {
            let text = self.cut_buffer.clone();
            self.insert_text(&text);
        }
    }

    fn move_left(&mut self) {
        if self.col > 0 {
            self.col -= 1;
        } else if self.row > 0 {
            self.row -= 1;
            self.col = self.current_line_len();
        }
        self.preferred_col = None;
    }

    fn move_right(&mut self) {
        if self.col < self.current_line_len() {
            self.col += 1;
        } else if self.row + 1 < self.lines.len() {
            self.row += 1;
            self.col = 0;
        }
        self.preferred_col = None;
    }

    // GNU nano retains a preferred column while moving vertically.
    fn move_vertical(&mut self, down: bool) {
        let target_col = *self.preferred_col.get_or_insert(self.col);
        if down {
            self.row = (self.row + 1).min(self.lines.len() - 1);
        } else {
            self.row = self.row.saturating_sub(1);
        }
        self.col = target_col.min(self.current_line_len());
    }

    fn move_home(&mut self) {
        self.col = 0;
        self.preferred_col = None;
    }

    fn move_end(&mut self) {
        self.col = self.current_line_len();
        self.preferred_col = None;
    }

    fn move_page(&mut self, down: bool, page_size: usize) {
        let target_col = *self.preferred_col.get_or_insert(self.col);
        if down {
            self.row = self.row.saturating_add(page_size).min(self.lines.len() - 1);
        } else {
            self.row = self.row.saturating_sub(page_size);
        }
        self.col = target_col.min(self.current_line_len());
    }

    fn search_forward(&mut self, term: &str, repeat: bool) -> bool {
        if term.is_empty() {
            return false;
        }
        let text = self.lines.join("\n");
        let start = self.byte_offset();
        let start = if repeat {
            start.saturating_add(self.char_byte_len_at_cursor())
        } else {
            start
        };
        let found = text
            .get(start..)
            .and_then(|remaining| remaining.find(term).map(|index| start + index))
            .or_else(|| text.get(..start).and_then(|prefix| prefix.find(term)));
        if let Some(offset) = found {
            self.set_byte_offset(offset);
            self.search_term = term.to_owned();
            true
        } else {
            false
        }
    }

    fn search_backward(&mut self, term: &str) -> bool {
        if term.is_empty() {
            return false;
        }
        let text = self.lines.join("\n");
        let start = self.byte_offset();
        let found = text
            .get(..start)
            .and_then(|prefix| prefix.rmatch_indices(term).next().map(|(index, _)| index))
            .or_else(|| {
                text.get(start..)
                    .and_then(|suffix| suffix.rmatch_indices(term).next())
                    .map(|(index, _)| start + index)
            });
        if let Some(offset) = found {
            self.set_byte_offset(offset);
            self.search_term = term.to_owned();
            true
        } else {
            false
        }
    }

    fn byte_offset(&self) -> usize {
        self.lines
            .iter()
            .take(self.row)
            .map(|line| line.len() + 1)
            .sum::<usize>()
            + byte_index(&self.lines[self.row], self.col)
    }

    fn set_byte_offset(&mut self, mut offset: usize) {
        for (row, line) in self.lines.iter().enumerate() {
            if offset <= line.len() {
                self.row = row;
                self.col = line[..offset].chars().count();
                self.preferred_col = None;
                return;
            }
            offset -= line.len();
            if row + 1 < self.lines.len() {
                if offset == 0 {
                    self.row = row;
                    self.col = line.chars().count();
                    self.preferred_col = None;
                    return;
                }
                offset -= 1;
            }
        }
        self.row = self.lines.len() - 1;
        self.col = self.current_line_len();
        self.preferred_col = None;
    }

    fn char_byte_len_at_cursor(&self) -> usize {
        self.lines[self.row]
            .chars()
            .nth(self.col)
            .map(char::len_utf8)
            .unwrap_or(1)
    }

    fn changed(&mut self) {
        self.modified = true;
        self.message.clear();
    }

    fn render<W: Write>(&mut self, output: &mut W) -> io::Result<()> {
        let (width, height) = terminal::size()?;
        let width = usize::from(width.max(1));
        let height = usize::from(height.max(4));
        let page_size = height.saturating_sub(3).max(1);
        if self.row < self.view_top {
            self.view_top = self.row;
        } else if self.row >= self.view_top + page_size {
            self.view_top = self.row + 1 - page_size;
        }

        let cursor_x = display_column(&self.lines[self.row], self.col);
        if cursor_x < self.view_left {
            self.view_left = cursor_x;
        } else if cursor_x >= self.view_left + width {
            self.view_left = cursor_x + 1 - width;
        }

        queue!(
            output,
            MoveTo(0, 0),
            Clear(ClearType::All),
            SetAttribute(Attribute::Reverse)
        )?;
        let title = format!(
            " ReTerm nano source port  |  {}{}",
            self.filename
                .as_ref()
                .map(|path| path.to_string_lossy().into_owned())
                .unwrap_or_else(|| "[New Buffer]".to_owned()),
            if self.modified { " [Modified]" } else { "" }
        );
        queue!(
            output,
            Print(pad_cells(&title, width)),
            SetAttribute(Attribute::Reset)
        )?;

        for screen_row in 0..page_size {
            let row = self.view_top + screen_row;
            queue!(output, MoveTo(0, (screen_row + 1) as u16))?;
            if let Some(line) = self.lines.get(row) {
                queue!(output, Print(render_cells(line, self.view_left, width)))?;
            }
        }

        let status_row = (height - 2) as u16;
        let help_row = (height - 1) as u16;
        let status = if self.message.is_empty() {
            format!(
                " Line {}, Column {}    {} lines ",
                self.row + 1,
                self.col + 1,
                self.lines.len()
            )
        } else {
            format!(" {}", self.message)
        };
        queue!(
            output,
            MoveTo(0, status_row),
            Print(pad_cells(&status, width))
        )?;
        queue!(
            output,
            MoveTo(0, help_row),
            SetAttribute(Attribute::Reverse),
            Print(pad_cells(
                "^G Help  ^O Write Out  ^W Where Is  ^K Cut  ^U Paste  ^X Exit",
                width
            )),
            SetAttribute(Attribute::Reset)
        )?;

        let screen_x = cursor_x.saturating_sub(self.view_left).min(width - 1) as u16;
        let screen_y = (self.row - self.view_top + 1) as u16;
        queue!(output, MoveTo(screen_x, screen_y), Show)?;
        output.flush()
    }
}

fn byte_index(text: &str, char_index: usize) -> usize {
    text.char_indices()
        .nth(char_index)
        .map(|(index, _)| index)
        .unwrap_or(text.len())
}

fn display_column(line: &str, char_index: usize) -> usize {
    let mut column = 0;
    for ch in line.chars().take(char_index) {
        column += if ch == '\t' {
            8 - column % 8
        } else if ch.is_control() {
            2
        } else {
            1
        };
    }
    column
}

fn render_cells(line: &str, left: usize, width: usize) -> String {
    let mut cells = Vec::new();
    let mut column = 0;
    for ch in line.chars() {
        if ch == '\t' {
            let spaces = 8 - column % 8;
            cells.extend(std::iter::repeat(' ').take(spaces));
            column += spaces;
        } else if ch.is_control() {
            cells.push('^');
            cells.push(char::from_u32((ch as u32 & 0x1f) + 64).unwrap_or('?'));
            column += 2;
        } else {
            cells.push(ch);
            column += 1;
        }
    }
    cells.into_iter().skip(left).take(width).collect()
}

fn pad_cells(text: &str, width: usize) -> String {
    let mut result = text.chars().take(width).collect::<String>();
    let count = result.chars().count();
    result.extend(std::iter::repeat(' ').take(width.saturating_sub(count)));
    result
}

fn read_file_stamp(path: &Path) -> io::Result<FileStamp> {
    let metadata = fs::metadata(path)?;
    Ok(FileStamp {
        modified: metadata.modified().ok(),
        len: metadata.len(),
    })
}

fn read_prompt<W: Write>(
    output: &mut W,
    prompt: &str,
    initial: &str,
) -> io::Result<Option<String>> {
    let mut value = initial.to_owned();
    loop {
        let (_, height) = terminal::size()?;
        queue!(
            output,
            MoveTo(0, height.saturating_sub(1)),
            Clear(ClearType::CurrentLine),
            Print(prompt),
            Print(&value)
        )?;
        output.flush()?;
        let key = read_key()?;
        match key.code {
            KeyCode::Enter => return Ok(Some(value)),
            KeyCode::Esc => return Ok(None),
            KeyCode::Backspace => {
                value.pop();
            }
            KeyCode::Char('u') if is_control(key, 'u') => {
                value.clear();
            }
            KeyCode::Char(ch)
                if !ch.is_control()
                    && !key
                        .modifiers
                        .intersects(KeyModifiers::CONTROL | KeyModifiers::ALT) =>
            {
                value.push(ch)
            }
            _ => {}
        }
    }
}

fn read_key() -> io::Result<KeyEvent> {
    loop {
        if let Event::Key(key) = event::read()? {
            if key.kind == KeyEventKind::Press {
                return Ok(key);
            }
        }
    }
}

fn ask_yes_no<W: Write>(output: &mut W, prompt: &str) -> io::Result<bool> {
    loop {
        let (_, height) = terminal::size()?;
        queue!(
            output,
            MoveTo(0, height.saturating_sub(1)),
            Clear(ClearType::CurrentLine),
            Print(prompt)
        )?;
        output.flush()?;
        match read_key()?.code {
            KeyCode::Char('y' | 'Y') => return Ok(true),
            KeyCode::Char('n' | 'N' | 'c' | 'C') | KeyCode::Esc => return Ok(false),
            _ => {}
        }
    }
}

fn make_backup(path: &Path) -> io::Result<()> {
    let metadata = match fs::metadata(path) {
        Ok(metadata) if metadata.is_file() => metadata,
        Ok(_) => return Ok(()),
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(error) => return Err(error),
    };
    let mut backup_name = path.as_os_str().to_os_string();
    backup_name.push("~");
    let backup_path = PathBuf::from(backup_name);
    match fs::remove_file(&backup_path) {
        Ok(()) => {}
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => return Err(error),
    }

    let result = (|| {
        let mut source = File::open(path)?;
        let mut backup = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&backup_path)?;
        io::copy(&mut source, &mut backup)?;
        backup.set_permissions(metadata.permissions())?;
        backup.sync_all()
    })();
    if let Err(error) = result {
        return match fs::remove_file(&backup_path) {
            Ok(()) => Err(error),
            Err(cleanup_error) if cleanup_error.kind() == io::ErrorKind::NotFound => Err(error),
            Err(cleanup_error) => Err(io::Error::new(
                error.kind(),
                format!("{error}; also could not remove incomplete backup: {cleanup_error}"),
            )),
        };
    }
    Ok(())
}

fn save_buffer<W: Write>(
    buffer: &mut Buffer,
    cwd: &Path,
    backup: bool,
    prompt_for_path: bool,
    output: &mut W,
) -> io::Result<bool> {
    let entered = if !prompt_for_path {
        if let Some(path) = &buffer.filename {
            Some(path.to_string_lossy().into_owned())
        } else {
            read_prompt(output, "File Name to Write: ", "")?
        }
    } else if let Some(path) = &buffer.filename {
        read_prompt(output, "File Name to Write: ", &path.to_string_lossy())?
    } else {
        read_prompt(output, "File Name to Write: ", "")?
    };
    let Some(entered) = entered else {
        buffer.message = "Cancelled".to_owned();
        return Ok(false);
    };
    let entered = entered.trim();
    if entered.is_empty() {
        buffer.message = "Cancelled".to_owned();
        return Ok(false);
    }
    let selected_path = PathBuf::from(entered);
    let target = if selected_path.is_absolute() {
        selected_path
    } else {
        cwd.join(selected_path)
    };
    let same_file = buffer.filename.as_deref() == Some(target.as_path());
    let existing_stamp = match read_file_stamp(&target) {
        Ok(stamp) => Some(stamp),
        Err(error) if error.kind() == io::ErrorKind::NotFound => None,
        Err(error) => return Err(error),
    };
    if let Some(current_stamp) = &buffer.stamp {
        if same_file && existing_stamp.as_ref() != Some(current_stamp) {
            if !ask_yes_no(output, "File has changed. Overwrite? (y/N) ")? {
                buffer.message = "Cancelled".to_owned();
                return Ok(false);
            }
        } else if !same_file
            && existing_stamp.is_some()
            && !ask_yes_no(output, "File exists. Overwrite? (y/N) ")?
        {
            buffer.message = "Cancelled".to_owned();
            return Ok(false);
        }
    } else if existing_stamp.is_some() && !ask_yes_no(output, "File exists. Overwrite? (y/N) ")? {
        buffer.message = "Cancelled".to_owned();
        return Ok(false);
    }

    if backup && existing_stamp.is_some() {
        make_backup(&target)?;
    }
    fs::write(&target, buffer.to_text())?;
    buffer.filename = Some(target.clone());
    buffer.stamp = Some(read_file_stamp(&target)?);
    buffer.modified = false;
    buffer.message = format!("Wrote {} lines", buffer.lines.len());
    Ok(true)
}

fn handle_exit<W: Write>(
    buffer: &mut Buffer,
    cwd: &Path,
    backup: bool,
    output: &mut W,
) -> io::Result<bool> {
    if !buffer.modified {
        return Ok(true);
    }
    loop {
        let (_, height) = terminal::size()?;
        queue!(
            output,
            MoveTo(0, height.saturating_sub(1)),
            Clear(ClearType::CurrentLine),
            Print("Save modified buffer? (Y)es, (N)o, (C)ancel ")
        )?;
        output.flush()?;
        match read_key()?.code {
            KeyCode::Char('y' | 'Y') => {
                return save_buffer(buffer, cwd, backup, false, output);
            }
            KeyCode::Char('n' | 'N') => return Ok(true),
            KeyCode::Char('c' | 'C') | KeyCode::Esc => return Ok(false),
            _ => {}
        }
    }
}

fn is_control(key: KeyEvent, ch: char) -> bool {
    key.modifiers.contains(KeyModifiers::CONTROL)
        && matches!(key.code, KeyCode::Char(value) if value.eq_ignore_ascii_case(&ch))
}

fn is_alt(key: KeyEvent, ch: char) -> bool {
    key.modifiers.contains(KeyModifiers::ALT)
        && matches!(key.code, KeyCode::Char(value) if value.eq_ignore_ascii_case(&ch))
}

fn run_editor(
    buffer: &mut Buffer,
    cwd: &Path,
    backup: bool,
    output: &mut io::Stdout,
) -> io::Result<()> {
    loop {
        buffer.render(output)?;
        let event = event::read()?;
        let Event::Key(key) = event else {
            continue;
        };
        if key.kind != KeyEventKind::Press {
            continue;
        }

        if is_control(key, 'x') {
            if handle_exit(buffer, cwd, backup, output)? {
                return Ok(());
            }
            continue;
        } else if is_control(key, 'o') || is_control(key, 's') {
            let _ = save_buffer(buffer, cwd, backup, is_control(key, 'o'), output)?;
        } else if is_control(key, 'w') {
            let initial = buffer.search_term.clone();
            if let Some(term) = read_prompt(output, "Search: ", &initial)? {
                if buffer.search_forward(&term, false) {
                    buffer.message = format!("Found: {term}");
                } else {
                    buffer.message = format!("\"{term}\" not found");
                }
            }
        } else if is_alt(key, 'w') {
            if buffer.search_forward(&buffer.search_term.clone(), true) {
                buffer.message = format!("Found: {}", buffer.search_term);
            } else {
                buffer.message = "Search wrapped; no further match".to_owned();
            }
        } else if is_alt(key, 'q') {
            if buffer.search_backward(&buffer.search_term.clone()) {
                buffer.message = format!("Found: {}", buffer.search_term);
            } else {
                buffer.message = "No earlier match".to_owned();
            }
        } else if is_control(key, 'k') {
            buffer.cut();
        } else if is_control(key, 'u') {
            buffer.paste();
        } else if is_control(key, 'g') {
            buffer.message =
                "GNU nano 9.2 source port: Ctrl+O save, Ctrl+W search, Ctrl+X exit".to_owned();
        } else if is_control(key, 'c') {
            buffer.message = format!("Line {}, column {}", buffer.row + 1, buffer.col + 1);
        } else if is_control(key, 'l') {
            buffer.message.clear();
        } else if is_control(key, 'a') || key.code == KeyCode::Home {
            buffer.move_home();
        } else if is_control(key, 'e') || key.code == KeyCode::End {
            buffer.move_end();
        } else if is_control(key, 'b') || key.code == KeyCode::Left {
            buffer.move_left();
        } else if is_control(key, 'f') || key.code == KeyCode::Right {
            buffer.move_right();
        } else if is_control(key, 'p') || key.code == KeyCode::Up {
            buffer.move_vertical(false);
        } else if is_control(key, 'n') || key.code == KeyCode::Down {
            buffer.move_vertical(true);
        } else if key.code == KeyCode::PageUp || is_control(key, 'y') {
            let page_size = terminal::size()?.1.saturating_sub(3).max(1) as usize;
            buffer.move_page(false, page_size);
        } else if key.code == KeyCode::PageDown || is_control(key, 'v') {
            let page_size = terminal::size()?.1.saturating_sub(3).max(1) as usize;
            buffer.move_page(true, page_size);
        } else {
            match key.code {
                KeyCode::Backspace => buffer.backspace(),
                KeyCode::Delete => buffer.delete(),
                KeyCode::Enter => buffer.enter(),
                KeyCode::Tab => buffer.insert_char('\t'),
                KeyCode::Char(ch)
                    if !key
                        .modifiers
                        .intersects(KeyModifiers::CONTROL | KeyModifiers::ALT) =>
                {
                    buffer.insert_char(ch)
                }
                _ => {}
            }
        }
    }
}

struct TerminalGuard {
    raw_mode: bool,
    alternate_screen: bool,
}

impl TerminalGuard {
    fn enter() -> io::Result<Self> {
        terminal::enable_raw_mode()?;
        let mut guard = Self {
            raw_mode: true,
            alternate_screen: false,
        };
        execute!(io::stdout(), EnterAlternateScreen)?;
        guard.alternate_screen = true;
        execute!(io::stdout(), Hide)?;
        Ok(guard)
    }

    fn restore(&mut self) -> io::Result<()> {
        let mut failures = Vec::new();
        if self.alternate_screen {
            if let Err(error) = execute!(io::stdout(), Show, LeaveAlternateScreen) {
                failures.push(error.to_string());
            } else {
                self.alternate_screen = false;
            }
        }
        if self.raw_mode {
            if let Err(error) = terminal::disable_raw_mode() {
                failures.push(error.to_string());
            } else {
                self.raw_mode = false;
            }
        }
        if failures.is_empty() {
            Ok(())
        } else {
            Err(io::Error::other(format!(
                "could not restore terminal: {}",
                failures.join("; ")
            )))
        }
    }
}

impl Drop for TerminalGuard {
    fn drop(&mut self) {
        let _ = self.restore();
    }
}

pub fn run(arguments: &[String], cwd: &Path) -> io::Result<()> {
    let mut backup = false;
    let mut position = None;
    let mut filename = None;
    let mut options_ended = false;
    for argument in arguments {
        if !options_ended && argument == "--" {
            options_ended = true;
        } else if !options_ended && matches!(argument.as_str(), "--help" | "-h") {
            let mut output = io::stdout().lock();
            writeln!(output, "Usage: nano [--backup] [+LINE[,COLUMN]] [FILE]")?;
            writeln!(output, "Edit a UTF-8 text file in the ReTerm terminal.")?;
            return Ok(());
        } else if !options_ended && matches!(argument.as_str(), "--backup" | "-B") {
            backup = true;
        } else if !options_ended && argument.starts_with('+') {
            position = Some(parse_position(argument)?);
        } else if !options_ended && argument.starts_with('-') {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported nano option: {argument}"),
            ));
        } else if filename.replace(argument.clone()).is_some() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "nano accepts one file at a time",
            ));
        }
    }

    if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
        return Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "nano requires an interactive terminal",
        ));
    }
    let path = filename.map(|name| {
        let path = PathBuf::from(name);
        if path.is_absolute() {
            path
        } else {
            cwd.join(path)
        }
    });
    let mut buffer = match path {
        Some(path) => Buffer::open(path)?,
        None => Buffer::empty(None),
    };
    if let Some((line, column)) = position {
        buffer.row = line.saturating_sub(1).min(buffer.lines.len() - 1);
        buffer.col = column
            .unwrap_or(1)
            .saturating_sub(1)
            .min(buffer.current_line_len());
    }

    let mut guard = TerminalGuard::enter()?;
    let mut output = io::stdout();
    let result = run_editor(&mut buffer, cwd, backup, &mut output);
    let restore = guard.restore();
    match (result, restore) {
        (Ok(()), Ok(())) => Ok(()),
        (Err(error), Ok(())) => Err(error),
        (Ok(()), Err(error)) => Err(error),
        (Err(error), Err(restore_error)) => Err(io::Error::new(
            error.kind(),
            format!("{error}; {restore_error}"),
        )),
    }
}

fn parse_position(argument: &str) -> io::Result<(usize, Option<usize>)> {
    let mut values = argument[1..].split(',');
    let line = values
        .next()
        .filter(|value| !value.is_empty())
        .unwrap_or("1")
        .parse::<usize>()
        .map_err(|_| io::Error::new(io::ErrorKind::InvalidInput, "invalid +LINE position"))?;
    let column = values
        .next()
        .map(|value| {
            value.parse::<usize>().map_err(|_| {
                io::Error::new(io::ErrorKind::InvalidInput, "invalid +COLUMN position")
            })
        })
        .transpose()?;
    if values.next().is_some() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "position must be +LINE or +LINE,COLUMN",
        ));
    }
    Ok((line.max(1), column))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    static NEXT_TEST_DIR: AtomicU64 = AtomicU64::new(0);

    fn test_directory() -> PathBuf {
        let path = std::env::temp_dir().join(format!(
            "reterm-nano-{}-{}",
            std::process::id(),
            NEXT_TEST_DIR.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).expect("create test directory");
        path
    }

    #[test]
    fn line_endings_and_trailing_newline_are_preserved() {
        let buffer = Buffer::from_text("one\r\ntwo\r\n", None, LineEnding::Dos);
        assert_eq!(buffer.lines, ["one", "two", ""]);
        assert_eq!(buffer.to_text(), "one\r\ntwo\r\n");
    }

    #[test]
    fn unicode_editing_uses_character_boundaries() {
        let mut buffer = Buffer::from_text("aé🙂", None, LineEnding::Unix);
        buffer.col = 2;
        buffer.backspace();
        assert_eq!(buffer.to_text(), "a🙂");
        buffer.insert_char('界');
        assert_eq!(buffer.to_text(), "a界🙂");
    }

    #[test]
    fn enter_and_backspace_join_lines() {
        let mut buffer = Buffer::from_text("abcd", None, LineEnding::Unix);
        buffer.col = 2;
        buffer.enter();
        assert_eq!(buffer.to_text(), "ab\ncd");
        buffer.backspace();
        assert_eq!(buffer.to_text(), "abcd");
        assert_eq!((buffer.row, buffer.col), (0, 2));
    }

    #[test]
    fn inserting_multiline_text_preserves_order_and_suffix() {
        let mut buffer = Buffer::from_text("abXY", None, LineEnding::Unix);
        buffer.col = 2;
        buffer.insert_text("1\n2\n3");
        assert_eq!(buffer.to_text(), "ab1\n2\n3XY");
        assert_eq!((buffer.row, buffer.col), (2, 1));
    }

    #[test]
    fn cut_and_paste_restore_a_whole_line() {
        let mut buffer = Buffer::from_text("one\ntwo\nthree", None, LineEnding::Unix);
        buffer.row = 1;
        buffer.cut();
        assert_eq!(buffer.to_text(), "one\nthree");
        buffer.paste();
        assert_eq!(buffer.to_text(), "one\ntwo\nthree");
    }

    #[test]
    fn vertical_navigation_keeps_preferred_column() {
        let mut buffer = Buffer::from_text("abcdef\nxy\nabcdef", None, LineEnding::Unix);
        buffer.col = 5;
        buffer.move_vertical(true);
        assert_eq!((buffer.row, buffer.col), (1, 2));
        buffer.move_vertical(true);
        assert_eq!((buffer.row, buffer.col), (2, 5));
    }

    #[test]
    fn search_wraps_and_tracks_utf8_offsets() {
        let mut buffer = Buffer::from_text("écho\nlast\nécho", None, LineEnding::Unix);
        buffer.row = 2;
        buffer.col = 4;
        assert!(buffer.search_forward("écho", false));
        assert_eq!((buffer.row, buffer.col), (0, 0));
        assert!(buffer.search_backward("last"));
        assert_eq!((buffer.row, buffer.col), (1, 0));
    }

    #[test]
    fn save_with_backup_preserves_the_previous_contents() {
        let directory = test_directory();
        let path = directory.join("sample.txt");
        fs::write(&path, "before\n").expect("create source file");
        let mut buffer = Buffer::open(path.clone()).expect("open source file");
        buffer.insert_char('!');
        let mut output = Vec::new();
        assert!(save_buffer(&mut buffer, &directory, true, false, &mut output).expect("save file"));
        assert_eq!(
            fs::read_to_string(&path).expect("read saved file"),
            "!before\n"
        );
        assert_eq!(
            fs::read_to_string(directory.join("sample.txt~")).expect("read backup"),
            "before\n"
        );
        fs::remove_dir_all(directory).expect("remove test directory");
    }

    #[test]
    fn parses_initial_line_and_column() {
        assert_eq!(parse_position("+12,4").unwrap(), (12, Some(4)));
        assert!(parse_position("+12,4,2").is_err());
    }
}
