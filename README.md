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

### Read / Write
```
~write "data.txt" "hello"           ## write (overwrite)
~append "data.txt" "new line"       ## append line
$content -> ~read "data.txt"        ## read entire file as string
$lines -> ~lines "data.txt"         ## read as list of lines
~touch "empty.txt"                  ## create empty file
```

### Check / Info
```
? ~exists "data.txt" :: >> "found" ;;
$sz -> ~size "data.txt"             ## file size in bytes
>> ~ext "photo.png"                 ## "png"
>> ~name "path/to/file.txt"        ## "file.txt"
>> ~dir "path/to/file.txt"         ## "path/to"
$cwd -> ~cwd                       ## current working directory
```

### Copy / Move / Rename / Delete
```
~copy "a.txt" "b.txt"              ## copy file
~move "a.txt" "new/a.txt"          ## move file
~rename "old.txt" "new.txt"        ## rename file
~del "data.txt"                    ## delete file
```

### Directories
```
~mkdir "folder/sub/deep"            ## create dirs (including parents)
~rmdir "folder"                     ## remove directory and contents
~cd "path"                          ## change working directory
$files -> ~ls "."                   ## list directory as list
```

### Search / Replace
```
~find "src" "*.txt" $results        ## find files matching pattern recursively
~replace "file.txt" "old" "new"     ## replace string in file contents
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

### Encoding
```
#base64 "hello"          ## "aGVsbG8="
#decode64 "aGVsbG8="     ## "hello"
```

### HEX Cipher (custom encryption)
Built-in encryption — no libraries needed. Python needs `pip install cryptography`.
```
$encrypted -> #encrypt "my secret" "password123"
## returns: "HX1:base64encodeddata..."

$decrypted -> #decrypt $encrypted "password123"
## returns: "my secret"
```
- XOR stream cipher with key derivation
- Random salt per encryption (same input = different output each time)
- Format: `HX1:<base64(salt + ciphertext)>`

### Debug
```
#dump $var               ## prints type + value
#assert $x > 0 "msg"     ## crash with message if false
```

---

## HTTP Requests

Built-in networking — no libraries needed. Python needs `pip install requests`.

```
## GET request
$response -> ~get "https://api.example.com/data"
>> $response

## POST request
$result -> ~post "https://api.example.com/submit" "name=niko&age=17"
>> $result

## Download a file
~download "https://example.com/file.zip" "file.zip"
```

---

## String Interpolation

Variables inside `{$var}` are automatically expanded in any string. Python needs `f""` prefix.

```
$name -> "Niko"
$age -> 17
>> "Hello {$name}, you are {$age} years old!"
## prints: Hello Niko, you are 17 years old!
```

---

## Colored Output

Built-in colors — no libraries needed. Python needs `pip install colorama`.

```
>> red "Error: something broke"
>> green "Success!"
>> yellow "Warning: be careful"
>> blue "Info"
>> cyan "Debug info"
>> magenta "Special"
>> bold "Important"
```

---

## JSON

Parse JSON strings into HEX maps/lists. Stringify HEX values to JSON.
Python needs `import json`. HEX does it natively.

```
## Parse
$data -> #json '{"name": "Niko", "age": 17, "skills": ["HEX", "C++"]}'
>> $data["name"]          ## Niko
>> $data["skills"][0]     ## HEX

## Stringify (pretty)
$json -> #tojson $data
>> $json

## Stringify (compact)
$compact -> #jsonc $data
>> $compact

## HTTP + JSON combo
$response -> ~get "https://api.example.com/data"
$parsed -> #json $response
>> $parsed["results"]
```

---

## Regex

Pattern matching and replacement. Python needs `import re`.

```
## Match — returns list of groups
$result -> #match "hello world" "[a-z]+"
>> $result               ## ["hello"]

## Match all occurrences
$all -> #matchall "a1 b2 c3" "[a-z]\\d"
>> $all                  ## ["a1", "b2", "c3"]

## Test — returns true/false
$valid -> #test "niko@hex.dev" "^[a-z]+@[a-z]+\\.[a-z]+$"

## Regex replace
$clean -> #replacex "Hello   World" "\\s+" " "
>> $clean                ## "Hello World"
```

---

## String Padding

```
#padl "42" 8 "0"         ## "00000042"
#padr "hello" 10 "."     ## "hello....."
#center "HEX" 20 "-"     ## "--------HEX---------"
```

---

## Enums

```
enum Color ::
    red
    green
    blue
;;

>> $Color["red"]          ## 0
>> $Color["blue"]         ## 2
```

---

## Class Inheritance

```
class Animal ::
    init $name ::
        $this["name"] -> $name
    ;;
    speak ::
        >> $this["name"] + " makes a sound"
    ;;
;;

