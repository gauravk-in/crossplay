# Checkmate

Chess puzzles where you find the mate: in one, two or three moves. GAMES >
CHECKMATE. Code in `src/apps_local/chesspuz/`.

## Playing

Pick MATE IN 1, 2 or 3. The board shows the position after the opponent's last
move, marked with corner brackets, and your side is always at the bottom. Tap a
piece, then tap where it goes; dots show where it can go. The opponent answers
on its own. The pips above the board count the moves you have left.

- **UNDO** takes back your last move and the reply to it. It always works,
  hints or not, until the puzzle is solved.
- **SKIP** moves on. The puzzle counts as played and not solved.
- Any move that mates solves the puzzle, whether or not it is the recorded one.

When it is solved the screen says how many hints and undos you used, and
NEXT PUZZLE takes the next one. Each length keeps its own place, easiest first,
and wraps round at the end.

## Hints

The HINTS switch is on the menu.

- **On**: a move off the solution is crossed out and the opponent does not
  answer, so you undo it. **HINT** frames the piece to move on the first press
  and rings its square on the second. Each cross and each press is one hint.
- **Off**: nothing is marked and there is no HINT button. Off the solution the
  opponent answers with the engine, so a miss shows up as NO MATE when your
  moves run out, and a different mate still counts.

What a puzzle has used is saved as you go, so leaving and coming back does not
clear it.

## Scores

A puzzle is **played** once it is solved or skipped, and **clean** when it was
solved without a single hint. Undos do not count against it. The day's score is
clean puzzles as a share of those played.

MY SCORES charts the last fourteen days: the score, today solid against the
fortnight's dashed average, and how many were played and solved each day.
OLDER and NEWER page back two weeks at a time. Scores need the clock, which is
set the first time the device joins Wi-Fi.

## Files

- `/.crosspoint/chesspuz/history.txt`: one line per day, `day|played|solved|clean`,
  the day counted from 1970-01-01.
- `/.crosspoint/chesspuz/progress.txt`: the HINTS switch, then
  `l<length>=<puzzle>,<hints>,<undos>` for the puzzle each length is on.

## The puzzles

750 puzzles from the [Lichess puzzle database](https://database.lichess.org/#puzzles)
(CC0), credited on the menu: 250 each of mate in one, two and three, all tagged
`endgame`, each played at least 300 times with a popularity of 80 or more,
spread across the rating range and sorted easiest first.
`tools_local/chesspuz/build_puzzles.py` picks them into
`src/apps_local/chesspuz/ChessPuzzleData.h`, which is committed, and
`host-tests/chesspuz` replays every one through the Chess app's rules.

The board, pieces and rules are the Chess app's (`ChessCore`, `ChessEngine`,
`ChessPieces.h`).
