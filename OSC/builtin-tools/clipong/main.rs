use std::io::{self, Stdout, Write};
use std::time::{Duration, Instant};

use crossterm::{
    cursor::{Hide, MoveTo, Show},
    event::{self, Event, KeyCode},
    execute, queue,
    style::Print,
    terminal::{self, Clear, ClearType, EnterAlternateScreen, LeaveAlternateScreen},
};

const WIDTH: u16 = 60;
const HEIGHT: u16 = 20;
const PADDLE_HEIGHT: i16 = 4;
const WINNING_SCORE: u8 = 5;

struct TerminalSession {
    stdout: Stdout,
}

impl TerminalSession {
    fn start() -> io::Result<Self> {
        let mut stdout = io::stdout();
        terminal::enable_raw_mode()?;
        if let Err(error) = execute!(stdout, EnterAlternateScreen, Hide) {
            let _ = terminal::disable_raw_mode();
            return Err(error);
        }
        Ok(Self { stdout })
    }
}

impl Drop for TerminalSession {
    fn drop(&mut self) {
        let _ = execute!(self.stdout, Show, LeaveAlternateScreen);
        let _ = terminal::disable_raw_mode();
    }
}

struct Game {
    ball_x: f32,
    ball_y: f32,
    velocity_x: f32,
    velocity_y: f32,
    left_paddle: i16,
    right_paddle: i16,
    player_score: u8,
    cpu_score: u8,
}

impl Game {
    fn new() -> Self {
        Self {
            ball_x: f32::from(WIDTH / 2),
            ball_y: f32::from(HEIGHT / 2),
            velocity_x: 1.0,
            velocity_y: 0.55,
            left_paddle: (HEIGHT / 2) as i16,
            right_paddle: (HEIGHT / 2) as i16,
            player_score: 0,
            cpu_score: 0,
        }
    }

    fn update(&mut self) {
        self.ball_x += self.velocity_x;
        self.ball_y += self.velocity_y;

        if self.ball_y <= 1.0 || self.ball_y >= f32::from(HEIGHT - 2) {
            self.velocity_y = -self.velocity_y;
            self.ball_y = self.ball_y.clamp(1.0, f32::from(HEIGHT - 2));
        }

        let ball_row = self.ball_y.round() as i16;
        let left_edge = 2.0;
        let right_edge = f32::from(WIDTH - 3);

        if self.velocity_x < 0.0
            && self.ball_x <= left_edge
            && (ball_row - self.left_paddle).abs() <= PADDLE_HEIGHT / 2
        {
            self.ball_x = left_edge;
            self.bounce_from_paddle(self.left_paddle);
        } else if self.velocity_x > 0.0
            && self.ball_x >= right_edge
            && (ball_row - self.right_paddle).abs() <= PADDLE_HEIGHT / 2
        {
            self.ball_x = right_edge;
            self.bounce_from_paddle(self.right_paddle);
        }

        if self.ball_x < 0.0 {
            self.cpu_score += 1;
            self.reset_ball(-1.0);
        } else if self.ball_x >= f32::from(WIDTH) {
            self.player_score += 1;
            self.reset_ball(1.0);
        }

        let target = ball_row;
        if self.right_paddle < target {
            self.right_paddle += 1;
        } else if self.right_paddle > target {
            self.right_paddle -= 1;
        }
        self.right_paddle = self.clamp_paddle(self.right_paddle);
    }

    fn bounce_from_paddle(&mut self, paddle: i16) {
        self.velocity_x = -self.velocity_x * 1.04;
        self.velocity_y = ((self.ball_y.round() as i16 - paddle) as f32 * 0.25).clamp(-1.4, 1.4);
    }

    fn reset_ball(&mut self, direction: f32) {
        self.ball_x = f32::from(WIDTH / 2);
        self.ball_y = f32::from(HEIGHT / 2);
        self.velocity_x = direction;
        self.velocity_y = 0.55;
    }

