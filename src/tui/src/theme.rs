// SPDX-License-Identifier: MIT
//! Color themes for the ratatui front end.
//!
//! Themes are presentation-only: they do not affect benchmark behavior,
//! result computation, or private-boundary calls.

use ratatui::style::{Color, Modifier, Style};

/// Selectable TUI color scheme.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum ThemeKind {
    /// Dark theme with cyan accents.
    #[default]
    Dark,
    /// Light theme for bright terminals.
    Light,
    /// High-contrast theme for accessibility.
    HighContrast,
    /// Dracula.
    Dracula,
    /// Nord.
    Nord,
    /// Gruvbox dark.
    GruvboxDark,
    /// Gruvbox light.
    GruvboxLight,
    /// Catppuccin Mocha.
    CatppuccinMocha,
    /// Catppuccin Latte.
    CatppuccinLatte,
    /// Tokyo Night.
    TokyoNight,
    /// Solarized dark.
    SolarizedDark,
    /// Solarized light.
    SolarizedLight,
    /// Monokai.
    Monokai,
    /// One Dark.
    OneDark,
    /// Rosé Pine.
    RosePine,
}

impl ThemeKind {
    /// All built-in themes in cycle order.
    pub const ALL: [Self; 15] = [
        Self::Dark,
        Self::Light,
        Self::HighContrast,
        Self::Dracula,
        Self::Nord,
        Self::GruvboxDark,
        Self::GruvboxLight,
        Self::CatppuccinMocha,
        Self::CatppuccinLatte,
        Self::TokyoNight,
        Self::SolarizedDark,
        Self::SolarizedLight,
        Self::Monokai,
        Self::OneDark,
        Self::RosePine,
    ];

    /// Stable display name.
    pub fn name(self) -> &'static str {
        match self {
            Self::Dark => "dark",
            Self::Light => "light",
            Self::HighContrast => "high-contrast",
            Self::Dracula => "dracula",
            Self::Nord => "nord",
            Self::GruvboxDark => "gruvbox-dark",
            Self::GruvboxLight => "gruvbox-light",
            Self::CatppuccinMocha => "catppuccin-mocha",
            Self::CatppuccinLatte => "catppuccin-latte",
            Self::TokyoNight => "tokyo-night",
            Self::SolarizedDark => "solarized-dark",
            Self::SolarizedLight => "solarized-light",
            Self::Monokai => "monokai",
            Self::OneDark => "one-dark",
            Self::RosePine => "rose-pine",
        }
    }

    /// Cycle to the next built-in theme.
    pub fn next(self) -> Self {
        match self {
            Self::Dark => Self::Light,
            Self::Light => Self::HighContrast,
            Self::HighContrast => Self::Dracula,
            Self::Dracula => Self::Nord,
            Self::Nord => Self::GruvboxDark,
            Self::GruvboxDark => Self::GruvboxLight,
            Self::GruvboxLight => Self::CatppuccinMocha,
            Self::CatppuccinMocha => Self::CatppuccinLatte,
            Self::CatppuccinLatte => Self::TokyoNight,
            Self::TokyoNight => Self::SolarizedDark,
            Self::SolarizedDark => Self::SolarizedLight,
            Self::SolarizedLight => Self::Monokai,
            Self::Monokai => Self::OneDark,
            Self::OneDark => Self::RosePine,
            Self::RosePine => Self::Dark,
        }
    }

    /// Return the palette for this theme.
    pub fn palette(self) -> ThemePalette {
        ThemePalette::for_kind(self)
    }
}

/// Concrete colors used by the renderer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ThemePalette {
    /// Primary accent for titles, selected items and key hints.
    pub accent: Color,
    /// Positive/success status color.
    pub ok: Color,
    /// Warning/unsupported/unavailable status color.
    pub warn: Color,
    /// Error/failure/blocked status color.
    pub bad: Color,
    /// Muted secondary text.
    pub muted: Color,
    /// Label/prompt text.
    pub label: Color,
    /// Default foreground text.
    pub fg: Color,
    /// Secondary value text.
    pub value: Color,
}

impl ThemePalette {
    /// Return the built-in palette for a theme kind.
    pub fn for_kind(kind: ThemeKind) -> Self {
        match kind {
            ThemeKind::Dark => Self::dark(),
            ThemeKind::Light => Self::light(),
            ThemeKind::HighContrast => Self::high_contrast(),
            ThemeKind::Dracula => Self::dracula(),
            ThemeKind::Nord => Self::nord(),
            ThemeKind::GruvboxDark => Self::gruvbox_dark(),
            ThemeKind::GruvboxLight => Self::gruvbox_light(),
            ThemeKind::CatppuccinMocha => Self::catppuccin_mocha(),
            ThemeKind::CatppuccinLatte => Self::catppuccin_latte(),
            ThemeKind::TokyoNight => Self::tokyo_night(),
            ThemeKind::SolarizedDark => Self::solarized_dark(),
            ThemeKind::SolarizedLight => Self::solarized_light(),
            ThemeKind::Monokai => Self::monokai(),
            ThemeKind::OneDark => Self::one_dark(),
            ThemeKind::RosePine => Self::rose_pine(),
        }
    }

