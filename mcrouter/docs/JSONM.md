JSON with macros (JSONM)
-----------------

JSONM is JSON extension that allows using macro definitions inside JSON.

### Why?
Main goal of this format is make configuration files more readable and get rid
of scripts that generate huge configuration files.

### What?
JSONM is superset of JSON. So any JSON object can be treated as JSON with
macros. On the other hand JSON with macros is still a valid JSON object.
The difference is adding special meaning to some JSON properties that allows
reusing similar parts of large JSON object.

### How?
After processing all macros and substituting all constants JSONM becomes
regular JSON. So, using simple preprocessor we can get JSON object from
user-friendly JSONM.

### Syntax
JSONM is JSON object with two special optional keys: `consts` and `macros`:

``` JSON
  {
    "consts": {
      "constName1": constDefinition,
      "constName2": constDefinition,
      …
    },
    "macros": {
      "macroName1": macroDefinition,
      "macroName2": macroDefinition,
      …
    },
    "customProperty1": valueWithMacros,
    "customProperty2": valueWithMacros
  }
```
<dl>
  <dt>value</dt>
  <dd>any JSON value (string, object, number, etc.)</dd>
  <dt>valueWithMacros</dt>
  <dd>JSON value that may contain macroCall, paramSubstitution, builtInCall</dd>
  <dt>macroCall</dt>
  <dd>"@macroName(param1,param2,…)"
  or
  ``` JSON
    {
      "type": "macroName",
      "paramName1": valueWithMacros,
      "paramName2": valueWithMacros,
      …
    }
  ```</dd>
  <dt>paramSubstitution</dt>
  <dd>string of form %paramName%. Examples: "%paramName%", "a%paramName%b".
    %paramName% part will be replaced with value of corresponding parameter.
    If whole string is a parameter substitution (i.e. "%paramName%") parameter
    value may be any valueWithParams, otherwise it should be a string.</dd>
  <dt>builtInCall</dt>
  <dd>In general same as template call, but has no short form.
  ``` JSON
    {
      "type": "merge|select|slice|transform",
      … params for this call …
    }
    ```</dd>
  <dt>constDefinition</dt>
  <dd>`valueWithMacros`, but only built-in macros and calls are allowed.</dd></dl>

### Comments
One can add comments to JSONM. Comments are C-style comments and are removed by
preprocessor. Example:

``` JSON
  {
    // some comment here
    "key": /* and one more comment here */ "value"
  }
```

after preprocessing becomes

``` JSON
  {
    "key": "value"
  }
```
### Macro
Macro is a reusable piece of JSON. One can think about it as a function that
takes list of values and returns `valueWithMacros`.

#### Macro definition
"macros" property should be object with macro definitions. Syntax is following:

``` JSON
"macros": {
  "macroName": {
    "type": "macroDef",
    "params": macroDefParamList,
    "result": valueWithMacros
  }
}
```
"result" is JSONM that may contain `paramSubstitution`.

<dl>
  <dt>macroDefParamList</dt>
  <dd>[ macroDefParam, macroDefParam, …]</dd>
  <dt>macroDefParam</dt>
  <dd>"paramName" | { "name": "paramName", "default": value }</dd>
</dl>

Example:

``` JSON
  {
    "pair": {
      "type": "macroDef",
      "params": [ "key", "value" ],
      "result": {
        "%key%": "%value%"
      }
    },
    "fullName": {
      "type": "macroDef",
      "params": [ "first", "last" ],
      "result": [ "@pair(first,%first%)", "@pair(last,%last%)" ]
    }
  }
```

One can add defaults to parameters. Example:

``` JSON
{
  "car": {
    "type": "macroDef",
    "params": [
      "model",
      // parameter with default
      {
        "name": "color",
        "default": "green"
      }
    ],
    "result": {
      "model": "%model%",
      "color": "%color%"
    }
  }
}
```

#### Macro call
Let's assume we have an object with macros from previous examples. Other
properties may call macros like:

``` JSON
  {
    "person": "@fullName(John,Doe)",
    "car": "@car(Mercedes)"
  }
```

after preprocessing becomes

``` JSON
  {
    "person": {
      "first": "John",
      "last": "Doe"
    },
    "car": {
      "model": "Mercedes",
      "color": "green"
    }
  }
```

### Escaping
Some characters have special meaning for preprocessor. These are ‘@', ‘%', ‘(',
‘)', ‘,'. One can escape them by adding two backslashes (\\) before any
character. That character will be added in string 'as is', it won't have any
special meaning. To add backslash one should write \\\\. The reason for using
two backslashes is JSON already uses backslash (\) as an escape character.
Example:

``` JSON
  {
    "email": "fake\\@fake.fake",
    "valid": "100\\%",
    "backslash": "\\\\"
  }
```

after preprocessing becomes

``` JSON
  {
    "email": "fake@fake.fake",
    "valid": "100%",
    "backslash": "\\"
  }
```

_Note: "backslash" is JSON property, so it is actually one backslash_.

### Built-in calls
These calls provide different operations with lists and objects.

#### merge
Merges multiple lists/objects into one.

``` JSON
"list": {
  "type": "merge",
  "params": [
    [1, 2],
    [3, 4]
  ]
}
```

after preprocessing becomes

``` JSON
  "list": [1, 2, 3, 4]
```

In case params contain list of objects, it will merge objects as well:

``` JSON
  "object": {
    "type": "merge",
    "params": [
      {"a": 1, "b": 2},
      {"b": 3, "c": 4}
    ]
  }
```

