import { Buffer } from "node:buffer";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import process from "node:process";
import { fromBinary, toBinary } from "@bufbuild/protobuf";
import { CodeGeneratorRequestSchema } from "@bufbuild/protobuf/wkt";
const metadata = new Set(["buf/validate/validate.proto", "google/api/annotations.proto", "google/api/http.proto"]);
/** Server validation and HTTP annotations do not participate in browser wire encoding. */
export function clientRequest(bytes) {
  const request = fromBinary(CodeGeneratorRequestSchema, bytes, { readUnknownFields: false });
  const messages = new Map();
  function collect(list, prefix) {
    for (const message of list) {
      const name = prefix + "." + message.name;
      messages.set(name, message);
      collect(message.nestedType, name);
    }
  }
  for (const file of request.protoFile) collect(file.messageType, "." + file.package);
  const required = new Set();
  function visit(name) {
    if (required.has(name)) return;
    const message = messages.get(name);
    if (!message) return;
    required.add(name);
    for (const field of message.field) if (field.typeName) visit(field.typeName);
  }
  const root = request.protoFile.find((file) => file.name === "psim/realtime/v1/realtime.proto");
  if (!root) throw new Error("Missing realtime protocol root");
  for (const message of root.messageType) visit("." + root.package + "." + message.name);
  function prune(list, prefix) {
    return list.flatMap((message) => {
      const name = prefix + "." + message.name;
      message.nestedType = prune(message.nestedType, name);
      return required.has(name) || message.nestedType.length ? [message] : [];
    });
  }
  const strip = (file) => {
    // The browser exchanges realtime frames; HTTP service descriptors remain server-owned.
    file.messageType = prune(file.messageType, "." + file.package);
    file.service = [];
    file.sourceCodeInfo = undefined;
    const retained = file.dependency.flatMap((name, index) => (metadata.has(name) ? [] : [index]));
    file.publicDependency = file.publicDependency.flatMap((index) =>
      retained.includes(index) ? [retained.indexOf(index)] : [],
    );
    file.weakDependency = file.weakDependency.flatMap((index) =>
      retained.includes(index) ? [retained.indexOf(index)] : [],
    );
    file.dependency = file.dependency.filter((name) => !metadata.has(name));
    return file;
  };
  request.protoFile = request.protoFile.filter((file) => !metadata.has(file.name)).map(strip);
  request.sourceFileDescriptors = request.sourceFileDescriptors.filter((file) => !metadata.has(file.name)).map(strip);
  request.fileToGenerate = request.fileToGenerate.filter((name) => !metadata.has(name));
  return toBinary(CodeGeneratorRequestSchema, request, { writeUnknownFields: false });
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const chunks = [];
  for await (const chunk of process.stdin) chunks.push(chunk);
  const result = spawnSync("protoc-gen-es", [], {
    input: clientRequest(Buffer.concat(chunks)),
    maxBuffer: 32 * 1024 * 1024,
  });
  if (result.stderr?.length) process.stderr.write(result.stderr);
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
  process.stdout.write(result.stdout);
}
