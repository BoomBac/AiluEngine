import * as vscode from "vscode";
import { findField, ShaderFieldSchema, ShaderMetaSchema } from "./schema";

export interface ShaderMetaField {
    key: string;
    value: string;
    line: number;
    key_range: vscode.Range;
    value_range: vscode.Range;
}

export interface ShaderPassBlock {
    begin_line: number;
    end_line: number;
    fields: ShaderMetaField[];
}

export interface ShaderFunctionSymbol {
    name: string;
    range: vscode.Range;
}

export interface ShaderMetaDocument {
    info_begin_line: number;
    info_end_line: number;
    passes: ShaderPassBlock[];
    fields: ShaderMetaField[];
    symbols: ShaderFunctionSymbol[];
    has_includes: boolean;
    structural_errors: ShaderStructuralError[];
}

export interface ShaderStructuralError {
    code: string;
    message: string;
    line: number;
    severity: vscode.DiagnosticSeverity;
}

function lineRange(line: number, start: number, end: number): vscode.Range {
    return new vscode.Range(new vscode.Position(line, start), new vscode.Position(line, end));
}

function isMarker(line: string, marker: string): boolean {
    return line.trim().toLowerCase() === marker.toLowerCase();
}

function parseField(document: vscode.TextDocument, line_number: number, line: string): ShaderMetaField | undefined {
    const match = /^(\s*\/\/\s*)([A-Za-z_][A-Za-z0-9_]*)(\s*):(.*)$/.exec(line);
    if (!match)
        return undefined;
    const key_start = match[1].length;
    const key_end = key_start + match[2].length;
    const value_start = key_end + match[3].length + 1;
    return {
        key: match[2],
        value: match[4].trim(),
        line: line_number,
        key_range: lineRange(line_number, key_start, key_end),
        value_range: lineRange(line_number, value_start, line.length)
    };
}

function parseSymbols(document: vscode.TextDocument): ShaderFunctionSymbol[] {
    const symbols: ShaderFunctionSymbol[] = [];
    const function_pattern = /\b(?:void|float(?:[1-4](?:x[1-4])?)?|half(?:[1-4](?:x[1-4])?)?|double|int(?:[1-4])?|uint(?:[1-4])?|bool(?:[1-4])?|[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:\:\s*[^\{;]+)?\s*(?=[\{;]|$)/;
    for (let line_number = 0; line_number < document.lineCount; ++line_number) {
        const line = document.lineAt(line_number).text;
        if (/^\s*\/\//.test(line) || /^\s*#/.test(line))
            continue;
        const match = function_pattern.exec(line);
        if (!match)
            continue;
        const start = match.index + match[0].indexOf(match[1]);
        symbols.push({name: match[1], range: lineRange(line_number, start, start + match[1].length)});
    }
    return symbols;
}

export function parseShaderDocument(document: vscode.TextDocument, schema: ShaderMetaSchema): ShaderMetaDocument {
    let info_begin_line = -1;
    let info_end_line = -1;
    let current_pass: ShaderPassBlock | undefined;
    const passes: ShaderPassBlock[] = [];
    const fields: ShaderMetaField[] = [];
    const structural_errors: ShaderStructuralError[] = [];
    let has_includes = false;

    for (let line_number = 0; line_number < document.lineCount; ++line_number) {
        const line = document.lineAt(line_number).text;
        const trimmed = line.trim().toLowerCase();
        if (/^#\s*include\b/.test(trimmed))
            has_includes = true;
        if (isMarker(line, schema.markers.info_begin)) {
            if (info_begin_line >= 0) {
                structural_errors.push({code: "AILU_SHADER_004", message: "Nested info block.", line: line_number,
                    severity: vscode.DiagnosticSeverity.Error});
            } else {
                info_begin_line = line_number;
            }
            continue;
        }
        if (schema.markers.info_begin_aliases.some(alias => isMarker(line, alias))) {
            info_begin_line = info_begin_line < 0 ? line_number : info_begin_line;
            structural_errors.push({code: "AILU_SHADER_006", message: "Legacy spelling. Prefer \"//info begin\".",
                line: line_number, severity: vscode.DiagnosticSeverity.Hint});
            continue;
        }
        if (isMarker(line, schema.markers.info_end)) {
            if (current_pass) {
                structural_errors.push({code: "AILU_SHADER_004", message: "Pass block is not closed before info end.",
                    line: line_number, severity: vscode.DiagnosticSeverity.Error});
                current_pass.end_line = line_number;
                passes.push(current_pass);
                current_pass = undefined;
            }
            info_end_line = line_number;
            continue;
        }
        if (trimmed === schema.markers.pass_begin.toLowerCase()) {
            if (current_pass) {
                structural_errors.push({code: "AILU_SHADER_004", message: "Nested pass block.", line: line_number,
                    severity: vscode.DiagnosticSeverity.Error});
            } else {
                current_pass = {begin_line: line_number, end_line: -1, fields: []};
            }
            if (info_begin_line < 0 || info_end_line >= 0)
                structural_errors.push({code: "AILU_SHADER_004", message: "Pass block is outside an info block.",
                    line: line_number, severity: vscode.DiagnosticSeverity.Error});
            continue;
        }
        if (trimmed === schema.markers.pass_end.toLowerCase()) {
            if (!current_pass) {
                structural_errors.push({code: "AILU_SHADER_004", message: "Unexpected pass end.", line: line_number,
                    severity: vscode.DiagnosticSeverity.Error});
            } else {
                current_pass.end_line = line_number;
                passes.push(current_pass);
                current_pass = undefined;
            }
            continue;
        }
        const keyword_match = /^\s*\/\/\s*multi_compile(?:\s+(.*))?$/i.exec(line);
        const field = keyword_match ? {
            key: "multi_compile",
            value: keyword_match[1]?.trim() ?? "",
            line: line_number,
            key_range: lineRange(line_number, line.toLowerCase().indexOf("multi_compile"),
                line.toLowerCase().indexOf("multi_compile") + "multi_compile".length),
            value_range: lineRange(line_number, keyword_match[1] ? line.indexOf(keyword_match[1]) : line.length, line.length)
        } : parseField(document, line_number, line);
        if (field && current_pass) {
            current_pass.fields.push(field);
            fields.push(field);
        }
    }
    if (current_pass) {
        structural_errors.push({code: "AILU_SHADER_004", message: "Pass block has no pass end marker.",
            line: current_pass.begin_line, severity: vscode.DiagnosticSeverity.Error});
        passes.push(current_pass);
    }
    if (info_begin_line >= 0 && info_end_line < 0)
        structural_errors.push({code: "AILU_SHADER_004", message: "Info block has no info end marker.", line: info_begin_line,
            severity: vscode.DiagnosticSeverity.Error});

    return {info_begin_line, info_end_line, passes, fields, symbols: parseSymbols(document), has_includes, structural_errors};
}

export function fieldSchema(schema: ShaderMetaSchema, field: ShaderMetaField): [string, ShaderFieldSchema] | undefined {
    return findField(schema, field.key);
}

export function passAt(document_data: ShaderMetaDocument, line: number): ShaderPassBlock | undefined {
    return document_data.passes.find(pass => line >= pass.begin_line && (pass.end_line < 0 || line <= pass.end_line));
}

export function fieldAt(document_data: ShaderMetaDocument, position: vscode.Position): ShaderMetaField | undefined {
    return document_data.fields.find(field => field.line === position.line
        && (field.value_range.contains(position) || field.key_range.contains(position)));
}