    fn move_player(&mut self, direction: i16) {
        self.left_paddle = self.clamp_paddle(self.left_paddle + direction);
    }

    fn clamp_paddle(&self, position: i16) -> i16 {
        position.clamp(1 + PADDLE_HEIGHT / 2, HEIGHT as i16 - 2 - PADDLE_HEIGHT / 2)
    }

    fn draw(&self, stdout: &mut Stdout) -> io::Result<()> {
        let ball_x = self.ball_x.round() as u16;
        let ball_y = self.ball_y.round() as u16;

        queue!(stdout, MoveTo(0, 0), Clear(ClearType::All))?;
        queue!(
            stdout,
            MoveTo(0, 0),
            Print(format!(
                " CLI PONG    You {} : {} CPU    First to {}    W/S move | Q quit",
                self.player_score, self.cpu_score, WINNING_SCORE
            ))
        )?;

        for x in 0..WIDTH {
            queue!(stdout, MoveTo(x, 1), Print("-"))?;
            queue!(stdout, MoveTo(x, HEIGHT - 1), Print("-"))?;
        }
        for y in 1..HEIGHT - 1 {
            queue!(stdout, MoveTo(WIDTH / 2, y), Print("|"))?;
        }
        for offset in -(PADDLE_HEIGHT / 2)..=(PADDLE_HEIGHT / 2) {
            let y = (self.left_paddle + offset) as u16;
            queue!(stdout, MoveTo(1, y), Print("|"))?;
            let y = (self.right_paddle + offset) as u16;
            queue!(stdout, MoveTo(WIDTH - 2, y), Print("|"))?;
        }
        queue!(stdout, MoveTo(ball_x, ball_y), Print("O"))?;
        stdout.flush()
    }
}

fn main() -> io::Result<()> {
    let (terminal_width, terminal_height) = terminal::size()?;
    if terminal_width < WIDTH || terminal_height < HEIGHT + 2 {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!(
                "Terminal must be at least {WIDTH} columns by {HEIGHT} rows (currently {terminal_width}x{terminal_height})."
            ),
        ));
    }

    let mut terminal_session = TerminalSession::start()?;
    let mut game = Game::new();
    let mut last_update = Instant::now();
    let tick = Duration::from_millis(45);
    let mut quit = false;

    while !quit && game.player_score < WINNING_SCORE && game.cpu_score < WINNING_SCORE {
        if event::poll(Duration::from_millis(10))? {
            if let Event::Key(key) = event::read()? {
                match key.code {
                    KeyCode::Char('w') | KeyCode::Up => game.move_player(-2),
                    KeyCode::Char('s') | KeyCode::Down => game.move_player(2),
                    KeyCode::Char('q') | KeyCode::Esc => quit = true,
                    _ => {}
                }
            }
        }

        if last_update.elapsed() >= tick {
            game.update();
            game.draw(&mut terminal_session.stdout)?;
            last_update = Instant::now();
        }
    }

    if !quit {
        queue!(
            terminal_session.stdout,
            MoveTo(0, HEIGHT + 1),
            Print(if game.player_score == WINNING_SCORE {
                "You win! Press any key to exit."
            } else {
                "CPU wins! Press any key to exit."
            })
        )?;
        terminal_session.stdout.flush()?;
        let _ = event::read()?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scores_when_ball_passes_a_paddle() {
        let mut game = Game::new();
        game.ball_x = f32::from(WIDTH - 1);
        game.velocity_x = 1.0;
        game.right_paddle = 2;
        game.update();

        assert_eq!(game.player_score, 1);
        assert_eq!(game.ball_x, f32::from(WIDTH / 2));
    }

    #[test]
    fn player_paddle_stays_inside_the_board() {
        let mut game = Game::new();
        game.move_player(-100);
        assert_eq!(game.left_paddle, 1 + PADDLE_HEIGHT / 2);

        game.move_player(100);
        assert_eq!(game.left_paddle, HEIGHT as i16 - 2 - PADDLE_HEIGHT / 2);
    }
}
