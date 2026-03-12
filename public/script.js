import { createEngineApi } from "./engineApi.js";

const DEFAULT_WASM_URL = new URL("./wasm/engine-core.wasm", import.meta.url);

function resolveWasmUrl(wasmUrl = DEFAULT_WASM_URL) {
  if (wasmUrl instanceof URL) {
    return wasmUrl;
  }

  return new URL(String(wasmUrl), import.meta.url);
}

function dispatchRuntimeEvent(type, detail) {
  if (typeof document === "undefined") {
    return;
  }

  document.dispatchEvent(new CustomEvent(type, { detail }));
}

export async function initEngineCore(options = {}) {
  const wasmUrl = resolveWasmUrl(options.wasmUrl);

  if (wasmUrl.protocol === "file:") {
    throw new Error("WASM harus dijalankan lewat HTTP server, bukan file://");
  }

  const engineApi = await createEngineApi(wasmUrl.href);

  if (options.resetState !== false) {
    engineApi.resetState();
  }

  const runtime = {
    engineApi,
    wasmUrl: wasmUrl.href,
  };

  if (typeof window !== "undefined") {
    window.engineCore = runtime;
  }

  dispatchRuntimeEvent("engine-core:ready", runtime);
  return runtime;
}

export const engineCoreReady = initEngineCore().catch((error) => {
  if (typeof window !== "undefined") {
    window.engineCore = {
      engineApi: null,
      wasmUrl: DEFAULT_WASM_URL.href,
      error,
    };
  }

  dispatchRuntimeEvent("engine-core:error", { error, wasmUrl: DEFAULT_WASM_URL.href });
  throw error;
});
