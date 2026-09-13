import * as vscode from "vscode";
import { fieldAt, fieldSchema, passAt, ShaderMetaDocument } from "./parser";
import { findField, ShaderFieldSchema, ShaderMetaSchema } from "./schema";

function item(label: string, detail: string, range: vscode.Range, insert_text = label): vscode.CompletionItem {
    const result = new vscode.CompletionItem(label, vscode.CompletionItemKind.Value);
    result.detail = detail;
    result.range = range;
    result.insertText = insert_text;
    return result;
}

function valueItems(values: string[], detail: string, range: vscode.Range): vscode.CompletionItem[] {
    return values.map(value => item(value, detail, range));
}

function tokenRange(line: number, start: number, end: number): vscode.Range {
    return new vscode.Range(new vscode.Position(line, start), new vscode.Position(line, end));
}

function currentPass(data: ShaderMetaDocument, line: number) {
    return passAt(data, line);
}

function isInPropertyBlock(document: vscode.TextDocument, line: number): boolean {
    let in_properties = false;
    for (let index = 0; index <= line; ++index) {
        const trimmed = document.lineAt(index).text.trim().toLowerCase();
        if (trimmed === "//properties")
            in_properties = true;
        else if (in_properties && trimmed === "//}")
            in_properties = false;
    }
    return in_properties;
}

export class ShaderCompletionProvider implements vscode.CompletionItemProvider {
    public constructor(private readonly _schema: ShaderMetaSchema, private readonly _getData: (document: vscode.TextDocument) => ShaderMetaDocument) {}

    public provideCompletionItems(document: vscode.TextDocument, position: vscode.Position): vscode.CompletionItem[] {
        const data = this._getData(document);
        const property_items = this.propertyItems(document, position);
        if (property_items)
            return property_items;
        if (!currentPass(data, position.line))
            return [];
        const prefix = document.lineAt(position.line).text.slice(0, position.character);
        const key_match = /^(\s*\/\/\s*)([A-Za-z_][A-Za-z0-9_]*)?$/.exec(prefix);
        if (key_match) {
            const range = tokenRange(position.line, key_match[1].length, position.character);
            return Object.entries(this._schema.fields)
                .filter(([key]) => key.toLowerCase() === "multi_compile"
                    || !currentPass(data, position.line)?.fields.some(field => field.key.toLowerCase() === key.toLowerCase()))
                .map(([key, field]) => {
                    const is_keyword_group = field.type === "keywords";
                    const result = item(key, field.description ?? "Ailu shader metadata field.", range,
                        is_keyword_group ? `${key} ` : `${key}: `);
                    result.kind = is_keyword_group ? vscode.CompletionItemKind.Keyword : vscode.CompletionItemKind.Field;
                    return result;
                });
        }
        const value_match = /^(\s*\/\/\s*)([A-Za-z_][A-Za-z0-9_]*)(\s*):(.*)$/.exec(prefix);
        if (!value_match)
            return this.keywordItems(prefix, position.line, position.character, data);
        const key = value_match[2];
        const schema_entry = findField(this._schema, key);
        if (!schema_entry)
            return [];
        const [canonical_key, field] = schema_entry;
        const raw_value_start = value_match[1].length + value_match[2].length + value_match[3].length + 1;
        const value_offset = value_match[4].search(/\S|$/);
        const value_start = raw_value_start + value_offset;
        const value = value_match[4].slice(value_offset);
        const range = tokenRange(position.line, value_start, position.character);
        if (field.type === "shader_entry") {
            const partial = value.trim();
            const symbols = data.symbols.filter(symbol => symbol.name.toLowerCase().startsWith(partial.toLowerCase()));
            return symbols.map(symbol => item(symbol.name, `${field.stage ?? "shader"} entry`, range));
        }
        if (field.type === "stencil")
            return this.stencilItems(value, position.line, value_start, field);
        if (field.type === "blend") {
            const partial_start = value.lastIndexOf(",") + 1;
            const partial_range = tokenRange(position.line, value_start + partial_start, position.character);
            return valueItems(field.values ?? [], `${canonical_key} blend factor`, partial_range);
        }
        if (field.values)
            return valueItems(field.values, field.description ?? canonical_key, range);
        if (field.type === "string" && canonical_key.toLowerCase() === "name") {
            const names = data.passes.flatMap(pass => pass.fields)
                .filter(item => item.key.toLowerCase() === "name")
                .map(item => item.value)
                .filter((name, index, values) => name && values.indexOf(name) === index);
            return names.filter(name => name.toLowerCase().startsWith(value.trim().toLowerCase()))
                .map(name => item(name, "Shader pass name", range));
        }
        return [];
    }

