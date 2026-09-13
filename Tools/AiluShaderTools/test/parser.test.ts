import * as assert from "node:assert/strict";
import * as path from "node:path";
import * as vscode from "vscode";
import { ShaderCompletionProvider } from "../src/completion_provider";
import { provideDiagnostics } from "../src/diagnostic_provider";
import { parseShaderDocument } from "../src/parser";
import { loadSchema, ShaderMetaSchema } from "../src/schema";

const schema: ShaderMetaSchema = {
    version: 1,
    markers: {
        info_begin: "//info begin",
        info_begin_aliases: ["//info bein"],
        info_end: "//info end",
        pass_begin: "//pass begin::",
        pass_end: "//pass end::"
    },
    fields: {
        name: {type: "string", required: true},
        vert: {type: "shader_entry", required: true, stage: "vertex"},
        pixel: {type: "shader_entry", stage: "pixel"},
        Cull: {type: "enum", values: ["Off", "Front", "Back"]},
        ZWrite: {type: "enum", values: ["On", "Off"]},
        ZTest: {type: "enum", values: ["Off", "Never", "Less", "LEqual", "Equal", "Greater", "GEqual", "NotEqual", "Always"]},
        ColorMask: {type: "color_mask", values: ["RGBA", "RGB", "RG", "R", "A"]},
        Blend: {type: "blend", values: ["Src", "OneMinusSrc", "One", "Zero"]},
        multi_compile: {type: "keywords"}
    },
    properties: {
        types: {
            Texture2D: {description: "texture"},
            Texture3D: {description: "texture"},
            Color: {description: "color"},
            Range: {description: "range", insert_text: "Range(${1:0}, ${2:1})"},
            Float: {description: "float"},
            Vector: {description: "vector"}
        },
        annotations: {
            Toggle: {description: "toggle"},
            Enum: {description: "enum"},
            HDR: {description: "hdr"}
        }
    }
};

class TestDocument {
    public readonly lines: string[];
    public constructor(text: string) {
        this.lines = text.split("\n");
    }
    public get lineCount(): number { return this.lines.length; }
    public lineAt(line: number): {text: string} { return {text: this.lines[line]}; }
}

