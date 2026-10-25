import { strict as assert } from "node:assert";
import { test } from "node:test";
import { create, fromBinary, toBinary } from "@bufbuild/protobuf";
import { CodeGeneratorRequestSchema } from "@bufbuild/protobuf/wkt";
import { clientRequest } from "./client-codegen.mjs";
test("browser descriptors retain wire semantics and remap dependencies while removing server-only options", () => {
  const request = create(CodeGeneratorRequestSchema, {
    fileToGenerate: ["buf/validate/validate.proto", "psim/realtime/v1/realtime.proto"],
    protoFile: [
      { name: "buf/validate/validate.proto" },
      {
        name: "psim/realtime/v1/realtime.proto",
        syntax: "proto3",
        package: "SamplePackage",
        dependency: [
          "buf/validate/validate.proto",
          "google/protobuf/timestamp.proto",
          "google/api/httpbody.proto",
          "google/api/annotations.proto",
        ],
        publicDependency: [1],
        weakDependency: [2],
        messageType: [
          {
            name: "Sample",
            oneofDecl: [{ name: "value" }],
            field: [
              { name: "sequence", number: 1, type: 4, jsonName: "sequence", oneofIndex: 0 },
              { name: "entries", number: 2, type: 11, typeName: ".SamplePackage.Sample.EntriesEntry", label: 3 },
            ],
            nestedType: [
              {
                name: "EntriesEntry",
                options: { mapEntry: true },
                field: [
                  { name: "key", number: 1, type: 9 },
                  { name: "value", number: 2, type: 9 },
                ],
              },
            ],
          },
        ],
      },
    ],
  });
  const options = request.protoFile[1].messageType[0].nestedType[0].options;
  options.$unknown = [{ no: 1000, wireType: 0, data: new Uint8Array([1]) }];
  const stripped = fromBinary(CodeGeneratorRequestSchema, clientRequest(toBinary(CodeGeneratorRequestSchema, request)));
  const file = stripped.protoFile[0];
  assert.deepEqual(stripped.fileToGenerate, ["psim/realtime/v1/realtime.proto"]);
  assert.deepEqual(file.dependency, ["google/protobuf/timestamp.proto", "google/api/httpbody.proto"]);
  assert.deepEqual(file.publicDependency, [0]);
  assert.deepEqual(file.weakDependency, [1]);
  assert.equal(file.messageType[0].nestedType[0].options.mapEntry, true);
  assert.equal(file.messageType[0].nestedType[0].options.$unknown, undefined);
  assert.equal(file.messageType[0].field[0].type, 4);
  assert.equal(file.messageType[0].field[0].oneofIndex, 0);
  assert.equal(file.messageType[0].field[1].typeName, ".SamplePackage.Sample.EntriesEntry");
});