    private propertyItems(document: vscode.TextDocument, position: vscode.Position): vscode.CompletionItem[] | undefined {
        if (!isInPropertyBlock(document, position.line))
            return undefined;
        const prefix = document.lineAt(position.line).text.slice(0, position.character);
        const annotation_match = /^(\s*\/\/\s*\[)([A-Za-z_]*)$/.exec(prefix);
        if (annotation_match) {
            const range = tokenRange(position.line, annotation_match[1].length, position.character);
            return Object.entries(this._schema.properties.annotations).map(([name, annotation]) => {
                const result = item(name, annotation.description, range, annotation.insert_text ?? `${name}]`);
                result.kind = vscode.CompletionItemKind.Keyword;
                return result;
            });
        }
        const type_match = /^(\s*\/\/\s*(?:\[[^\]]+\]\s*)?[A-Za-z_]\w*\s*\(\s*[^,]*,\s*)([A-Za-z_]*)$/i.exec(prefix);
        if (!type_match)
            return [];
        const range = tokenRange(position.line, type_match[1].length, position.character);
        return Object.entries(this._schema.properties.types).map(([name, property]) => {
            const result = item(name, property.description, range, property.insert_text ?? name);
            result.kind = vscode.CompletionItemKind.TypeParameter;
            if (property.insert_text?.includes("${"))
                result.insertText = new vscode.SnippetString(property.insert_text);
            return result;
        });
    }

    private keywordItems(prefix: string, line: number, character: number, data: ShaderMetaDocument): vscode.CompletionItem[] {
        const match = /^(\s*\/\/\s*)multi_compile(?:\s+(.*))?$/i.exec(prefix);
        if (!match)
            return [];
        const value = match[2] ?? "";
        const token_start = value.lastIndexOf(" ") + 1;
        const range = tokenRange(line, match[1].length + "multi_compile".length + 1 + token_start, character);
        const keywords = new Set(["_"]);
        for (const field of data.fields) {
            if (field.key.toLowerCase() !== "multi_compile")
                continue;
            for (const keyword of field.value.split(/\s+/).filter(Boolean))
                keywords.add(keyword);
        }
        const partial = value.slice(token_start).toLowerCase();
        return [...keywords].filter(keyword => keyword.toLowerCase().startsWith(partial))
            .map(keyword => item(keyword, "multi_compile keyword", range));
    }

    private stencilItems(value: string, line: number, value_start: number,
                         field: ShaderFieldSchema): vscode.CompletionItem[] {
        const children = field.children ?? {};
        const content_start = value.startsWith("{") ? 1 : 0;
        const last_comma = value.lastIndexOf(",");
        const segment_start = Math.max(content_start, last_comma + 1);
        const segment = value.slice(segment_start);
        const colon = segment.indexOf(":");
        if (colon < 0) {
            const partial = segment.trim().toLowerCase();
            const range = new vscode.Range(new vscode.Position(line, value_start + segment_start + segment.search(/\S|$/)),
                new vscode.Position(line, value_start + value.length));
            return Object.entries(children).filter(([key]) => key.toLowerCase().startsWith(partial))
                .map(([key, child]) => item(key, child.description ?? "Stencil field", range, `${key}:`));
        }
        const key = segment.slice(0, colon).trim().toLowerCase();
        const value_offset = segment_start + colon + 1;
        const partial = segment.slice(colon + 1).trim();
        const leading_spaces = segment.slice(colon + 1).search(/\S|$/);
        const range = new vscode.Range(new vscode.Position(line, value_start + value_offset + leading_spaces),
            new vscode.Position(line, value_start + value.length));
        const child = Object.entries(children).find(([name]) => name.toLowerCase() === key)?.[1];
        if (child?.values)
            return child.values.filter(value => value.toLowerCase().startsWith(partial.toLowerCase()))
                .map(value => item(value, child.description ?? "Stencil value", range));
        return [];
    }
}
