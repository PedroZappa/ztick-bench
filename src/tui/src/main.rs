// SPDX-License-Identifier: MIT
//! Terminal entry point for the optional ztick-bench front end.
//!
//! This binary contains no benchmark logic. It copies read-only envelopes from
//! the private C11 boundary, renders them with ratatui and processes input until
//! the user quits. Later Phase 5 tasks add the interactive screens.

use std::io;
use std::time::Duration;

use ratatui::crossterm::event::{self, Event, KeyCode, KeyEventKind, KeyModifiers};

use ztick_tui::app::App;
use ztick_tui::frontend::Session;
use ztick_tui::ui;

/// Poll interval used when no terminal input is waiting.
const POLL_INTERVAL: Duration = Duration::from_millis(200);

/// Run the terminal application until the user requests exit.
fn main() -> io::Result<()> {
    let session = Session::new().map_err(io::Error::other)?;
    let snapshot = session.snapshot().map_err(io::Error::other)?;
    let mut app = App::new();
    app.set_snapshot(snapshot);

    ratatui::run(|terminal| {
        loop {
            terminal.draw(|frame| ui::render(frame, &app))?;
            if !event::poll(POLL_INTERVAL)? {
                continue;
            }
            match event::read()? {
                Event::Key(key) if key.kind == KeyEventKind::Press => match key.code {
                    KeyCode::Char('q') | KeyCode::Esc => break Ok(()),
                    KeyCode::Char('c') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                        break Ok(());
                    }
                    _ => {}
                },
                Event::Resize(_, _) => {}
                _ => {}
            }
        }
    })
}
