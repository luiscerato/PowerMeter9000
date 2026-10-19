(function (root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (root) root.WebFsImage = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const pageSize = 2048;
  const headerSize = 64;
  const entrySize = 128;
  const pathSize = 96;
  const magic = 0x32335346;
  const version = 1;

  const mimeTypes = {
    binary: 0, html: 1, css: 2, javascript: 3, json: 4,
    svg: 5, png: 6, jpeg: 7, ico: 8, text: 9
  };

  // Normalizes a file path for the flat WebFS table.
  function normalizePath(value) {
    const path = String(value || "").replace(/\\/g, "/").replace(/^\/+/, "");
    if (!path || path.split("/").some(part => part === ".." || !part)) throw new Error("Invalid WebFS path");
    return "/" + path;
  }

  // Returns the WebFS MIME identifier inferred from a complete path.
  function getMimeType(path) {
    const extension = path.slice(path.lastIndexOf(".") + 1).toLowerCase();
    if (extension === "html" || extension === "htm") return mimeTypes.html;
    if (extension === "css") return mimeTypes.css;
    if (extension === "js" || extension === "mjs") return mimeTypes.javascript;
    if (extension === "json") return mimeTypes.json;
    if (extension === "svg") return mimeTypes.svg;
    if (extension === "png") return mimeTypes.png;
    if (extension === "jpg" || extension === "jpeg") return mimeTypes.jpeg;
    if (extension === "ico") return mimeTypes.ico;
    if (extension === "txt" || extension === "csv") return mimeTypes.text;
    return mimeTypes.binary;
  }

  // Calculates the IEEE CRC32 of bytes.
  function crc32(bytes) {
    let crc = 0xffffffff;
    for (let index = 0; index < bytes.length; index++) {
      crc ^= bytes[index];
      for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return (crc ^ 0xffffffff) >>> 0;
  }

  // Rounds a value to the next page boundary.
  function alignPage(value) {
    return Math.ceil(value / pageSize) * pageSize;
  }

  // Builds a complete WebFS V1 image from normalized file descriptors.
  function buildImage(inputFiles, options) {
    const settings = options || {};
    const files = inputFiles.map(file => {
      const path = normalizePath(file.path);
      const original = new Uint8Array(file.data);
      const stored = file.storedData ? new Uint8Array(file.storedData) : original;
      const useGzip = !!file.storedData && stored.length < original.length;
      const pathBytes = new TextEncoder().encode(path);
      if (pathBytes.length >= pathSize) throw new Error("WebFS path is too long: " + path);
      return {
        path, pathBytes, original, stored,
        modifiedTime: Number(file.modifiedTime || 0) >>> 0,
        mimeType: file.mimeType === undefined ? getMimeType(path) : file.mimeType,
        encoding: useGzip ? 1 : 0
      };
    }).sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0);

    for (let index = 1; index < files.length; index++) {
      if (files[index - 1].path === files[index].path) throw new Error("Duplicate WebFS path: " + files[index].path);
    }
    if (files.length > 0xffff) throw new Error("Too many WebFS files");

    const tableOffset = headerSize;
    const dataOffset = alignPage(tableOffset + files.length * entrySize);
    let imageSize = dataOffset;
    for (const file of files) {
      file.dataOffset = imageSize;
      imageSize = alignPage(imageSize + file.stored.length);
    }
    const image = new Uint8Array(imageSize);
    const view = new DataView(image.buffer);
    for (let index = 0; index < files.length; index++) {
      const file = files[index];
      const entryOffset = tableOffset + index * entrySize;
      image.set(file.pathBytes, entryOffset);
      view.setUint32(entryOffset + 96, file.dataOffset, true);
      view.setUint32(entryOffset + 100, file.stored.length, true);
      view.setUint32(entryOffset + 104, file.original.length, true);
      view.setUint32(entryOffset + 108, file.modifiedTime, true);
      view.setUint32(entryOffset + 112, crc32(file.stored), true);
      view.setUint16(entryOffset + 116, file.mimeType, true);
      view.setUint8(entryOffset + 118, file.encoding);
      image.set(file.stored, file.dataOffset);
    }

    view.setUint32(0, magic, true);
    view.setUint16(4, version, true);
    view.setUint16(6, entrySize, true);
    view.setUint32(8, imageSize, true);
    view.setUint16(12, files.length, true);
    view.setUint32(16, tableOffset, true);
    view.setUint32(20, dataOffset, true);
    view.setUint32(24, Number(settings.createdAt || Math.floor(Date.now() / 1000)) >>> 0, true);
    view.setUint32(28, crc32(image.subarray(tableOffset, dataOffset)), true);
    view.setUint32(32, crc32(image.subarray(dataOffset)), true);
    view.setUint32(36, 0, true);
    view.setUint32(36, crc32(image.subarray(0, headerSize)), true);

    return {
      image,
      files: files.map(file => ({ path: file.path, originalSize: file.original.length, storedSize: file.stored.length, encoding: file.encoding })),
      originalBytes: files.reduce((total, file) => total + file.original.length, 0),
      storedBytes: imageSize,
      dataBytes: files.reduce((total, file) => total + file.stored.length, 0)
    };
  }

  return { pageSize, headerSize, entrySize, mimeTypes, normalizePath, getMimeType, crc32, buildImage };
});
