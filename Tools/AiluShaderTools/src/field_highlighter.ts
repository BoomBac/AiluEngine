import * as vscode from "vscode";
import { fieldSchema, ShaderMetaDocument } from "./parser";
import { ShaderFieldSchema, ShaderMetaSchema } from "./schema";

const kFieldColors: Record<string, string> = {
    name: "terminal.ansiCyan",
    vert: "terminal.ansiBlue",
    pixel: "terminal.ansiMagenta",
    geometry: "terminal.ansiGreen",
    cull: "terminal.ansiYellow",
    queue: "charts.orange",
    topology: "charts.blue",
    blend: "charts.purple",
    fill: "charts.green",
    ztest: "charts.yellow",
    zwrite: "terminal.ansiRed",
    stencil: "charts.purple",
    colormask: "terminal.ansiCyan",
    conservative: "charts.orange",
    multi_compile: "terminal.ansiMagenta",
    unknown: "editorError.foreground"
};

function fieldColor(key: string, field: ShaderFieldSchema | undefined): string {
    if (!field)
        return kFieldColors.unknown;
    return kFieldColors[key.toLowerCase()] ?? kFieldColors.unknown;
}

export class ShaderFieldHighlighter implements vscode.Disposable {
    private readonly _decorations = new Map<string, vscode.TextEditorDecorationType>();

    public constructor(private readonly _schema: ShaderMetaSchema,
                       private readonly _getData: (document: vscode.TextDocument) => ShaderMetaDocument) {
        for (const key of Object.keys(this._schema.fields)) {
            const color = fieldColor(key, this._schema.fields[key]);
            this._decorations.set(key.toLowerCase(), vscode.window.createTextEditorDecorationType({
                color: new vscode.ThemeColor(color),
                fontWeight: "600"
            }));
        }
        this._decorations.set("unknown", vscode.window.createTextEditorDecorationType({
            color: new vscode.ThemeColor(kFieldColors.unknown),
            fontWeight: "600"
        }));
        this._decorations.set("property_type", vscode.window.createTextEditorDecorationType({
            color: new vscode.ThemeColor("terminal.ansiGreen"),
            fontWeight: "600"
        }));
        this._decorations.set("property_annotation", vscode.window.createTextEditorDecorationType({
            color: new vscode.ThemeColor("terminal.ansiYellow"),
            fontWeight: "600"
        }));
    }

    public update(document: vscode.TextDocument): void {
        const data = this._getData(document);
        const ranges = new Map<string, vscode.Range[]>();
        for (const field of data.fields) {
            const token_key = fieldSchema(this._schema, field)?.[0].toLowerCase() ?? "unknown";
            const field_ranges = ranges.get(token_key) ?? [];
            field_ranges.push(field.key_range);
            if (!field.value_range.isEmpty)
                field_ranges.push(field.value_range);
            ranges.set(token_key, field_ranges);
        }
        this.addPropertyRanges(document, ranges);
        for (const editor of vscode.window.visibleTextEditors) {
            if (editor.document.uri.toString() !== document.uri.toString())
                continue;
            for (const [key, decoration] of this._decorations)
                editor.setDecorations(decoration, ranges.get(key) ?? []);
        }
    }

    public clear(document: vscode.TextDocument): void {
        for (const editor of vscode.window.visibleTextEditors) {
            if (editor.document.uri.toString() !== document.uri.toString())
                continue;
            for (const decoration of this._decorations.values())
                editor.setDecorations(decoration, []);
        }
    }

    public dispose(): void {
        for (const decoration of this._decorations.values())
            decoration.dispose();
        this._decorations.clear();
    }

    private addPropertyRanges(document: vscode.TextDocument, ranges: Map<string, vscode.Range[]>): void {
        let active = false;
        for (let line_number = 0; line_number < document.lineCount; ++line_number) {
            const line = document.lineAt(line_number).text;
            const trimmed = line.trim().toLowerCase();
            if (trimmed === "//properties") {
                active = true;
                continue;
            }
            if (!active)
                continue;
            if (trimmed === "//}") {
                active = false;
                continue;
            }
            const annotation = /^(\s*\/\/\s*)\[([^\]]+)\]/.exec(line);
            if (annotation) {
                const start = annotation[1].length;
                this.addRange(ranges, "property_annotation", new vscode.Range(new vscode.Position(line_number, start),
                    new vscode.Position(line_number, start + annotation[2].length + 2)));
            }
            const comma = line.indexOf(",", annotation?.index ?? 0);
            if (comma < 0)
                continue;
            const type_match = /\b(Texture2D|Texture3D|Color|Range|Float|Vector)\b/.exec(line.slice(comma + 1));
            if (!type_match)
                continue;
            const start = comma + 1 + type_match.index;
            this.addRange(ranges, "property_type", new vscode.Range(new vscode.Position(line_number, start),
                new vscode.Position(line_number, start + type_match[0].length)));
        }
    }

    private addRange(ranges: Map<string, vscode.Range[]>, key: string, range: vscode.Range): void {
        const values = ranges.get(key) ?? [];
        values.push(range);
        ranges.set(key, values);
    }
}
