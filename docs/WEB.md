# Emojineer-Native Web Markup

Emojineer 1.0 includes a declarative web-authoring surface whose source is
emoji markup itself. It is not HTML hidden in front matter, and it is not a
JavaScript reimplementation of Emojineer.

A `.emjweb` file parses into the typed `WebDocument` / `WebNode` IR.
That IR can be inspected as JSON or lowered to semantic HTML. Interactive
actions name ordinary Train 21 Emojineer exports; behavior still compiles to
EMJBC and executes through the production VM under the browser sandbox.

## Start a document

Every document begins with exactly one declaration:

```text
🌐 📜Page title📜 📜en📜
```

The language tag is optional and defaults to `en`. Blocks close with `🏁`.
Text fields use the same visible `📜...📜` convention as Emojineer source.
IDs use portable ASCII letters, digits, `_`, and `-`, beginning with a letter.
Use `-` where a grammar position permits an omitted ID.

## Markup commands

| Command | Meaning |
| --- | --- |
| `📑 id` | open a semantic section |
| `🧭 id` | open navigation; only links are allowed inside |
| `🔠 level id 📜text📜` | heading level 1 through 6 |
| `🧾 id 📜text📜` | paragraph |
| `🔗 id 📜url📜 📜text📜` | link |
| `🖼️ id 📜url📜 📜alt📜` | image with required alt text |
| `🔘 id 📜label📜 export.name target` | button with explicit click binding |
| `📝 id export.name target 📜label📜` | form with explicit submit binding |
| `🏷️ id name type 📜label📜` | labelled form input; type is `🔤`, `🔢`, or `📧` |
| `📋 id 🔹` / `📋 id 🔢` | unordered / ordered list |
| `🔹 📜text📜` | list item |
| `📊 id` | table block |
| `📌 cells...` | exactly one table header row |
| `📍 cells...` | table data row |
| `🎨 📜selector📜 📜property📜 📜value📜` | document-level safe style rule |
| `🏁` | close the current block |

Tables require one header row and at least one data row, with equal cell counts.
The renderer emits semantic headings, `nav`, labelled inputs, scoped `th`
cells, lists, forms, and other corresponding HTML elements.

## Behavior is ordinary Emojineer

Markup does not contain executable JavaScript. A binding names an explicit
interop export with the fixed browser-event signature `(text) -> text`:

```emoji
📡 🚀 📜web.greet📜 🔤 🫴 🔤 🤲
🛠️ 🚀 🫴 🍎 🤲
📦 📜Hello: 📜 ➕ 🍎
🏁
```

`invoke_web_behavior()` compiles that source with the ordinary lexer, parser,
compiler, verifier, and production VM. Browser behavior always uses
`ExecutionMode::Sandbox`; filesystem, network, process, clock, random, and
host-environment authority therefore fail preflight before behavior executes.

The browser host supplies button IDs as click payloads and JSON text containing
form fields as submit payloads. The returned text replaces only the declared
target element's text content. The generated page receives no script execution.

## Safety boundaries

Links allow relative paths, fragments, HTTPS, and `mailto:`. Images allow
relative paths and HTTPS. IDs, nesting depth, node count, source size, behavior
payload/response sizes, style properties, and style values are bounded.

HTML text and attributes are escaped. URL-bearing CSS values, imports,
expressions, unsafe style properties, duplicate IDs, invalid parent/child
relationships, mismatched action kinds, missing binding targets, malformed
tables, and unclosed blocks are rejected.

## CLI

```bash
emojineer web-check examples/web_page.emjweb
emojineer web-dump examples/web_page.emjweb
emojineer web-bindings examples/web_page.emjweb
emojineer web-build examples/web_page.emjweb -o page.html
```

`web-dump` emits `emojineer.web-ir.v1`. `web-bindings` emits the explicit
event/export/target table. `web-build` emits semantic HTML only; it does not
inject JavaScript behavior.

## Browser playground

The curated **Native web page** example renders through the same C++ web parser
compiled to WebAssembly. Its preview iframe allows same-origin DOM inspection
by the playground host but does not allow scripts. The parent playground
attaches only the declared binding table and routes events back through the
sandboxed Emojineer behavior export bridge.
