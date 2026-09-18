const fs = require("fs");
const path = require("path");
const root = path.resolve(__dirname, "..");
const pkg = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));
const extension = fs.readFileSync(path.join(root, "extension.js"), "utf8");

function requireTrue(condition, message) {
  if (!condition) throw new Error(message);
}

const commands = new Set(pkg.contributes.commands.map(item => item.command));
for (const command of [
  "emojineer.run",
  "emojineer.check",
  "emojineer.format",
  "emojineer.debug",
  "emojineer.semanticPalette"
]) {
  requireTrue(commands.has(command), "missing command " + command);
}

requireTrue(pkg.contributes.languages[0].extensions.includes(".emoji"),
  "extension must own .emoji files");
requireTrue(extension.includes('command: executable("emojineer-lsp")'),
  "extension must launch the production LSP");
requireTrue(extension.includes("Semantic Palette"),
  "semantic palette is missing");
for (const glyph of ["🐍","📝","🤔","🔁","🛠️","📦","📚","🔌","📡","🗃️","🧬","🧳","✍️","📁","🛰️"]) {
  requireTrue(extension.includes(glyph), "semantic palette missing " + glyph);
}
JSON.parse(fs.readFileSync(path.join(root, "language-configuration.json"), "utf8"));
JSON.parse(fs.readFileSync(path.join(root, "syntaxes", "emojineer.tmLanguage.json"), "utf8"));
console.log("Emojineer VS Code package validation passed");
