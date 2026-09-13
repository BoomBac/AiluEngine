import * as vscode from "vscode";
import { ShaderCompletionProvider } from "./completion_provider";
import { provideDiagnostics } from "./diagnostic_provider";
import { ShaderDefinitionProvider } from "./definition_provider";
import { DocumentIndex } from "./document_index";
import { ShaderHoverProvider } from "./hover_provider";
import { findField, loadSchema } from "./schema";
import { ShaderFieldHighlighter } from "./field_highlighter";

const kSelector: vscode.DocumentSelector = [{language: "hlsl"}, {pattern: "**/*.hlsl"}];

function isHlsl(document: vscode.TextDocument): boolean {
    return document.languageId === "hlsl" || document.fileName.toLowerCase().endsWith(".hlsl");
}

function shouldTriggerValueCompletion(document: vscode.TextDocument, change: vscode.TextDocumentContentChangeEvent,
                                      schema: ReturnType<typeof loadSchema>): boolean {
    if (change.text.length !== 1 || !/[A-Za-z0-9_]/.test(change.text)
        || change.range.start.line !== change.range.end.line)
        return false;
    const line = document.lineAt(change.range.start.line).text.slice(0, change.range.start.character + change.text.length);
    const match = /^\s*\/\/\s*([A-Za-z_][A-Za-z0-9_]*)\s*:/.exec(line);
    return match !== null && findField(schema, match[1]) !== undefined;
}

async function normalizeMetadata(index: DocumentIndex, schema: ReturnType<typeof loadSchema>): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !isHlsl(editor.document))
        return;
    const data = index.get(editor.document);
    const edit = new vscode.WorkspaceEdit();
    for (let line = 0; line < editor.document.lineCount; ++line) {
        const text = editor.document.lineAt(line).text;
        const trimmed = text.trim().toLowerCase();
        if (schema.markers.info_begin_aliases.some(alias => trimmed === alias.toLowerCase()))
            edit.replace(editor.document.uri, new vscode.Range(new vscode.Position(line, 0),
                new vscode.Position(line, text.length)), text.replace(/info\s+bein/i, "info begin"));
        const field = data.fields.find(item => item.line === line);
        if (!field)
            continue;
        const schema_entry = findField(schema, field.key);
        if (!schema_entry)
            continue;
        const [, field_info] = schema_entry;
        const canonical = Object.entries(field_info.aliases ?? {}).find(([alias]) => alias.toLowerCase() === field.value.toLowerCase());
        if (canonical) {
            const value_start = field.value_range.start.character;
            const value_text = text.slice(value_start);
            const value_offset = value_text.search(/\S|$/);
            const value_position = value_start + value_offset;
            edit.replace(editor.document.uri, new vscode.Range(new vscode.Position(line, value_position),
                new vscode.Position(line, value_position + field.value.length)), canonical[1]);
        }
    }
    if (edit.size > 0) {
        await vscode.workspace.applyEdit(edit);
        index.update(editor.document);
    }
}

function insertPass(): void {
    const editor = vscode.window.activeTextEditor;
    if (!editor || !isHlsl(editor.document))
        return;
    editor.insertSnippet(new vscode.SnippetString([
        "//pass begin::",
        "//name: ${1:pass_name}",
        "//vert: ${2:VSMain}",
        "//pixel: ${3:PSMain}",
        "//Cull: Back",
        "//Queue: Opaque",
        "//Fill: Solid",
        "//ZTest: LEqual",
        "//ZWrite: On",
        "//pass end::",
        "$0"
    ].join("\n")));
}

export function activate(context: vscode.ExtensionContext): void {
    const schema = loadSchema(context.extensionPath);
    const index = new DocumentIndex(schema);
    const diagnostics = vscode.languages.createDiagnosticCollection("ailu-shader");
    const highlighter = new ShaderFieldHighlighter(schema, document => index.get(document));
    const refresh = (document: vscode.TextDocument): void => {
        if (!isHlsl(document))
            return;
        const data = index.update(document);
        diagnostics.set(document.uri, provideDiagnostics(document, schema, data));
        highlighter.update(document);
    };
    for (const document of vscode.workspace.textDocuments)
        refresh(document);

    const completion = new ShaderCompletionProvider(schema, document => index.get(document));
    const hover = new ShaderHoverProvider(schema, document => index.get(document));
    const definition = new ShaderDefinitionProvider(schema, document => index.get(document));
    context.subscriptions.push(
        diagnostics,
        vscode.languages.registerCompletionItemProvider(kSelector, completion, ":", ",", "{", " ", "["),
        vscode.languages.registerHoverProvider(kSelector, hover),
        vscode.languages.registerDefinitionProvider(kSelector, definition),
        highlighter,
        vscode.workspace.onDidOpenTextDocument(refresh),
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && isHlsl(editor.document))
                highlighter.update(editor.document);
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (!isHlsl(event.document))
                return;
            highlighter.update(event.document);
            index.scheduleUpdate(event.document, data => {
                diagnostics.set(event.document.uri, provideDiagnostics(event.document, schema, data));
                highlighter.update(event.document);
            });
            const change = event.contentChanges.length === 1 ? event.contentChanges[0] : undefined;
            if (change && vscode.window.activeTextEditor?.document.uri.toString() === event.document.uri.toString()
                && shouldTriggerValueCompletion(event.document, change, schema))
                void vscode.commands.executeCommand("editor.action.triggerSuggest");
        }),
        vscode.workspace.onDidSaveTextDocument(refresh),
        vscode.workspace.onDidCloseTextDocument(document => {
            diagnostics.delete(document.uri);
            highlighter.clear(document);
            index.remove(document);
        }),
        vscode.commands.registerCommand("ailuShader.validateCurrentFile", () => {
            const editor = vscode.window.activeTextEditor;
            if (editor && isHlsl(editor.document))
                refresh(editor.document);
        }),
        vscode.commands.registerCommand("ailuShader.normalizeMetadata", () => normalizeMetadata(index, schema)),
        vscode.commands.registerCommand("ailuShader.insertPass", insertPass)
    );
}

export function deactivate(): void {}
