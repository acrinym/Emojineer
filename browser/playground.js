const examples = {
  "Hello": "📝 📜Hello from Emojineer 🚀📜\n",
  "Records + results": [
    "🐍 🧑 🟰 🗃️ 🫴 📜Person📜 📚 🫴 📜name📜 📜Ada📜 📜age📜 37 🤲 🤲",
    "📝 🔎 🫴 🧑 📜name📜 🤲",
    "🐍 🟩 🟰 🟢 🫴 📜saved📜 🤲",
    "📝 👌 🫴 🟩 🤲",
  ].join("\n") + "\n",
  "Bytes + encoding": [
    "🐍 🧪 🟰 🧬 🫴 📜Hi🙂📜 🤲",
    "📝 🔡 🫴 🧪 🤲",
    "📝 📨 🫴 🧪 🤲",
  ].join("\n") + "\n",
  "Capability boundary": "📝 🌐 🫴 📜https://example.com📜 🤲\n",
};

const source = document.querySelector("#source");
const example = document.querySelector("#example");
const run = document.querySelector("#run");
const share = document.querySelector("#share");
const stdout = document.querySelector("#stdout");
const diagnostic = document.querySelector("#diagnostic");
const capabilities = document.querySelector("#capabilities");
const runtimeStatus = document.querySelector("#runtime-status");
let runtime = null;

for (const name of Object.keys(examples)) {
  const option = document.createElement("option");
  option.value = name;
  option.textContent = name;
  example.append(option);
}

const shared = new URLSearchParams(location.hash.slice(1)).get("source");
source.value = shared ?? examples.Hello;

example.addEventListener("change", () => {
  source.value = examples[example.value];
});

run.addEventListener("click", () => {
  if (!runtime) return;
  stdout.textContent = "";
  diagnostic.textContent = "";
  capabilities.textContent = "";
  try {
    const raw = runtime.ccall("emojineer_browser_run", "string", ["string"], [source.value]);
    const result = JSON.parse(raw);
    stdout.textContent = result.stdout;
    diagnostic.textContent = result.diagnostic;
    capabilities.textContent = `required capabilities: ${result.requiredCapabilities}`;
  } catch (error) {
    diagnostic.textContent = `playground host error: ${error}`;
  }
});

share.addEventListener("click", async () => {
  const hash = new URLSearchParams();
  hash.set("source", source.value);
  history.replaceState(null, "", `#${hash.toString()}`);
  try {
    await navigator.clipboard.writeText(location.href);
    share.textContent = "Copied ✓";
    setTimeout(() => { share.textContent = "Copy share link"; }, 1400);
  } catch {
    share.textContent = "Share URL ready";
  }
});

EmojineerModule().then((module) => {
  runtime = module;
  run.disabled = false;
  runtimeStatus.textContent = `Runtime ${runtime.ccall("emojineer_browser_version", "string", [], [])} ready`;
}).catch((error) => {
  runtimeStatus.textContent = "Runtime failed to load";
  diagnostic.textContent = String(error);
});
