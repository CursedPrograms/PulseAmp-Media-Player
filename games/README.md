# PulseAmp games

**Games** in the menu bar of the full window (not in Classic mode) turns the
video / visualizer area into a neon arcade, with the visualizer running dimmed
behind it. The music keeps playing and the games move with it: Runner
obstacles arrive on the beat, Invaders march to it, and Snake gets bonus notes
on the beat.

| Keys | |
|---|---|
| Arrows / WASD | Move / steer |
| Space | Fire (Invaders), jump (Runner; press twice for a double jump, hold for higher) |
| P | Pause |
| R | Restart |
| Esc | Leave the game |

Best scores are kept in `games.ini` in PulseAmp's settings folder.

## Making a game: `.pag` files

A `.pag` file ("PulseAmp game") is a small text file in JSON (with `//`
comments allowed). It picks one of the three built-in game engines and sets its
rules, colours and levels. Put it in `%APPDATA%\PulseAmp\games` (Windows) or
`~/.pulseamp/games` (Linux/macOS): **Games → Open games folder** takes you
there. It shows up in the Games menu the next time the menu opens. If a file
has a mistake, the menu says which file and what's wrong.

The three files in this folder are complete examples.

### Every game

```jsonc
{
  "name": "My Game",            // shown in the Games menu
  "engine": "snake",            // snake | runner | invaders (required)
  "author": "You",
  "colors": { "primary": "#00ff8c", "secondary": "#ff3cc8", "accent": "#01dcff" },
  "speed": 1.0,                 // 0.25 - 4
  "beat": true                  // move with the music (false: fixed timing)
}
```

### Snake

| Key | |
|---|---|
| `map` | Rows of text, `#` = wall, anything else = floor. The size of the map sets the size of the arena (default 32 x 20, no walls). |
| `wrap` | `true` (default): leaving one edge brings you back on the other. `false`: the edges kill. |

Colours: primary = snake head, accent = tail and walls, secondary = food.

### Runner

| Key | |
|---|---|
| `gravity` | 0.3 - 3 (default 1): lower is floatier |
| `gap` | 0.3 - 4 (default 1): space between obstacles |

Colours: primary = ground, secondary = obstacles and skyline, accent = you.

### Invaders

| Key | |
|---|---|
| `lives` | 1 - 9 (default 3) |
| `waves` | A list of formations, each a list of rows: `S` squid (30 points), `C` crab (20), `B` boss (50), `.` gap. Up to 6 rows of 12. After the last wave they start again from the first. |

Colours: primary = aliens, secondary = your ship, accent = shots and bosses.
