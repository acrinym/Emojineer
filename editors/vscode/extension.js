const vscode = require("vscode");
const path = require("path");
const { LanguageClient } = require("vscode-languageclient/node");

let client;

const semanticTokens = [
  ["🐍", "variable", "declare variable"],
  ["✏️", "assign", "assign variable"],
  ["📝", "print", "print value"],
  ["🤔", "if", "begin if block"],
  ["🙅", "else", "begin else block"],
  ["🔁", "while", "begin while loop"],
  ["🏁", "end", "end block"],
  ["📥", "input", "read one line of input"],
  ["✅", "true", "boolean true"],
  ["❌", "false", "boolean false"],
  ["🧩", "module", "declare module"],
  ["🔗", "import", "import module"],
  ["📤", "export", "export symbol"],
  ["🔌", "interop import", "declare typed host/WASM import"],
  ["📡", "interop export", "export typed function"]
];

semanticTokens.push(
  ["🔢", "number type", "number type"],
  ["🔤", "text type", "text type"],
  ["🎯", "boolean type", "boolean type"],
  ["🛠️", "function", "define function"],
  ["📦", "return", "return from function"],
  ["➕", "add", "addition or text concatenation"],
  ["➖", "subtract", "subtraction or numeric negation"],
  ["✖️", "multiply", "multiplication"],
  ["➗", "divide", "division"],
  ["🪄", "modulo", "remainder"],
  ["🟰", "equal", "assignment separator or equality"],
  ["🔽", "less", "less-than comparison"],
  ["🔼", "greater", "greater-than comparison"],
  ["🚫", "not", "boolean negation"],
  ["🫴", "group start", "begin expression or argument list"],
  ["🤲", "group end", "end expression or argument list"]
);

semanticTokens.push(
  ["📚", "array", "array type or literal"],
  ["🔎", "index", "read collection element"],
  ["📏", "length", "collection or text length"],
  ["📎", "append", "return array with appended value"],
  ["🧷", "set index", "return array with replaced element"]
);

function executable(name) {
  const config = vscode.workspace.getConfiguration("emojineer");
  if (name === "emojineer-lsp") {
    const explicit = config.get("lspPath", "");
    if (explicit) return explicit;
  }
  const root = config.get("toolchainPath", "");
  const suffix = process.platform === "win32" ? ".exe" : "";
  return root ? path.join(root, name + suffix) : name + suffix;
}

function currentFile() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "emojineer") {
    throw new Error("Open an Emojineer .emoji file first.");
  }
  return editor.document.uri.fsPath;
}

async function runTool(command, title) {
  const file = currentFile();
  const task = new vscode.Task(
    { type: "emojineer", command, file },
    vscode.TaskScope.Workspace,
    title,
    "Emojineer",
    new vscode.ProcessExecution(executable("emojineer"), [command, file])
  );
  task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, clear: true };
  await vscode.tasks.executeTask(task);
}

function shellQuote(value) {
  if (process.platform === "win32") return '"' + value.replace(/"/g, '""') + '"';
  return "'" + value.replace(/'/g, "'\\''") + "'";
}

function debugCurrentFile() {
  const file = currentFile();
  const terminal = vscode.window.createTerminal("Emojineer Debug");
  terminal.show();
  terminal.sendText(shellQuote(executable("emojineer")) + " debug " + shellQuote(file));
}

async function showSemanticPalette() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) throw new Error("Open an editor before inserting an Emojineer token.");

  const items = semanticTokens.map(([glyph, name, description]) => ({
    label: glyph + "  " + name,
    description,
    detail: "Insert canonical Emojineer token " + glyph,
    glyph
  }));
  const selected = await vscode.window.showQuickPick(items, {
    title: "Emojineer Semantic Palette",
    placeHolder: "Search by meaning, then insert native emoji source",
    matchOnDescription: true,
    matchOnDetail: true
  });
  if (!selected) return;
  await editor.edit(edit => {
    for (const selection of editor.selections) {
      edit.replace(selection, selected.glyph);
    }
  });
}

async function activate(context) {
  const serverOptions = {
    command: executable("emojineer-lsp"),
    args: []
  };
  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "emojineer" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.{emoji,toml}")
    }
  };

  client = new LanguageClient(
    "emojineerLanguageServer",
    "Emojineer Language Server",
    serverOptions,
    clientOptions
  );
  context.subscriptions.push(client);
  await client.start();

  context.subscriptions.push(
    vscode.commands.registerCommand("emojineer.run", () => runTool("run", "Run Emojineer")),
    vscode.commands.registerCommand("emojineer.check", () => runTool("check", "Check Emojineer")),
    vscode.commands.registerCommand("emojineer.format", () =>
      vscode.commands.executeCommand("editor.action.formatDocument")),
    vscode.commands.registerCommand("emojineer.debug", debugCurrentFile),
    vscode.commands.registerCommand("emojineer.semanticPalette", showSemanticPalette)
  );
}

async function deactivate() {
  if (client) await client.stop();
}

module.exports = { activate, deactivate, semanticTokens };
