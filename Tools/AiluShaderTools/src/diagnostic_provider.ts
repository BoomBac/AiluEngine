import * as vscode from "vscode";
import { canonicalValue, findField, ShaderMetaSchema } from "./schema";
import { fieldSchema, parseShaderDocument, ShaderMetaDocument, ShaderMetaField, ShaderPassBlock } from "./parser";

function diagnostic(code: string, message: string, range: vscode.Range, severity: vscode.DiagnosticSeverity): vscode.Diagnostic {
    const item = new vscode.Diagnostic(range, `${code} ${message}`, severity);
    item.code = code;
    item.source = "Ailu Shader";
    return item;
}

function closestValue(values: string[] | undefined, value: string): string | undefined {
    if (!values || !value)
        return undefined;
    const lower = value.toLowerCase();
    return values.find(item => item.toLowerCase().startsWith(lower[0] ?? ""));
}

function validateStencil(field: ShaderMetaField, field_info: import("./schema").ShaderFieldSchema,
                        result: vscode.Diagnostic[]): void {
    const value = field.value.trim();
    if (!value)
        return;
    if (!value.startsWith("{")) {
        result.push(diagnostic("AILU_SHADER_002", "Stencil value must use {Ref:int,Comp:comparison,Pass:operation}.",
            field.value_range, vscode.DiagnosticSeverity.Error));
        return;
    }
    if (!value.endsWith("}"))
        return;
    const content = value.slice(1, -1);
    const seen = new Set<string>();
    for (const part of content.split(",")) {
        const pair = /^\s*([A-Za-z]+)\s*:\s*(.*?)\s*$/.exec(part);
        if (!pair)
            continue;
        const key = pair[1];
        const lower_key = key.toLowerCase();
        const key_start = field.value_range.start.character + value.indexOf(key);
        const range = new vscode.Range(new vscode.Position(field.line, key_start),
            new vscode.Position(field.line, key_start + key.length));
        if (seen.has(lower_key))
            result.push(diagnostic("AILU_SHADER_005", `Duplicate stencil field "${key}".`, range,
                vscode.DiagnosticSeverity.Warning));
        seen.add(lower_key);
        const child = Object.entries(field_info.children ?? {}).find(([name]) => name.toLowerCase() === lower_key)?.[1];
        if (!child) {
            result.push(diagnostic("AILU_SHADER_001", `Unknown stencil field "${key}".`, range,
                vscode.DiagnosticSeverity.Error));
            continue;
        }
        const part_value = pair[2];
        const value_start = field.value_range.start.character + value.indexOf(part_value, value.indexOf(key));
        const value_range = new vscode.Range(new vscode.Position(field.line, value_start),
            new vscode.Position(field.line, value_start + part_value.length));
        if (child.type === "integer" && !/^[-+]?\d+$/.test(part_value))
            result.push(diagnostic("AILU_SHADER_002", `Stencil Ref must be an integer, got "${part_value}".`, value_range,
                vscode.DiagnosticSeverity.Error));
        if (child.type === "enum" && !canonicalValue(child, part_value))
            result.push(diagnostic("AILU_SHADER_002", `Invalid stencil value "${part_value}".`, value_range,
                vscode.DiagnosticSeverity.Error));
    }
}

