# HEX

A symbol-heavy, hacker-aesthetic programming language. Interpreted, built in C++.

```
$name -> "world"
>> "hello " + $name

? 1 + 1 == 2 ::
    >> "math works"
;;
```

No semicolons. No braces. No `def`/`var`/`let`. Just symbols.

---

## Install

Download `install_hex.exe` and run it. It will:
- Download the latest `hex.exe` from this repo
- Install to `C:\Program Files\HEX\`
- Add `hex` to your system PATH
- Register `.hex` file association (double-click to run)

After installing, open a **new** terminal and type `hex` to start.

---

## Run

```bash
hex script.hex        # run a file
hex                   # interactive REPL
```

---

## Symbol Cheatsheet

| Symbol | Meaning | Example |
|--------|---------|---------|
| `$` | Variable | `$name -> "Niko"` |
| `>>` | Print / output | `>> "hello"` |
| `<<` | Input / read | `<< $name "Name?"` |
| `->` | Assignment | `$x -> 42` |
| `=>` | Return value | `=> $x + $y` |
| `?` | If | `? $x > 5 ::` |
| `??` | Else if | `?? $x > 3 ::` |
| `::` | Block start | `? true ::` |
| `;;` | Block end | `;;` |
| `~` | File operations | `~write "f.txt" "hi"` |
| `!` | Shell command | `! "whoami"` |
| `&` | List/string ops | `&push $list 5` |
| `#` | Builtins | `#rand 1 100` |
| `##` | Comment | `## this is ignored` |
| `&&` | Logical AND | `$a && $b` |
| `\|\|` | Logical OR | `$a \|\| $b` |
| `!!` | Logical NOT | `!!$a` |

---

## Variables

```
$name -> "Niko"
$age -> 17
$pi -> 3.14
$alive -> true
$nothing -> null
```

---

## Print & Input

```
>> "hello world"
>> $name
>> "age: " + $age
>> 5 + 3

<< $name "What's your name?"
<< $x "Enter a number:"
```

---

## Math

```
$x -> 5 + 3       ## 8
$x -> 10 - 2      ## 8
$x -> 4 * 3       ## 12
$x -> 10 / 2      ## 5
$x -> 10 % 3      ## 1
$x -> 2 ^ 10      ## 1024
```

---

## Comparisons & Logic

```
==    !=    <    >    <=    >=
&&    ||    !!
```

---

## If / Else If / Else

```
? $x > 10 ::
    >> "big"
?? $x > 5 ::
    >> "medium"
:: else ::
    >> "small"
;;
```

---

## Loops

### While
```
$i -> 0
while $i < 10 ::
    >> $i
    $i -> $i + 1
;;
```

### For (range)
```
for $i in 1..10 ::
    >> $i
;;
```

### For (list)
```
$items -> ["a", "b", "c"]
for $item in $items ::
    >> $item
;;
```

### Repeat
```
rep 5 ::
    >> "hello"
;;
```

### Break & Continue
```
while true ::
    ? $x > 100 ::
        break
    ;;
    $x -> $x + 1
;;
```

---

## Functions

```
greet $name ::
    >> "yo " + $name + "!"
;;

greet "Niko"

add $a $b ::
    => $a + $b
;;

$result -> add 10 20
```

---

## Lists

```
$nums -> [1, 2, 3, 4, 5]
>> $nums[0]              ## 1
>> $nums[1:3]            ## [2, 3]

&push $nums 6            ## append
&pop $nums               ## remove last
&rm $nums 0              ## remove at index
&insert $nums 2 99       ## insert at index
&sort $nums              ## sort ascending
&reverse $nums           ## reverse order
&shuffle $nums           ## randomize order
&unique $nums            ## remove duplicates
&flat $nested            ## flatten nested lists
&fill $zeros 0 5         ## create [0,0,0,0,0]
&len $nums               ## print length
&sum $nums               ## print sum of all numbers
&min $nums               ## print minimum
&max $nums               ## print maximum
&find $nums 99           ## print index of value (-1 if not found)
&contains $nums 99       ## print true/false
&count $nums 3           ## print how many times value appears
```

---

