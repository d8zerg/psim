"""Validate captured public API responses against the committed OpenAPI schemas."""
import json
from pathlib import Path
import re
import sys
import warnings
import yaml
from openapi_schema_validator import OAS30Validator
with warnings.catch_warnings():
    warnings.simplefilter("ignore", DeprecationWarning)
    from jsonschema import RefResolver


def main():
    root = Path(__file__).resolve().parents[2]
    document = yaml.safe_load((root / "contracts/openapi/psim-api-v1.yaml").read_text())
    resolver = RefResolver.from_schema(document)
    error_codes = {item["code"]: item["http"] for item in yaml.safe_load((root / "contracts/errors/errors.yaml").read_text())["codes"]}
    operations = []
    for path, methods in document["paths"].items():
        pattern = "^" + re.sub(r"\\\{[^}]+\\\}", "[^/]+", re.escape(path)) + "$"
        for method, operation in methods.items():
            operations.append((path.count("{"), re.compile(pattern), method.upper(), operation))
    operations.sort(key=lambda item: item[0])
    captures = json.loads(Path(sys.argv[1]).read_text())
    assert len(captures) >= 25, "insufficient contract observations"
    for capture in captures:
        path = capture["path"].split("?", 1)[0]
        operation = next((op for _, pattern, method, op in operations if method == capture["method"] and pattern.fullmatch(path)), None)
        assert operation is not None, (capture["method"], path)
        response = operation["responses"].get(str(capture["status"]))
        assert response is not None, (operation["operationId"], capture["status"])
        if "$ref" in response:
            response = resolver.resolve(response["$ref"])[1]
        schema = response["content"][capture["media"]]["schema"]
        OAS30Validator(schema, resolver=resolver).validate(capture["body"])
        if capture["status"] >= 400:
            assert capture["body"]["status"] == capture["status"]
            assert error_codes[capture["body"]["code"]] == capture["status"]
    print(f"OpenAPI: {len(captures)} public responses valid")


if __name__ == "__main__":
    main()