function validateField(field: ShaderMetaField, schema: ShaderMetaSchema, result: vscode.Diagnostic[]): void {
    const schema_entry = fieldSchema(schema, field);
    if (!schema_entry) {
        result.push(diagnostic("AILU_SHADER_001", `Unknown metadata field "${field.key}".`, field.key_range,
            vscode.DiagnosticSeverity.Error));
        return;
    }
    const [canonical_key, field_info] = schema_entry;
    if (canonical_key !== field.key) {
        result.push(diagnostic("AILU_SHADER_006", `Prefer canonical metadata casing "${canonical_key}".`, field.key_range,
            vscode.DiagnosticSeverity.Hint));
    }
    if (!field.value)
        return;
    if (field_info.type === "enum") {
        if (!canonicalValue(field_info, field.value)) {
            const suggestion = closestValue(field_info.values, field.value);
            const suffix = suggestion ? ` Did you mean "${suggestion}"?` : "";
            result.push(diagnostic("AILU_SHADER_002", `Invalid ${canonical_key} value "${field.value}".${suffix}`,
                field.value_range, vscode.DiagnosticSeverity.Error));
        } else if (canonicalValue(field_info, field.value) !== field.value) {
            result.push(diagnostic("AILU_SHADER_006", `Prefer canonical value "${canonicalValue(field_info, field.value)}".`,
                field.value_range, vscode.DiagnosticSeverity.Hint));
        }
    } else if (field_info.type === "blend") {
        const parts = field.value.split(",").map(item => item.trim()).filter(Boolean);
        if (parts.length !== 2 || parts.some(item => !field_info.values?.includes(item)))
            result.push(diagnostic("AILU_SHADER_002", "Blend expects two factors separated by a comma.", field.value_range,
                vscode.DiagnosticSeverity.Error));
    } else if (field_info.type === "color_mask" && !canonicalValue(field_info, field.value)) {
        result.push(diagnostic("AILU_SHADER_002", `Invalid ColorMask value "${field.value}".`, field.value_range,
            vscode.DiagnosticSeverity.Error));
    } else if (field_info.type === "stencil") {
        validateStencil(field, field_info, result);
    }
}

function validatePass(pass: ShaderPassBlock, schema: ShaderMetaSchema, data: ShaderMetaDocument,
                      result: vscode.Diagnostic[]): void {
    const seen = new Set<string>();
    for (const field of pass.fields) {
        validateField(field, schema, result);
        const key = field.key.toLowerCase();
        if (key !== "multi_compile" && seen.has(key))
            result.push(diagnostic("AILU_SHADER_005", `Duplicate metadata field "${field.key}" in the same pass.`,
                field.key_range, vscode.DiagnosticSeverity.Warning));
        if (key !== "multi_compile")
            seen.add(key);
        const field_info = findField(schema, field.key)?.[1];
        if (field.key.toLowerCase() === "multi_compile") {
            const keywords = field.value.split(/\s+/).filter(Boolean);
            if (keywords.length === 0)
                result.push(diagnostic("AILU_SHADER_002", "multi_compile requires at least one keyword option.",
                    field.value_range, vscode.DiagnosticSeverity.Error));
            const seen_keywords = new Set<string>();
            for (const keyword of keywords) {
                if (seen_keywords.has(keyword))
                    result.push(diagnostic("AILU_SHADER_005", `Duplicate keyword "${keyword}" in the same group.`,
                        field.value_range, vscode.DiagnosticSeverity.Warning));
                seen_keywords.add(keyword);
            }
        }
        if (field_info?.type === "shader_entry" && field.value && !data.has_includes && data.symbols.length > 0
            && !data.symbols.some(symbol => symbol.name === field.value)) {
            result.push(diagnostic("AILU_SHADER_003", `Shader entry "${field.value}" was not found in this file.`,
                field.value_range, vscode.DiagnosticSeverity.Error));
        }
    }
    if (pass.end_line >= 0) {
        for (const [key, field_info] of Object.entries(schema.fields)) {
            if (field_info.required && !pass.fields.some(field => field.key.toLowerCase() === key.toLowerCase()))
                result.push(diagnostic("AILU_SHADER_007", `Required metadata field "${key}" is missing.`,
                    new vscode.Range(new vscode.Position(pass.begin_line, 0), new vscode.Position(pass.begin_line,
                        schema.markers.pass_begin.length)), vscode.DiagnosticSeverity.Warning));
        }
    }
}

export function provideDiagnostics(document: vscode.TextDocument, schema: ShaderMetaSchema,
                                   data = parseShaderDocument(document, schema)): vscode.Diagnostic[] {
    const result = data.structural_errors.map(error => diagnostic(error.code, error.message,
        new vscode.Range(new vscode.Position(error.line, 0), new vscode.Position(error.line,
            document.lineAt(error.line).text.length)), error.severity));
    for (const pass of data.passes)
        validatePass(pass, schema, data, result);
    return result;
}
