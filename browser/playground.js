const examples = {
  "Hello": {
    kind: "program",
    source: "📝 📜Hello from Emojineer 🚀📜\n",
  },
  "Records + results": {
    kind: "program",
    source: [
      "🐍 🧑 🟰 🗃️ 🫴 📜Person📜 📚 🫴 📜name📜 📜Ada📜 📜age📜 37 🤲 🤲",
      "📝 🔎 🫴 🧑 📜name📜 🤲",
      "🐍 🟩 🟰 🟢 🫴 📜saved📜 🤲",
      "📝 👌 🫴 🟩 🤲",
    ].join("\n") + "\n",
  },
  "Bytes + encoding": {
    kind: "program",
    source: [
      "🐍 🧪 🟰 🧬 🫴 📜Hi🙂📜 🤲",
      "📝 🔡 🫴 🧪 🤲",
      "📝 📨 🫴 🧪 🤲",
    ].join("\n") + "\n",
  },
  "Capability boundary": {
    kind: "program",
    source: "📝 🌐 🫴 📜https://example.com📜 🤲\n",
  },
  "Native web page": {
    kind: "web",
    source: [
      "🌐 📜Emojineer Web📜 📜en📜",
      "🎨 📜body📜 📜font-family📜 📜system-ui📜",
      "🎨 📜body📜 📜max-width📜 📜48rem📜",
      "📑 main",
      "🔠 1 hero 📜Emojineer-native web markup📜",
      "🧾 status 📜Ready for an explicit Emojineer action.📜",
      "🔘 greet 📜Greet📜 web.greet status",
      "📝 contact web.submit status 📜Submit📜",
      "🏷️ email email 📧 📜Email address📜",
      "🏁",
      "📋 features 🔹",
      "🔹 📜Semantic HTML📜",
      "🔹 📜No page-side JavaScript language📜",
      "🏁",
      "🏁",
    ].join("\n") + "\n",
    behavior: [
      "📡 🚀 📜web.greet📜 🔤 🫴 🔤 🤲",
      "📡 🌟 📜web.submit📜 🔤 🫴 🔤 🤲",
      "🛠️ 🚀 🫴 🍎 🤲",
      "📦 📜Clicked: 📜 ➕ 🍎",
      "🏁",
      "🛠️ 🌟 🫴 🍎 🤲",
      "📦 📜Submitted: 📜 ➕ 🍎",
      "🏁",
    ].join("\n") + "\n",
  },
};
const source = document.querySelector("#source");
const behavior = document.querySelector("#behavior");
const behaviorPanel = document.querySelector("#behavior-panel");
const example = document.querySelector("#example");
const run = document.querySelector("#run");
const share = document.querySelector("#share");
const stdout = document.querySelector("#stdout");
const diagnostic = document.querySelector("#diagnostic");
const capabilities = document.querySelector("#capabilities");
const runtimeStatus = document.querySelector("#runtime-status");
const previewHeading = document.querySelector("#preview-heading");
const webPreview = document.querySelector("#web-preview");
let runtime = null;
let mode = "program";

for (const name of Object.keys(examples)) {
  const option = document.createElement("option");
  option.value = name;
  option.textContent = name;
  example.append(option);
}

function setMode(nextMode) {
  mode = nextMode === "web" ? "web" : "program";
  behaviorPanel.hidden = mode !== "web";
  previewHeading.hidden = true;
  webPreview.hidden = true;
  run.textContent = mode === "web" ? "Render ▶" : "Run ▶";
}
function applyExample(name) {
  const selected = examples[name];
  source.value = selected.source;
  behavior.value = selected.behavior ?? "";
  setMode(selected.kind);
}

const shared = new URLSearchParams(location.hash.slice(1));
if (shared.has("source")) {
  source.value = shared.get("source");
  behavior.value = shared.get("behavior") ?? "";
  setMode(shared.get("mode"));
} else {
  applyExample("Hello");
}

example.addEventListener("change", () => {
  applyExample(example.value);
});

function clearOutput() {
  stdout.textContent = "";
  diagnostic.textContent = "";
  capabilities.textContent = "";
  previewHeading.hidden = true;
  webPreview.hidden = true;
}

function invokeBehavior(exportName, payload, target) {
  const raw = runtime.ccall(
    "emojineer_browser_invoke_web",
    "string",
    ["string", "string", "string"],
    [behavior.value, exportName, payload]
  );
  const result = JSON.parse(raw);
  diagnostic.textContent = result.diagnostic;
  capabilities.textContent = `required capabilities: ${result.requiredCapabilities}`;
  if (result.ok) target.textContent = result.response;
}

function bindWebActions(bindings) {
  const doc = webPreview.contentDocument;
  if (!doc) return;
  for (const binding of bindings) {
    const element = doc.getElementById(binding.elementId);
    const target = doc.getElementById(binding.targetId);
    if (!element || !target) {
      diagnostic.textContent = `rendered binding target missing: ${binding.elementId}`;
      continue;
    }
    element.addEventListener(binding.event, (event) => {
      event.preventDefault();
      let payload = element.id;
      if (binding.event === "submit") {
        payload = JSON.stringify(Object.fromEntries(new FormData(element).entries()));
      }
      invokeBehavior(binding.export, payload, target);
    });
  }
}

function runProgram() {
  const raw = runtime.ccall("emojineer_browser_run", "string", ["string"], [source.value]);
  const result = JSON.parse(raw);
  stdout.textContent = result.stdout;
  diagnostic.textContent = result.diagnostic;
  capabilities.textContent = `required capabilities: ${result.requiredCapabilities}`;
}

function renderWeb() {
  const raw = runtime.ccall(
    "emojineer_browser_render_web", "string", ["string"], [source.value]
  );
  const result = JSON.parse(raw);
  if (!result.ok) {
    diagnostic.textContent = result.diagnostic;
    return;
  }
  stdout.textContent = JSON.stringify(result.ir, null, 2);
  capabilities.textContent = "page behavior: explicit sandboxed Emojineer exports";
  previewHeading.hidden = false;
  webPreview.hidden = false;
  webPreview.onload = () => bindWebActions(result.bindings);
  webPreview.srcdoc = result.html;
}

run.addEventListener("click", () => {
  if (!runtime) return;
  clearOutput();
  try {
    if (mode === "web") renderWeb();
    else runProgram();
  } catch (error) {
    diagnostic.textContent = `playground host error: ${error}`;
  }
});
share.addEventListener("click", async () => {
  const hash = new URLSearchParams();
  hash.set("source", source.value);
  if (mode === "web") {
    hash.set("mode", "web");
    hash.set("behavior", behavior.value);
  }
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
  runtimeStatus.textContent =
    `Runtime ${runtime.ccall("emojineer_browser_version", "string", [], [])} ready`;
}).catch((error) => {
  runtimeStatus.textContent = "Runtime failed to load";
  diagnostic.textContent = String(error);
});
