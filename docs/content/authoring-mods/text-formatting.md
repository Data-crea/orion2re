# Text Formatting

Some game text resources contain inline control codes for layout, dynamic values, conditional text, and visual styles. When editing supported strings through [JSON5 string overrides](string-overrides.md) or replacing resources through the [VFS](vfs-and-assets.md), preserve any controls that the screen or game system expects.

This page describes the control language processed by the text formatting engine. It does not make an arbitrary asset accept formatted text; the code that displays the asset must already use this engine.

## Byte notation

The examples use C-style notation to make non-printable bytes visible:

- `\0`, `\a`, `\b`, `\t`, `\n`, `\v`, `\f`, and `\r` denote standard control bytes.
- `\x17` through `\x1F` denote bytes by hexadecimal value.
- `<arg>` denotes an argument supplied by the surrounding game code. Variable indexes are hexadecimal digits from `0` through `F`.

These sequences describe bytes, not necessarily literal backslash characters in an asset. Use an editor or conversion tool that can write the required byte values, and test the resulting resource in the screen where it is used.

JSON5 string overrides preserve numeric byte values without code-page conversion. Use `\xXX` or `\u00XX` for a byte, `\n` for a newline, and `\u0007` for the bell byte shown here as `\a`; `\a` is not a JSON5 bell escape. The JSON5 file itself must be UTF-8, and string values cannot contain characters above `U+00FF`. Do not add an ending `\0`; the game adds the string terminator, and embedded NUL is rejected. See [String override encoding and limits](string-overrides.md#syntax-encoding-and-limits) before copying byte sequences into JSON5.

## Standard control characters

| Code | Name | Behavior | Example |
| --- | --- | --- | --- |
| `\0` (`0x00`) | NUL | Ends the current string. A nested string returns to its parent. | `Text\0` |
| `\a` (`0x07`) | Bell | Starts a function sequence. | `\aF1.Text` |
| `\b` (`0x08`) | Backspace | Marks a wrap point inside a word. A hyphen is inserted if the line wraps there. | `Hyphen\bated` |
| `\t` (`0x09`) | Tab | Advances to the next configured tab stop. | `Column 1\tColumn 2` |
| `\n` (`0x0A`) or `\r` (`0x0D`) | Line or paragraph break | Moves to the start of the next line and resets paragraph alignment. | `Line 1\nLine 2` |
| `\v` (`0x0B`) | Vertical tab | Advances by the current line height without resetting paragraph settings such as indentation. | `Line 1\vLine 2` |
| `\f` (`0x0C`) | Form feed | Advances vertically by the configured form-feed height. | `Block 1\fBlock 2` |

## Paragraph positions (`\x17`)

These commands capture or clear paragraph positions. Uppercase commands use the cursor's current horizontal position.

| Command | Behavior | Example |
| --- | --- | --- |
| `\x17I` | Sets the first-line indent. | `    \x17IText` |
| `\x17L` | Sets the left indent. | `  \x17LText` |
| `\x17R` | Sets the right indent. | `Text\x17R` |
| `\x17T` | Adds a tab stop. | `    \x17T` |
| `\x17i` | Clears the first-line indent. | `\x17iText` |
| `\x17l` | Clears the left indent. | `\x17lText` |
| `\x17m` | Clears the left and right indents. | `\x17mText` |
| `\x17r` | Clears the right indent. | `\x17rText` |
| `\x17t` | Clears all tab stops. | `\x17tText` |

## Dynamic values and text (`\x18`)

These commands insert supplied values, select text conditionally, or change letter case. Commands that take `<arg>` use one hexadecimal variable index.

### Values and ordinals

| Command | Behavior | Example result |
| --- | --- | --- |
| `\x18A<arg>` | Inserts a string argument. | `Hello \x18A0!` can produce `Hello Player!` |
| `\x18B<arg>` | Inserts a numeric argument. | `You have \x18B1 credits.` can produce `You have 5 credits.` |
| `\x18C<arg>` | Inserts a numeric ordinal such as `1st` or `2nd`. | `You finished \x18C0.` |
| `\x18D<arg>` | Inserts an ordinal word such as `First`. | `The \x18D0 stage` |

### Plural and conditional text

The formatting context maintains plural and conditional flags. Some commands use those flags directly; others test a supplied numeric argument.

| Command | Behavior |
| --- | --- |
| `\x18E` | Inserts `s` when the plural flag is set. |
| `\x18F<arg>` | Inserts `s` when the argument is not `1`. |
| `\x18G` | Inserts `es` when the plural flag is set. |
| `\x18H<arg>` | Inserts `es` when the argument is not `1`. |
| `\x18I<arg>` | Inserts the selected string when the plural flag is set. |
| `\x18J<arg>` | Inserts the next string argument when the selected numeric argument is not `1`. |
| `\x18K<arg>` | Inserts the selected string when the plural flag is clear. |
| `\x18L<arg>` | Inserts the next string argument when the selected numeric argument is `1`. |
| `\x18M<arg>` | Inserts the selected string when the conditional flag is set. |
| `\x18N<arg>` | Inserts the next string argument when the selected numeric argument is nonzero. |
| `\x18O<arg>` | Inserts the selected string when the conditional flag is clear. |
| `\x18P<arg>` | Inserts the next string argument when the selected numeric argument is zero. |

For example, `Apple\x18E` produces either `Apple` or `Apples`, according to the current plural flag.

### Letter case

| Command | Behavior | Example |
| --- | --- | --- |
| `\x18Q` | Converts following text to uppercase. | `\x18Qlarge\x18T` |
| `\x18R` | Converts following text to lowercase. | `\x18RSMALL\x18T` |
| `\x18S` | Converts following text to title case. | `\x18Stitle text\x18T` |
| `\x18T` | Restores the source text's letter case. | `\x18QUPPER \x18TNormal` |

## Conditions (`\x19`)

Condition commands update the current conditional flag or copy values. Later `\x18` commands can use the flag to select text, and `\x1FL` can use it to leave the current nesting level.

| Command | Operation |
| --- | --- |
| `\x190<arg>` | Sets the conditional flag when the selected variable is nonzero. |
| `\x191` | Copies the plural flag to the conditional flag. |
| `\x192` | Inverts the conditional flag. |
| `\x193<arg>` | Stores the conditional flag in the selected variable. |
| `\x194<arg1><arg2>` | Copies `var[arg1]` to `var[arg2]`. |
| `\x195<arg1><arg2>` | Sets the flag when `var[arg2] == var[arg1]`. |
| `\x196<arg1><arg2>` | Sets the flag when `var[arg2] != var[arg1]`. |
| `\x197<arg1><arg2>` | Sets the flag when `var[arg2] > var[arg1]`. |
| `\x198<arg1><arg2>` | Sets the flag when `var[arg2] < var[arg1]`. |
| `\x199<arg1><arg2>` | Sets the flag when `var[arg2] >= var[arg1]`. |
| `\x19A<arg1><arg2>` | Sets the flag when `var[arg2] <= var[arg1]`. |

For example, `\x19501` compares variable `1` with variable `0` and sets the conditional flag when they are equal.

## Alignment (`\x1A`)

| Command | Alignment |
| --- | --- |
| `\x1A0` | Left |
| `\x1A1` | Right |
| `\x1A2` | Centered |
| `\x1A3` | Fully justified |

The selected alignment applies to following paragraph text. For example, `\x1A2Centered` centers the text between the current margins.

## Color attributes and styles

`\x1B<byte>` sets the current color attribute from the following byte. The engine shifts that byte left by five bits before applying it. The visible result depends on the active font's color definitions. For example, the byte sequence represented by `\x1B\x01Text` applies attribute `1` to `Text`.

`\x1C` selects foreground or background colors:

| Command | Behavior | Example |
| --- | --- | --- |
| `\x1C<0-5>` | Selects a foreground color index. | `\x1C2Text` |
| `\x1C6<0-7>` | Selects a background color index. | `\x1C61Text` |

Color indexes are palette and font dependent. Test them in the target screen rather than assuming that an index always represents the same visible color.

## Parameter reset (`\x1D`)

`\x1D<char>` uses the low four bits of the following character to select reset flags. Depending on that selection, it resets font spacing, space width, line advance, or other paragraph values. For example, `\x1D\x01` applies the reset selection represented by byte `0x01`.

## Leaving a context (`\x1F`)

| Command | Behavior |
| --- | --- |
| `\x1FL` | Leaves the current nesting level when the conditional flag is set. Processing then returns to the parent string. |
| `\x1FP` | Stops processing the current paragraph. |

## Function sequences (`\a`)

The bell byte (`\a`) starts a function sequence. A function is identified by one character and can take numeric arguments. Prefix a number with `+` or `-` for a relative adjustment, and separate multiple functions or arguments with commas. A period ends the sequence; a character that is not valid in a function sequence also returns the parser to text.

### Variables (`=`)

The `=` function selects and updates an internal variable. The first use selects the variable index and the second supplies its value:

```text
\a=0,=5.Text
```

This sets variable `0` to `5`. A signed value applies a relative change, so `\a=0,=+2.Text` adds `2` to variable `0`.

### User callback (`C`)

The `C` function invokes a callback supplied by the code displaying the text. The first argument sets the auxiliary user argument and the second identifies the callback:

```text
\aC1,C100.Text
```

Available callbacks depend on the screen and resource. Do not assume that a callback identifier is portable to another text context.

### Fonts (`F` and `R`)

`F` selects the standard font and `R` selects the highlighted font. Both accept font IDs from `0` through `5`.

```text
\aF2.Text
```

The appearance of an ID depends on the fonts configured for the current text context.

### Horizontal spacing (`H`)

`H` sets the extra horizontal spacing between characters. Its range is `0` through `63`; signed values adjust the current setting.

```text
\aH+2.Text
```

### First-line indent (`I`)

`I` sets the first-line indent relative to the base left indent.

```text
\aI10.Text
```

### Margins (`M`)

`M` sets a paragraph margin. Supply the position followed by a side value: `1` selects the left margin and any other value selects the right margin.

```text
\aM15,1.Text
```

### Origin and width (`O`)

`O` changes the text bounding box. Supply the value followed by an axis selector: `1` changes X, `2` changes Y, and `3` changes the width.

```text
\aO200,3.Text
```

This sets the text area width to 200 pixels.

### Form-feed advance (`P`)

`P` sets the vertical advance used by `\f`. Its range is `0` through `63`.

```text
\aP20.Text
```

### Space width (`S`)

`S` sets the width of a space character. Its range is `0` through `63`.

```text
\aS5.Text
```

### Tab stops (`T`)

`T` adds a tab stop at the supplied coordinate. Repeat the function to add more than one stop. `T` without an argument clears all tab stops.

```text
\aT50,T100.Text
\aT.Text
```

### Vertical spacing (`V`)

`V` sets the line advance. Its range is `0` through `63`.

```text
\aV12.Text
```

### Cursor position (`X` and `Y`)

`X` changes the text cursor's horizontal position. A signed argument moves it relative to the current position.

```text
\aX+10.Text
```

`Y` changes the paragraph's vertical position within the current bounds.

```text
\aY50.Text
```

### Superscript (`^`)

`^` selects a superscript style by shifting the font ID. Argument `0` restores the previous style.

```text
Base\a^1.Superscript\a^0.Base
```

## Test formatted resources

Formatting is stateful and context dependent. Font IDs, colors, callbacks, supplied arguments, bounds, and default paragraph settings can differ between screens. After replacing a formatted text resource:

1. Open every screen that uses the changed text.
2. Test singular, plural, zero, and large numeric values where substitutions are present.
3. Check wrapping at the target resolution and with the longest expected text.
4. Verify that case, font, color, indentation, and alignment return to their expected states after styled spans.
5. Test the packaged mod without loose development overrides before publishing it.