## Strings

```
$msg -> "hello world"

&upper $msg              ## HELLO WORLD
&lower $msg              ## hello world
&trim $msg               ## strip whitespace from both ends
&title $msg              ## Hello World
&capitalize $msg         ## Hello world
&swapcase $msg           ## HELLO WORLD -> hello world
&replace $msg "world" "hex"
&split $msg " "          ## turns into list ["hello", "world"]
&join $msg "-"           ## joins list back: "hello-world"
&repeat $msg 3           ## "hahaha"
&chars $msg              ## ["h", "e", "l", ...]
&has $msg "hello"        ## true/false
&startswith $msg "hel"   ## true/false
&endswith $msg "rld"     ## true/false
&index $msg "world"      ## position (or -1)
&count $msg "l"          ## count occurrences
&isdigit $msg            ## true if all digits
&isalpha $msg            ## true if all letters

>> $msg[0]               ## first char
>> $msg[0:5]             ## slice
>> #len $msg             ## length
```

---

## Maps / Dicts

```
$person -> {"name": "Niko", "age": 17}
>> $person["name"]
$person["age"] -> 18

&keys $person            ## turns into list of keys
&vals $person            ## turns into list of values
&del $person "age"       ## delete a key
&haskey $person "name"   ## true/false
&merge $map1 $map2       ## merge map2 into map1
```

---

## File I/O

```
~write "data.txt" "hello"
~append "data.txt" "new line"
$content -> ~read "data.txt"
? ~exists "data.txt" ::
    >> "found"
;;
~del "data.txt"
```

---

## Shell Commands

```
! "dir"
$user -> ! "whoami"
>> "logged in as: " + $user
```

---

## Builtins (#)

### Type Conversion
```
$x -> #int "42"
$x -> #float "3.14"
$x -> #str 42
$x -> #num "42"          ## safe — returns null on fail
$x -> #bool $val         ## truthy check
```

### Type Checks
```
#isnum $x                ## true/false
#isstr $x
#islist $x
#ismap $x
#isbool $x
#isnull $x
```

### Math
```
#abs -5                  ## 5
#round 3.7               ## 4
#floor 3.7               ## 3
#ceil 3.2                ## 4
#sqrt 16                 ## 4
#min 3 7                 ## 3
#max 3 7                 ## 7
#clamp 15 0 10           ## 10
#pow 2 8                 ## 256
#log 2.718               ## ~1
#sin 3.14                ## ~0
#cos 0                   ## 1
#tan 0                   ## 0
#sign -5                 ## -1
#rand 1 100              ## random number
```

### Utility
```
#len $var                ## length of string/list/map
#type $var               ## "number", "string", etc.
#time                    ## current time in ms
#date                    ## "2026-04-05"
#clock                   ## "23:45:12"
#wait 1000               ## sleep 1000ms
#clear                   ## clear screen
#swap $a $b              ## swap two variables
#exit                    ## exit program
```

### Debug
```
#dump $var               ## prints type + value
#assert $x > 0 "msg"     ## crash with message if false
```

---

## Error Handling

```
#try ::
    $x -> #int "abc"
#catch ::
    >> "error caught!"
;;
```

### Error Types
Errors display Python-style tracebacks:
```
  Traceback:
    File "test.hex", line 5
      $x -> 10 / 0
                  ^
  ZeroDivisionError: division by zero
```

| Error | When |
|-------|------|
| `SyntaxError` | Bad syntax, unknown command |
| `NameError` | Undefined variable |
| `TypeError` | Wrong type for operation |
| `ValueError` | Wrong value |
| `IndexError` | Index out of range |
| `KeyError` | Map key not found |
| `ZeroDivisionError` | Division by zero |
| `FileError` | File operation failed |
| `ArgumentError` | Wrong number of args |

---

## Examples

See the `examples/` folder:
- `hello.hex` — hello world + input
- `calculator.hex` — math calculator
- `guessing_game.hex` — number guessing game
- `fizzbuzz.hex` — classic fizzbuzz
- `file_demo.hex` — file operations
- `errors.hex` — error system demo
- `new_features.hex` — all string/list/map/math builtins
