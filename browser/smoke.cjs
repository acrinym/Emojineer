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

  const denied = run("📝 🌐 🫴 📜https://example.com📜 🤲\n");
  if (denied.ok || denied.requiredCapabilities !== "network" || denied.stdout !== "") {
    throw new Error(`browser sandbox boundary failed: ${JSON.stringify(denied)}`);
  }

  console.log("browser WASM smoke passed");
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