    /// Dark palette.
    pub const fn dark() -> Self {
        Self {
            accent: Color::Rgb(110, 200, 255),
            ok: Color::Rgb(120, 220, 140),
            warn: Color::Rgb(255, 200, 90),
            bad: Color::Rgb(255, 110, 110),
            muted: Color::Rgb(150, 160, 175),
            label: Color::Rgb(170, 190, 220),
            fg: Color::White,
            value: Color::Gray,
        }
    }

    /// Light palette.
    pub const fn light() -> Self {
        Self {
            accent: Color::Rgb(0, 95, 175),
            ok: Color::Rgb(0, 135, 0),
            warn: Color::Rgb(175, 95, 0),
            bad: Color::Rgb(175, 0, 0),
            muted: Color::Rgb(90, 90, 90),
            label: Color::Rgb(60, 60, 120),
            fg: Color::Black,
            value: Color::Rgb(50, 50, 50),
        }
    }

    /// High-contrast palette.
    pub const fn high_contrast() -> Self {
        Self {
            accent: Color::Cyan,
            ok: Color::Green,
            warn: Color::Yellow,
            bad: Color::Red,
            muted: Color::White,
            label: Color::Cyan,
            fg: Color::White,
            value: Color::White,
        }
    }

    /// Dracula palette.
    pub const fn dracula() -> Self {
        Self {
            accent: Color::Rgb(189, 147, 249),
            ok: Color::Rgb(80, 250, 123),
            warn: Color::Rgb(241, 250, 140),
            bad: Color::Rgb(255, 85, 85),
            muted: Color::Rgb(98, 114, 164),
            label: Color::Rgb(139, 233, 253),
            fg: Color::Rgb(248, 248, 242),
            value: Color::Rgb(191, 191, 191),
        }
    }

    /// Nord palette.
    pub const fn nord() -> Self {
        Self {
            accent: Color::Rgb(136, 192, 208),
            ok: Color::Rgb(163, 190, 140),
            warn: Color::Rgb(235, 203, 139),
            bad: Color::Rgb(191, 97, 106),
            muted: Color::Rgb(76, 86, 106),
            label: Color::Rgb(129, 161, 193),
            fg: Color::Rgb(236, 239, 244),
            value: Color::Rgb(216, 222, 233),
        }
    }

    /// Gruvbox dark palette.
    pub const fn gruvbox_dark() -> Self {
        Self {
            accent: Color::Rgb(131, 165, 152),
            ok: Color::Rgb(184, 187, 38),
            warn: Color::Rgb(250, 189, 47),
            bad: Color::Rgb(251, 73, 52),
            muted: Color::Rgb(146, 131, 116),
            label: Color::Rgb(211, 134, 155),
            fg: Color::Rgb(235, 219, 178),
            value: Color::Rgb(213, 196, 161),
        }
    }

    /// Gruvbox light palette.
    pub const fn gruvbox_light() -> Self {
        Self {
            accent: Color::Rgb(7, 102, 120),
            ok: Color::Rgb(121, 116, 14),
            warn: Color::Rgb(181, 118, 20),
            bad: Color::Rgb(157, 0, 6),
            muted: Color::Rgb(146, 131, 116),
            label: Color::Rgb(143, 63, 113),
            fg: Color::Rgb(60, 56, 54),
            value: Color::Rgb(80, 73, 69),
        }
    }

    /// Catppuccin Mocha palette.
    pub const fn catppuccin_mocha() -> Self {
        Self {
            accent: Color::Rgb(137, 180, 250),
            ok: Color::Rgb(166, 227, 161),
            warn: Color::Rgb(249, 226, 175),
            bad: Color::Rgb(243, 139, 168),
            muted: Color::Rgb(127, 132, 156),
            label: Color::Rgb(203, 166, 247),
            fg: Color::Rgb(205, 214, 244),
            value: Color::Rgb(186, 194, 222),
        }
    }

    /// Catppuccin Latte palette.
    pub const fn catppuccin_latte() -> Self {
        Self {
            accent: Color::Rgb(30, 102, 245),
            ok: Color::Rgb(64, 160, 43),
            warn: Color::Rgb(223, 142, 29),
            bad: Color::Rgb(210, 15, 57),
            muted: Color::Rgb(140, 143, 161),
            label: Color::Rgb(136, 57, 239),
            fg: Color::Rgb(76, 79, 105),
            value: Color::Rgb(92, 95, 119),
        }
    }

    /// Tokyo Night palette.
    pub const fn tokyo_night() -> Self {
        Self {
            accent: Color::Rgb(122, 162, 247),
            ok: Color::Rgb(158, 206, 106),
            warn: Color::Rgb(224, 175, 104),
            bad: Color::Rgb(247, 118, 142),
            muted: Color::Rgb(86, 95, 137),
            label: Color::Rgb(187, 154, 247),
            fg: Color::Rgb(192, 202, 245),
            value: Color::Rgb(169, 177, 214),
        }
    }

