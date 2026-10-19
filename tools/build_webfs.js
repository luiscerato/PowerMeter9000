#!/usr/bin/env node
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");
const WebFsImage = require("../data/webfs_image.js");

// Recursively collects files to package; directory and relativePath describe the current source folder.
function collectFiles(directory, relativePath, output) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (entry.name === "desktop.ini" || entry.name.endsWith(".fs32")) continue;
    const sourcePath = path.join(directory, entry.name);
    const targetPath = path.join(relativePath, entry.name);
    if (entry.isDirectory()) collectFiles(sourcePath, targetPath, output);
    else if (entry.isFile()) {
      const stat = fs.statSync(sourcePath);
      output.push({ path: targetPath, data: fs.readFileSync(sourcePath), modifiedTime: Math.floor(stat.mtimeMs / 1000) });
    }
  }
}

// Compresses eligible files when requested; files contains the descriptors used by WebFS.
function addCompression(files, enabled) {
  if (!enabled) return files;
  return files.map(file => {
    const compressed = zlib.gzipSync(file.data, { level: 9, mtime: 0 });
    return compressed.length < file.data.length ? { ...file, storedData: compressed } : file;
  });
}

// Parses command line arguments; no parameters are required for the default data folder.
function parseArguments() {
  const values = process.argv.slice(2);
  const gzip = values.includes("--gzip");
  const paths = values.filter(value => value !== "--gzip");
  return {
    gzip,
    source: path.resolve(paths[0] || path.join(__dirname, "..", "data")),
    output: path.resolve(paths[1] || path.join(__dirname, "..", ".pio", "build", "webfs.fs32"))
  };
}

const options = parseArguments();
const files = [];
collectFiles(options.source, "", files);
const image = WebFsImage.buildImage(addCompression(files, options.gzip));
fs.mkdirSync(path.dirname(options.output), { recursive: true });
fs.writeFileSync(options.output, image.image);
const saved = image.files.reduce((total, file) => total + file.originalSize - file.storedSize, 0);
console.log(`WebFS: ${image.files.length} files, ${image.storedBytes} bytes image, ${saved} bytes file compression, gzip=${options.gzip}`);
console.log(options.output);