after preprocessing becomes

``` JSON
  "object": {
    "a": 1,
    "b": 3,
    "c": 4
  }
```
 
#### select
Returns element from list/object.

``` JSON
  "value": {
    "type": "select",
    "key": "a",
    "dictionary": {
      "a": 1,
      "b": 2
    }
  }
```

after preprocessing becomes

``` JSON
  "value": 1
```

#### shuffle
Randomly shuffles list.

``` JSON
  "list": {
    "type": "shuffle",
    "dictionary": [1, 2, 3, 4]
  }
```

after preprocessing may become

``` JSON
  "list": [2, 4, 3, 1]
```


#### slice
Returns subrange (slice) of list/object.

``` JSON
  "list": {
    "type": "slice",
    "from": 1,
    "to": 2,
    "dictionary": [1, 2, 3, 4]
  }
```

after preprocessing becomes

``` JSON
  "list": [2, 3]
```

#### transform
Transforms elements of list/object. It uses `itemTransform` and `keyTransform`
properties (`keyTransform` is optional and available only if dictionary is
object) to change elements of list/object. `itemTransform` and `keyTransform`
are `valueWithMacros` and may use additional parameters: `item` (`key` and
`item` in case of dictionary).

``` JSON
  "transform": {
    "type": "transform",
    "keyTransform": "%item%",
    "itemTransform": "%key%",
    "dictionary": {
      "a": "b",
      "b": "c"
    }
  }
```

after preprocessing becomes

``` JSON
  {
    "b": "a",
    "c": "b"
  }
```

### Consts
Consts are `valueWithMacros` that may be substituted everywhere. One may use
built-in macros and built-in types in consts.

``` JSON
  {
    "consts": [
      {
        "type": "constDef",
        "name": "author",
        "value": "John Doe"
      },
      {
        "type": "constDef",
        "name": "copyright",
        "value": "%author% owns it"
      }
    ],
    "file": "%copyright%. Some content"
  }
```

after preprocessing becomes

``` JSON
  {
    "file": "John Doe owns it. Some content"
  }
```

### Built-in macros
#### import
Allows loading JSONM from external source. Example:

> File: cities.json
``` JSON
  {
    "cities": [
      "New York",
      "Washington"
    ]
  }
```

> File: city.json
``` JSON
  {
    "city": {
      "type": "select",
      "key": 0,
      "dictionary": "@import(cities.json) "
    }
  }
```

After preprocessing `city.json` becomes

``` JSON
  {
    "city": "New York"
  }
```

#### int, str
`@int` casts its argument to an integer:

``` JSON
  {
    "key": "@int(100)"
  }
```

after preprocessing becomes

``` JSON
  {
    "key": 100
  }
```
`@str` casts it's argument to string.

### Advanced
#### Preprocessing steps

* Expand macros in 'consts' except objects with `'type' : 'constDef'`
* Parse 'consts', one by one and add parsed constant to global context.
* Expand macros except objects with `'type' : 'macroDef'`
* Remove 'consts' and 'macros' properties
* Expand everything else

#### Macro context
Global context is set of all constants. When some macro is called it has it's
own context which is set of all parameters passed.

When some substitution is performed, parameters are looked up in current macro
context, then in global context. It is an error if parameter is found neither
in macro context nor in global context.

Scope of macro context is entire result of macro definition:

``` JSON
  "macros": {
    "sample": {
      "type": "macroDef",
      "params": [ "param" ],
      "result" {
        // one can use %param% here. That's the scope of
        // ‘sample' macro context.
      }
    }
  }
```

Here is more complex example and corresponding preprocessor steps.

> File: constsFile
``` JSON
  {
    "constA": "a"
  }
```

> File: macrosFileA
``` JSON
  {
    "callInner": {
      "type": "macroDef",
      "params": [ "inner" ],
      "result": "@%inner%(%constA%)"
    }
  }
```

> File: mainFile
``` JSON
  {
    "consts": "@import(constsFile)",
    "macros": {
      "type": "merge",
      "params": [
        "@import(macrosFile%constA%)",
        {
          "list": {
            "type": "macroDef",
            "params": [ "param" ],
            "result": [ "%param%" ]
          }
        }
      ]
    },
    "list": "@callInner(list)"
  }
```

after preprocessing mainFile becomes

``` JSON
  {
    "list": [ "a" ]
  }
```

Here are preprocessing steps:

- Expand consts
- Load `constsFile` via `import`
- Add `constA` with value = 'a' to global context
- Expand macros
- Found `merge`, expand params
- Substitute parameter for import: macrosFile%constA% => macrosFileA
- Load `macrosFileA`
- Found 'callInner', it is macroDef, so no need to expand it
- Found 'list', it is macroDef, so no need to expand it
- After merge 'macros' becomes object with macro definitions
- Parse macro definitions for 'callInner' and 'list'
- Expand "@callInner(list)":
- Found macro call with one parameter
- Macro context: `{ "inner": "list" }`, global context: `{ "constA": "a" }`
- Expand "@%inner%(%constA%)"
- Found macro call with one parameter, after substitution we have macro name:
  "list"; context: { "param" : "a" }
- Macro context: { "param": "a" }
- After substitution [ "%param%" ] becomes [ "a" ]
- Nothing to substitute else

So macro call "@macroName(paramList)" is processed like this:

- Expand macroName with current context
- Expand paramList with current context. Note: after this step paramList will
  be a list of JSON values (without macros)
- Create macro context from paramList (inner context)
- Expand 'result' of corresponding macro definition with inner context.
