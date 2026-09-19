const path = require("node:path");

const modulePath = path.resolve(process.argv[2]);
const moduleDirectory = path.dirname(modulePath);
const factory = require(modulePath);

(async () => {
  const module = await factory({
    locateFile: (file) => path.join(moduleDirectory, file),
  });
  const run = (source) => JSON.parse(
    module.ccall("emojineer_browser_run", "string", ["string"], [source])
  );

  const hello = run("📝 📜Hello from WASM 🚀📜\n");
  if (!hello.ok || hello.stdout !== "Hello from WASM 🚀\n") {
    throw new Error(`unexpected WASM hello result: ${JSON.stringify(hello)}`);
  }

  const unicode14 = run(
    "🐍 🫱🏻‍🫲🏿 🟰 📜handshake-ok📜\n📝 🫱🏻‍🫲🏿\n" +
    "🐍 🫶🏻 🟰 📜heart-hands-ok📜\n📝 🫶🏻\n"
  );
  if (!unicode14.ok || unicode14.stdout !== "handshake-ok\nheart-hands-ok\n") {
    throw new Error(`browser Unicode 14 parity failed: ${JSON.stringify(unicode14)}`);
  }

  const unicode15 = run("🐍 🩷 🟰 📜pink-heart-ok📜\n📝 🩷\n");
  if (!unicode15.ok || unicode15.stdout !== "pink-heart-ok\n") {
    throw new Error(`browser Unicode 15 parity failed: ${JSON.stringify(unicode15)}`);
  }

  const renderWeb = (source) => JSON.parse(
    module.ccall("emojineer_browser_render_web", "string", ["string"], [source])
  );
  const invokeWeb = (behavior, exportName, payload) => JSON.parse(
    module.ccall(
      "emojineer_browser_invoke_web",
      "string",
      ["string", "string", "string"],
      [behavior, exportName, payload]
    )
  );

  const web = renderWeb(
    "🌐 📜WASM web📜 📜en📜\n" +
    "📑 main\n" +
    "🔠 1 hero 📜Native web📜\n" +
    "🧾 status 📜Ready📜\n" +
    "🔘 greet 📜Greet📜 web.greet status\n" +
    "🏁\n"
  );
  if (!web.ok || !web.html.includes("<h1 id=\"hero\">Native web</h1>") ||
      web.html.includes("<script") || web.bindings.length !== 1 ||
      web.bindings[0].export !== "web.greet") {
    throw new Error(`browser web render parity failed: ${JSON.stringify(web)}`);
  }

  const behavior =
    "📡 🚀 📜web.greet📜 🔤 🫴 🔤 🤲\n" +
    "🛠️ 🚀 🫴 🍎 🤲\n" +
    "📦 📜Hello: 📜 ➕ 🍎\n" +
    "🏁\n";
  const behaviorResult = invokeWeb(behavior, "web.greet", "button");
  if (!behaviorResult.ok || behaviorResult.response !== "Hello: button" ||
      behaviorResult.requiredCapabilities !== "none") {
    throw new Error(`browser web behavior parity failed: ${JSON.stringify(behaviorResult)}`);
  }

  const hostileWeb = renderWeb(
    "🌐 📜x📜\n🔗 bad 📜//evil.example/path📜 📜bad📜\n"
  );
  if (hostileWeb.ok || !hostileWeb.diagnostic.includes("link URL")) {
    throw new Error(`browser hostile web markup boundary failed: ${JSON.stringify(hostileWeb)}`);
  }

  const denied = run("📝 🌐 🫴 📜https://example.com📜 🤲\n");
  if (denied.ok || denied.requiredCapabilities !== "network" || denied.stdout !== "") {
    throw new Error(`browser sandbox boundary failed: ${JSON.stringify(denied)}`);
  }

  console.log("browser WASM smoke passed");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