    /// Solarized dark palette.
    pub const fn solarized_dark() -> Self {
        Self {
            accent: Color::Rgb(38, 139, 210),
            ok: Color::Rgb(133, 153, 0),
            warn: Color::Rgb(181, 137, 0),
            bad: Color::Rgb(220, 50, 47),
            muted: Color::Rgb(88, 110, 117),
            label: Color::Rgb(42, 161, 152),
            fg: Color::Rgb(131, 148, 150),
            value: Color::Rgb(147, 161, 161),
        }
    }

    /// Solarized light palette.
    pub const fn solarized_light() -> Self {
        Self {
            accent: Color::Rgb(38, 139, 210),
            ok: Color::Rgb(133, 153, 0),
            warn: Color::Rgb(181, 137, 0),
            bad: Color::Rgb(220, 50, 47),
            muted: Color::Rgb(147, 161, 161),
            label: Color::Rgb(42, 161, 152),
            fg: Color::Rgb(101, 123, 131),
            value: Color::Rgb(88, 110, 117),
        }
    }

    /// Monokai palette.
    pub const fn monokai() -> Self {
        Self {
            accent: Color::Rgb(102, 217, 239),
            ok: Color::Rgb(166, 226, 46),
            warn: Color::Rgb(230, 219, 116),
            bad: Color::Rgb(249, 38, 114),
            muted: Color::Rgb(117, 113, 94),
            label: Color::Rgb(174, 129, 255),
            fg: Color::Rgb(248, 248, 242),
            value: Color::Rgb(204, 204, 204),
        }
    }

    /// One Dark palette.
    pub const fn one_dark() -> Self {
        Self {
            accent: Color::Rgb(97, 175, 239),
            ok: Color::Rgb(152, 195, 121),
            warn: Color::Rgb(229, 192, 123),
            bad: Color::Rgb(224, 108, 117),
            muted: Color::Rgb(92, 99, 112),
            label: Color::Rgb(198, 120, 221),
            fg: Color::Rgb(171, 178, 191),
            value: Color::Rgb(190, 196, 207),
        }
    }

    /// Rosé Pine palette.
    pub const fn rose_pine() -> Self {
        Self {
            accent: Color::Rgb(156, 207, 216),
            ok: Color::Rgb(49, 116, 143),
            warn: Color::Rgb(246, 193, 119),
            bad: Color::Rgb(235, 111, 146),
            muted: Color::Rgb(110, 106, 134),
            label: Color::Rgb(196, 167, 231),
            fg: Color::Rgb(224, 222, 244),
            value: Color::Rgb(144, 140, 170),
        }
    }

    /// Style for status-like text.
    pub fn status_style(&self, text: &str) -> Style {
        let lower = text.to_ascii_lowercase();
        if lower.contains("blocked")
            || lower.contains("failed")
            || lower.contains("failure")
            || lower.contains("error")
            || lower.contains("incomplete")
            || lower.contains("regression")
            || lower.contains("invalid")
            || lower.contains("untrusted")
        {
            Style::default().fg(self.bad).add_modifier(Modifier::BOLD)
        } else if lower.contains("warning")
            || lower.contains("unsupported")
            || lower.contains("unavailable")
            || lower.contains("unknown")
            || lower.contains("not_applicable")
            || lower.contains("pending")
        {
            Style::default().fg(self.warn).add_modifier(Modifier::BOLD)
        } else if lower.contains("success")
            || lower.contains("complete")
            || lower.contains("trusted")
            || lower.contains("allowed")
            || lower.contains("measured")
            || lower.contains("ok")
            || lower.contains("value")
        {
            Style::default().fg(self.ok).add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(self.accent)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{ThemeKind, ThemePalette};

    #[test]
    fn theme_kind_cycles_through_all_themes() {
        let mut current = ThemeKind::Dark;
        let mut seen = Vec::new();
        for _ in 0..ThemeKind::ALL.len() {
            seen.push(current);
            current = current.next();
        }
        assert_eq!(current, ThemeKind::Dark);
        assert_eq!(seen, ThemeKind::ALL);
    }

    #[test]
    fn theme_names_are_unique_and_stable() {
        let mut names = std::collections::BTreeSet::new();
        for kind in ThemeKind::ALL {
            assert!(names.insert(kind.name()), "duplicate name: {}", kind.name());
            assert_eq!(kind.palette(), ThemePalette::for_kind(kind));
        }
    }

    #[test]
    fn status_style_maps_semantic_classes() {
        let p = ThemePalette::dark();
        assert_eq!(p.status_style("complete").fg, Some(p.ok));
        assert_eq!(p.status_style("warning").fg, Some(p.warn));
        assert_eq!(p.status_style("failed").fg, Some(p.bad));
        assert_eq!(p.status_style("other").fg, Some(p.accent));
    }
}