class Dog extends Animal ::
    speak ::
        >> $this["name"] + " says WOOF!"
    ;;
    fetch ::
        >> $this["name"] + " fetches the ball"
    ;;
;;

$rex -> new Dog "Rex"
$rex.speak               ## Rex says WOOF!
$rex.fetch               ## Rex fetches the ball
```

---

## Error Handling

```
#try ::
    $x -> #int "abc"
#catch ::
    >> "error caught!"
#finally ::
    >> "always runs"
;;
```

### Catch Specific Types
```
#try ::
    $x -> 10 / 0
#catch ZeroDivisionError ::
    >> "caught division by zero!"
;;
```

### Error Info Variables
Inside `#catch`, `$__error_type` and `$__error_msg` are set automatically.

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

## Compound Assignment

```
$x -> 10
$x += 5      ## 15
$x -= 3      ## 12
$x *= 2      ## 24
$x /= 4      ## 6
```

---

## Ternary Expression

```
$result -> ($age >= 18 ? "adult" : "minor")
$max -> ($a > $b ? $a : $b)
```

---

## Print Without Newline

```
>>> "loading"
>>> "."
>>> "."
>> " done!"     ## >> still adds newline
```

---

## Multi-line Strings

```
$text -> """this is
a multi-line
string in HEX"""
```

---

## Constants

```
lock $PI -> 3.14159     ## can never be reassigned
$PI -> 999              ## RuntimeError: cannot reassign constant
```

---

## Match / Switch

```
match $day ::
    case "monday" ::
        >> "start of week"
    ;;
    case "friday" ::
        >> "TGIF"
    ;;
    default ::
        >> "just another day"
    ;;
;;
```

---

## Async Functions

Define functions that run in background threads. `await` launches the thread and blocks until the result is ready.

```
## Define async function
async fetch_data $url ::
    $response -> ~get $url
    => $response
;;

## Call it — runs in a separate thread
$data -> await fetch_data "https://api.example.com"
>> $data

## Async with delay
async slow_add $a $b ::
    #wait 1000
    => $a + $b
;;

$result -> await slow_add 10 20
>> $result    ## 30 (after 1 second)
```

---

## Forever Loop

```
forever ::
    >> "runs until break"
    ? $done ::
        break
    ;;
;;
```

---

## Do-While Loop

```
do ::
    >> "runs at least once"
    $n -= 1
;; while $n > 0
```

---

## Import

```
import "utils.hex"       ## runs another .hex file in current scope
```

---

## Destructuring

```
[$a, $b, $c] -> [10, 20, 30]
>> $a    ## 10
>> $b    ## 20

[$x, $y] -> $some_list
```

---

## Classes

```
class Animal ::
    init $name $sound ::
        $this["name"] -> $name
        $this["sound"] -> $sound
    ;;

    speak ::
        >> $this["name"] + " says " + $this["sound"]
    ;;
;;

$dog -> new Animal "Rex" "woof"
$dog.speak                      ## Rex says woof
>> $dog["name"]                 ## Rex
```

---

## Lambda

```
$double -> fn $x => $x * 2
```

---

## Map / Filter / Reduce

```
double $x ::
    => $x * 2
;;
$nums -> [1, 2, 3, 4, 5]
&map $nums double               ## [2, 4, 6, 8, 10]

is_even $x ::
    => $x % 2 == 0
;;
&filter $nums is_even            ## [4, 8]

add $a $b ::
    => $a + $b
;;
&reduce $nums add                ## single value

&any $list                       ## true if any truthy
&all $list                       ## true if all truthy
&zip $list1 $list2               ## [[a1,b1], [a2,b2], ...]
&enumerate $list                 ## [[0,a], [1,b], ...]
```

---

## Range

```
$r -> #range 1 10               ## [1, 2, 3, ..., 10]
$r -> #range 10 1               ## [10, 9, 8, ..., 1]
```

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
- `network_crypto.hex` — HTTP requests, base64, encryption
- `crypto_test.hex` — custom HEX cipher demo
- `v2_features.hex` — compound assignment, ternary, match, classes, destructuring, map/filter/reduce
- `async_test.hex` — async functions with await
- `v3_features.hex` — JSON, regex, enums, inheritance, error handling v2
- `file_ops.hex` — all file/directory operations

---

## What HEX Does That Python Can't (without pip)

| Feature | Python | HEX |
|---------|--------|-----|
| HTTP requests | `pip install requests` | `~get "url"` built-in |
| Colored output | `pip install colorama` | `>> red "text"` built-in |
| Encryption | `pip install cryptography` | `#encrypt "msg" "pass"` built-in |
| String interpolation | `f"hello {name}"` (needs f prefix) | `"hello {$name}"` (always works) |
| Base64 | `import base64` (verbose) | `#base64 "text"` one-liner |
