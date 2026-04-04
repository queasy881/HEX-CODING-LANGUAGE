"# HEX-CODING-LANGUAGE" 
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

## Build

```bash
g++ -std=c++17 -O2 -static -o hex.exe hex.cpp
```

## Run

```bash
hex.exe script.hex        # run a file
hex.exe                   # interactive REPL
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

Auto-typed. No declarations needed.

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
>> "hello world"              ## print a string
>> $name                      ## print a variable
>> "age: " + $age             ## concatenation
>> 5 + 3                      ## print expression result

<< $name "What's your name?"  ## prompt + read into $name
<< $x "Enter a number:"      ## auto-detects number vs string
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

? $age >= 18 && $name != "" ::
    >> "valid"
;;
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

`?` = if, `??` = else if, `:: else ::` = else, `;;` = end block.

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
## Define
greet $name ::
    >> "yo " + $name + "!"
;;

## Call
greet "Niko"

## With return value
add $a $b ::
    => $a + $b
;;

$result -> add 10 20
>> $result              ## 30
```

---

## Lists

```
$nums -> [1, 2, 3, 4, 5]
>> $nums                 ## [1, 2, 3, 4, 5]
>> $nums[0]              ## 1
>> $nums[1:3]            ## [2, 3]

&push $nums 6            ## append 6
&pop $nums               ## remove last
&rm $nums 0              ## remove index 0
&len $nums               ## print length
&sort $nums              ## sort ascending
&reverse $nums           ## reverse
```

---

## Strings

```
$msg -> "hello world"

&upper $msg              ## "HELLO WORLD"
&lower $msg              ## "hello world"
&replace $msg "world" "hex"
&has $msg "hello"        ## prints true/false
&split $msg " "          ## turns $msg into list ["hello", "world"]
&join $msg "-"           ## joins list back: "hello-world"

>> $msg[0]               ## "h"
>> $msg[0:5]             ## "hello"
>> #len $msg             ## 11
```

---

## Maps / Dicts

```
$person -> {"name": "Niko", "age": 17}

>> $person["name"]       ## "Niko"
$person["age"] -> 18     ## update value
>> $person               ## {"age": 18, "name": "Niko"}
```

---

## File I/O

```
~write "data.txt" "hello"           ## write (overwrite)
~append "data.txt" "new line"       ## append
$content -> ~read "data.txt"        ## read entire file
>> $content

? ~exists "data.txt" ::             ## check if file exists
    >> "found it"
;;

~del "data.txt"                     ## delete file
```

---

## Shell Commands

```
! "dir"                             ## run command (output to console)
! "cls"                             ## clear screen

$user -> ! "whoami"                 ## capture output into variable
>> "logged in as: " + $user
```

---

## Builtins (#)

### Expressions (return a value)
```
$x -> #int "42"          ## parse to integer
$x -> #float "3.14"      ## parse to float
$x -> #str 42            ## convert to string
$x -> #rand 1 100        ## random number in range
$x -> #abs -5            ## absolute value (5)
$x -> #round 3.7         ## round (4)
$x -> #floor 3.7         ## floor (3)
$x -> #ceil 3.2          ## ceil (4)
$x -> #sqrt 16           ## square root (4)
$x -> #len $mylist       ## length of list/string/map
$x -> #type $var         ## type as string: "number", "string", etc.
$t -> #time              ## current time in ms
```

### Statements (perform an action)
```
#wait 1000               ## sleep 1000ms
#clear                   ## clear screen
#swap $a $b              ## swap two variables
#exit                    ## exit program
#exit 1                  ## exit with code
```

---

## Error Handling

```
#try ::
    $x -> #int "not_a_number"
#catch ::
    >> "error caught!"
;;
```

---

## Project Structure

```
hex/
  token.h            Token types + Token struct
  value.h            Value type system (num, str, bool, list, map, func, null)
  environment.h      Variable scopes
  lexer.h            Tokenizer
  interpreter.h      Parser + executor (the big one)
  hex.cpp            main() entry point
  README.md          This file
  examples/
    hello.hex        Hello world + input
    calculator.hex   Math calculator
    guessing_game.hex  Number guessing game
    fizzbuzz.hex     Classic fizzbuzz
    file_demo.hex    File read/write/append/delete
```

---

## How It Works

1. **Lexer** (`lexer.h`) reads source code and produces tokens
2. **Interpreter** (`interpreter.h`) walks the token stream directly (no AST)
3. **Values** (`value.h`) are dynamically typed — numbers, strings, bools, lists, maps
4. **Environment** (`environment.h`) handles variable scopes with parent chain lookup
5. Blocks use `::` to open and `;;` to close — no indentation rules, no braces

---

## Examples

```bash
hex.exe examples/hello.hex
hex.exe examples/fizzbuzz.hex
hex.exe examples/guessing_game.hex
hex.exe examples/calculator.hex
hex.exe examples/file_demo.hex
```
