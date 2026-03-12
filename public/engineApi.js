import { loadWasm } from "./helper/loadWasm.js";
import { copyFloat32, readCString, resolveExport, withHeapAllocation } from "./helper/wasmMemory.js";

const FLOAT_BYTES = Float32Array.BYTES_PER_ELEMENT;
const LEGACY_BLOCK_STRIDE_FLOATS = 6;
const BLOCK_STRIDE_FLOATS = 7;
const TERRAIN_VERTEX_STRIDE_FLOATS = 9;
const FOLIAGE_STRIDE_FLOATS = 10;

function requireExport(wasmExports, exportName) {
  const resolved = resolveExport(wasmExports, exportName);
  if (!resolved) {
    throw new Error(`Missing WASM export: ${exportName}`);
  }

  return resolved;
}

export async function createEngineApi(wasmUrl) {
  const wasmExports = await loadWasm(wasmUrl);
  const memory = requireExport(wasmExports, "memory");
  const malloc = requireExport(wasmExports, "malloc");
  const free = requireExport(wasmExports, "free");
  const resetState = requireExport(wasmExports, "engine_web_reset_state");
  const setTerrainProfile = requireExport(wasmExports, "engine_web_set_terrain_profile");
  const setFoliageProfile = requireExport(wasmExports, "engine_web_set_foliage_profile");
  const fillHeightBuffer = requireExport(wasmExports, "engine_web_fill_height_buffer");
  const getHeightAt = requireExport(wasmExports, "engine_web_get_height_at");
  const getTerrainVertexCount = requireExport(wasmExports, "engine_web_get_terrain_vertex_count");
  const getTerrainIndexCount = requireExport(wasmExports, "engine_web_get_terrain_index_count");
  const copyTerrainVertices = requireExport(wasmExports, "engine_web_copy_terrain_vertices");
  const getBlockCount = requireExport(wasmExports, "engine_web_get_block_count");
  const copyBlocksLegacy = requireExport(wasmExports, "engine_web_copy_blocks");
  const copyBlockInstances = requireExport(wasmExports, "engine_web_copy_block_instances");
  const getFoliageCapacity = requireExport(wasmExports, "engine_web_get_foliage_capacity");
  const copyFoliageInstances = requireExport(wasmExports, "engine_web_copy_foliage_instances");
  const placeBlockColumn = requireExport(wasmExports, "engine_web_place_block_column");
  const removeBlockColumn = requireExport(wasmExports, "engine_web_remove_block_column");
  const describeState = requireExport(wasmExports, "engine_web_describe_state");

  return {
    strides: {
      terrainVertexFloats: TERRAIN_VERTEX_STRIDE_FLOATS,
      blockFloats: BLOCK_STRIDE_FLOATS,
      foliageFloats: FOLIAGE_STRIDE_FLOATS,
    },

    resetState() {
      resetState();
    },

    setTerrainProfile(profile) {
      setTerrainProfile(
        Number(profile.baseHeight),
        Number(profile.heightScale),
        Number(profile.roughness),
        Number(profile.ridgeStrength),
      );
    },

    setFoliageProfile(profile) {
      setFoliageProfile(
        Number(profile.palmSize),
        Number(profile.palmCount),
        Number(profile.palmFruitDensity),
        Number(profile.palmRenderRadius),
      );
    },

    sampleTerrain(view) {
      const sampleCount = view.sampleWidth * view.sampleHeight;
      const byteLength = sampleCount * FLOAT_BYTES;

      return withHeapAllocation(malloc, free, byteLength, (pointer) => {
        const written = fillHeightBuffer(
          view.sampleWidth,
          view.sampleHeight,
          view.centerX,
          view.centerZ,
          view.step,
          pointer,
        );

        if (written !== sampleCount) {
          throw new Error(`Unexpected terrain sample count: ${written}`);
        }

        return copyFloat32(memory, pointer, sampleCount);
      });
    },

    getHeightAt(x, z) {
      return getHeightAt(Number(x), Number(z));
    },

    getTerrainMesh(view) {
      const resolution = Math.max(24, Math.round(view.meshResolution || 160));
      const vertexCount = getTerrainVertexCount(resolution);
      const indexCount = getTerrainIndexCount(resolution);
      const byteLength = vertexCount * TERRAIN_VERTEX_STRIDE_FLOATS * FLOAT_BYTES;

      return withHeapAllocation(malloc, free, byteLength, (pointer) => {
        const copied = copyTerrainVertices(
          Number(view.centerX),
          Number(view.centerZ),
          Number(view.halfExtent),
          resolution,
          pointer,
          vertexCount,
        );

        if (copied !== vertexCount) {
          throw new Error(`Unexpected terrain vertex count: ${copied}`);
        }

        return {
          resolution,
          vertexCount,
          indexCount,
          vertices: copyFloat32(memory, pointer, vertexCount * TERRAIN_VERTEX_STRIDE_FLOATS),
        };
      });
    },

    getBlocks() {
      const blockCount = Math.max(getBlockCount(), 1);
      const byteLength = blockCount * BLOCK_STRIDE_FLOATS * FLOAT_BYTES;

      return withHeapAllocation(malloc, free, byteLength, (pointer) => {
        const copied = copyBlockInstances(pointer, blockCount);
        return {
          count: copied,
          raw: copyFloat32(memory, pointer, copied * BLOCK_STRIDE_FLOATS),
        };
      });
    },

    getBlocksLegacy() {
      const blockCount = Math.max(getBlockCount(), 1);
      const byteLength = blockCount * LEGACY_BLOCK_STRIDE_FLOATS * FLOAT_BYTES;

      return withHeapAllocation(malloc, free, byteLength, (pointer) => {
        const copied = copyBlocksLegacy(pointer, blockCount);
        return copyFloat32(memory, pointer, copied * LEGACY_BLOCK_STRIDE_FLOATS);
      });
    },

    getFoliage(view) {
      const radius = Math.max(36, Number(view.radius || 260));
      const instanceCapacity = Math.max(getFoliageCapacity(radius), 1);
      const byteLength = instanceCapacity * FOLIAGE_STRIDE_FLOATS * FLOAT_BYTES;

      return withHeapAllocation(malloc, free, byteLength, (pointer) => {
        const copied = copyFoliageInstances(
          Number(view.centerX),
          Number(view.centerZ),
          radius,
          pointer,
          instanceCapacity,
        );

        return {
          count: copied,
          raw: copyFloat32(memory, pointer, copied * FOLIAGE_STRIDE_FLOATS),
        };
      });
    },

    placeBlock(x, z, blockType) {
      return placeBlockColumn(Math.round(x), Math.round(z), blockType);
    },

    removeBlock(x, z) {
      return removeBlockColumn(Math.round(x), Math.round(z));
    },

    getStateSummary() {
      const pointer = describeState();
      if (!pointer) {
        return "";
      }

      try {
        return readCString(memory, pointer);
      } finally {
        free(pointer);
      }
    },
  };
}