export function runParserTests(): void {
    const document = new TestDocument([
        "//info bein",
        "//pass begin::",
        "//name: Main",
        "//vert: VSMain",
        "//multi_compile _ _CIRCLE",
        "//pass end::",
        "//info end",
        "void VSMain() {"
    ].join("\n")) as unknown as vscode.TextDocument;
    const parsed = parseShaderDocument(document, schema);
    assert.equal(parsed.passes.length, 1);
    assert.equal(parsed.passes[0].fields.length, 3);
    assert.equal(parsed.passes[0].fields[2].key, "multi_compile");
    assert.equal(parsed.structural_errors.some(error => error.code === "AILU_SHADER_006"), true);
    assert.equal(parsed.symbols[0].name, "VSMain");

    const incomplete = new TestDocument("//info begin\n//pass begin::\n//name: Main") as unknown as vscode.TextDocument;
    const incomplete_parsed = parseShaderDocument(incomplete, schema);
    assert.equal(incomplete_parsed.passes.length, 1);
    assert.equal(incomplete_parsed.structural_errors.some(error => error.message.includes("no pass end")), true);

    const bad_document = new TestDocument([
        "//info begin",
        "//pass begin::",
        "//name: Main",
        "//vert: VSMain",
        "//ZWirte: Off",
        "//ZWrite: False",
        "//ZWrite: On",
        "//Cull: Left",
        "//pass end::",
        "//info end",
        "void VSMain() {"
    ].join("\n")) as unknown as vscode.TextDocument;
    const bad_diagnostics = provideDiagnostics(bad_document, schema);
    assert.equal(bad_diagnostics.some(item => item.code === "AILU_SHADER_001"), true);
    assert.equal(bad_diagnostics.some(item => item.code === "AILU_SHADER_002"), true);
    assert.equal(bad_diagnostics.some(item => item.code === "AILU_SHADER_005"), true);

    const key_document = new TestDocument("//info begin\n//pass begin::\n//C") as unknown as vscode.TextDocument;
    const key_provider = new ShaderCompletionProvider(schema, document => parseShaderDocument(document, schema));
    const key_items = key_provider.provideCompletionItems(key_document, new vscode.Position(2, 4));
    assert.equal(key_items.some(item => item.label === "Cull"), true);
    assert.equal(key_items.some(item => item.label === "ColorMask"), true);

    const value_document = new TestDocument("//info begin\n//pass begin::\n//ZTest: ") as unknown as vscode.TextDocument;
    const value_items = key_provider.provideCompletionItems(value_document, new vscode.Position(2, 9));
    assert.equal(value_items.some(item => item.label === "LEqual"), true);

    const partial_value_document = new TestDocument("//info begin\n//pass begin::\n//ZTest: L") as unknown as vscode.TextDocument;
    const partial_value_items = key_provider.provideCompletionItems(partial_value_document, new vscode.Position(2, 10));
    assert.equal(partial_value_items.some(item => item.label === "Less"), true);
    assert.equal(partial_value_items.some(item => item.label === "LEqual"), true);

    const blend_document = new TestDocument("//info begin\n//pass begin::\n//Blend: Src,") as unknown as vscode.TextDocument;
    const blend_items = key_provider.provideCompletionItems(blend_document, new vscode.Position(2, 13));
    assert.equal(blend_items.some(item => item.label === "OneMinusSrc"), true);

    const multiple_compile_document = new TestDocument([
        "//info begin",
        "//pass begin::",
        "//name: Main",
        "//vert: VSMain",
        "//multi_compile _ ALPHA_TEST",
        "//multi_compile _ PER_OBJECT_CB",
        "//pass end::",
        "//info end",
        "void VSMain() {"
    ].join("\n")) as unknown as vscode.TextDocument;
    const multiple_compile_diagnostics = provideDiagnostics(multiple_compile_document, schema);
    assert.equal(multiple_compile_diagnostics.some(item => item.code === "AILU_SHADER_005"), false);

    const full_schema = loadSchema(path.resolve(__dirname, "..", ".."));
    const full_provider = new ShaderCompletionProvider(full_schema, document => parseShaderDocument(document, full_schema));
    const all_key_document = new TestDocument("//info begin\n//pass begin::\n//") as unknown as vscode.TextDocument;
    const all_key_items = full_provider.provideCompletionItems(all_key_document, new vscode.Position(2, 3));
    for (const key of Object.keys(full_schema.fields))
        assert.equal(all_key_items.some(item => item.label === key), true);

    const stencil_document = new TestDocument("//info begin\n//pass begin::\n//Stencil: {Comp:") as unknown as vscode.TextDocument;
    const stencil_items = full_provider.provideCompletionItems(stencil_document, new vscode.Position(2, 18));
    assert.equal(stencil_items.some(item => item.label === "LEqual"), true);

    const keyword_document = new TestDocument([
        "//info begin",
        "//pass begin::",
        "//multi_compile _ ALPHA_TEST",
        "//multi_compile _ "
    ].join("\n")) as unknown as vscode.TextDocument;
    const keyword_items = full_provider.provideCompletionItems(keyword_document, new vscode.Position(3, 18));
    assert.equal(keyword_items.some(item => item.label === "ALPHA_TEST"), true);

    const value_cases: Array<[string, string]> = [
        ["Cull: ", "Back"],
        ["Queue: ", "Opaque"],
        ["Topology: ", "Triangle"],
        ["Blend: ", "Src"],
        ["Fill: ", "Solid"],
        ["ZTest: ", "LEqual"],
        ["ZWrite: ", "On"],
        ["ColorMask: ", "RGBA"],
        ["Conservative: ", "On"]
    ];
    for (const [field_line, expected_value] of value_cases) {
        const value_case_document = new TestDocument(`//info begin\n//pass begin::\n//${field_line}`) as unknown as vscode.TextDocument;
        const items = full_provider.provideCompletionItems(value_case_document,
            new vscode.Position(2, value_case_document.lineAt(2).text.length));
        assert.equal(items.some(item => item.label === expected_value), true, field_line);
    }

    const property_document = new TestDocument([
        "//info begin",
        "//pass begin::",
        "//name: Main",
        "//vert: VSMain",
        "//pass end::",
        "//Properties",
        "//{",
        "//_Slice(\"Slice\",Ran"
    ].join("\n")) as unknown as vscode.TextDocument;
    const property_items = full_provider.provideCompletionItems(property_document,
        new vscode.Position(7, property_document.lineAt(7).text.length));
    assert.equal(property_items.some(item => item.label === "Range"), true);
    assert.equal(property_items.some(item => item.label === "Texture2D"), true);

    const annotation_document = new TestDocument([
        "//info begin",
        "//pass begin::",
        "//name: Main",
        "//vert: VSMain",
        "//pass end::",
        "//Properties",
        "//{",
        "//[T"
    ].join("\n")) as unknown as vscode.TextDocument;
    const annotation_items = full_provider.provideCompletionItems(annotation_document,
        new vscode.Position(7, annotation_document.lineAt(7).text.length));
    assert.equal(annotation_items.some(item => item.label === "Toggle"), true);
}
