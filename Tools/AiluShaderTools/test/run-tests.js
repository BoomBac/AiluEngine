const Module = require("node:module");
const fs = require("node:fs");
const path = require("node:path");

class Position {
    constructor(line, character) { this.line = line; this.character = character; }
}

class Range {
    constructor(start, end) { this.start = start; this.end = end; }
    contains(position) {
        return (position.line > this.start.line || position.line === this.start.line && position.character >= this.start.character)
            && (position.line < this.end.line || position.line === this.end.line && position.character <= this.end.character);
    }
}

class Diagnostic {
    constructor(range, message, severity) { this.range = range; this.message = message; this.severity = severity; }
}

class CompletionItem {
    constructor(label, kind) { this.label = label; this.kind = kind; }
}

const vscode_mock = {
    Position,
    Range,
    Diagnostic,
    CompletionItem,
    CompletionItemKind: {Value: 12, Field: 4, Keyword: 17, TypeParameter: 25},
    SnippetString: class { constructor(value) { this.value = value; } },
    DiagnosticSeverity: {Error: 0, Warning: 1, Information: 2, Hint: 3}
};
const load = Module._load;
Module._load = function(request, parent, is_main) {
    if (request === "vscode") return vscode_mock;
    return load.call(this, request, parent, is_main);
};

require("../out/test/parser.test.js").runParserTests();
const {loadSchema} = require("../out/src/schema.js");
const {parseShaderDocument} = require("../out/src/parser.js");
const {provideDiagnostics} = require("../out/src/diagnostic_provider.js");

class FileDocument extends Position {
    constructor(text) { super(0, 0); this.lines = text.split(/\r?\n/); }
    get lineCount() { return this.lines.length; }
    lineAt(line) { return {text: this.lines[line]}; }
}

function shaderFiles(directory) {
    const result = [];
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
        const entry_path = path.join(directory, entry.name);
        if (entry.isDirectory()) result.push(...shaderFiles(entry_path));
        else if (entry.name.toLowerCase().endsWith(".hlsl")) result.push(entry_path);
    }
    return result;
}

const shader_directory = path.resolve(__dirname, "../../..", "Engine", "Res", "Shaders");
const schema = loadSchema(path.resolve(__dirname, ".."));
let errors = 0;
let warnings = 0;
for (const file of shaderFiles(shader_directory)) {
    const document = new FileDocument(fs.readFileSync(file, "utf8"));
    const data = parseShaderDocument(document, schema);
    const diagnostics = provideDiagnostics(document, schema, data);
    errors += diagnostics.filter(item => item.severity === 0).length;
    warnings += diagnostics.filter(item => item.severity === 1).length;
}
if (errors > 0)
    throw new Error(`Repository shader regression found ${errors} error diagnostics.`);
console.log(`Repository shader regression passed (${shaderFiles(shader_directory).length} files, ${warnings} warnings).`);
console.log("Ailu Shader Tools parser tests passed.");
