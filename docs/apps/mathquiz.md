# Math Quiz

Arithmetic practice for school kids, levelled by school class, with a chart of
how today went against the two weeks before it.

## Using it

**Apps > Math Quiz.** Pick the class you are in, 1 to 6. Each tile shows a
typical sum for that class, so a child who is unsure can pick by what the
questions look like. The class picked last time is the filled tile.

A sum fills the top of the screen and four answers sit under it, two by two.
Tap one. The right answer turns black; a wrong pick is struck through and the
screen says what the answer was. **NEXT** deals another sum; **END** (or the
Back swipe) ends the round.

Ending a round opens **MY SCORES**: two bar charts over the last fourteen days,
today's bar solid and the rest shaded.

- **RIGHT ANSWERS** is the share of questions answered right each day.
- **SECONDS EACH** is the average time to answer a question that day. As a
  child gets better this one should come down.

The dashed line across each chart is the average over the fortnight, leaving
today out, and the legend gives both numbers: today's beside the solid square,
the fortnight's beside the dash. **OLDER** and **NEWER** page back and forward
by two weeks at a time. **PRACTISE** starts another round at the same class.
**MY SCORES** on the class picker opens the charts without playing first.

## What each class gets

Questions are made on the device, so there is nothing to download and a class
never runs out. Each has its own mix:

| Class | Questions |
| ----- | --------- |
| 1 | Adding and taking away within 20 |
| 2 | Adding and taking away within 100 |
| 3 | Times tables to 10, their division facts, three-digit sums |
| 4 | Two- and three-digit numbers times one digit, short division, four-digit sums |
| 5 | Two digits times two digits, dividing by two digits, decimals to tenths |
| 6 | Order of operations, negative numbers, percentages |

The three wrong answers are the mistakes a child actually makes, such as one
row too many in a times table, a carry in the wrong column, adding instead of
taking away, or a lost minus sign. That way the right answer can't be spotted
as the odd one out.

The time starts when the sum is on the screen, not when it was dealt. A
question left open longer than a minute counts as a minute, so a child who
wanders off mid-question does not wreck the day's average.

## The files

Both are plain text in `/.crosspoint/mathquiz/` on the card.

- `history.txt` holds one line per day, `day|asked|right|ms`: the day as days
  since 1970-01-01, the questions answered, how many were right, and the total
  answering time in milliseconds. It is written after every answer, so a round
  left by the Home key or a flat battery still counts.
- `class.txt` holds the class picked last.

History is kept by day, so it needs the clock. If the clock has never been set,
the charts say so. Connecting to Wi-Fi once sets it.

## Code

`src/apps_local/mathquiz/`: `MathQuizCore` (questions, history, chart windows;
freestanding), `MathQuizScreens` (the three screens; freestanding) and
`MathQuizActivity` (files, round, input). `host-tests/mathquiz/run.sh` checks
every class's questions against an independent evaluator, plus the history
format and the chart windows.
